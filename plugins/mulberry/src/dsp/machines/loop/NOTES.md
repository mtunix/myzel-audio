# Loop: a tape-loop echo

A loop of tape running past an erase head, a record head and three playback heads. Time is the
capstan speed: change it and the motor bends every echo already on the tape. The heads, the
transport, the record path and the losses are shared with Reel (`../TapeMachine.{h,cpp}`).

| File | Contents |
|---|---|
| `LoopMachine.{h,cpp}` | geometry, voicing, transport, wear |
| `../TapeMachine.{h,cpp}` | transport per line, heads, record path, losses (shared with Reel) |
| `../../common/Transport.h` | speed with inertia; capstan, pinch, splice, drift, scrape |
| `../../common/TapeHeads.h` | position-tracked heads (exact Doppler) |
| `../../common/TapeRecord.h` | record amp and its bandwidth, NAB/IEC emphasis, the core at 8x, losses at 2x |
| `../../common/TapeCore.h` | the Jiles–Atherton equations solved for 8x (tabulated Langevin, RK2) |
| `../../common/Oversampler2x.h` | polyphase IIR half-bands, cascaded to 8x |
| `../../common/TapeLosses.h` | Bertram's gap, spacing and thickness losses, the head bump |
| `tests/machines/loop/` | gates, Loop's own checks, `validation/` (Python references) |

## Signal flow

```
rec ─► (erase ghost: (1-e) rec + e · previous pass) ─► 8x: record amp ─► its bandwidth (16 kHz)
   ─► pre-emphasis (70 µs) ─► head field (×2.0, core saturation) ─► Jiles–Atherton ─► de-emphasis
   ─► 2x: losses(v) ─► 1x ─► tape (written with the position the record head was at, latency taken off)
heads: read where the tape is, P(now) − distance ─► dropouts (wear map) ─► buses
```

## The medium

- **Transport.** Capstan speed `v = d / Time` with d = 3 cm (head 1). Second-order motor and
  flywheel (1.2 Hz, damping 0.75): a Time change bends in over about half a second with a little
  overshoot, and every echo on the tape follows it. Speed never goes below d / 6 s.
- **Heads** at d, 2d, 3d; the plan's reads are distances on the tape (fraction × d, Feel and the
  diffuser's delay converted at the target speed). A head finds the sample recorded where the
  tape is now minus its distance (`TapeTrack`), so its pitch is exactly v(t) / v(t′).
  The **Doppler gate** (`BlockTests`, "[doppler]"): speed modulated ±2 % at 3 Hz, a 1 kHz tone
  read 3 cm downstream; the instantaneous frequency matches f₀ v(t) / v(t′) to **2.1e-6**
  (gate 1e-5), checked again by `validation/doppler_reference.py` (bisection for t′, its own
  phase fit): 2.094e-6.
- **Record path.** The nonlinear part runs at 8x (three polyphase IIR half-band pairs: 8, 4 and
  2 coefficients, -100 dB stop bands where products could fold under 20 kHz), the losses at 2x;
  the cascade's 4.41 samples and the record amp's bandwidth (0.17 samples at 48 kHz) are taken
  off by writing each sample with the record head's position that much earlier. Record amp: a
  tanh stage with an operating-point offset (headroom 1.8, offset 0.2: the 2nd harmonic; Padé
  residual, no normalisation, so its slope never exceeds 1), then its bandwidth: a 4-pole
  Butterworth at 16 kHz (an echo unit's record amp, narrower than a studio deck's 24 kHz).
  Pre-emphasis 70 µs, its boost levelling off at 12 kHz, the field ×2.0 per unit record level,
  the head core's soft limit, the Jiles–Atherton core (α 0.01, k 0.55, c 0.88: a little less
  bias than Umber's Reel), the exact inverse de-emphasis. Loud repeats compress and lose top,
  every pass.
- **The core's solver** (`TapeCore`): the equations of `common/dsp/JilesAtherton.h` (shared with
  Umber, untouched), with the Langevin function tabulated (Hermite cubics, 1e-10 of the closed
  form) and midpoint (RK2) sub-steps of at most 0.2 in field. At 8x a sample moves the field a
  quarter as far as at 2x. A 0.4 step left solver error that folded back at −82 dBc; RK4 cost
  half again as much for the same result. The two lines run in lockstep (`processPair`), so the
  processor overlaps their dependency chains: 4.1 % instead of 5.9 % for Loop.
- **Core expansion.** A JA loop expands before it saturates (Rayleigh region). Its largest
  describing-function gain over the small-signal gain is measured once (`TapeRecordPath::
  expansion`, 1.023 for this loop: +0.2 dB) and divided out, so the path never gains at any level and a
  trail at 100 % still decays. Without it, trails at 100 % with high Drive grew 0.3 dB per window.
- **Losses** (Bertram): gap 1.2 µm, spacing 0.4 µm + 1.0 µm × Wear, effective record depth
  1.0 µm, head bump at v / 5.4 mm. Applied in the record path: they depend only on the recorded
  wavelength, so filtering when the tape is written (with the speed then) is the same as when it
  is read. Losses follow the speed between head-1 times of 50 and 1000 ms and hold beyond.
  Longer echoes, slower tape, darker repeats: at 8 kHz a pass loses 2.3 dB at 300 ms and 15 dB at
  800 ms (new tape).
- **Wear** (Param B): dropouts from a fixed map of bad spots along the 1.2 m loop (8 % of 512
  cells, depth 0.3–1 × Wear, smoothstep between cells), the same spots every pass; spacing +1 µm; the
  erase head leaves 3 % × Wear of the previous pass (when it is still in memory); wow × (1 + Wear).

## Losses: the fits and the gate

Each loss is a function of length / wavelength alone, so one analog prototype per loss serves
every speed (`validation/tape_losses.py`, `fit_shelves.py`): gap, two 2-pole low-passes (0.10 dB
to −12 dB); spacing (exp(−2πx): its dB falls linearly in f, which no all-pole response follows,
so first-order shelves: 0.23 dB to −27 dB) and thickness (0.06 dB). Digital sections: pole-matched
with the magnitude exact at DC and Nyquist (Vicanek's matched first- and second-order designs)
up to 0.2 fs, bilinear with the axis matched at 16 kHz above 0.3 fs, blended in between (a
gliding speed moves corners through that range; switching designs at once ticked at −16 dB on
the glide gate). Redesigned within 8 samples whenever the speed has moved 0.03 %.

**Gate missed: fitted vs closed forms, 0.3 dB to 16 kHz.** Measured at the 2x rates (88.2–192 kHz)
over each machine's speed range, down to −12 dB of loss: worst **0.55 dB**, and up to **1.03 dB**
at 15.8 kHz where a corner sits in the blend zone. Deeper than −12 dB the error grows. The
per-repeat gate is unaffected (it compares with the filters as built: 0.001 dB).

## Measured (48 kHz; one head, new tape unless noted; `MulberryMeasure loop`)

| Gate | Result |
|---|---|
| Mean echo time vs Time, 44.1–192 kHz | ≤ 0.059 % |
| Per-repeat loss vs prediction | 0.002 dB |
| Per-repeat loss across 44.1 / 48 / 96 / 192 kHz | 0.084 dB |
| Drive compensation, −12 … +24 dB | 0.113 dB spread |
| Flutter, Wobble 0 / nominal (default Wear) | 0.000 % / 0.250 % (target 0.25 %) |
| 300 → 450 ms (the motor) | −65 dB crest (steady −77) |
| THD at 1 kHz, Drive 0: −24 / −12 / −6 / 0 dBFS | 0.20 / 0.84 / 2.2 / 5.6 % |
| Aliasing, 7 kHz −1 dBFS, 44.1 kHz: Drive 0 / max | −94 / −100 dBc (was −71 / −17 at 2x) |
| 120 %, Drive max, square 60 s | +0.6 dBFS peak |
| Runaway | −24.0 dBFS at 8–10 s, peak +1.45 dB over 6–8 s (gate 1.5; head field ×2.0, PLAN.md Deviations 13), RMS +6.9 dB; −100 dB 20 s after 90 % |
| Hold 60 s | 0.000 dB drift, seam −82 dB |
| CPU, Single / Pattern 16 steps added (defaults) | 4.06 % (budget 5 %) / +1.31 % (budget +1.5 %) |

**Aliasing.** Measured one stage at a time (the gate's 7 kHz at −1 dBFS, 44.1 kHz). At Drive 0
the core at 2x was the source: its 11th to 13th harmonics (77–91 kHz) fold about 88.2 kHz to
2.8–11.2 kHz (−71 dBc); with every stage linear the path is at −130 dBc. At Drive max the
record amp clips, the pre-emphasis lifts its harmonics by up to 17 dB and the head and the core
turn them into harmonics past the 50th. Even at 8x: the amp alone −103 dBc, amp and head −64,
the core alone −44. The amp's bandwidth (here 16 kHz) keeps its harmonics out of the core, the
emphasis boost levels off at 12 kHz, and at 8x the products land below −90 dBc. The solver
matters too: RK2 steps of 0.4 left −82 dBc at Drive 0, steps of 0.2 −94 dBc.

## References

- S. Arnardottir, J. S. Abel, J. O. Smith, *A Digital Model of the Echoplex Tape Delay*, AES 125
  (2008): read/write pointers, the delay trajectory's capstan and pinch components, drift.
- H. N. Bertram, *Theory of Magnetic Recording*, 1994: gap, spacing (Wallace), thickness loss.
- J. Chowdhury, *Real-Time Physical Modelling for Analog Tape Machines*, DAFx-19; the shared JA
  core is validated in Umber's `models/reel/NOTES.md`.
- M. Vicanek, *Matched Second Order Digital Filters* (2016), *Matched One-Pole Digital Shelving
  Filters* (2019).
- Valenzuela & Constantinides polyphase IIR half-bands; the coefficient design after L. de Soras'
  HIIR.
- AES6-2008 / IEC 60386: weighted flutter (`tests/support/EchoAnalysis.h`, `FlutterMeter`).
