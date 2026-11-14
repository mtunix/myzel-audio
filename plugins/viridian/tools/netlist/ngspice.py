"""
AC analysis of a netlist in ngspice at given control values: the reference the generated
transfer functions are checked against. The netlist runs unchanged; only its .control block is
replaced by one that sets the parameters, sweeps and writes V(output) / V(input).
"""

import os
import shutil
import subprocess
import tempfile

import numpy as np

from spice import NetlistError


def available():
    return shutil.which("ngspice") is not None


def _deck(path):
    """The netlist's own lines without its .control blocks and .end."""
    out, in_control = [], False
    with open(path, encoding="utf-8") as fh:
        for line in fh.read().splitlines():
            low = line.strip().lower()
            if low.startswith(".control"):
                in_control = True
                continue
            if low.startswith(".endc"):
                in_control = False
                continue
            if in_control or low == ".end":
                continue
            if low.startswith(".include") or low.startswith(".inc "):
                target = line.strip().split(None, 1)[1].strip().strip('"')
                line = ".include " + os.path.join(os.path.dirname(os.path.abspath(path)), target)
            out.append(line)
    return out


def ac(netlist, settings, points_per_decade=40, start=10.0, stop=100000.0):
    """[(frequencies, complex H)] for each dict in `settings` (control name -> value)."""
    if not available():
        raise NetlistError("ngspice is not installed")
    source = next(e for e in netlist.elements if e.name == netlist.input)
    with tempfile.TemporaryDirectory() as tmp:
        lines = _deck(netlist.source)
        lines += [".control", "set wr_singlescale", "option numdgt=15"]
        for k, values in enumerate(settings):
            kinds = {c.name: c for c in netlist.controls}
            for name, v in values.items():
                c = kinds.get(name)
                lines.append(f"alterparam {name}={(c.spice_value(v) if c else v)!r}")
            lines.append("reset")
            lines.append(f"ac dec {points_per_decade} {start!r} {stop!r}")
            probes = [netlist.output] + [n for n in source.nodes if n != "0"]
            lines.append(f"wrdata {os.path.join(tmp, f'r{k}.txt')} " + " ".join(f"v({n})" for n in probes))
        lines += [".endc", ".end", ""]
        deck = os.path.join(tmp, "deck.cir")
        with open(deck, "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines))
        run = subprocess.run(["ngspice", "-b", deck], capture_output=True, text=True, cwd=tmp)
        results = []
        for k in range(len(settings)):
            path = os.path.join(tmp, f"r{k}.txt")
            if not os.path.exists(path):
                raise NetlistError("ngspice failed:\n" + run.stdout[-2000:] + run.stderr[-2000:])
            d = np.loadtxt(path, ndmin=2)
            columns = [d[:, 1 + 2 * j] + 1j * d[:, 2 + 2 * j] for j in range(len(probes))]
            vin = columns[1] if source.nodes[0] != "0" else -columns[1]
            if len(columns) > 2:
                vin = columns[1] - columns[2]
            results.append((d[:, 0], columns[0] / vin))
        return results
