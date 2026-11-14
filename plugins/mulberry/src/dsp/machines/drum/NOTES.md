# Drum: a magnetic-drum echo

A heavy drum with an oxide-coated rim turns past an erase head, a record head and four playback
heads spaced evenly round it. Heads tracks which of them play; Swell takes the feedback from all
four, the dense, swelling build-up these units are known for. The transport, the position-tracked
heads, the 8x record path and the losses are the tape machines' (`../TapeMachine.{h,cpp}`, see
`../loop/NOTES.md`).

| File | Contents |
|---|---|
| `DrumMachine.{h,cpp}` | geometry, voicing, transport |
| `../TapeMachine.{h,cpp}` | transport per line, heads, record path, losses (shared with Loop, Reel, Oil Can) |
| `../../TapPlan.{h,cpp}` | the heads at quarters of Time, Swell's feedback shares |
| `tests/machines/drum/` | gates, heads, Swell, losses over speed, the glide |

## The mechanism

- **Drum**: 0.32 m circumference (about 100 mm across). Head 4 sits 0.24 m after the record
  head; heads 1–3 at its quarters. **Time is head 4**: the plan reads the heads at 1/4, 2/4, 3/4
  and 4/4 of Time, so with Sync the heads fall on the note's subdivisions (Time 1/2: eighth-note
  heads) and the delay memory never needs more than Time (PLAN.md Deviations 24).
- **Transport**: Time sets the motor speed (head arc / Time). The drum is its own flywheel
  (0.7 Hz, damping 0.85): a Time change glides over about a second, barely overshooting, and every
  echo on the drum bends with it. Speed never goes below arc / 8 s.
- **Heads** (Param A): every combination of the four, singles first: 1, 2, 3, 4, 1+2, 2+3, 3+4,
  1+3, 2+4, 1+4, 1+2+3, 2+3+4, 1+2+4, 1+3+4, 1+2+3+4 (15 positions). The playing heads feed back
  equally, as Loop's head sum. Pattern mode replaces the heads (Heads dims).
- **Swell** (Param B): the feedback shares move from the playing heads (0) towards all four heads
  equally (1): head k feeds back `(1 − s) [k plays] / n + s / 4`, so the shares sum to 1 and
  Feedback alone sets the loop gain. With head 1 alone at 80 %, the trail falls 28 dB from 200 to
  1100 ms without Swell and 7.3 dB with full Swell. Riding Swell keeps the reads in place, so
  the chain ramps the weights; leaving 0 adds the taps with a 20 ms plan crossfade. Swell works
  in Pattern mode too, around the pattern's end.
- **Heads ride off the oxide.** The heads never touch the drum: 6 µm of spacing, a 4 µm gap,
  2 µm effective record depth. Spacing loss dominates the tone, so short times are clear and
  long times dark: at 6 kHz a pass loses 3.1 dB at 250 ms and 9.1 dB at 900 ms. The losses
  follow the speed between head-4 times of 120 and 1200 ms and hold beyond.
- **Head bump** at a 40 mm wavelength (20 Hz at 300 ms). At 8 mm the contour loss below the
  bump took 1.2 dB per pass off 125 Hz; the runaway then built so slowly from a click that its
  peak was still rising 2–5 dB per 2 s at 10 s, whatever the head field. With the low end flat the
  runaway settles inside the gate (PLAN.md Deviations 26).
- **Record path**: a valve stage drives the head (headroom 1.6, offset 0.2: soft, early,
  asymmetric), its bandwidth a 4-pole low-pass at 16 kHz; pre-emphasis 50 µs, field ×2.0 per unit
  record level (Loop's), the Jiles–Atherton core at Umber's bias-linearised set (α 0.01, k 0.5,
  c 0.9; expansion 1.019, divided out), the exact inverse de-emphasis. At 8x, as Loop.
- **Wobble**: drum eccentricity once per turn, the motor pulley four times per turn, slow drift
  of the motor, a little bearing noise; 35 % random. AES6 weighted 0.15 % at nominal: steadier
  than a tape loop, livelier than a studio deck.
- **Noise**: hiss at −64 dBFS at 50 %.
- **Groove** applies, as on Loop, only with one head at Time feeding back alone: head 4 and
  Swell 0. Otherwise the heads are the rhythm (PLAN.md Deviations 25).

## Measured (48 kHz; head 4 alone, Swell 0; `MulberryMeasure drum`)

| Gate | Result |
|---|---|
| Mean echo time vs Time, 44.1–192 kHz | ≤ 0.056 % |
| Per-repeat loss vs prediction | 0.001 dB |
| Per-repeat loss across 44.1–192 kHz | 0.063 dB |
| Drive compensation, −12 … +24 dB | 0.089 dB spread |
| Flutter, Wobble 0 / nominal | 0.000 % / 0.149 % (target 0.15 %) |
| 300 → 450 ms (the drum) | −68.2 dB crest (steady −77.0) |
| THD at 1 kHz, Drive 0: −24 / −12 / −6 / 0 dBFS | 0.24 / 0.99 / 2.5 / 6.4 % |
| Aliasing, 7 kHz −1 dBFS, 44.1 kHz: Drive 0 / max | −101 / −90 dBc |
| 120 %, Drive max, square 60 s | +0.6 dBFS peak |
| Runaway | −24.1 dBFS at 8–10 s, peak +0.76 dB over 6–8 s, RMS +6.2 dB; −99 dB 20 s after 90 % |
| Hold 60 s | 0.000 dB drift, seam −68 dB (below the machine's own crest) |
| CPU, Single / Pattern 16 steps added | 4.16 % (budget 5 %) / +1.19 % (budget +1.5 %) |

CPU at the defaults (heads 1+3, Swell 50 %: two output reads and four feedback taps per line):
4.15 % against Loop's 4.02 % in the same run. With one head both machines execute the same
instructions (callgrind, 11.9 G each for the perf test).

Per-repeat loss at 300 ms (Feedback 70 %, so −3.10 dB of it is Feedback): −3.58 dB at 125 Hz,
−3.81 at 1 kHz, −5.65 at 4 kHz, −7.70 at 8 kHz.

## References

- The magnetic-drum echoes of the 1950s–60s (a rotating oxide-coated drum, fixed heads, a
  regeneration switch feeding back from all heads): behaviour references only; no circuit or
  measurement data is published, so the constants are physical estimates checked by the gates.
- H. N. Bertram, *Theory of Magnetic Recording*, 1994: gap, spacing, thickness loss.
- The shared core, record path and transport: `../loop/NOTES.md`.
