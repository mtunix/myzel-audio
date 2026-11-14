"""
H(s) split into blocks: the irreducible factors of the denominator over QQ[s, pots], each with the
numerator factors that belong to it, plus a gain that does not depend on s.

A factor of the denominator is irreducible over the pot positions, so a band that the circuit
buffers from the others is a factor of its own and stays a block of its own; a network whose
controls interact (one denominator polynomial in several pots) is one block, factored into
second-order sections at control rate. Numerator factors join the block that shares their pots,
then the one whose poles lie nearest their zeros at the default positions (a high-pass's zeros at
s = 0 join its poles). Polynomials are emitted in sigma = s / omega_ref.
"""

import math
from dataclasses import dataclass, field

import numpy as np
import sympy as sp

from mna import S
from spice import NetlistError

OMEGA_REF = 2 * math.pi * 1000.0


@dataclass
class Block:
    den: object                         # sympy Poly in (s, *pots)
    nums: list = field(default_factory=list)

    @property
    def num(self):
        p = sp.Poly(1, *self.den.gens)
        for f in self.nums:
            p = p * f
        return p

    def capacity(self):
        return self.den.degree(S) - sum(f.degree(S) for f in self.nums)

    def pots(self):
        return self.den.free_symbols - {S}


@dataclass
class Split:
    blocks: list
    gain: object                        # sympy expression in the pots


def _roots_at(poly, defaults):
    p = sp.Poly(poly.as_expr().xreplace(defaults), S)
    coeffs = [float(c) for c in p.all_coeffs()]
    return np.roots(coeffs) if len(coeffs) > 1 else np.array([])


def _distance(zeros, poles):
    """Mean log distance from each zero to its nearest pole (1 rad/s floor for roots at DC)."""
    if len(zeros) == 0 or len(poles) == 0:
        return 0.0
    lz = np.log(np.abs(zeros) + 1.0)
    lp = np.log(np.abs(poles) + 1.0)
    return float(np.mean([np.min(np.abs(lp - z)) for z in lz]))


def _pot_grid(pots, limit=240, seed=3):
    """Pot settings spanning the range, ends included: a full grid when small, else a sample."""
    import itertools
    import random
    values = [0.0, 0.01, 0.1, 0.3, 0.5, 0.7, 0.9, 0.99, 1.0]
    if len(values) ** len(pots) <= limit:
        return [dict(zip(pots, v)) for v in itertools.product(values, repeat=len(pots))]
    rng = random.Random(seed)
    corners = [dict(zip(pots, v)) for v in itertools.product([0.0, 1.0], repeat=len(pots))][:limit // 2]
    return corners + [{p: rng.choice(values) for p in pots} for _ in range(limit - len(corners))]


def _spread(blocks, grid, freqs):
    """The worst block's spread over frequency (max - min of log10 |H|) over the pot grid."""
    s = 2j * np.pi * freqs
    pots = sorted({p for b in blocks for p in b.pots() | (b.num.free_symbols - {S})}, key=str)
    worst = 0.0
    evaluators = []
    for b in blocks:
        num = sp.lambdify([S] + pots, b.num.as_expr(), "numpy")
        den = sp.lambdify([S] + pots, b.den.as_expr(), "numpy")
        evaluators.append((num, den))
    for point in grid:
        args = [point.get(p, 0.5) for p in pots]
        for num, den in evaluators:
            with np.errstate(all="ignore"):
                h = np.abs(np.asarray(num(s, *args), dtype=complex) / np.asarray(den(s, *args), dtype=complex))
            h = h[np.isfinite(h)]
            if len(h) == 0:
                return np.inf
            top = np.max(h)
            if top == 0.0:
                continue
            worst = max(worst, float(np.log10(top) - np.log10(max(np.min(h), 1e-12 * top))))
    return worst


def _assign(nfs, dfs, controls, defaults, limit=4000):
    """Numerator factors to denominator blocks: of every assignment that fits the degrees, the one
    whose worst block varies least over frequency anywhere in the pot range (a block whose gain
    runs away while another's falls in step would carry that in its states); ties go to shared
    pots, then to zeros nearest the poles at the defaults."""
    pots = [c.symbol for c in controls if c.continuous]
    grid = _pot_grid(pots)
    freqs = np.geomspace(10.0, 100000.0, 41)
    zeros_at = [_roots_at(f, defaults) for f in nfs]
    poles_at = [_roots_at(f, defaults) for f in dfs]
    candidates = []

    def search(k, assignment, room):
        if len(candidates) >= limit:
            return
        if k == len(nfs):
            candidates.append(list(assignment))
            return
        seen = set()
        for b in range(len(dfs)):
            key = (str(dfs[b].as_expr()), tuple(sorted(str(nfs[j].as_expr()) for j, x in enumerate(assignment) if x == b)))
            if room[b] < nfs[k].degree(S) or key in seen:
                continue
            seen.add(key)
            room[b] -= nfs[k].degree(S)
            assignment.append(b)
            search(k + 1, assignment, room)
            assignment.pop()
            room[b] += nfs[k].degree(S)

    search(0, [], [f.degree(S) for f in dfs])
    if not candidates:
        # No assignment fits block by block: one block for everything.
        whole = dfs[0]
        for f in dfs[1:]:
            whole = whole * f
        return [Block(whole, list(nfs))]

    def blocks_of(assignment):
        out = [Block(f) for f in dfs]
        for f, b in zip(nfs, assignment):
            out[b].nums.append(f)
        return out

    def tie_break(assignment):
        shared = sum(len((nfs[j].free_symbols - {S}) & (dfs[b].free_symbols - {S})) for j, b in enumerate(assignment))
        distance = sum(_distance(zeros_at[j], poles_at[b]) for j, b in enumerate(assignment))
        return (-shared, distance)

    if len(candidates) == 1:
        return blocks_of(candidates[0])
    scored = [(round(_spread(blocks_of(a), grid, freqs), 6), tie_break(a), a) for a in candidates]
    scored.sort(key=lambda x: (x[0], x[1]))
    return blocks_of(scored[0][2])


def split(N, D, controls, pieces=None):
    """`pieces`: [(N, D), ...] whose product is N / D (mna.transfer_pieces); each is factored on
    its own, which is much cheaper than factoring the product."""
    defaults = {c.symbol: c.default_coordinate for c in controls if c.continuous}
    gens = N.gens

    def factors(P):
        coeff, fl = sp.factor_list(P.as_expr(), *gens)
        constant, moving = sp.sympify(coeff), []
        for f, m in fl:
            fp = sp.Poly(f, *gens)
            if fp.degree(S) == 0:
                constant *= f ** m
            else:
                moving.extend([fp] * m)
        return constant, moving

    cn, nfs, cd, dfs = sp.Integer(1), [], sp.Integer(1), []
    for n, d in pieces or [(N, D)]:
        c, f = factors(n)
        cn, nfs = cn * c, nfs + f
        c, f = factors(d)
        cd, dfs = cd * c, dfs + f
    if sum(f.degree(S) for f in nfs) > sum(f.degree(S) for f in dfs):
        raise NetlistError("the numerator has a higher degree in s than the denominator")
    nfs.sort(key=lambda f: (-f.degree(S), str(f.as_expr())))
    blocks = _assign(nfs, dfs, controls, defaults)

    def centre(b):
        r = _roots_at(b.den, defaults)
        return float(np.exp(np.mean(np.log(np.abs(r) + 1e-9)))) if len(r) else 0.0

    blocks.sort(key=lambda b: (centre(b), str(b.den.as_expr())))
    return Split(blocks, sp.cancel(cn / cd))


def sigma_coefficients(poly, scale=OMEGA_REF):
    """Coefficients of poly(s = scale * sigma), ascending in sigma, as expressions in the pots."""
    coeffs = sp.Poly(poly.as_expr(), S).all_coeffs()[::-1]
    w = sp.Float(scale, 30)
    return [sp.expand(c * w ** k) for k, c in enumerate(coeffs)]


def normalised_blocks(split_result, controls):
    """Per block (num, den) sigma-coefficient lists scaled to O(1) at the defaults, and the gain."""
    defaults = {c.symbol: c.default_coordinate for c in controls if c.continuous}
    gain = split_result.gain
    out = []
    for b in split_result.blocks:
        num = sigma_coefficients(b.num)
        den = sigma_coefficients(b.den)

        def scale_of(coeffs):
            m = max(abs(float(c.xreplace(defaults))) for c in coeffs)
            if m == 0.0:
                m = 1.0
            return sp.Float(10.0 ** round(math.log10(m)), 30)

        sn, sd = scale_of(num), scale_of(den)
        num = [sp.expand(c / sn) for c in num]
        den = [sp.expand(c / sd) for c in den]
        gain = gain * sn / sd
        out.append((num, den, sorted(str(p) for p in b.den.free_symbols | b.num.free_symbols if p != S)))
    return out, gain
