#!/usr/bin/env python3
"""Tape playback losses (Bertram, Theory of Magnetic Recording, 1994) and their low-order fits.

Each loss is a function of one dimensionless variable x = length * f / v (the length over the
recorded wavelength), so one analog prototype per mechanism serves every speed: the section's
cutoffs scale with v / length.

    gap       |sin(pi x) / (pi x)|          x = g f / v       (gap length g)
    spacing   exp(-2 pi x)                  x = d f / v       (head-to-tape spacing d, Wallace)
    thickness (1 - exp(-2 pi x)) / (2 pi x) x = delta f / v   (effective recording depth delta)

The fits are cascades of TPT sections (one-pole and 2-pole low-pass, Q), optimised for the
largest dB error where the closed form is above -24 dB, and required never to exceed the closed
form by more than the tolerance anywhere beyond. Prints the constants for TapeLosses.h and the
fit errors.

    python3 tape_losses.py            fit and print
    python3 tape_losses.py --check    only verify the constants in TapeLosses.h
"""
import re
import sys
from pathlib import Path

import numpy as np

FLOORS_DB = {"gap": -12.0, "spacing": -24.0, "thickness": -24.0}
# The range each fit has to cover: the largest x any machine reaches at 16 kHz at its slowest
# loss speed is 0.64 (gap), 0.75 (spacing), 0.53 (thickness); beyond, the fit only has to stay down.
RANGES = {"gap": 1.5, "spacing": 1.5, "thickness": 1.0}


def gap(x):
    return np.abs(np.sinc(x))


def spacing(x):
    return np.exp(-2.0 * np.pi * x)


def thickness(x):
    k = 2.0 * np.pi * np.maximum(x, 1e-12)
    return (1.0 - np.exp(-k)) / k


def lp1(x, c):
    r = x / c
    return 1.0 / np.sqrt(1.0 + r * r)


def lp2(x, c, q):
    r = x / c
    return 1.0 / np.sqrt((1.0 - r * r) ** 2 + (r / q) ** 2)


MODELS = {
    # name: (closed form, structure, initial parameters); "1+2x2" = one-pole, two 2-pole sections
    "gap": (gap, "2x2", [0.55, 0.8, 0.75, 1.5]),
    "spacing": (spacing, "2x2x2", [0.15, 0.6, 0.3, 0.6, 0.6, 0.6]),
    "thickness": (thickness, "1+2x2", [0.15, 0.6, 0.7, 1.5, 0.7]),
}


def model(structure, p, x):
    p = np.abs(p)
    if structure == "2x2":
        return lp2(x, p[0], p[1]) * lp2(x, p[2], p[3])
    if structure == "2x2x2":
        return lp2(x, p[0], p[1]) * lp2(x, p[2], p[3]) * lp2(x, p[4], p[5])
    return lp1(x, p[0]) * lp2(x, p[1], p[2]) * lp2(x, p[3], p[4])


def errors(closed, structure, p, x, floor, limit=10.0):
    target = 20 * np.log10(np.maximum(closed(x), 1e-12))
    fit = 20 * np.log10(model(structure, p, x))
    inside = (target >= floor) & (x <= limit)
    err_in = np.max(np.abs(fit[inside] - target[inside]))
    # Beyond the floor (or the range) the fit must stay down: never louder than the closed form's
    # level at the range's end, so it keeps falling.
    end = min(floor, np.min(target[inside])) if np.any(inside) else floor
    over = np.max(np.maximum(fit[~inside] - end, 0.0)) if np.any(~inside) else 0.0
    return err_in, over


def cost(closed, structure, p, x, floor, limit=10.0):
    e, over = errors(closed, structure, p, x, floor, limit)
    return e + 4.0 * over


def nelder_mead(f, x0, step=0.2, iters=6000, tol=1e-10):
    n = len(x0)
    pts = [np.array(x0, float)]
    for i in range(n):
        y = np.array(x0, float)
        y[i] *= 1.0 + step
        pts.append(y)
    vals = [f(p) for p in pts]
    for _ in range(iters):
        order = np.argsort(vals)
        pts = [pts[i] for i in order]
        vals = [vals[i] for i in order]
        if abs(vals[-1] - vals[0]) < tol:
            break
        centroid = np.mean(pts[:-1], axis=0)
        xr = centroid + (centroid - pts[-1])
        fr = f(xr)
        if fr < vals[0]:
            xe = centroid + 2.0 * (centroid - pts[-1])
            fe = f(xe)
            pts[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]:
            pts[-1], vals[-1] = xr, fr
        else:
            xc = centroid + 0.5 * (pts[-1] - centroid)
            fc = f(xc)
            if fc < vals[-1]:
                pts[-1], vals[-1] = xc, fc
            else:
                for i in range(1, len(pts)):
                    pts[i] = pts[0] + 0.5 * (pts[i] - pts[0])
                    vals[i] = f(pts[i])
    best = int(np.argmin(vals))
    return pts[best], vals[best]


def fit_all():
    x = np.linspace(1e-4, 1.5, 6000)
    result = {}
    rng = np.random.default_rng(7)
    for name, (closed, structure, p0) in MODELS.items():
        floor = FLOORS_DB[name]
        f = lambda p: cost(closed, structure, p, x, floor, RANGES[name])
        best, best_val = None, 1e9
        for attempt in range(40):
            start = np.array(p0) * np.exp(rng.normal(0.0, 0.5 if attempt else 0.0, len(p0)))
            p, v = nelder_mead(f, list(start))
            for _ in range(3):
                p, v = nelder_mead(f, list(np.abs(p)), step=0.05)
            if v < best_val:
                best, best_val = np.abs(p), v
        e, over = errors(closed, structure, best, x, floor, RANGES[name])
        result[name] = (structure, best, e, over)
    return result


def constants_from_header():
    header = Path(__file__).resolve().parents[6] / "common/dsp/TapeLosses.h"
    text = header.read_text()
    found = {}
    for name in MODELS:
        m = re.search(name + r"Fit\s*\{([^}]*)\}", text)
        if m:
            found[name] = np.array([float(v.replace("f", "")) for v in m.group(1).split(",")])
    return found


def main():
    x = np.linspace(1e-4, 1.5, 6000)
    if "--check" in sys.argv:
        ok = True
        for name, p in constants_from_header().items():
            closed, structure, _ = MODELS[name]
            e, over = errors(closed, structure, p, x, FLOORS_DB[name], RANGES[name])
            print(f"{name:10s} max error {e:.3f} dB above {FLOORS_DB[name]} dB, overshoot beyond {over:.3f} dB")
            ok = ok and e <= 0.3 and over <= 0.3
        sys.exit(0 if ok else 1)

    for name, (structure, p, e, over) in fit_all().items():
        print(f"{name:10s} {structure}  " + ", ".join(f"{v:.6f}f" for v in p)
              + f"   max error {e:.3f} dB, overshoot {over:.3f} dB")


if __name__ == "__main__":
    main()
