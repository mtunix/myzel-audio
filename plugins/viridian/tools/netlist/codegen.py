"""
The generated C++ header: one struct per netlist with its controls, size constants and an
evaluate() that writes every block's sigma-polynomials and the gain for given control values,
through a function per state and functions of bounded size within it.
"""

import itertools
import os
import re
from dataclasses import dataclass

import sympy as sp
from sympy.printing.c import C99CodePrinter

import sources
from sections import OMEGA_REF


class _Printer(C99CodePrinter):
    def _print_Float(self, expr):
        text = repr(float(expr))
        if "e" not in text and "." not in text and "inf" not in text and "nan" not in text:
            text += ".0"
        return text

    def _print_Rational(self, expr):
        return self._print_Float(sp.Float(expr, 30))

    def _print_Integer(self, expr):
        return f"{int(expr)}.0"

    def _print_Pow(self, expr):
        base, e = expr.as_base_exp()
        if e.is_Integer and 1 < int(e) <= 6:
            b = self._print(base)
            return "(" + "*".join([f"({b})"] * int(e)) + ")"
        return super()._print_Pow(expr)


def _cpp(expr, names):
    return _Printer().doprint(expr.xreplace(names))


def pot_conditions(netlist):
    """Relations on pots in .if conditions and ternary values: each splits the pot range into
    regions, and every feasible combination is a state of its own (exact within it)."""
    pots = {c.symbol for c in netlist.controls if c.continuous}
    found = set()
    for e in netlist.elements:
        for x in [e.condition] + ([e.value] if e.value is not None else []):
            for rel in sp.sympify(x).atoms(sp.core.relational.Relational):
                if rel.free_symbols & pots:
                    found.add(rel)
    return sorted(found, key=str)


def _feasible(atoms, truths, pots):
    """False when single-pot conditions contradict each other within 0 ... 1."""
    for p in pots:
        region = sp.Interval(0, 1)
        for rel, truth in zip(atoms, truths):
            if rel.free_symbols != {p}:
                continue
            allowed = sp.solve_univariate_inequality(rel, p, relational=False)
            region = region.intersect(allowed if truth else sp.Interval(-sp.oo, sp.oo) - allowed)
        if region.is_empty:
            return False
    return True


def states(netlist):
    """(switches, pot conditions, keys): a key is (switch positions, condition truths)."""
    switches = [c for c in netlist.controls if c.kind == "switch"]
    atoms = pot_conditions(netlist)
    if len(atoms) > 6:
        raise ValueError("more than six pot conditions")
    pots = [c.symbol for c in netlist.controls if c.continuous]
    keys = []
    for positions in itertools.product(*[range(c.positions) for c in switches]):
        for truths in itertools.product((0, 1), repeat=len(atoms)):
            if _feasible(atoms, truths, pots):
                keys.append((positions, truths))
    return switches, atoms, keys


def state_index(switches, atoms, key):
    positions, truths = key
    index = 0
    for c, v in zip(switches, positions):
        index = index * c.positions + v
    for t in truths:
        index = index * 2 + t
    return index


def substitutions(switches, atoms, key):
    positions, truths = key
    subs = {c.symbol: sp.Integer(v) for c, v in zip(switches, positions)}
    subs.update({rel: (sp.true if t else sp.false) for rel, t in zip(atoms, truths)})
    return subs


# A generated function holds at most this many statements or characters: one evaluate() per
# network of several megabytes is more than MSVC and Apple Clang build comfortably.
PART_STATEMENTS = 32
PART_CHARACTERS = 16384


@dataclass
class State:
    """One state's evaluation: the shared temporaries, the block layout and the outputs."""
    index: int
    label: str
    temps: list         # [(name, C++ expression)], each name "x<k>"
    meta: list          # C++ statements: block count, degrees, control masks
    outputs: list       # [(target, C++ expression)]


_TEMP = re.compile(r"\bx(\d+)\b")


def _parts(statements):
    """Consecutive runs of statements within PART_STATEMENTS and PART_CHARACTERS."""
    parts, current, size = [], [], 0
    for text in statements:
        if current and (len(current) >= PART_STATEMENTS or size + len(text) > PART_CHARACTERS):
            parts.append(current)
            current, size = [], 0
        current.append(text)
        size += len(text)
    if current:
        parts.append(current)
    return parts


def emit_evaluate(w, stateful, selector, evaluated):
    """evaluate() and its parts: a function per state, its temporaries and outputs in functions of
    bounded size (PART_STATEMENTS), the temporaries in an array the parts share. The statements are
    the same expressions in the same order, so every output is the same to the bit."""
    def temp(expr):
        return _TEMP.sub(r"x[\1]", expr)

    w("    static void evaluate ([[maybe_unused]] const double* c, Polynomials& out) noexcept")
    w("    {")
    if stateful:
        w("        int state = 0;")
        for line in selector:
            w("        " + line)
        w("")
        w("        switch (state)")
        w("        {")
        for st in evaluated:
            w(f"            case {st.index}:   // {st.label}")
            w(f"                evaluate{st.index} (c, out);")
            w("                break;")
        w("            default:")
        w("                out.numBlocks = 0;")
        w("                out.gain = 0.0;")
        w("                break;")
        w("        }")
    else:
        w(f"        evaluate{evaluated[0].index} (c, out);")
    w("    }")
    w("")
    w("private:")
    for st in evaluated:
        name = f"evaluate{st.index}"
        for k, n in enumerate(t for t, _ in st.temps):
            if n != f"x{k}":
                raise ValueError(f"temporary {n} out of order")
        temp_parts = _parts([f"x[{k}] = {temp(e)};" for k, (_, e) in enumerate(st.temps)])
        out_parts = _parts([f"{t} = {temp(e)};" for t, e in st.outputs])
        w("")
        if st.label:
            w(f"    // {st.label}")
        w(f"    static void {name} ([[maybe_unused]] const double* c, Polynomials& out) noexcept")
        w("    {")
        if st.temps:
            w(f"        std::array<double, {len(st.temps)}> x;")
        for k in range(len(temp_parts)):
            w(f"        {name}Terms{k} (c, x.data());")
        for line in st.meta:
            w("        " + line)
        x = "x.data()" if st.temps else "nullptr"
        for k in range(len(out_parts)):
            w(f"        {name}Out{k} (c, {x}, out);")
        w("    }")
        for k, part in enumerate(temp_parts):
            w("")
            w(f"    VIRIDIAN_GENERATED_PART static void {name}Terms{k} ([[maybe_unused]] const double* c, double* x) noexcept")
            w("    {")
            for line in part:
                w("        " + line)
            w("    }")
        for k, part in enumerate(out_parts):
            w("")
            w(f"    VIRIDIAN_GENERATED_PART static void {name}Out{k} ([[maybe_unused]] const double* c, [[maybe_unused]] const double* x, Polynomials& out) noexcept")
            w("    {")
            for line in part:
                w("        " + line)
            w("    }")


def header(netlist, analysed, command, namespace="viridian::dsp::generated"):
    """`analysed`: [(key, [(num, den, pots) per block], gain)] for every state (see states())."""
    switches, atoms, _ = states(netlist)
    control_index = {c.symbol: k for k, c in enumerate(netlist.controls)}
    max_blocks = max(len(blocks) for _, blocks, _ in analysed)
    max_degree = max(len(den) - 1 for _, blocks, _ in analysed for _, den, _ in blocks)
    max_sections = max(sum((len(den) - 1 + 1) // 2 for _, den, _ in blocks) for _, blocks, _ in analysed)

    # The netlist and every file it includes: a changed card netlist behind a wrapper is stale too.
    digest = sources.digest(netlist.source)
    rel = os.path.relpath(netlist.source, _repo_root())

    lines = []
    w = lines.append
    w("#pragma once")
    w("")
    w("// GENERATED by plugins/viridian/tools/netlist from " + rel + f" (sha256 {digest}).")
    w("// Do not edit by hand. Regenerate from the repository root with:")
    w("//   " + command)
    w("")
    w('#include "dsp/common/NetworkTypes.h"')
    w("")
    w("#include <algorithm>")
    w("#include <array>")
    w("#include <cmath>")
    w("")
    w(f"namespace {namespace}")
    w("{")
    w("")
    w(f"/** {netlist.title.strip()} */")
    w(f"struct {netlist.name}")
    w("{")
    w(f"    static constexpr int numControls = {len(netlist.controls)};")
    for c in netlist.controls:
        if c.kind == "step":
            values = ", ".join(repr(float(v)) for v in c.values)
            w(f"    static constexpr std::array<double, {c.positions}> {c.name}Steps {{ {values} }};")
    w(f"    static constexpr std::array<NetworkControl, {len(netlist.controls)}> controls {{{{")
    for c in netlist.controls:
        kind = {"pot": "pot", "switch": "selector", "step": "stepped"}[c.kind]
        steps = f", {c.name}Steps.data()" if c.kind == "step" else ""
        w(f'        {{ "{c.name}", NetworkControl::Kind::{kind}, {c.positions}, {float(c.default)!r}{steps} }},')
    w("    }};")
    w(f"    static constexpr int maxBlocks = {max_blocks};")
    w(f"    static constexpr int maxDegree = {max_degree};")
    w(f"    static constexpr int maxSections = {max_sections};")
    w(f"    static constexpr double omegaRef = {OMEGA_REF!r};   // the polynomials are in s / omegaRef")
    w("")
    w("    using Polynomials = NetworkPolynomials<maxBlocks, maxDegree>;")
    w("")
    pot_names = {}
    for c in netlist.controls:
        if c.continuous:
            pot_names[c.symbol] = sp.Symbol(f"c[{control_index[c.symbol]}]")
    stateful = bool(switches or atoms)
    selector = []
    if stateful:
        for c in switches:
            k = control_index[c.symbol]
            selector.append(f"state = state * {c.positions} + std::clamp ((int) std::lround (c[{k}]), 0, {c.positions - 1});")
        for rel in atoms:
            selector.append(f"state = state * 2 + (({_cpp(rel, pot_names)}) ? 1 : 0);")
    evaluated = []
    for key, blocks, gain in analysed:
        index, label = 0, ""
        if stateful:
            index = state_index(switches, atoms, key)
            label = ", ".join([f"{c.name} = {v}" for c, v in zip(switches, key[0])]
                              + [f"{rel} {'yes' if t else 'no'}" for rel, t in zip(atoms, key[1])])
        exprs, targets = [], []
        for bi, (num, den, pots) in enumerate(blocks):
            for k, e in enumerate(num):
                exprs.append(e)
                targets.append(f"out.blocks[{bi}].num[{k}]")
            for k, e in enumerate(den):
                exprs.append(e)
                targets.append(f"out.blocks[{bi}].den[{k}]")
        exprs.append(gain)
        targets.append("out.gain")
        subs, reduced = sp.cse([sp.sympify(e).xreplace(pot_names) for e in exprs],
                               symbols=sp.numbered_symbols("x"), optimizations="basic")
        temps = [(str(sym), _cpp(e, {})) for sym, e in subs]
        meta = [f"out.numBlocks = {len(blocks)};"]
        for bi, (num, den, pots) in enumerate(blocks):
            mask = 0
            for p in pots:
                mask |= 1 << control_index[sp.Symbol(p)]
            for c in switches:
                mask |= 1 << control_index[c.symbol]
            for rel in atoms:
                for p in rel.free_symbols:
                    mask |= 1 << control_index[p]
            meta.append(f"out.blocks[{bi}].numDegree = {len(num) - 1};")
            meta.append(f"out.blocks[{bi}].denDegree = {len(den) - 1};")
            meta.append(f"out.blocks[{bi}].controls = 0x{mask:x}u;")
        outputs = [(t, _cpp(e, {})) for t, e in zip(targets, reduced)]
        evaluated.append(State(index, label, temps, meta, outputs))
    emit_evaluate(w, stateful, selector, evaluated)
    w("};")
    w("")
    w(f"}} // namespace {namespace}")
    w("")
    return "\n".join(lines)


def vectors_header(netlist, points, command, namespace="viridian::dsp::generated"):
    """Test points: control values, a frequency and the exact H(j 2 pi f) of the unfactored N / D."""
    n = len(netlist.controls)
    lines = ["#pragma once", "",
             "// GENERATED by plugins/viridian/tools/netlist: exact responses of the netlist (N / D before",
             "// factoring, 30 digits) for the tests. Do not edit by hand. Regenerate with:",
             "//   " + command, "", "#include <array>", "", f"namespace {namespace}", "{", "",
             f"struct {netlist.name}Vectors", "{",
             "    struct Point",
             "    {",
             f"        std::array<double, {max(n, 1)}> controls;",
             "        double frequency, re, im;",
             "    };", "",
             f"    static constexpr std::array<Point, {len(points)}> points {{{{"]
    for ctrl, f, h in points:
        cs = ", ".join(repr(float(x)) for x in (ctrl if n else [0.0]))
        lines.append(f"        {{ {{ {cs} }}, {float(f)!r}, {float(h.real)!r}, {float(h.imag)!r} }},")
    lines += ["    }};", "};", "", f"}} // namespace {namespace}", ""]
    return "\n".join(lines)


def _repo_root():
    here = os.path.abspath(os.path.dirname(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "..", ".."))
