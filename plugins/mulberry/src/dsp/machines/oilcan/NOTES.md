# Oil Can: an electrostatic drum echo

A drum spinning in oil stores the signal as charge: a brush writes it, pickups downstream read
it, and the oil lets it leak. No published model of the mechanism exists, so it is built from
the physics, on the tape machine's position-tracked transport (`../TapeMachine.{h,cpp}`).

| File | Contents |
|---|---|
| `OilCanMachine.{h,cpp}` | geometry, record stage, aperture, leakage, brushes, drum transport |
| `../TapeMachine.{h,cpp}` | transport, position-tracked pickups, record path (non-magnetic mode), brushes |
| `tests/machines/oilcan/` | gates, Time and Oil darken, Brushes smear |

## The mechanism

- **Drum**: 0.3 m circumference (about 95 mm across); the pickup sits 0.225 m after the record
  brush. Time sets the drum speed (arc / Time) through a slipping rubber belt: a soft, slow motor
  (0.6 Hz, damping 0.6), so a Time change glides.
- **Record stage**: a tube amplifier drives the brush (headroom 1.6, offset 0.2: soft, early,
  asymmetric), capacitively coupled (40 Hz high-pass), its bandwidth a 4-pole low-pass at
  24 kHz. No magnetic core, no emphasis. It runs at 8x (the tape machines' record path): at 2x
  its clipping at Drive max folded back at −29 dBc, at 4x still −48 dBc.
- **Aperture**: the brush contact width acts as a gap, 25 µm (new oil) to 70 µm (old): the same
  sinc loss as a tape gap, scaling with the surface speed. Longer times, slower drum, softer
  repeats. No spacing or thickness loss, no head bump.
- **Leakage**: the charge decays through the oil over each pickup's dwell time,
  τ = 30 / (1 + 3 Oil) s, from the drum's mechanical speed (from the target speed it clicked).
  Longer delays are quieter.
- **Brushes** (Param A): one pickup (1) to five over 15 % of the arc, weights from one dominant
  (tilt 1.2) to even, summing to 1: one echo smears into a cluster.
- **Oil** (Param B): new to old: a wider aperture, faster leakage and more belt slip
  (× (1 + 1.5 Oil) on every wobble component).
- **Wobble**: drum rotation and its 2nd harmonic (an eccentric drum), the belt slipping once per
  turn, slow drift; 45 % random. AES6 weighted 1.0 % at nominal: the warble is the sound.
- **Noise**: −66 dBFS at 50 %.

## Measured (48 kHz, one pickup, Oil 50 %; `MulberryMeasure oilcan`)

| Gate | Result |
|---|---|
| Mean echo time vs Time, 44.1–192 kHz | ≤ 0.045 % |
| Per-repeat loss vs prediction | 0.001 dB |
| Per-repeat loss across 44.1–192 kHz | 0.002 dB |
| Drive compensation | 0.018 dB spread |
| Flutter, Wobble 0 / nominal | 0.000 % / 1.00 % |
| 300 → 450 ms (the belt) | −72.9 dB crest |
| THD at 1 kHz, Drive 0: −24 / −12 / −6 / 0 dBFS | 0.22 / 0.88 / 1.8 / 4.0 % |
| Aliasing, 7 kHz −1 dBFS, 44.1 kHz: Drive 0 / max | −130 / −92 dBc (was −29 at 2x) |
| 120 %, Drive max, square 60 s | +0.4 dBFS peak |
| Runaway | −27.7 dBFS at 8–10 s, peak +1.17 dB over 6–8 s; −111 dB 20 s after 90 % |
| Hold 60 s | 0.000 dB drift, seam −87 dB |
| CPU, Single / Pattern 16 steps added | 2.14 % (budget 4 %) / +1.25 % (budget +1.5 %) |

The gates run with one pickup: a cluster is a deliberate comb, whose per-repeat response is not
a single loss.

## References

- The electrostatic drum echoes of the 1950s–60s (patents on capacitive drum storage); no
  circuit-level model is published, so the parameters are physical estimates voiced by ear and
  by the gates.
- H. N. Bertram, *Theory of Magnetic Recording* (the aperture loss shares the gap's maths).
