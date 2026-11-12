# Bucket: a bucket-brigade delay

A BBD chip with its input (anti-aliasing) and output (reconstruction) filters and an
NE570-style compander. The chip is sampled at its own clock through the shared clocked domain
(`../../common/ClockedDomain.h`), so its aliasing and clock images are the device's own.

| File | Contents |
|---|---|
| `BucketMachine.{h,cpp}` | clock, chip, transfer, losses, compander, image normalisation |
| `../../common/ClockedDomain.h` | Holters–Parker combined filter/device model (shared with Rack) |
| `tests/machines/bucket/` | gates, the Python reference case, the 64x brute force, chip and compander checks |
| `tests/machines/bucket/validation/hp_reference.py` | an independent Holters–Parker implementation |

## Signal flow

```
rec ─► compressor (2:1 × Compander) ─► input filter (4-pole Butterworth, modal)
    ─► clocked sampling ─► transfer (soft rail 1.6, offset 0.12) ─► buckets (charge loss, leak)
    ─► taps (fixed stages per bus) ─► zero-order hold ─► output filter ─► expander per bus
```

## The model

- **Clocked domain** (Holters & Parker, DAFx-18). The filters are kept as parallel first-order
  modes; at each clock instant (a phase crossing between host samples) the input filter's modes
  are evaluated exactly from their free response, and each new held output value steps into the
  output filter's modes. Nothing is resampled; the device's aliasing and images come out as they
  would from the circuit.
- **Clock** = stages / (2 (Time − filter delay)), capped at 200 kHz (below a chip's shortest time
  fewer stages are used). The filters' low-frequency group delay is allowed for, so the echo
  lands on Time (mean error ≤ 0.027 %). The clock glides (60 ms) when Time moves: a pitch glide.
  Wobble is a triangle LFO on the clock (0.55 Hz, 0.2 % pitch at nominal) plus drift, per sample.
- **Taps are fixed stages of the chain**, set from the target clock (the clock moves the time,
  not the taps; taps from the gliding clock jumped).
- **Chip** (Param A): 1024 / 2048 / 4096 / 8192 stages, with filters at 6.5 / 5 / 3.8 / 3.2 kHz
  (shorter chips were built for shorter times). Long times on a short chip clock low: darker,
  with more aliasing and images.
- **Transfer**: rail × (tanh((v + 0.12) / rail) − tanh(0.12 / rail)), rail 1.6: soft, asymmetric
  (2nd harmonic), slope ≤ 1. Charge transfer loss 2e-6 per stage; leakage τ 20 s over the dwell.
- **Image excess.** When the clock is low for the filters, the gain of one pass summed over the
  input's aliases and the output's images exceeds 1 at some frequency even with every filter
  below 1. The device output is divided by that excess (computed per 0.5 % clock change), so
  trails at 100 % decay; long times on a short chip are a little quieter.
- **Compander** (Param B): compressor 2:1 × B before the chip, expander 1:2 × B after it, one
  expander per output bus. The chain's loss between them makes the expander mistrack: at full
  setting the per-pass loss in dB doubles (the test checks it to 1e-9). The expander never
  undoes more than the compressor did one Time earlier: a full compander in the loop otherwise
  pumped itself into oscillation below 100 %.
- **Noise** (Advanced): −78 dBFS at 50 %.

## Validation

- **Python reference** (`validation/hp_reference.py`): its own Butterworth poles and residues
  (numpy roots), clock instants from their index, the input modes integrated exactly from the host
  samples, the ideal device, the output modes stepped through every held value. Against the C++
  dump (3.5 kHz filters, 18.75 kHz clock, 32 stages): error energy **−272.6 dB** (gate −100 dB).
- **64x brute force** (`BucketTests`): the same chain simulated directly at 64x the host rate
  (the filters as exact modal steps, the device sampling at the nearest 64x instant, its
  staircase held). A 3 kHz tone through a 9 kHz clock and 5 kHz filters: at the tone (3 kHz), its
  alias (6 kHz) and an image (12 kHz) the clocked domain matches within **0.05 dB** (gate 1 dB).

## Measured (48 kHz, 8192 stages, Compander 0; `MulberryMeasure bucket`)

| Gate | Result |
|---|---|
| Mean echo time vs Time, 44.1–192 kHz | ≤ 0.027 % |
| Per-repeat loss vs prediction | 0.003 dB |
| Per-repeat loss across 44.1–192 kHz | 0.000 dB |
| Drive compensation | 0.021 dB spread |
| Flutter, Wobble 0 / nominal | 0.000 % / 0.062 % |
| 300 → 450 ms (the clock glides) | −52.2 dB crest, the steady floor of its own images (−52.2) |
| THD at 1 kHz, Drive 0: −24 / −12 / −6 / 0 dBFS | 0.14 / 0.59 / 1.3 / 3.0 % |
| Aliasing | the device's own; matches the 64x reference within 0.05 dB |
| 120 %, Drive max, square 60 s | +0.9 dBFS peak |
| Runaway | −22.6 dBFS at 8–10 s, peak +1.0 dB over 6–8 s; −92 dB 20 s after 90 % |
| Hold 60 s | 0.000 dB drift, seam −47 dB (the steady crest of its images) |
| CPU, Single / Pattern 16 steps added | 1.40 % (budget 4 %) / +0.10 % |

## References

- M. Holters, J. D. Parker, *A Combined Model for a Bucket Brigade Device and its Input and Output
  Filters*, DAFx-18.
- C. Raffel, J. O. Smith, *Practical Modeling of Bucket-Brigade Device Circuits*, DAFx-10.
- Signetics NE570/571 compandor data sheet; Panasonic MN3005/MN3008 and Reticon SAD-1024 data.
