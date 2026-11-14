"""Checks of the Console netlists: ngspice AC sweeps against the published figures (../NOTES.md).

    python3 check.py              the full report (every table in NOTES.md, "ngspice results")
    python3 check.py --calibrate  solve the trimmers; print the values the netlists hold

Two engines read the same files: ngspice 42 (the reference, AC sweeps) and a small nodal solver
here (exact nullor op-amps), which also gives the poles: an AC sweep cannot see an unstable network.
Needs ngspice, numpy and scipy.
"""

import itertools
import math
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
import scipy.linalg

HERE = os.path.dirname(os.path.abspath(__file__))
FAMILIES = ("early", "later")
BANDS = ("hf", "hmf", "lmf", "lf")
STAGE = {"hf": "front", "hmf": "front", "lmf": "back", "lf": "back"}
PART_BANDS = {"front": ("hf", "hmf"), "back": ("lmf", "lf")}
FILTERS_OUT = dict(hpf_in=0, lpf_in=0)
MIDS = ("hmf", "lmf")

# Frequency pots: nominal f = 1 / (2 pi sqrt((Ri + r) Ci (Rf + r) Cf)), r = fraction * pot.
BAND_RC = {
    "early": {
        "hf":  dict(ri=4.3e3, ci=2.2e-9, rf=5.1e3, cf=2.42e-9, pot=50e3),
        "hmf": dict(ri=4.3e3, ci=4.7e-9, rf=5.1e3, cf=4.7e-9, pot=50e3),
        "lmf": dict(ri=4.3e3, ci=15e-9, rf=5.1e3, cf=15e-9, pot=50e3),
        "lf":  dict(ri=4.3e3, ci=80e-9, rf=5.1e3, cf=80e-9, pot=50e3),
    },
    "later": {
        "hf":  dict(ri=3.0e3, ci=2.2e-9, rf=3.6e3, cf=2.42e-9, pot=50e3),
        "hmf": dict(ri=3.0e3, ci=4.7e-9, rf=3.6e3, cf=4.7e-9, pot=50e3),
        "lmf": dict(ri=3.0e3, ci=22e-9, rf=3.6e3, cf=22e-9, pot=50e3),
        "lf":  dict(ri=4.3e3, ci=80e-9, rf=4.7e3, cf=80e-9, pot=50e3),
    },
}

# Filter pots: nominal f = A / (R0 + r); A is fitted (least squares, log) to the -3 dB corners.
FILTER_RC = {
    "early": {"hpf": dict(r0=1.8e3, pot=50e3), "lpf": dict(r0=470.0, pot=10e3)},
    "later": {"hpf": dict(r0=1.8e3, pot=47e3), "lpf": dict(r0=680.0, pot=10e3)},
}

# Calibration (NOTES.md, "Calibration"): full boost, mids at highest Q, HF and LF on bell.
CAL_FREQ = {"hf": 12000.0, "hmf": 3000.0, "lmf": 1000.0, "lf": 80.0}
CAL_TARGET = {"early": 15.0, "later": 18.0}
TRIMS = {"early": ("hmf", "lmf"), "later": ("hf", "hmf", "lmf", "lf")}
TRIM_RANGE = {"hf": 10e3, "lf": 10e3, "hmf": 500.0, "lmf": 500.0}

AUDIO = np.geomspace(20.0, 20e3, 1200)


# ---------------------------------------------------------------------------------------------
# Netlist reader and nodal solver

SUFFIX = {"meg": 1e6, "t": 1e12, "g": 1e9, "k": 1e3, "m": 1e-3, "u": 1e-6, "n": 1e-9, "p": 1e-12, "f": 1e-15}
NUMBER = re.compile(r"(?<![A-Za-z_])(\d+\.?\d*(?:[eE][+-]?\d+)?)(meg|[tgkmunpf])?(?![A-Za-z_])", re.IGNORECASE)


def evaluate(text, params):
    """A SPICE value or {expression} with engineering suffixes and parameters."""
    expr = text[1:-1] if text.startswith("{") else text
    expr = NUMBER.sub(lambda m: repr(float(m.group(1)) * SUFFIX.get((m.group(2) or "").lower(), 1.0)), expr)
    return float(eval(expr, {"__builtins__": {}}, dict(params)))


def parse(netlist, overrides):
    """Elements of a netlist under the given parameters: .param defaults, overrides, .if resolved.

    The subset the Console netlists use (and tools/netlist reads): R, C, V (0 V shorts), X ... opamp.
    """
    with open(os.path.join(HERE, netlist)) as fh:
        lines = [ln.strip() for ln in fh]
    lines = [ln for ln in lines if ln and not ln.startswith("*")]
    params = {}
    for ln in lines:
        if ln.lower().startswith(".param"):
            for name, value in re.findall(r"(\w+)\s*=\s*(\S+)", ln[6:]):
                params[name.lower()] = evaluate(value, params)
    params.update({k.lower(): float(v) for k, v in overrides.items()})

    elements, stack = [], []
    for ln in lines:
        word = ln.split()[0].lower()
        if word == ".if":
            stack.append(bool(evaluate(ln[3:].strip(), params)))
        elif word == ".else":
            stack[-1] = not stack[-1]
        elif word == ".endif":
            stack.pop()
        elif word.startswith(".") or not all(stack):
            continue
        else:
            t = ln.split()
            kind = t[0][0].upper()
            if kind in "RCV":
                value = evaluate(t[3], params)
                # A pot segment at its end is 0 ohm: a short, stamped as a 0 V source.
                elements.append(("V" if kind == "R" and value == 0 else kind, t[0], t[1], t[2], value))
            elif kind == "X" and t[4].lower() == "opamp":
                elements.append(("X", t[0], t[1], t[2], t[3]))
            else:
                raise ValueError(f"{netlist}: unsupported element {ln}")
    return elements


def mna(netlist, overrides):
    """(G, C, b, nodes) of (G + sC) x = b with node `in` driven by 1 V; op-amps are nullors."""
    elements = parse(netlist, overrides)
    nodes = {}
    for e in elements:
        for n in e[2:4] + ((e[4],) if e[0] == "X" else ()):
            if n != "0":
                nodes.setdefault(n, len(nodes))
    branches = sum(1 for e in elements if e[0] in "VX") + 1
    size = len(nodes) + branches
    G, C = np.zeros((size, size)), np.zeros((size, size))

    def idx(n):
        return None if n == "0" else nodes[n]

    def stamp(M, a, b, y):
        for i, j, s in ((a, a, 1), (b, b, 1), (a, b, -1), (b, a, -1)):
            if i is not None and j is not None:
                M[i, j] += s * y

    row = len(nodes)
    for e in elements:
        kind, a, b = e[0], idx(e[2]), idx(e[3])
        if kind == "R":
            stamp(G, a, b, 1.0 / e[4])
        elif kind == "C":
            stamp(C, a, b, e[4])
        elif kind == "V":
            for n, s in ((a, 1), (b, -1)):
                if n is not None:
                    G[n, row] += s
                    G[row, n] += s
            row += 1
        else:
            out = idx(e[4])
            if out is not None:
                G[out, row] += 1
            for n, s in ((a, 1), (b, -1)):
                if n is not None:
                    G[row, n] += s
            row += 1
    G[nodes["in"], row] += 1
    G[row, nodes["in"]] += 1
    b = np.zeros(size)
    b[row] = 1.0
    return G, C, b, nodes


def solve(netlist, overrides, freqs):
    """Every node voltage at every frequency: (array [freq, node], node index)."""
    G, C, b, nodes = mna(netlist, overrides)
    x = np.array([np.linalg.solve(G + 2j * math.pi * f * C, b) for f in freqs])
    return x[:, :len(nodes)], nodes


def response(netlist, overrides, freqs, probe="out"):
    x, nodes = solve(netlist, overrides, freqs)
    return x[:, nodes[probe]]


def poles(netlist, overrides):
    """Finite natural frequencies (rad/s): the roots of det(G + sC)."""
    G, C, _, _ = mna(netlist, overrides)
    w = scipy.linalg.eigvals(G, -C)
    return w[np.isfinite(w) & (np.abs(w) < 1e12)]


def min_damping(netlist, overrides):
    """The smallest damping ratio -Re(p)/|p| over the poles: below 0 the network oscillates."""
    p = poles(netlist, overrides)
    return float(np.min(-p.real / np.abs(p))) if len(p) else 1.0


def opamp_outputs(netlist):
    """Output nodes and names of the op-amps, with every switch in (each name once)."""
    seen = {}
    for over in ({}, dict(hpf_in=1, lpf_in=1)):
        for e in parse(netlist, over):
            if e[0] == "X":
                seen[e[1]] = e[4]
    return list(seen.values()), list(seen.keys())


# ---------------------------------------------------------------------------------------------
# ngspice

def ac(netlist, params, f0=10.0, f1=100e3, ppd=400, probes=("out",)):
    """ngspice AC sweep with parameter overrides: (freqs, complex [freq, probe])."""
    deck = [
        "* check.py",
        f".include {os.path.join(HERE, 'opamp.lib')}",
        f".include {os.path.join(HERE, netlist)}",
        *[f".param {k}={float(v)!r}" for k, v in params.items()],
        "Vin in 0 AC 1",
        ".control",
        "set filetype=ascii",
        "set wr_singlescale",
        f"ac lin 1 {f0} {f1}" if f0 == f1 else f"ac dec {ppd} {f0} {f1}",
        "wrdata __OUT__ " + " ".join(f"v({p})" for p in probes),
        "quit",
        ".endc",
        ".end",
    ]
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "o.txt")
        cir = os.path.join(d, "deck.cir")
        with open(cir, "w") as fh:
            fh.write("\n".join(deck).replace("__OUT__", out) + "\n")
        r = subprocess.run(["ngspice", "-b", cir], capture_output=True, text=True)
        if not os.path.exists(out) or "rror" in r.stdout + r.stderr:
            raise RuntimeError(r.stdout + r.stderr)
        a = np.loadtxt(out, ndmin=2)
    return a[:, 0], a[:, 1::2] + 1j * a[:, 2::2]


# ---------------------------------------------------------------------------------------------
# Measures

def db(h):
    return 20 * np.log10(np.abs(h))


def extremum(f, h, sign):
    """(Hz, dB) of the largest deviation (sign +1 peak, -1 dip), refined parabolically in log f."""
    y = sign * db(h)
    i = int(np.argmax(y))
    if 0 < i < len(f) - 1:
        y0, y1, y2 = y[i - 1:i + 2]
        den = y0 - 2 * y1 + y2
        d = 0.5 * (y0 - y2) / den if den != 0 else 0.0
        lf = np.log(f)
        return float(np.exp(lf[i] + d * (lf[i + 1] - lf[i]))), float(sign * (y1 - 0.25 * (y0 - y2) * d))
    return float(f[i]), float(sign * y[i])


def crossing(f, y, level, i0, step):
    """Log-interpolated frequency where y crosses level, walking from index i0 by step."""
    i = i0
    while 0 <= i + step < len(f):
        j = i + step
        if (y[i] - level) * (y[j] - level) <= 0 and y[i] != y[j]:
            t = (level - y[i]) / (y[j] - y[i])
            return float(np.exp(np.log(f[i]) + t * (np.log(f[j]) - np.log(f[i]))))
        i = j
    return float("nan")


def bell_q(f, h, sign, drop=3.0):
    """fc / bandwidth, the edges `drop` dB below the extremum, fc their geometric mean."""
    y = sign * db(h)
    i = int(np.argmax(y))
    lo = crossing(f, y, y[i] - drop, i, -1)
    hi = crossing(f, y, y[i] - drop, i, +1)
    return math.sqrt(lo * hi) / (hi - lo)


# ---------------------------------------------------------------------------------------------
# Controls and laws

def nominal_freq(fam, band, x):
    b = BAND_RC[fam][band]
    r = b["pot"] * x
    return 1.0 / (2 * math.pi * math.sqrt((b["ri"] + r) * b["ci"] * (b["rf"] + r) * b["cf"]))


def freq_fraction(fam, band, hz):
    """The rheostat fraction whose nominal frequency is hz: the root of (Ri + r)(Rf + r) = k."""
    b = BAND_RC[fam][band]
    k = 1.0 / ((2 * math.pi * hz) ** 2 * b["ci"] * b["cf"])
    s = b["ri"] + b["rf"]
    r = (-s + math.sqrt(s * s - 4 * (b["ri"] * b["rf"] - k))) / 2
    return r / b["pot"]


def band_params(band, gain, freq, shape=None):
    """Netlist parameters for one band; shape is the Q fraction (mids) or 1 bell / 0 shelf."""
    p = {f"{band}_gain": gain, f"{band}_freq": freq}
    if shape is not None:
        p[f"{band}_q" if band in MIDS else f"{band}_bell"] = shape
    return p


def stage(fam, band):
    return f"{fam}_{STAGE[band]}.cir"


def window(hz):
    return np.geomspace(max(hz / 10, 10.0), min(hz * 10, 100e3), 1500)


def relative(fam, band, p, f):
    """A band's response divided by the same stage with its gain at the centre tap."""
    return response(stage(fam, band), p, f) / response(stage(fam, band), {**p, f"{band}_gain": 0.5}, f)


def cal_peak(fam, band, gain=1.0, shape=1.0, extra=None):
    """(Hz, dB) of the band's deviation at its calibration point."""
    hz = CAL_FREQ[band]
    p = band_params(band, gain, freq_fraction(fam, band, hz), shape)
    p.update(extra or {})
    f = window(hz)
    return extremum(f, relative(fam, band, p, f), 1 if gain >= 0.5 else -1)


def cal_q(fam, band, q):
    hz = CAL_FREQ[band]
    p = band_params(band, 1.0, freq_fraction(fam, band, hz), q)
    f = window(hz)
    return bell_q(f, relative(fam, band, p, f), 1)


def calibrate(fam, band):
    """The trimmer for CAL_TARGET, on the stable branch (below it the band-pass oscillates)."""
    hz = CAL_FREQ[band]
    base = band_params(band, 1.0, freq_fraction(fam, band, hz), 1.0)
    key = f"{band}_trim"

    def is_stable(t):
        return min_damping(stage(fam, band), {**base, key: t}) > 0

    lo, hi = 0.0, TRIM_RANGE[band]
    if not is_stable(hi):
        raise RuntimeError(f"{fam} {band}: unstable at the trimmer's end")
    if not is_stable(lo):
        for _ in range(50):
            mid = 0.5 * (lo + hi)
            lo, hi = (lo, mid) if is_stable(mid) else (mid, hi)
        lo, hi = hi, TRIM_RANGE[band]
    edge = lo
    for _ in range(50):
        mid = 0.5 * (lo + hi)
        if cal_peak(fam, band, extra={key: mid})[1] > CAL_TARGET[fam]:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi), edge


def calibrate_hpf_gain():
    """RV5 for no level step at 1 kHz when the high-pass switches in at its lowest corner."""
    off = response("later_front.cir", FILTERS_OUT, [1e3])

    def gain(trim):
        h = response("later_front.cir", dict(hpf_freq=1.0, hpf_in=1, lpf_in=0, hpf_gain_trim=trim), [1e3])
        return float(db(h / off)[0])

    lo, hi = 0.0, 500.0
    for _ in range(50):
        mid = 0.5 * (lo + hi)
        lo, hi = (mid, hi) if gain(mid) > 0 else (lo, mid)
    return 0.5 * (lo + hi)


def filter_corner(fam, kind, x, engine="mna"):
    """-3 dB corner of one filter (the other bypassed), the EQ flat; re the front with both out."""
    p = dict(hpf_freq=x, hpf_in=1, lpf_in=0) if kind == "hpf" else dict(lpf_freq=x, hpf_in=0, lpf_in=1)
    f0, f1 = (0.1, 20e3) if kind == "hpf" else (100.0, 2e6)
    if engine == "mna":
        f = np.geomspace(f0, f1, 3000)
        h = response(f"{fam}_front.cir", p, f) / response(f"{fam}_front.cir", FILTERS_OUT, f)
    else:
        f, h = ac(f"{fam}_front.cir", p, f0, f1, 400)
        _, h0 = ac(f"{fam}_front.cir", FILTERS_OUT, f0, f1, 400)
        h = h[:, 0] / h0[:, 0]
    d = db(h)
    if kind == "hpf":
        ref = float(np.interp(np.log(5e3), np.log(f), d))
        return crossing(f, d, ref - 3, len(f) - 1, -1), ref, f, d
    ref = float(np.interp(np.log(100.0), np.log(f), d))
    return crossing(f, d, ref - 3, 0, 1), ref, f, d


def filter_constant(fam, kind):
    """A of f = A / (R0 + r), fitted in log to the corners at 21 positions; and the worst error."""
    rc = FILTER_RC[fam][kind]
    xs = np.linspace(0, 1, 21)
    fc = np.array([filter_corner(fam, kind, x)[0] for x in xs])
    k = fc * (rc["r0"] + xs * rc["pot"])
    a = float(np.exp(np.mean(np.log(k))))
    return a, float(np.max(np.abs(k / a - 1)))


# ---------------------------------------------------------------------------------------------
# Report

def table(head, rows):
    print("| " + " | ".join(head) + " |")
    print("|" + "|".join("---" for _ in head) + "|")
    for r in rows:
        print("| " + " | ".join(str(c) for c in r) + " |")
    print()


def section_calibration():
    print("## Calibration\n")
    rows = []
    for fam in FAMILIES:
        for band in TRIMS[fam]:
            netlist_value = parse_param(stage(fam, band), f"{band}_trim")
            trim, edge = calibrate(fam, band)
            spice_peak = spice_cal_peak(fam, band)
            rows.append((fam, band, f"{trim:.2f}", f"{netlist_value:.2f}", f"{edge:.1f}",
                         f"{spice_peak[1]:+.3f} dB at {spice_peak[0]:.0f} Hz"))
    table(("card", "band", "solved (ohm)", "netlist (ohm)", "stable above (ohm)", "ngspice, netlist value"),
          rows)
    _, h = ac("later_front.cir", dict(hpf_freq=1.0, hpf_in=1, lpf_in=0), 1e3, 1e3)
    _, h0 = ac("later_front.cir", FILTERS_OUT, 1e3, 1e3)
    print(f"later RV5: solved {calibrate_hpf_gain():.2f} ohm, netlist "
          f"{parse_param('later_front.cir', 'hpf_gain_trim'):.2f} ohm; level step at 1 kHz when the "
          f"high-pass switches in at its lowest corner {float(db(h[:, 0] / h0[:, 0])[0]):+.4f} dB (ngspice)\n")


def parse_param(netlist, name):
    with open(os.path.join(HERE, netlist)) as fh:
        for ln in fh:
            if ln.lower().startswith(".param"):
                for k, v in re.findall(r"(\w+)\s*=\s*(\S+)", ln[6:]):
                    if k.lower() == name:
                        return evaluate(v, {})
    raise KeyError(name)


def spice_cal_peak(fam, band):
    hz = CAL_FREQ[band]
    p = band_params(band, 1.0, freq_fraction(fam, band, hz), 1.0)
    f, h = ac(stage(fam, band), p, hz / 10, min(hz * 10, 100e3))
    _, h0 = ac(stage(fam, band), {**p, f"{band}_gain": 0.5}, hz / 10, min(hz * 10, 100e3))
    return extremum(f, h[:, 0] / h0[:, 0], 1)


def random_settings(rng, part):
    p = {}
    for band in PART_BANDS[part]:
        gain, freq = rng.random(), rng.random()
        shape = rng.random() if band in MIDS else int(rng.random() < .5)
        p.update(band_params(band, gain, freq, shape))
    if part == "front":
        p.update(hpf_freq=rng.random(), lpf_freq=rng.random(),
                 hpf_in=int(rng.random() < .5), lpf_in=int(rng.random() < .5))
    return p


def whole_channel(fam):
    """A temporary netlist of the whole card (front into back), to show the split is exact."""
    def renamed(part, mapping):
        out = []
        with open(os.path.join(HERE, f"{fam}_{part}.cir")) as fh:
            for ln in fh:
                t = ln.split()
                if not t or ln.startswith(("*", ".")):
                    out.append(ln.rstrip())
                    continue
                for i in (1, 2, 3) if t[0][0].upper() == "X" else (1, 2):
                    t[i] = mapping(t[i])
                t[0] = t[0] + ("_b" if part == "back" else "")
                out.append(" ".join(t))
        return "\n".join(out) + "\n"

    text = renamed("front", lambda n: "mid" if n == "out" else n)
    text += renamed("back", lambda n: "mid" if n == "in" else (n if n in ("0", "out") else "b_" + n))
    path = os.path.join(tempfile.mkdtemp(), f"{fam}_whole.cir")
    with open(path, "w") as fh:
        fh.write(text)
    return path


def section_agreement():
    print("## ngspice against the nodal solver; the front/back split\n")
    rng = np.random.default_rng(7)
    worst_db, worst_deg, n = 0.0, 0.0, 0
    for fam in FAMILIES:
        for part in ("front", "back"):
            netlist = f"{fam}_{part}.cir"
            for _ in range(18):
                p = random_settings(rng, part)
                f, h = ac(netlist, p, 10.0, 40e3, 50)
                hm = response(netlist, p, f)
                worst_db = max(worst_db, float(np.max(np.abs(db(h[:, 0]) - db(hm)))))
                worst_deg = max(worst_deg, float(np.max(np.abs(np.angle(h[:, 0] / hm, deg=True)))))
                n += 1
    print(f"{n} random settings over the four netlists, 10 Hz - 40 kHz: worst difference "
          f"{worst_db:.1e} dB, {worst_deg:.1e} degrees\n")
    worst, n = 0.0, 0
    for fam in FAMILIES:
        whole = whole_channel(fam)
        for _ in range(40):
            p = {**random_settings(rng, "front"), **random_settings(rng, "back")}
            pf = {k: v for k, v in p.items() if not k.startswith(("lmf", "lf"))}
            pb = {k: v for k, v in p.items() if k.startswith(("lmf", "lf"))}
            split = response(f"{fam}_front.cir", pf, AUDIO) * response(f"{fam}_back.cir", pb, AUDIO)
            worst = max(worst, float(np.max(np.abs(db(response(whole, p, AUDIO)) - db(split)))))
            n += 1
    print(f"front x back against one netlist of the whole card, {n} random settings: worst difference "
          f"{worst:.1e} dB\n")


def section_stability():
    print("## Stability: the least damped pole over a grid of every control (nodal solver)\n")
    rows = []
    g5 = (0.0, 0.25, 0.5, 0.75, 1.0)
    g3 = (0.0, 0.5, 1.0)
    for fam in FAMILIES:
        for part in ("front", "back"):
            a, b = PART_BANDS[part]
            grids = [(g5, [FILTERS_OUT] if part == "front" else [{}])]
            if part == "front":
                grids.append((g3, [dict(hpf_freq=x, lpf_freq=y, hpf_in=i, lpf_in=j)
                                   for x, y, i, j in itertools.product(g3, g3, (0, 1), (0, 1))]))
            worst, count = (2.0, None), 0
            for levels, filters in grids:
                sa = levels if a in MIDS else (0, 1)
                sb = levels if b in MIDS else (0, 1)
                grid = itertools.product(levels, levels, sa, levels, levels, sb, filters)
                for ga, xa, qa, gb, xb, qb, fp in grid:
                    p = {**band_params(a, ga, xa, qa), **band_params(b, gb, xb, qb), **fp}
                    worst = min(worst, (min_damping(f"{fam}_{part}.cir", p), p), key=lambda w: w[0])
                    count += 1
            rows.append((f"{fam}_{part}.cir", count, f"{worst[0]:.3f}", worst[1]))
    table(("netlist", "settings", "least damping ratio", "at"), rows)


def section_mids():
    print("## Mid bands (ngspice): travel ends, Q ends, full boost / full cut\n")
    rows = []
    for fam in FAMILIES:
        for band in MIDS:
            for x in (0.0, 0.5, 1.0):
                for q in (0.0, 0.5, 1.0):
                    cells = []
                    for g in (1.0, 0.0):
                        lo, hi = nominal_freq(fam, band, x) / 10, min(nominal_freq(fam, band, x) * 10, 100e3)
                        f, h = ac(stage(fam, band), band_params(band, g, x, q), lo, hi)
                        _, h0 = ac(stage(fam, band), band_params(band, 0.5, x, q), lo, hi)
                        s = 1 if g > 0.5 else -1
                        fp, gp = extremum(f, h[:, 0] / h0[:, 0], s)
                        cells.append((fp, gp, bell_q(f, h[:, 0] / h0[:, 0], s)))
                    rows.append((fam, band, f"{x:.1f}", f"{nominal_freq(fam, band, x):.0f}", f"{q:.1f}",
                                 f"{cells[0][0]:.0f}", f"{cells[0][1]:+.2f}", f"{cells[0][2]:.2f}",
                                 f"{cells[1][1]:+.2f}", f"{cells[1][2]:.2f}"))
    table(("card", "band", "freq pot", "nominal Hz", "Q pot", "peak Hz", "boost dB", "Q", "cut dB", "Q cut"),
          rows)


def section_ends():
    print("## HF and LF (ngspice): bell and shelf, full boost / full cut\n")
    rows = []
    for fam in FAMILIES:
        for band in ("hf", "lf"):
            for x in (0.0, 0.5, 1.0):
                nom = nominal_freq(fam, band, x)
                lo, hi = max(nom / 30, 5.0), min(nom * 30, 100e3)
                bell = []
                for g in (1.0, 0.0):
                    f, h = ac(stage(fam, band), band_params(band, g, x, 1), lo, hi)
                    _, h0 = ac(stage(fam, band), band_params(band, 0.5, x, 1), lo, hi)
                    s = 1 if g > 0.5 else -1
                    fp, gp = extremum(f, h[:, 0] / h0[:, 0], s)
                    bell.append((fp, gp, bell_q(f, h[:, 0] / h0[:, 0], s)))
                shelf = []
                for g in (1.0, 0.0):
                    f, h = ac(stage(fam, band), band_params(band, g, x, 0), 20.0, 20e3)
                    _, h0 = ac(stage(fam, band), band_params(band, 0.5, x, 0), 20.0, 20e3)
                    d = db(h[:, 0] / h0[:, 0])
                    edge = d[-1] if band == "hf" else d[0]
                    ext = float(np.max(d) if g > 0.5 else np.min(d))
                    far = float(np.min(d) if g > 0.5 else np.max(d))
                    slope = float(np.max(np.abs(np.diff(d) / np.diff(np.log2(f)))))
                    shelf.append((edge, ext, slope, far))
                rows.append((fam, band, f"{x:.1f}", f"{nom:.0f}", f"{bell[0][0]:.0f}", f"{bell[0][1]:+.2f}",
                             f"{bell[0][2]:.2f}", f"{bell[1][1]:+.2f}", f"{bell[1][2]:.2f}",
                             f"{shelf[0][0]:+.2f} / {shelf[1][0]:+.2f}",
                             f"{shelf[0][1]:+.2f} / {shelf[1][1]:+.2f}",
                             f"{shelf[0][3]:+.3f} / {shelf[1][3]:+.3f}",
                             f"{max(shelf[0][2], shelf[1][2]):.1f}"))
    table(("card", "band", "freq pot", "nominal Hz", "bell Hz", "bell boost", "Q", "bell cut", "Q cut",
           "shelf at 20 kHz / 20 Hz (boost / cut)", "shelf extreme in band", "opposite excursion",
           "max slope dB/oct"), rows)


def section_laws():
    print("## Laws at the calibration points (nodal solver)\n")
    gs = (0.0, 0.05, 0.1, 0.2, 0.3, 0.4, 0.45, 0.5, 0.55, 0.6, 0.7, 0.8, 0.9, 0.95, 1.0)
    rows = []
    for fam in FAMILIES:
        for band in BANDS:
            rows.append((fam, band, *[("0" if g == 0.5 else f"{cal_peak(fam, band, g)[1]:+.2f}") for g in gs]))
    print("Gain: the band's deviation (dB) against the boost/cut wiper.\n")
    table(("card", "band", *[str(g) for g in gs]), rows)
    qs = (0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0)
    rows = [(fam, band, *[f"{cal_q(fam, band, q):.2f}" for q in qs]) for fam in FAMILIES for band in MIDS]
    print("Q (-3 dB from the peak, full boost) against the Q pot's gang-a wiper.\n")
    table(("card", "band", *[str(q) for q in qs]), rows)


def section_q_vs_gain():
    print("## Q against gain at the calibration points (nodal solver)\n")
    print("Bandwidth at half the peak (in dB), so it stays defined at small boosts.\n")
    gs = (0.55, 0.6, 0.7, 0.8, 0.9, 1.0)
    rows = []
    for fam in FAMILIES:
        for band in MIDS:
            for q in (0.0, 0.5, 1.0):
                hz = CAL_FREQ[band]
                x = freq_fraction(fam, band, hz)
                f = window(hz)
                cells = []
                for g in gs:
                    h = relative(fam, band, band_params(band, g, x, q), f)
                    peak = extremum(f, h, 1)[1]
                    cells.append(f"{peak:+.1f} dB: {bell_q(f, h, 1, drop=peak / 2):.2f}")
                rows.append((fam, band, f"{q:.1f}", *cells))
    table(("card", "band", "Q pot", *[f"gain {g}" for g in gs]), rows)


def section_filters():
    print("## Filters (ngspice)\n")
    rows = []
    for fam in FAMILIES:
        for kind in ("hpf", "lpf"):
            a, err = filter_constant(fam, kind)
            rc = FILTER_RC[fam][kind]
            for x in (0.0, 0.25, 0.5, 1.0):
                fc, ref, f, d = filter_corner(fam, kind, x, "spice")
                far = (fc / 8, fc / 4) if kind == "hpf" else (fc * 4, fc * 8)
                lo_db, hi_db = np.interp(np.log(far), np.log(f), d)
                slope = abs(float(hi_db - lo_db))
                peak = float(np.max(d) - ref)
                rows.append((fam, kind, f"{x:.2f}", f"{fc:.1f}", f"{a / (rc['r0'] + x * rc['pot']):.1f}",
                             f"{ref:+.3f}", f"{slope:.1f}", f"{peak:+.3f}"))
            print(f"{fam} {kind}: A = {a:.6g} ohm*Hz, nominal law within {100 * err:.2f} % of the -3 dB corner")
    print()
    table(("card", "filter", "pot", "-3 dB Hz", "nominal Hz", "passband dB",
           "slope dB/oct, fc/8-fc/4 or 4fc-8fc", "max over passband dB"), rows)


def section_flat():
    print("## Flat (filters out, every gain at its centre): whole channel, nodal solver and ngspice\n")
    rows = []
    for fam in FAMILIES:
        h = response(f"{fam}_front.cir", {}, AUDIO) * response(f"{fam}_back.cir", {}, AUDIO)
        f1, s1 = ac(f"{fam}_front.cir", {}, 20.0, 20e3, 200)
        _, s2 = ac(f"{fam}_back.cir", {}, 20.0, 20e3, 200)
        hs = s1[:, 0] * s2[:, 0]
        d = db(h)
        at = (20, 100, 1e3, 5e3, 10e3, 15e3, 20e3)
        cells = [f"{v:+.3f}" for v in np.interp(np.log(at), np.log(AUDIO), d)]
        rows.append((fam, *cells, f"{d.min():+.3f} / {d.max():+.3f}",
                     f"{db(hs).min():+.3f} / {db(hs).max():+.3f}"))
    table(("card", "20 Hz", "100 Hz", "1 kHz", "5 kHz", "10 kHz", "15 kHz", "20 kHz", "min / max",
           "ngspice min / max"), rows)
    print("Every gain at its centre, the other controls over a grid (filters out): the largest deviation.\n")
    rows = []
    f = np.geomspace(20.0, 20e3, 300)
    for fam in FAMILIES:
        for part in ("front", "back"):
            a, b = PART_BANDS[part]
            sa = (0.0, 0.5, 1.0) if a in MIDS else (0, 1)
            sb = (0.0, 0.5, 1.0) if b in MIDS else (0, 1)
            worst = (0.0, None, 0.0, 0.0)
            for xa, qa, xb, qb in itertools.product((0, 0.25, 0.5, 0.75, 1), sa, (0, 0.25, 0.5, 0.75, 1), sb):
                p = {**band_params(a, 0.5, xa, qa), **band_params(b, 0.5, xb, qb)}
                d = db(response(f"{fam}_{part}.cir", p, f))
                i = int(np.argmax(np.abs(d)))
                if abs(d[i]) > abs(worst[0]):
                    worst = (float(d[i]), p, float(f[i]), 0.0)
            label = ", ".join(f"{k} {v:g}" for k, v in worst[1].items() if not k.endswith("_gain"))
            rows.append((f"{fam}_{part}.cir", f"{worst[0]:+.3f} dB at {worst[2]:.0f} Hz", label))
    table(("netlist", "largest deviation", "at"), rows)


def section_headroom():
    print("## Headroom: largest op-amp output re the stage input, 20 Hz - 20 kHz (nodal solver)\n")
    rows = []
    g3 = (0.0, 1.0)
    x3 = (0.0, 0.5, 1.0)
    for fam in FAMILIES:
        for part in ("front", "back"):
            netlist = f"{fam}_{part}.cir"
            outs, names = opamp_outputs(netlist)
            worst = {n: (-200.0, "") for n in names}
            a, b = PART_BANDS[part]
            settings = []
            if part == "front":
                for i, j, x in itertools.product((0, 1), (0, 1), (0.0, 1.0)):
                    settings.append((dict(hpf_in=i, lpf_in=j, hpf_freq=x, lpf_freq=x),
                                     f"filters {'in' if i else 'out'}/{'in' if j else 'out'}, pots {x}"))
            for band in (a, b):
                for g, x, s in itertools.product(g3, x3, (0.0, 1.0)):
                    settings.append((band_params(band, g, x, s), f"{band} {'boost' if g else 'cut'} pot {x} "
                                     f"{('Q ' + str(s)) if band in MIDS else ('bell' if s else 'shelf')}"))
            settings.append(({**band_params(a, 1, 0.5, 1), **band_params(b, 1, 0.5, 1)}, "both boost, mid"))
            for p, label in settings:
                x, nodes = solve(netlist, p, AUDIO)
                for o, n in zip(outs, names):
                    if o not in nodes:
                        continue
                    g = float(np.max(db(x[:, nodes[o]])))
                    if g > worst[n][0]:
                        worst[n] = (g, label)
            for o, n in zip(outs, names):
                rows.append((netlist, n, o, f"{worst[n][0]:+.1f}", worst[n][1]))
    table(("netlist", "op-amp", "node", "max gain dB", "at"), rows)


def cmd_calibrate():
    for fam in FAMILIES:
        for band in TRIMS[fam]:
            trim, edge = calibrate(fam, band)
            print(f"{fam} {band}_trim = {trim:.2f} ohm   (stable above {edge:.1f} ohm)")
    print(f"later hpf_gain_trim = {calibrate_hpf_gain():.2f} ohm")


def cmd_report():
    section_calibration()
    section_agreement()
    section_stability()
    section_mids()
    section_ends()
    section_laws()
    section_q_vs_gain()
    section_filters()
    section_flat()
    section_headroom()


if __name__ == "__main__":
    if "--calibrate" in sys.argv:
        cmd_calibrate()
    else:
        cmd_report()
