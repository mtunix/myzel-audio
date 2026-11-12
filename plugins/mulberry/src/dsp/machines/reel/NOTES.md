# Reel: a studio tape deck

A deck at a fixed speed with a playback head that slides along the tape: Time moves the head (it
glides, so a change bends in pitch) while the tape speed, and so the tone, stays put. Transport,
heads, record path and losses are Loop's (`../TapeMachine.{h,cpp}`, see `../loop/NOTES.md`).

| File | Contents |
|---|---|
| `ReelMachine.{h,cpp}` | speeds, bias mapping, voicing, transport |
| `tests/machines/reel/` | gates and Reel's own checks |

## Voicing

- **Speed** (Param A): 7.5 / 15 / 30 ips. The tape speed sets the head bump (v / 5.4 mm: 35, 70,
  141 Hz), the losses (gap 2.5 µm, spacing 0.15 µm, record depth 1.0 µm), the IEC record EQ
  (70 / 35 / 17.5 µs) and the hiss (−72 dBFS at 15 ips, 3 dB more per halving of the speed).
  Switching speed glides the transport (0.8 Hz, damping 0.9) and the head with it.
- **The sliding head**: head distance = speed × Time, an inertial glide (250 ms, 60 ms
  acceleration, at most half the tape speed): at most an octave down while it moves.
- **Bias** (Param B) moves the Jiles–Atherton loop: nominal (c 0.9, k 0.5, Umber's
  bias-linearised set); under-biased towards the wide unbiased loop (c down to 0.55, k up to 1.0:
  more hysteresis, Rayleigh expansion, more distortion, brighter: the record depth drops to
  0.6 µm); over-biased narrower (c 0.97, k 0.35) and duller (bias erasure as +0.6 µm spacing,
  record depth 1.4 µm). Each position's core expansion (1.019 nominal, 1.24 fully under-biased) is tabulated
  (41 points) and divided out: an under-biased tape is quieter at low level, never louder than
  unity at any level.
- **Record amp**: headroom 2.5, operating-point offset 0.15 (the 2nd harmonic), its slope never
  above 1, so a trail at 100 % decays at every level; its bandwidth a 4-pole low-pass at 24 kHz.
  The record path (Loop's, at 8x) is described in `../loop/NOTES.md`.
- **Transport** at nominal Wobble: capstan 20 mm and pinch 30 mm eccentricity, slow reel wow
  (0.35 Hz), a little scrape; AES6 weighted 0.050 % (target 0.05 %).

## Measured (48 kHz, 15 ips, nominal bias; `MulberryMeasure reel`)

| Gate | Result |
|---|---|
| Mean echo time vs Time, 44.1–192 kHz | ≤ 0.048 % |
| Per-repeat loss vs prediction | 0.001 dB |
| Per-repeat loss across 44.1–192 kHz | 0.040 dB |
| Drive compensation | 0.062 dB spread |
| Flutter, Wobble 0 / nominal | 0.000 % / 0.050 % |
| 300 → 450 ms (the head slides) | −74 dB crest |
| THD at 1 kHz, Drive 0: −24 / −12 / −6 / 0 dBFS | 0.10 / 0.40 / 1.0 / 3.3 % (Umber's Reel: 0.11 / 0.43 / 1.2 / 3.5 %) |
| Aliasing, 7 kHz −1 dBFS, 44.1 kHz: Drive 0 / max | −91 / −98 dBc (was −65 / −22 at 2x; Loop's notes); at 7.5 ips fully under-biased −75 / −82 (outside the gate's setting) |
| 120 %, Drive max, square 60 s | +0.7 dBFS peak |
| Runaway | −14.0 dBFS at 8–10 s, peak steady (0.28 dB); −84 dB 20 s after 90 % |
| Hold 60 s | 0.000 dB drift, seam −86 dB |
| CPU, Single / Pattern 16 steps added | 3.72 % (budget 6 %) / +1.33 % (budget +1.5 %) |
