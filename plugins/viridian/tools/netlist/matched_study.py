#!/usr/bin/env python3
"""
The study behind MatchedSection's design (README.md, "Matched sections"): how close a digital
section can come to an analog one at 44.1 and 48 kHz, to 20 kHz. Not part of the test suite
(about two minutes); the numbers it prints are quoted in README.md and PLAN.md.

1. Vicanek's matched second-order section (impulse-invariant poles, numerator matched in
   magnitude at DC, the centre frequency and Nyquist).
2. The best any single biquad can do: minimax |dB| error by Lawson-weighted least squares.
3. The frequency map's relative error of Omega^2 by degree D (a digital order of D per analog
   order), the basis of the method used.

    python3 plugins/viridian/tools/netlist/matched_study.py
"""

import numpy as np
from scipy.optimize import least_squares


def analog(num, den, f):
    s = 2j * np.pi * f
    return np.polyval(num[::-1], s) / np.polyval(den[::-1], s)


def digital(b, a, f, fs):
    z = np.exp(-2j * np.pi * f / fs)
    return np.polyval(b[::-1], z) / np.polyval(a[::-1], z)


def bell(f0, q, gain_db):
    w, a = 2 * np.pi * f0, 10 ** (gain_db / 40)
    return np.array([w * w, a * w / q, 1.0]), np.array([w * w, w / (a * q), 1.0])


def phis(w):
    p1 = np.sin(w / 2) ** 2
    p0 = 1 - p1
    return p0, p1, 4 * p0 * p1


def vicanek(num, den, fs, fm):
    """Impulse-invariant poles; |B|^2 = B0 phi0 + B1 phi1 + B2 phi2 matched at 0, fm, Nyquist."""
    poles = np.roots(den[::-1])
    a = np.real(np.poly(np.exp(poles / fs)))
    A = ((a[0] + a[1] + a[2]) ** 2, (a[0] - a[1] + a[2]) ** 2, -4 * a[0] * a[2])
    h0 = abs(analog(num, den, 0.0)) ** 2
    hn = abs(analog(num, den, fs / 2)) ** 2
    hm = abs(analog(num, den, fm)) ** 2
    p0, p1, p2 = phis(2 * np.pi * fm / fs)
    B0, B1 = A[0] * h0, A[1] * hn
    B2 = (hm * (A[0] * p0 + A[1] * p1 + A[2] * p2) - B0 * p0 - B1 * p1) / p2
    r0, r1 = np.sqrt(B0), np.sqrt(B1)
    W = 0.5 * (r0 + r1)
    if W * W + B2 < 0:
        return None, a
    b0 = 0.5 * (W + np.sqrt(W * W + B2))
    return np.array([b0, 0.5 * (r0 - r1), -B2 / (4 * b0)]), a


def worst_db(num, den, b, a, fs, top=20000.0):
    f = np.geomspace(10, min(top, fs / 2 * 0.9999), 2000)
    return float(np.max(np.abs(20 * np.log10(abs(digital(b, a, f, fs)) / abs(analog(num, den, f))))))


def best_biquad(num, den, fs, top=20000.0):
    f = np.geomspace(10, min(top, fs / 2 * 0.9999), 600)
    target = 20 * np.log10(abs(analog(num, den, f)))

    def error(x):
        return 20 * np.log10(abs(digital(x[:3], np.r_[1, x[3:]], f, fs))) - target

    best = np.inf
    for fm in (100.0, 500.0, 2000.0, 4000.0):
        b, a = vicanek(num, den, fs, fm)
        if b is None:
            continue
        x, w = np.r_[b, a[1:]], np.ones_like(f)
        for _ in range(60):
            x = least_squares(lambda x: np.sqrt(w) * error(x), x, xtol=1e-14, ftol=1e-14, gtol=1e-14, max_nfev=2000).x
            e = np.abs(error(x))
            w = w * (e + 1e-12)
            w /= w.sum()
        best = min(best, float(np.max(np.abs(error(x)))))
    return best


def warp_error(fs, degree, top=20000.0):
    """Minimax relative error of a degree-D rational M (t) for asin^2 (sqrt t) on 0 ... t(top)."""
    T = np.sin(np.pi * min(top, 0.46 * fs) / fs) ** 2
    t = T * np.sin(0.5 * np.pi * (np.arange(1024) + 0.5) / 1024)
    w = np.arcsin(np.sqrt(t)) ** 2
    na, nb = degree - 1, degree

    def m(x):
        n = t * (1 + sum(x[i] * t ** (i + 1) for i in range(na)))
        d = 1 + sum(x[na + j] * t ** (j + 1) for j in range(nb))
        return n / d

    x, lam = np.zeros(na + nb), np.ones_like(t)
    for _ in range(80):
        x = least_squares(lambda x: np.sqrt(lam) * (m(x) / w - 1), x, xtol=1e-15, ftol=1e-15, gtol=1e-15).x
        e = np.abs(m(x) / w - 1)
        lam = lam * (e + 1e-18)
        lam /= lam.sum()
    return float(np.max(np.abs(m(x) / w - 1)))


def main():
    cases = [(1000, 0.4, -18), (3000, 1, 15), (5000, 0.4, -18), (10000, 1, -18), (10000, 4, 18), (16000, 1, 15)]
    for fs in (44100, 48000):
        print(f"--- {fs} Hz, worst |dB| to 20 kHz")
        print("  bell                    Vicanek   best biquad")
        for f0, q, g in cases:
            num, den = bell(f0, q, g)
            b, a = vicanek(num, den, fs, min(f0, 0.45 * fs))
            v = worst_db(num, den, b, a, fs) if b is not None else float("nan")
            print(f"  {f0:6d} Hz Q {q:<4} {g:+3d} dB  {v:8.3f}   {best_biquad(num, den, fs):8.4f}")
        print("  frequency map, relative error of Omega^2 to 20 kHz: " +
              ", ".join(f"D = {d}: {warp_error(fs, d):.1e}" for d in (1, 2, 3)))


if __name__ == "__main__":
    main()
