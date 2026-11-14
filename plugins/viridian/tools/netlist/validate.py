#!/usr/bin/env python3
"""
Validates the network tool on its reference circuits (reference/*.cir); ctest runs it as
"network: tools/netlist reference circuits".

1. Closed forms: the tool's H(s) equals the hand-derived transfer function exactly (sympy), in
   every switch state. The derivations are independent of the nodal analysis (written from the
   circuit by inspection, ideal op-amps as virtual earths and followers).
2. ngspice: gain x product of the generated blocks against ngspice AC (10 Hz ... 100 kHz,
   40 points per decade) over a grid of control settings: within 0.01 dB and 0.1 degree.
3. Generated headers are current: regenerating every reference netlist reproduces its checked-in
   header byte for byte (--all: every netlist under plugins/viridian with a '*@ generate'), and
   every header's stamp equals its netlist's and includes' digest (sources.py, which ctest also
   runs alone, without sympy or ngspice).

    python3 plugins/viridian/tools/netlist/validate.py [--all]
"""

import argparse
import difflib
import os
import sys

import sympy as sp

sys.dont_write_bytecode = True     # no __pycache__ in the source tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import codegen   # noqa: E402
import mna       # noqa: E402
import netlist   # noqa: E402
import sources   # noqa: E402
import spice     # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REFERENCE = os.path.join(HERE, "reference")
S = mna.S


def closed_forms():
    """name -> function (params, symbols, switch values) -> H(s)."""

    def rc_lowpass(p, x, sw):
        return 1 / (1 + S * p["cap"] * (p["rmin"] + x["pos"] * p["rpot"]))

    def rlc_bell(p, x, sw):
        r2 = p["rmin"] + x["depth"] * p["rpot"]
        lc = p["ind"] * p["cap"]
        return (S**2 * lc + S * r2 * p["cap"] + 1) / (S**2 * lc + S * (p["r1"] + r2) * p["cap"] + 1)

    def gyrator_bell(p, x, sw):
        a, b = p["p"] * (1 - x["boost"]), p["p"] * x["boost"]
        c2 = sp.Rational(47, 10**9) if sw["range"] else sp.Rational(100, 10**9)
        rg2 = p["rgmin"] + x["freq"] * p["rgpot"]
        z = p["rs"] + p["rg1"] * (1 + S * p["cg"] * rg2) / (1 + S * p["cg"] * p["rg1"]) + 1 / (S * c2)
        return (a * b + z * (a + b) + p["r2"] * a) / (a * b + z * (a + b) + p["r1"] * b)

    def baxandall_shelf(p, x, sw):
        c = sp.Rational(22, 10**9) if sw["turnover"] else sp.Rational(47, 10**9)

        def parallel(r):
            return r / (1 + S * c * r)

        zi = p["r1"] + parallel(p["p"] * (1 - x["bass"]))
        zf = p["r2"] + parallel(p["p"] * x["bass"])
        return -zf / zi

    def stepped_bell(p, x, sw):
        cap = sp.Rational(47, 10**9) + x["cap"] * (sp.Rational(1, 10**6) - sp.Rational(47, 10**9))
        r2 = p["rmin"] + x["depth"] * p["rpot"]
        lc = p["ind"] * cap
        return (S**2 * lc + S * r2 * cap + 1) / (S**2 * lc + S * (p["r1"] + r2) * cap + 1)

    return {"RcLowpass": rc_lowpass, "RlcBell": rlc_bell, "GyratorBell": gyrator_bell,
            "BaxandallShelf": baxandall_shelf, "SteppedBell": stepped_bell}


def check_closed_form(a, form):
    params = {k: v for k, v in a.netlist.params.items() if isinstance(v, sp.Basic) and not v.free_symbols}
    symbols = {c.name: c.symbol for c in a.pots}
    for state, N, D, _, _, _ in a.per_state:
        sw = {c.name: v for c, v in zip(a.switches, state[0])}
        difference = sp.cancel(N.as_expr() / D.as_expr() - form(params, symbols, sw))
        if difference != 0:
            return f"differs from the closed form in state {state}: {difference}"
    return None


def check_fresh(path, analysis):
    """Regenerates from the analysis and compares with the checked-in files."""
    header, vectors = spice.read(path).generate
    args = argparse.Namespace(netlist=path, out=header, vectors=vectors, namespace="viridian::dsp::generated")
    texts = netlist.texts(analysis, args, netlist.command_line(args))
    fresh = {header: texts[0]}
    if vectors:
        fresh[vectors] = texts[1]
    for target, text in fresh.items():
        if not os.path.exists(target):
            return f"{os.path.relpath(target)} is missing"
        with open(target, encoding="utf-8") as fh:
            current = fh.read()
        if current != text:
            diff = "".join(list(difflib.unified_diff(current.splitlines(True), text.splitlines(True), n=1))[:20])
            return f"{os.path.relpath(target)} is out of date (run netlist.py regenerate):\n{diff}"
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--all", action="store_true", help="also every model netlist with a '*@ generate' directive")
    args = p.parse_args()

    failures = []
    for header, source, why in sources.stale_headers(os.path.normpath(os.path.join(HERE, "..", "..")), codegen._repo_root()):
        failures.append(os.path.basename(header))
        print(f"{os.path.relpath(header)}: stale ({why})")
    forms = closed_forms()
    paths = sorted(os.path.join(REFERENCE, f) for f in os.listdir(REFERENCE) if f.endswith(".cir"))
    if args.all:
        paths = sorted(set(paths) | set(netlist.netlists_with_targets()))

    for path in paths:
        name = spice.read(path).name
        a = netlist.Analysis(path)
        if name in forms:
            problem = check_closed_form(a, forms[name])
            print(f"{name}: closed form {'FAILED: ' + problem if problem else 'exact'}")
            if problem:
                failures.append(name)
        worst_db, worst_deg, worst_factor = netlist.check(path, verbose=True, analysis=a)
        if not (worst_db < 0.01 and worst_deg < 0.1 and worst_factor < 1e-9):
            failures.append(name)
            print(f"{name}: FAILED against ngspice (gate 0.01 dB, 0.1 deg; blocks vs N/D 1e-9)")
        if spice.read(path).generate:
            problem = check_fresh(path, a)
            print(f"{name}: generated headers {'FAILED: ' + problem if problem else 'current'}")
            if problem:
                failures.append(name)

    for name in forms:
        if not any(spice.read(p).name == name for p in paths):
            failures.append(name)
            print(f"{name}: reference netlist missing")

    if failures:
        print(f"FAILED: {', '.join(sorted(set(failures)))}")
        return 1
    print(f"all {len(paths)} netlists pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
