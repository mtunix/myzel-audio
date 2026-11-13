# Rack: an early rack digital delay

An op-amp front end, an anti-aliasing filter, sampling at the unit's own clock, a gain-ranging
converter, word memory, a zero-order-hold DAC and a reconstruction filter. Every repeat is
converted again, so the grain builds down the trail.

| File | Contents |
|---|---|
| `RackMachine.{h,cpp}` | front end, clock, converter, memory, crossfaded Time jumps |
| `common/dsp/ClockedDomain.h` | the filters and the clocked sampling (shared with Bucket, validated there) |
| `tests/machines/rack/` | gates, gain ranging, grain down the trail |

## Signal flow

```
rec ─► op-amp (rails at 2, ADAA residual) ─► AA filter (4-pole Butterworth at Rate)
    ─► sample at 2.4 × Rate ─► gain ranging (×1, ×2, ×4, ×8; 30 ms hold) ─► Bits converter
    ─► word memory ─► ZOH ─► reconstruction filter (Rate)
```

## The model

- **Front end**: transparent until its rails (record level 2), the clip's Padé residual
  antialiased (ADAA), as Clean.
- **Clock** 2.4 × Rate (Param B: Rate 6–20 kHz, so 14.4–48 kHz). The filters and sampling are the
  shared Holters–Parker clocked domain; aliasing and images are the device's own.
- **Gain-ranging converter** (Param A: 8–16 bits of mantissa). A peak detector picks the highest
  of four 6 dB gain steps that keeps the peak inside the converter: lowered at once, raised only
  after a 30 ms hold (syllabic). The quantisation floor rides the signal, as in the units of the
  era. Full scale clips.
- **Memory** in whole words: at Wobble 0 the read is exact (linear interpolation between words lost
  top end on every pass). With Wobble a sine LFO moves the address (0.4 Hz, 0.08 % pitch) and the
  read goes between words, linearly.
- **Time jumps** with a 10 ms crossfade: these units did not glide.
- **Noise**: −86 dBFS at 50 %.

## Measured (48 kHz, 16 bits, Rate 50 %; `MulberryMeasure rack`)

| Gate | Result |
|---|---|
| Mean echo time vs Time, 44.1–192 kHz | ≤ 0.037 % |
| Per-repeat loss vs prediction | 0.043 dB |
| Per-repeat loss across 44.1–192 kHz | 0.017 dB |
| Drive compensation | 0.012 dB spread |
| Flutter, Wobble 0 / nominal | 0.000 % / 0.019 % |
| 300 → 450 ms (a crossfaded jump) | −41.0 dB crest, the steady floor of its own grain (−41.0) |
| THD at 1 kHz, Drive 0: −24 / −12 / −6 / 0 dBFS | 0.007 / 0.11 / 0.45 / 1.7 % |
| Aliasing | the device's own (the clocked domain is validated against 64x in Bucket's tests; the converter's quantisation aliases by design) |
| 120 %, Drive max, square 60 s | +0.5 dBFS peak |
| Runaway | −16.1 dBFS at 8–10 s, peak steady (0.03 dB); −71 dB 20 s after 90 % |
| Hold 60 s | 0.14 dB drift, seam −44 dB (its grain) |
| CPU, Single / Pattern 16 steps added | 0.92 % (budget 3 %) / +0.58 % |

The glide and Hold seams are measured against the machine's own steady crest: quantisation and
clock images dominate an absolute 3rd-difference crest.

## References

- M. Holters, J. D. Parker, DAFx-18 (the clocked domain).
- Gain-ranging (floating-point) converters of early digital delays and samplers: the
  mantissa/exponent principle as in the service literature of the late 1970s rack units.
