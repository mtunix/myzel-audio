"""The Console through the real chain against ngspice.

    python3 plugins/viridian/tools/measure/compare.py --measure build/plugins/viridian/tools/ViridianMeasure
    python3 plugins/viridian/tools/measure/compare.py --curves curves.jsonl     (a file ViridianMeasure --curves wrote)

Runs `ViridianMeasure --curves` (the Console's grid of settings through the chain at 44.1, 48, 96 and
192 kHz: the response with EQ In over the response with EQ In off, so the amps, oversamplers and
DC block divide out), then an ngspice AC analysis of the card netlists for the pots each setting
ran (front times back, src/dsp/models/console/netlist). It gates the magnitude and reports the
phase (PLAN.md, Settled 13: phase is reported, not gated):

    magnitude   within 0.1 dB from 20 Hz to 20 kHz, every setting at every rate (gated)
    phase       per rate, to 10 kHz: the bands' own phase (a setting over its flat twin: the same
                knobs, every gain centred, the filters Off) where every acting band and filter
                sits below fs / 8 and where one sits above, by the highest acting band's octave,
                the network's at flat and the whole response's (reported)

Exit status 1 when the magnitude gate fails, 2 when the measurement could not run. Needs ngspice,
numpy and scipy (the netlists' own check.py).
"""

import argparse
import json
import multiprocessing
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
NETLISTS = os.path.normpath(os.path.join(HERE, "..", "..", "src", "dsp", "models", "console", "netlist"))
sys.path.insert(0, NETLISTS)
import check  # noqa: E402

FRONT = ("hpf_in", "hpf_freq", "lpf_in", "lpf_freq", "hf_gain", "hf_freq", "hf_bell", "hmf_gain", "hmf_freq", "hmf_q")
BACK = ("lmf_gain", "lmf_freq", "lmf_q", "lf_gain", "lf_freq", "lf_bell")
MAGNITUDE_DB = 0.1
PHASE_TO_HZ = 10000.0
STOP_BAND_DB = -80.0    # re the setting's peak: a filter's stop band, compared in absolute terms
OCTAVES = (250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0)


def spice(job):
    """ngspice's response of one setting on the curve grid: front times back."""
    card, pots, f0, f1, ppd = job
    f, hf = check.ac(f"{card}_front.cir", {k: pots[k] for k in FRONT}, f0, f1, ppd)
    _, hb = check.ac(f"{card}_back.cir", {k: pots[k] for k in BACK}, f0, f1, ppd)
    return f, hf[:, 0] * hb[:, 0]


def complex_array(pairs):
    a = np.asarray(pairs, dtype=float)
    return a[:, 0] + 1j * a[:, 1]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--measure", help="the ViridianMeasure executable")
    source.add_argument("--curves", help="a curves file ViridianMeasure wrote (--curves) instead of a new run")
    parser.add_argument("--jobs", type=int, default=os.cpu_count(), help="parallel ngspice runs and measure threads")
    parser.add_argument("--keep", help="keep the curves file at this path")
    args = parser.parse_args()

    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        path = args.curves or args.keep or os.path.join(tmp, "curves.jsonl")
        if args.measure:
            run = subprocess.run([args.measure, "--curves", path, "--jobs", str(args.jobs)], capture_output=True, text=True)
            sys.stdout.write(run.stdout)
            if run.returncode not in (0, 1) or not os.path.exists(path):
                sys.stderr.write(run.stderr)
                return 2
            failed = run.returncode != 0
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().splitlines()

    head = json.loads(lines[0])
    hz = np.asarray(head["hz"])
    rates = [int(r) for r in head["rates"]]
    rows = [json.loads(line) for line in lines[1:]]
    ppd = 40
    f0, f1 = hz[0], hz[-1]

    # ngspice once per distinct pot set (the flat twins repeat).
    def key(row, pots):
        return row["card"], tuple(sorted(row[pots].items()))

    keys = list(dict.fromkeys(key(r, pots) for r in rows for pots in ("pots", "flat_pots")))
    with multiprocessing.Pool(max(1, args.jobs)) as pool:
        results = pool.map(spice, [(card, dict(pots), f0, f1, ppd) for card, pots in keys], chunksize=4)
    for f, _ in results:
        if len(f) != len(hz) or np.max(np.abs(f / hz - 1)) > 1e-6:
            sys.stderr.write("ngspice's frequency grid differs from ViridianMeasure's\n")
            return 2
    spiced = {k: h for k, (_, h) in zip(keys, results)}
    reference = [spiced[key(r, "pots")] for r in rows]
    flat_reference = [spiced[key(r, "flat_pots")] for r in rows]

    in_phase = hz <= PHASE_TO_HZ * (1 + 1e-9)
    stop_limit = 10 ** (STOP_BAND_DB / 20) * (10 ** (MAGNITUDE_DB / 20) - 1)
    print(f"\nConsole through the chain against ngspice: {len(rows)} settings, {len(spiced)} ngspice pot sets, "
          f"{len(hz)} frequencies 20 Hz ... 20 kHz")
    for rate in rates:
        worst = (0.0, "", 0.0)
        worst_stop = 0.0
        band_low = (0.0, "")       # the bands' own phase, every band below fs / 8
        band_high = (0.0, "")      # the same with a band above
        network = 0.0              # the network at its flat twin
        total = 0.0
        by_octave = {}             # (low-pass in, octave of the highest acting band) -> the bands' phase
        for row, h, h_flat in zip(rows, reference, flat_reference):
            measured = complex_array(row[f"measured_{rate}"])
            measured_flat = complex_array(row[f"flat_measured_{rate}"])
            peak = np.max(np.abs(h))
            pass_band = 20 * np.log10(np.abs(h) / peak) > STOP_BAND_DB
            error = np.where(pass_band, np.abs(20 * np.log10(np.abs(measured) / np.abs(h))), 0.0)
            k = int(np.argmax(error))
            if error[k] > worst[0]:
                worst = (float(error[k]), row["name"], float(hz[k]))
            if (~pass_band).any():
                worst_stop = max(worst_stop, float(np.max(np.abs(measured - h)[~pass_band]) / peak))

            gated = in_phase & pass_band
            if not gated.any():
                continue
            phase = np.abs(np.degrees(np.angle(measured / h)))[gated]
            band = np.abs(np.degrees(np.angle((measured / measured_flat) / (h / h_flat))))[gated]
            total = max(total, float(np.max(phase)))
            network = max(network, float(np.max(np.abs(np.degrees(np.angle(measured_flat / h_flat)))[in_phase])))
            if row["acting_hz"]:
                key = (bool(row["pots"]["lpf_in"]), int(np.searchsorted(OCTAVES, max(row["acting_hz"]))))
                by_octave[key] = max(by_octave.get(key, 0.0), float(np.max(band)))
            low = all(f < rate / 8 for f in row["acting_hz"])
            if low and np.max(band) > band_low[0]:
                band_low = (float(np.max(band)), row["name"])
            if not low and np.max(band) > band_high[0]:
                band_high = (float(np.max(band)), row["name"])

        magnitude_ok = worst[0] <= MAGNITUDE_DB and worst_stop <= stop_limit
        failed |= not magnitude_ok
        print(f"\n  {rate / 1000:.1f} kHz")
        print(f"    magnitude to 20 kHz, worst      {worst[0]:9.4f} dB   (gate {MAGNITUDE_DB})  "
              f"{'ok' if worst[0] <= MAGNITUDE_DB else 'FAIL'}   {worst[1]} at {worst[2]:.0f} Hz")
        print(f"    stop bands below {STOP_BAND_DB:.0f} dB re peak   {worst_stop:9.2e}      (gate {stop_limit:.2e} of the peak)  "
              f"{'ok' if worst_stop <= stop_limit else 'FAIL'}")
        print(f"    phase, reported (Settled 13):")
        print(f"    bands' phase to 10 kHz, every band below fs/8   {band_low[0]:6.2f} deg   {band_low[1]}")
        print(f"    bands' phase to 10 kHz, a band above fs/8       {band_high[0]:6.2f} deg   {band_high[1]}")
        print(f"    the network's phase at flat, to 10 kHz          {network:6.2f} deg")
        print(f"    the whole response's phase to 10 kHz            {total:6.2f} deg")
        edges = ["0"] + [f"{f / 1000:g}k" for f in OCTAVES] + ["up"]
        for lpf in (False, True):
            cells = [f"{edges[k]}-{edges[k + 1]} {by_octave[(lpf, k)]:.1f}" for k in range(len(OCTAVES) + 1) if (lpf, k) in by_octave]
            print(f"    bands' phase by the highest band (Hz: deg), low-pass {'in ' if lpf else 'out'}: {', '.join(cells)}")

    flat_rows = [(r, h) for r, h in zip(rows, reference) if not r["acting_hz"]]
    lift = max(float(np.max(np.abs(20 * np.log10(np.abs(h))))) for _, h in flat_rows)
    print(f"\n  flat settings (every gain centred, filters Off): {len(flat_rows)}; the circuit departs from flat by up to "
          f"{lift:.3f} dB (ngspice), the chain follows it within the magnitude gate above")
    print("\nthe magnitude gate passes" if not failed else "\nFAIL")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
