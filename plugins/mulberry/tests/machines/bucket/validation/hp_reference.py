#!/usr/bin/env python3
"""Independent reference for Bucket's clocked domain: Holters & Parker, "A Combined Model for a
Bucket Brigade Device and its Input and Output Filters", DAFx-18.

Recomputes the C++ test case ("Bucket: the clocked domain matches the Python reference") from the
dumped input, with its own construction of everything: the Butterworth poles and residues from
numpy's polynomial roots (not the C++ formula), each clock instant from its index (t_k = k / f_clock,
not by accumulating a phase), the input filter's output at t_k by integrating each mode exactly from
the host samples (an impulse of area u T at each host instant), the ideal device (the sample K clocks
earlier), and the output filter's modes stepped through every held value between host instants.

    MULBERRY_DUMP_DIR=/tmp/mb build-dev/plugins/mulberry/tests/MulberryTests_bucket "*Python*"
    python3 hp_reference.py /tmp/mb
"""
import sys

import numpy as np

FS, CUTOFF, CLOCK, DELAY = 48000.0, 3500.0, 18750.0, 32


def butterworth_modes(cutoff, order=4):
    wc = 2 * np.pi * cutoff
    poles = wc * np.exp(1j * np.pi * (2 * np.arange(order) + 1 + order) / (2 * order))
    den = np.poly(poles)                      # monic
    num = np.prod(-poles)                     # DC gain 1
    residues = np.array([num / np.polyval(np.polyder(den), p) for p in poles])
    return poles, residues


def main():
    u = np.fromfile(sys.argv[1] + "/hp_input.f64")
    y_cpp = np.fromfile(sys.argv[1] + "/hp_output.f64")
    T = 1.0 / FS
    p, r = butterworth_modes(CUTOFF)

    n = len(u)
    t_end = n * T
    k = np.arange(0, int(t_end * CLOCK) + 2)
    tk = k / CLOCK
    tk = tk[(tk > 0.0) & (tk <= (n - 1) * T + 1e-15)]

    # Input filter output at each clock instant. Host sample m is an impulse of area u[m] T at the
    # end of its period, (m + 1) T; only impulses strictly before an instant reach it.
    samples = np.zeros(len(tk))
    state = np.zeros(len(p), dtype=complex)
    host = 0
    t_last = 0.0
    for i, t in enumerate(tk):
        while (host + 1) * T < t - 1e-15 and host < n:
            state = state * np.exp(p * ((host + 1) * T - t_last)) + u[host] * T
            t_last = (host + 1) * T
            host += 1
        samples[i] = np.real(np.sum(r * state * np.exp(p * (t - t_last))))

    held = np.concatenate([np.zeros(DELAY), samples[:-DELAY]]) if len(samples) > DELAY else np.zeros(len(samples))

    # Output filter: modes driven by the held staircase, read at the host instants.
    out = np.zeros(n)
    y = np.zeros(len(p), dtype=complex)
    value = 0.0
    t_now = 0.0
    j = 0
    for m in range(n):
        t_host = (m + 1) * T
        while j < len(tk) and tk[j] <= t_host + 1e-15:
            target = -r / p * value
            y = target + (y - target) * np.exp(p * (tk[j] - t_now))
            t_now = tk[j]
            value = held[j]
            j += 1
        target = -r / p * value
        y = target + (y - target) * np.exp(p * (t_host - t_now))
        t_now = t_host
        out[m] = np.real(np.sum(y))

    # The C++ output at index m is read at the end of host period m; compare in energy.
    m = min(len(out), len(y_cpp))
    err = out[:m] - y_cpp[:m]
    db = 10 * np.log10((np.sum(err ** 2) + 1e-300) / (np.sum(y_cpp[:m] ** 2) + 1e-300))
    print(f"error energy {db:.1f} dB relative to the signal (gate -100 dB)")
    sys.exit(0 if db < -100 else 1)


if __name__ == "__main__":
    main()
