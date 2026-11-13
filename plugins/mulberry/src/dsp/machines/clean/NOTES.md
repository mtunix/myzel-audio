# Clean: ideal memory

A delay line that does nothing to the sound until it has to: exact repeats, a chorus as its
wobble, a soft ceiling for the dub runaway, and the one machine that lets you choose how Time
changes.

| File | Contents |
|---|---|
| `CleanMachine.{h,cpp}` | the machine: reads, record stage (`Limiter`), Glide, Shift, Wobble |
| `common/dsp/Oversampler2x.h` | `LinearPhaseHalfBand`: the limiter's 8x interpolators and decimators (shared) |
| `common/dsp/Interpolators.h` (repository root) | the windowed-sinc fractional read (shared by every machine) |
| `common/dsp/GrainVoice.h` | the granular shifter voice (shared with Cerulean's Halo) |
| `tests/machines/clean/` | the gates (`support/GateCases.h`) and Clean's own checks |

## Topology

```
rec ─► (+ noise) ─► memory (per line), as recorded
rec ─► 8x linear-phase: residual tanh(x) - x ─► added to that sample 39 samples later (ceiling 3.0)
memory ─► reads at fraction · T(t) + offset (12-tap windowed sinc) ─► buses
  (feedback reads only where the limiting has arrived)
  feedback buses ─► [Shift: grain voice, crossfaded in over 30 ms] ─► loop (chain)
```

- **Reads.** 12-tap Kaiser-windowed sinc (beta 8), 512 tabulated fractions, linear between
  them. Every fraction's kernel has the same DC gain and the table carries one scale so that no
  kernel exceeds unity at any frequency (the raw kernels overshoot 0.0013 dB near Nyquist). A
  moving read therefore never modulates the level, and the loop at 100 % can never grow. The
  price is a uniform 0.0013 dB per pass, which `predictedPassGain()` reports. Measured over
  every fraction at 48 kHz: worst loss 0.003 dB to 10 kHz, 0.14 dB at 16 kHz (gate: 0.1 / 0.5).
  Cubic Lagrange would lose 3.3 dB at 16 kHz per pass at half a sample.
  The first per-fraction normalisation (scale only where a kernel overshot) gave each fraction
  a slightly different DC gain: a gliding read then ran through fractions at a few hundred Hz
  and modulated the level by 0.3 %, a -31 dB click crest on the glide gate. One scale fixed it.
- **Record stage.** A tanh scaled to a ceiling of 3.0 (+9.5 dBFS at the record level):
  -0.3 dB at 0 dBFS, inaudible below. The sample goes into memory exactly as recorded; its
  limiting, the residual `clip(x) - x`, is computed at 8x and added to that sample afterwards.
  The 8x path is linear-phase (Kaiser half-band FIRs, K = 14 / 8 / 4, 90–95 dB stop band, one
  extra 8x sample of delay), so its delay is exactly 39 host samples at every frequency and the
  residual lands on the sample it belongs to. The loop's linear part therefore stays exact
  (nothing filters the recorded signal; only the distortion, a fraction of a percent at playing
  level, passes the half-bands) and it limits cleanly when driven. Feedback reads wait until the residual has arrived: they read at least 46
  samples back, so below 46 samples plus the diffuser's 3.7 ms the loop is a little longer than
  Time (outputs read anywhere). The residual's pass band ends near 0.4 fs; a safety ceiling of
  5.0 bounds what it cannot reach (a runaway in the top band only, never at playing level).
  - The first record stage (host rate, Padé approximant with first-order ADAA on the residual)
    aliased at −27 dBc at Drive max: at +24 dB the stage is a hard limiter, its square wave's
    5th, 7th, 9th … harmonics fold at 44.1 kHz. An IIR-oversampled clip would have fixed that
    but put its phase (+3 samples of group delay at 16 kHz per pass) into every repeat; a
    linear-phase path on the written sample would have delayed it by 39 samples, too long
    for Time's 1 ms. Adding the residual after the fact keeps both. tanh replaces the Padé
    approximant because the approximant's clamp (a corner in its second derivative) left
    harmonics that still folded at 8x.
- **Time.** Each line's time has a glided part (Cerulean Echo's inertial transport: 200 ms pull,
  30 ms acceleration, at most 0.5 samples per sample) and a jumped part. Glide (Param A) splits
  every change: (1 - Glide) of it jumps by crossfading two read positions over 40 ms, the rest
  glides. Crossfade gains are equal power with smoothstep easing, so neither end has a kink.
- **Shift** (Param B, ±12 semitones, 0 = off). The feedback buses run through a grain voice (85 ms
  grains, aligned, power-normalised; see Cerulean's Halo notes). The first echo is unshifted,
  each repeat moves one more interval. The voice delays by its mean latency, so the feedback
  reads move earlier by that latency (gliding over 30 ms when the interval changes), and the
  shifted loop keeps the period of Time. Off is an exact bypass; on/off crossfades over 30 ms.
- **Wobble.** A read-position chorus: 0.75 sine + 0.25 smooth random at 0.8 Hz × Wobble Rate,
  peak pitch deviation 0.1 % at nominal Wobble (depth = deviation · fs / (2π f)). One transport
  for both lines; in Dual the right line's own LFO blends in over 100 ms.
- **Noise.** White, -100 dBFS RMS at Noise 50 % (a converter's floor), recorded with the signal.

## Measured (48 kHz unless noted; `MulberryMeasure clean`)

| Gate | Result |
|---|---|
| First echo vs Time, 44.1–192 kHz, 80 / 375 / 1000 ms (and 1–3000 ms in the tests) | ≤ 0.008 samples |
| Per-repeat loss vs prediction, 125 Hz – 8 kHz, 8 repeats | 0.000 dB |
| Per-repeat loss across 44.1 / 48 / 96 / 192 kHz | 0.000 dB |
| Echo level over Drive −12 … +24 dB (−40 dBFS) | 0.006 dB spread |
| AES6 weighted flutter: Wobble 0 / nominal | 0.000 % / 0.047 % |
| 300 → 450 ms jump, 3rd-difference crest (Glide 0 / 0.5 / 1) | −78.7 / −77.8 / −73.4 dB |
| 7 kHz at −1 dBFS, 44.1 kHz, worst alias: Drive 0 / Drive max | −130 dBc / −106 dBc |
| THD at 1 kHz, Drive 0: −24 / −12 / −6 / 0 dBFS | 0.004 / 0.058 / 0.23 / 0.90 % |
| 120 %, Drive max, 0 dBFS square, 60 s | peak +1.7 dBFS (dry + wet), finite |
| Runaway from a click at 120 % | at the ceiling within 2.5 s; peak change 6–8 → 8–10 s 0.49 dB |
| Pulling back to 90 % | −69 dB after 20 s |
| Hold 60 s: octave-band drift / seam crest | 0.000 dB / −88 dB |
| CPU, Single / Pattern 16 steps (added) | 1.64 % (budget 2 %; the limiter is 1 %) / +1.35 % (budget +1.5 %) |

**A flat loop's runaway keeps filling in.** With no loss anywhere, the click's energy hits the
ceiling in 2.5 s and the peak level is steady from then on, but the waveform keeps densifying:
the RMS still rises 7.8 dB between 6–8 s and 8–10 s and settles after about 25 s, as a
square-like wall. The runaway gate checks the peak.

## References

- T. Laakso et al., *Splitting the Unit Delay*, IEEE SPM 1996: windowed-sinc fractional delay.
- J. F. Kaiser, *Nonrecursive Digital Filter Design Using the I0-sinh Window Function*, 1974:
  the half-band FIRs.
- J. Dattorro, *Effect Design Part 2*, JAES 1997: modulated reads, chorus depth vs rate.
