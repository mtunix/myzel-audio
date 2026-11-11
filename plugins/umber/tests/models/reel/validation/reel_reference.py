#!/usr/bin/env python3
"""
Independent reference for the Reel model (Umber). Requires numpy + scipy (matplotlib optional).

    REEL_DUMP_DIR=/tmp/reel build-reel/tests/UmberTests_reel "[.dump]"
    python3 tests/models/reel/validation/reel_reference.py /tmp/reel [--plots]

Checks, each against an implementation written separately from the C++ code:

1. Jiles-Atherton tape core
   a) "same path": the exact sampled (piecewise-linear) field the C++ solver saw, integrated with
      scipy's DOP853 at rtol 1e-12 along each monotone run in field space. Measures pure solver
      (RK4 + sub-stepping) error.
   b) "continuous": the classic time-domain JA ODE dM/dt = (dM/dH) dH/dt for the true continuous
      sine, integrated with DOP853 between the turning points. Measures the total discretisation
      error, including the field being known only at the sample instants (peak clipping between
      samples).
2. Record amp: the triode stage re-solved with nested Brent root finding (no Newton, no table).
3. Playback EQ: the analog prototypes of the coupling cap / LF roll-off / head bump / gap loss vs the C++
   model's measured low-level frequency response (the pre-/de-emphasis pair must cancel).
"""

import glob
import os
import re
import sys

import numpy as np
from scipy.integrate import solve_ivp
from scipy.optimize import brentq

# ---- tape parameters (must match ReelHysteresis.h) -------------------------------------------
ALPHA, K, C = 0.01, 0.5, 0.9


def langevin(q):
    if abs(q) < 1e-4:
        return q / 3.0, 1.0 / 3.0 - q * q / 15.0
    return 1.0 / np.tanh(q) - 1.0 / q, 1.0 / (q * q) - 1.0 / np.sinh(q) ** 2


def dm_dh(h, m, direction):
    man, dman = langevin(h + ALPHA * m)
    diff = man - m
    irr = 0.0
    if diff * direction > 0:
        irr = (1 - C) * diff / ((1 - C) * direction * K - ALPHA * diff)
    return (irr + C * dman) / (1 - C * ALPHA * dman)


RTOL, ATOL = 1e-12, 1e-14


def reference_same_path(h):
    """Integrate dm/dh along the sampled path; monotone runs are integrated as one piece."""
    m = np.zeros_like(h)
    cur = 0.0
    i = 0
    n = len(h)
    hp = 0.0
    while i < n:
        d = h[i] - hp
        if d == 0:
            m[i] = cur
            hp = h[i]
            i += 1
            continue
        direction = 1.0 if d > 0 else -1.0
        j = i
        while j + 1 < n and (h[j + 1] - h[j]) * direction > 0:
            j += 1
        pts = h[i:j + 1]
        sol = solve_ivp(lambda x, y: [dm_dh(x, y[0], direction)], (hp, pts[-1]), [cur],
                        method="DOP853", rtol=RTOL, atol=ATOL, t_eval=pts)
        m[i:j + 1] = sol.y[0]
        cur = sol.y[0][-1]
        hp = pts[-1]
        i = j + 1
    return m


def reference_continuous(rate, freq, amp, n):
    """Time-domain JA ODE for h(t) = amp sin(w t), split at the turning points."""
    w = 2 * np.pi * freq
    t_samples = np.arange(n) / rate
    m_out = np.zeros(n)
    t_turn = (np.arange(0, 64) + 0.5) * np.pi / w
    t0, m0 = 0.0, 0.0
    k = 0
    while t0 < t_samples[-1]:
        t1 = min(t_turn[k], t_samples[-1])
        tm = 0.5 * (t0 + t1)
        direction = 1.0 if np.cos(w * tm) > 0 else -1.0

        def rhs(t, y):
            return [dm_dh(amp * np.sin(w * t), y[0], direction) * amp * w * np.cos(w * t)]

        mask = (t_samples >= t0) & (t_samples <= t1)
        sol = solve_ivp(rhs, (t0, t1), [m0], method="DOP853", rtol=RTOL, atol=ATOL,
                        t_eval=t_samples[mask] if mask.any() else None, max_step=0.02 / freq)
        if mask.any():
            m_out[mask] = sol.y[0]
        m0 = solve_ivp(rhs, (t0, t1), [m0], method="DOP853", rtol=RTOL, atol=ATOL,
                       max_step=0.02 / freq).y[0][-1]
        t0 = t1
        k += 1
    return m_out


def check_ja(dump, plots):
    print("\n== 1. Jiles-Atherton core: C++ (RK4 in field space) vs scipy DOP853 ==")
    print("  rate      freq   amp | same-path max|dm|  | continuous max|dm|  rms|dm|  | fund. err (dB)")
    worst_same = 0.0
    rows = []
    for path in sorted(glob.glob(os.path.join(dump, "ja_*.csv"))):
        rate, freq, amp = map(float, re.match(r".*ja_([\d.]+)_([\d.]+)_([\d.e+-]+)\.csv", path).groups())
        data = np.loadtxt(path, delimiter=",")
        h, m_cpp = data[:, 0], data[:, 1]
        m_same = reference_same_path(h)
        m_cont = reference_continuous(rate, freq, amp, len(h))
        e_same = np.max(np.abs(m_cpp - m_same))
        e_cont = np.max(np.abs(m_cpp - m_cont))
        r_cont = np.sqrt(np.mean((m_cpp - m_cont) ** 2))
        # fundamental over the last two periods (rectangular window over whole periods)
        per = rate / freq
        a, b = int(round(2 * per)), int(round(4 * per))
        t = np.arange(a, b) / rate

        def fund(x):
            return 2 * abs(np.sum(x[a:b] * np.exp(-2j * np.pi * freq * t))) / (b - a)

        fe = 20 * np.log10(fund(m_cpp) / fund(m_cont))
        worst_same = max(worst_same, e_same)
        rows.append((rate, freq, amp, e_same, e_cont, r_cont, fe))
        print("  %6.0f %7.0f %5.1f |      %.2e      |      %.2e      %.2e | %+.4f" %
              (rate, freq, amp, e_same, e_cont, r_cont, fe))
        if plots and freq == 1000.0 and rate == 192000.0:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
            plt.figure(figsize=(5, 4))
            plt.plot(h, m_cont, "k-", lw=2.5, alpha=0.35, label="scipy reference")
            plt.plot(h, m_cpp, "r-", lw=0.8, label="C++")
            plt.xlabel("h = H/a")
            plt.ylabel("m = M/Ms")
            plt.title("M-H loop, amp %g" % amp)
            plt.legend()
            plt.tight_layout()
            plt.savefig(os.path.join(dump, "loop_%g.png" % amp), dpi=110)
            plt.close()
    print("  worst same-path error: %.2e (m is normalised to Ms = 1)" % worst_same)
    return rows


# ---- record amp (must match ReelRecordAmp.h) -------------------------------------------------
MU, EX, KG1, KP, KVB = 100.0, 1.4, 1060.0, 600.0, 300.0
GG, XI, CG = 6.06e-4, 1.354, 13.9
B, RP, RK, RL, RS = 250.0, 100e3, 1.5e3, 470e3, 33e3


def ip(vgk, vpk):
    if vpk <= 0:
        return 0.0
    e1 = vpk / KP * np.logaddexp(0.0, KP * (1 / MU + vgk / np.sqrt(KVB + vpk * vpk)))
    return 2 * e1 ** EX / KG1 if e1 > 0 else 0.0


def ig(vgk):
    return GG * (np.logaddexp(0.0, CG * vgk) / CG) ** XI


def triode_solve(vin, vp0=None):
    def grid(vk):
        return brentq(lambda vg: vg - vin + RS * ig(vg - vk), min(vin, vk) - 50, max(vin, vk) + 50, xtol=1e-14)

    def plate(vg, vk):
        def f(vp):
            load = (B - vp) / RP - (0.0 if vp0 is None else (vp - vp0) / RL)
            return load - ip(vg - vk, vp - vk)
        return brentq(f, vk, B + 1e-6, xtol=1e-14) if f(vk) > 0 else vk

    def resid(vk):
        vg = grid(vk)
        return vk - RK * (ip(vg - vk, plate(vg, vk) - vk) + ig(vg - vk))

    vk = brentq(resid, 0.0, 300.0, xtol=1e-14)
    return plate(grid(vk), vk)


def check_triode(dump):
    print("\n== 2. Record amp: C++ table (NR + Hermite) vs nested Brent solve ==")
    data = np.loadtxt(os.path.join(dump, "triode.csv"), delimiter=",")
    vp0 = triode_solve(0.0)
    idx = np.linspace(0, len(data) - 1, 60).astype(int)
    errs = []
    for i in idx:
        vin, vout = data[i]
        errs.append(abs((triode_solve(vin, vp0) - vp0) - vout))
    print("  quiescent plate %.3f V; max |error| over -40..40 V: %.2e V (plate swing up to ~170 V)" %
          (vp0, max(errs)))
    return max(errs)


# ---- playback EQ (must match ReelModel.h) ----------------------------------------------------
def peak(f, f0, db, q):
    s = 1j * f / f0
    return 1 + (10 ** (db / 20) - 1) / q * s / (s * s + s / q + 1)


def analog_playback(f):
    s1 = 1j * f / 3.0                       # interstage coupling cap
    s2 = 1j * f / 12.0                      # playback LF roll-off, Q 0.6
    hp2 = s2 * s2 / (s2 * s2 + s2 / 0.6 + 1)
    return s1 / (1 + s1) * hp2 * peak(f, 75.0, 2.0, 0.9) / (1 + 1j * f / 40000.0)


def check_eq(dump):
    print("\n== 3. Low-level response (-50 dBFS) vs analog prototypes (coupling, roll-off, bump, gap loss) ==")
    print("  rate     max dev 20 Hz-10 kHz   max dev 10-20 kHz   response range 20 Hz-20 kHz")
    for path in sorted(glob.glob(os.path.join(dump, "eq_*.csv"))):
        rate = float(re.match(r".*eq_([\d.]+)\.csv", path).group(1))
        data = np.loadtxt(path, delimiter=",")
        f, g = data[:, 0], data[:, 1]
        ref = 20 * np.log10(np.abs(analog_playback(f)) / np.abs(analog_playback(np.array([1000.0])))[0])
        lo = (f >= 20) & (f <= 10000)
        hi = (f > 10000) & (f <= 20000)
        band = (f >= 20) & (f <= 20000)
        print("  %6.0f        %.3f dB              %.3f dB          %+.2f .. %+.2f dB" %
              (rate, np.max(np.abs(g - ref)[lo]), np.max(np.abs(g - ref)[hi]), g[band].min(), g[band].max()))


if __name__ == "__main__":
    dump = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("REEL_DUMP_DIR", ".")
    plots = "--plots" in sys.argv
    check_ja(dump, plots)
    check_triode(dump)
    check_eq(dump)
