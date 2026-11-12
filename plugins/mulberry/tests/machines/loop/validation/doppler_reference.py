#!/usr/bin/env python3
"""Independent reference for the Doppler gate (PLAN.md): a head on a moving tape plays at exactly
v(t) / v(t'), with t' the record instant.

The C++ test "Blocks: a head on a moving tape plays at exactly v(t) / v(t')" records a 1 kHz tone on
a TapeTrack whose speed is v0 (1 + depth sin (2 pi fm t)) and reads it 3 cm downstream. With
MULBERRY_DUMP_DIR set it writes the head's output. This script solves P(t') = P(t) - d by bisection
(independently of the test's Newton iteration), predicts the output sin (2 pi f0 t'), and reports the
relative error of the instantaneous frequency, f0 v(t) / v(t'), per 5 ms window.

    MULBERRY_DUMP_DIR=/tmp/mb build-dev/plugins/mulberry/tests/MulberryTests "[doppler]"
    python3 doppler_reference.py /tmp/mb
"""
import sys

import numpy as np

FS, V0, DEPTH, FM, D, F0 = 48000.0, 0.1, 0.02, 3.0, 0.03, 1000.0


def position(t):
    return V0 * (t - DEPTH / (2 * np.pi * FM) * (np.cos(2 * np.pi * FM * t) - 1.0))


def record_instant(t):
    lo, hi = t - 2.0 * D / V0, t
    target = position(t) - D
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        below = position(mid) < target
        lo = np.where(below, mid, lo)
        hi = np.where(below, hi, mid)
    return 0.5 * (lo + hi)


def main():
    out = np.fromfile(sys.argv[1] + "/doppler.f32", dtype=np.float32).astype(float)
    t = np.arange(len(out)) / FS
    phase = 2 * np.pi * F0 * record_instant(t)
    window = int(0.005 * FS)
    errors = []
    for start in range(int(0.6 * FS), len(out) - window, window):
        s, c, y = np.sin(phase[start:start + window]), np.cos(phase[start:start + window]), out[start:start + window]
        a, b = np.linalg.lstsq(np.stack([s, c], axis=1), y, rcond=None)[0]
        errors.append(np.arctan2(b, a))
    relative = np.diff(errors) / (2 * np.pi * F0 * window / FS)
    worst = np.max(np.abs(relative))
    print(f"worst relative frequency error {worst:.3e} (gate 1e-5)")
    sys.exit(0 if worst < 1e-5 else 1)


if __name__ == "__main__":
    main()
