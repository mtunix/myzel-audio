#!/usr/bin/env python3
"""
Viridian's network tool: an ngspice netlist with named pots and switches -> exact H(s) by
symbolic nodal analysis -> blocks of sigma-polynomials -> a generated C++ header.

    netlist.py generate <netlist.cir> <out.h> [--vectors <out.h>] [--namespace <ns>]
    netlist.py regenerate                           every netlist with a '*@ generate' directive
    netlist.py check <netlist.cir> [--points N]     the generated blocks against ngspice AC
    netlist.py show <netlist.cir>                   H(s) and the blocks, per switch state

Needs python3 with sympy and numpy; `check` needs ngspice. See README.md.
"""

import argparse
import itertools
import multiprocessing
import os
import random
import sys

import numpy as np
import sympy as sp

sys.dont_write_bytecode = True     # no __pycache__ in the source tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import codegen    # noqa: E402
import mna        # noqa: E402
import ngspice    # noqa: E402
import sections   # noqa: E402
import spice      # noqa: E402


_analysing = None


def _analyse_state(index):
    """N, D, split, blocks and gain of one state of the netlist being analysed."""
    a = _analysing
    subs = codegen.substitutions(a.switches, a.atoms, a.states[index])
    pieces = mna.transfer_pieces(a.netlist, subs)
    N, D = pieces[0]
    for n, d in pieces[1:]:
        N, D = N * n, D * d
    split = sections.split(N, D, a.netlist.controls, pieces)
    blocks, gain = sections.normalised_blocks(split, a.netlist.controls)
    return N, D, split, blocks, gain


class Analysis:
    """Every switch state of one netlist: H(s) = N / D and its normalised blocks."""

    def __init__(self, path):
        self.netlist = spice.read(path)
        self.switches, self.atoms, self.states = codegen.states(self.netlist)
        self.pots = [c for c in self.netlist.controls if c.continuous]
        # The states are independent: one process each (the symbolic work dominates).
        global _analysing
        _analysing = self
        if len(self.states) > 1 and multiprocessing.get_start_method(allow_none=True) in (None, "fork"):
            with multiprocessing.get_context("fork").Pool(min(len(self.states), os.cpu_count() or 1)) as pool:
                results = pool.map(_analyse_state, range(len(self.states)))
        else:
            results = [_analyse_state(k) for k in range(len(self.states))]
        self.per_state = [(state, *r) for state, r in zip(self.states, results)]

    def state_of(self, values):
        positions = tuple(int(round(values[c.name])) for c in self.switches)
        numeric = {c.symbol: sp.Rational(repr(float(values[c.name]))) for c in self.pots}
        truths = tuple(1 if bool(rel.xreplace(numeric)) else 0 for rel in self.atoms)
        return next(p for p in self.per_state if p[0] == (positions, truths))

    def exact(self, values, freqs):
        """H(j 2 pi f) of the unfactored N / D."""
        _, N, D, _, _, _ = self.state_of(values)
        subs = {c.symbol: sp.Rational(repr(float(values[c.name]))) for c in self.pots}
        n = sp.lambdify(mna.S, N.as_expr().xreplace(subs), "mpmath")
        d = sp.lambdify(mna.S, D.as_expr().xreplace(subs), "mpmath")
        import mpmath
        mpmath.mp.dps = 40
        out = []
        for f in freqs:
            s = mpmath.mpc(0, 2 * mpmath.pi * mpmath.mpf(repr(float(f))))
            out.append(complex(n(s) / d(s)))
        return np.array(out)

    def blocks_response(self, values, freqs):
        """H(j 2 pi f) of the gain times the product of the normalised blocks (what C++ evaluates)."""
        _, _, _, _, blocks, gain = self.state_of(values)
        subs = {c.symbol: values[c.name] for c in self.pots}
        sigma = 2j * np.pi * np.asarray(freqs, dtype=float) / sections.OMEGA_REF
        h = np.full(sigma.shape, complex(sp.sympify(gain).xreplace(subs)), dtype=complex)
        for num, den, _ in blocks:
            nc = [complex(sp.sympify(c).xreplace(subs)) for c in num]
            dc = [complex(sp.sympify(c).xreplace(subs)) for c in den]
            h *= np.polyval(nc[::-1], sigma) / np.polyval(dc[::-1], sigma)
        return h

    def control_grid(self, count, seed=1):
        """Corners, centre and seeded random positions of every control."""
        rng = random.Random(seed)
        out = []
        for positions in sorted({k[0] for k in self.states}):
            base = {c.name: float(v) for c, v in zip(self.switches, positions)}
            corners = list(itertools.product([0.0, 1.0], repeat=len(self.pots))) if len(self.pots) <= 4 else []
            for corner in corners + [tuple(0.5 for _ in self.pots)]:
                out.append(dict(base, **{c.name: v for c, v in zip(self.pots, corner)}))
            for _ in range(count):
                out.append(dict(base, **{c.name: round(rng.random(), 6) for c in self.pots}))
        return out


def command_line(args):
    root = codegen._repo_root()
    rel = lambda p: os.path.relpath(os.path.abspath(p), root)
    parts = ["python3", rel(__file__), "generate", rel(args.netlist), rel(args.out)]
    if args.vectors:
        parts += ["--vectors", rel(args.vectors)]
    if args.namespace != "viridian::dsp::generated":
        parts += ["--namespace", args.namespace]
    return " ".join(parts)


def texts(a, args, command):
    """The header and the vectors header (None without --vectors) of one analysis."""
    header = codegen.header(a.netlist, [(s, b, g) for s, _, _, _, b, g in a.per_state], command, args.namespace)
    if not args.vectors:
        return header, None
    freqs = [20.0, 63.0, 200.0, 1000.0, 3150.0, 10000.0, 20000.0, 40000.0]
    points = []
    for values in a.control_grid(3, seed=7):
        h = a.exact(values, freqs)
        ctrl = [values[c.name] for c in a.netlist.controls]
        points += [(ctrl, f, v) for f, v in zip(freqs, h)]
    return header, codegen.vectors_header(a.netlist, points, command, args.namespace)


def generate(args, command=None, quiet=False):
    header, vectors = texts(Analysis(args.netlist), args, command or command_line(args))
    _write(args.out, header, quiet)
    if vectors is not None:
        _write(args.vectors, vectors, quiet)


def netlists_with_targets(root=None):
    """Every .cir under plugins/viridian that names its generated header ('*@ generate')."""
    root = root or os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    found = []
    for base, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if not d.startswith(".") and not d.startswith("build"))
        for f in sorted(files):
            if f.endswith(".cir"):
                path = os.path.join(base, f)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    if any(line.strip().lower().startswith("*@ generate") for line in fh):
                        found.append(path)
    return found


def regenerate():
    for path in netlists_with_targets():
        header, vectors = spice.read(path).generate
        generate(argparse.Namespace(netlist=path, out=header, vectors=vectors,
                                    namespace="viridian::dsp::generated"))


def _write(path, text, quiet=False):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    if not quiet:
        print(f"wrote {path}")


def check(path, points=6, verbose=True, analysis=None):
    """Worst |dB| and phase (degrees) of the blocks against ngspice over a control grid, and
    the worst disagreement between the blocks and the unfactored N / D (a factoring check)."""
    a = analysis or Analysis(path)
    grid = a.control_grid(points)
    runs = ngspice.ac(a.netlist, grid)
    worst_db = worst_deg = worst_factor = 0.0
    for values, (f, ref) in zip(grid, runs):
        h = a.blocks_response(values, f)
        floor = 1e-5 * np.max(np.abs(ref))
        ok = np.abs(ref) > floor
        if np.any(ok):
            worst_db = max(worst_db, float(np.max(np.abs(20 * np.log10(np.abs(h[ok]) / np.abs(ref[ok]))))))
            worst_deg = max(worst_deg, float(np.max(np.abs(np.angle(h[ok] / ref[ok], deg=True)))))
        # Below the floor (a pot at its end shorting the path) compare against the peak instead.
        worst_floor = float(np.max(np.abs(h[~ok] - ref[~ok]))) / np.max(np.abs(ref)) if np.any(~ok) else 0.0
        worst_db = max(worst_db, 20 * np.log10(1 + worst_floor))
        exact = a.exact(values, f[::8])
        worst_factor = max(worst_factor, float(np.max(np.abs(h[::8] - exact) / np.maximum(np.abs(exact), 1e-300))))
    if verbose:
        print(f"{a.netlist.name}: {len(grid)} control settings x {len(runs[0][0])} frequencies, "
              f"worst {worst_db:.2e} dB, {worst_deg:.2e} deg against ngspice; "
              f"blocks vs N/D relative {worst_factor:.1e}")
    return worst_db, worst_deg, worst_factor


def show(path):
    a = Analysis(path)
    for state, N, D, split, blocks, gain in a.per_state:
        label = ", ".join([f"{c.name}={v}" for c, v in zip(a.switches, state[0])]
                          + [f"{rel}: {bool(t)}" for rel, t in zip(a.atoms, state[1])]) or "(one state)"
        print(f"--- {label}")
        print("N =", sp.factor(N.as_expr()))
        print("D =", sp.factor(D.as_expr()))
        for k, b in enumerate(split.blocks):
            print(f"block {k}: ({sp.factor(b.num.as_expr())}) / ({sp.factor(b.den.as_expr())})")
        print("gain =", split.gain)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate")
    g.add_argument("netlist")
    g.add_argument("out")
    g.add_argument("--vectors")
    g.add_argument("--namespace", default="viridian::dsp::generated")
    c = sub.add_parser("check")
    c.add_argument("netlist")
    c.add_argument("--points", type=int, default=6)
    s = sub.add_parser("show")
    s.add_argument("netlist")
    sub.add_parser("regenerate")
    args = p.parse_args()
    try:
        if args.cmd == "generate":
            generate(args)
        elif args.cmd == "check":
            check(args.netlist, args.points)
        elif args.cmd == "regenerate":
            regenerate()
        else:
            show(args.netlist)
    except spice.NetlistError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
