# Room

Small to medium rooms, early-reflection heavy, natural. The reflections are computed from a
shoebox (image sources), and the late field grows out of them.

## Topology

```
in L/R -> bandwidth LP (one-pole, 3-20 kHz from Tone and Walls)
       -> image-source reflections, per (mic, source):
            order-1 taps --------------------------+
            order-2 taps -> LP_bounce -> x R ----> + -> LP_bounce -> x R -> x norm = part[mic][src]
          (two sources = the two inputs, two subcardioid mics; 12 order-1 + 36 order-2 taps per mic)

  early L/R = part[mic][L src] + part[mic][R src] ---------------------------------- x early gain --+
                                                                                                    |
  part[0][0], part[0][1], part[1][0], part[1][1] on Hadamard rows 1, 2, 4, 8                        |
       -> 16-line FDN: line (mod) x gamma^D -> allpass (x gamma^D) -> shelves -> Hadamard          |
       -> outputs on Hadamard rows 3 (L) and 5 (R) x sqrt (tank scale) ---------- x late gain --+---> out
```

- **Reflections.** Room 7.3 x 5.5 x 3.1 m at 1x (ratios 1 : 0.75 : 0.42, nothing commensurate),
  two sources on a small stage (slightly asymmetric), two subcardioid mics (0.6 + 0.4 cos) 0.6 m
  apart and angled +-37 degrees. All images of order 1-2 (Allen & Berkley), delay relative to the
  shortest direct path, gain 1/r x directivity. Scattering: +-0.18 ms x order time jitter, +-20 %
  gain spread, random polarity from order 2 (first-order reflections keep their physical
  polarity). Each bounce costs the wall reflection coefficient R and one absorption low-pass, so
  order-2 reflections are darker than order-1 ones, as in a real room. Taps glide with Size to
  integer lengths and are read exactly (integer) at rest, cubic Lagrange only while gliding.
- **Late field fed by the reflections**, not by the dry input: it grows out of the reflection
  pattern. Kept as four signals (per mic and source) on four orthogonal injection patterns, so
  the late field gets the *power sum* of four different reflection colourations instead of one
  comb-like FIR imprinted on the whole tail (that alone cost ~3 dB of spectral peaks).
- **Tank:** 16 lines of 21-66 ms (1x), each with a 1.9-5.9 ms allpass inside (coefficient from
  Walls and Diffusion), Hadamard mixing, smooth-random + sine modulation of up to 0.6 ms
  (0.21 ms at the default Mod, ~1.4 cents) at 0.45-0.75 Hz x Mod Rate.
- **Decay: exact exponential window**, as in Plate: gamma^D on every line and every in-loop
  allpass, shelves per line for the low/high band ratios over the line's full length. Decay
  0.3 s measures 0.30 s.
- **Freeze:** every decay rate scaled by (1 - freeze) in the log domain (each band's loop gain
  <= 1 for any freeze < 1, exactly 1 at freeze 1), modulation off, integer lengths: lossless.

## Character controls

| Control | Effect |
|---|---|
| Param A, **Early/Late** | Equal-power balance, theta = 0.2 .. pi/2 - 0.2 (the weaker part at -14 dB at the ends). "Even" = equal early and late energy at Decay 1 s (a typical medium-room RT60); the late gain carries sqrt (tank scale), so Even holds at every Size. |
| Param B, **Walls** | Reflection coefficient R 0.50 -> 0.71 -> 0.92 per bounce, absorption low-pass 2.4 -> 6.6 -> 18 kHz per bounce, input bandwidth -0.7 / +0.7 oct, tank high-band ratio exponent 2.3 -> 1 -> 0.44 and crossover x0.57 -> x1.75, in-loop allpass 0.30 -> 0.42 -> 0.54. The ER energy lost to R is compensated, so soft is softer, not quieter. |

Measured (tests in `tests/algorithms/room`):

| | Soft (0) | Middle | Hard (1) |
|---|---|---|---|
| Reflections > 4 kHz re broadband (first 40 ms) | -10.2 dB | - | -1.7 dB |
| Reflection octave balance at 8 kHz re 500 Hz-1 kHz | -20.7 dB | -10.7 dB | -1.9 dB |
| RT60(8 kHz) / RT60(mid) | 0.37 | - | 0.82 |
| Pink noise level | -11.0 dB | -11.1 dB | -11.2 dB |
| Flutter (see below) at 0.3x / 1x / 2x | - | - | 0.14 / 0.14 / 0.17 |

| Size | 0.3x (booth) | 1x | 2x (live room) |
|---|---|---|---|
| Late re early at A = 0.5, Decay 1 s | -0.2 dB | +0.1 dB | -1.2 dB |
| Early over late at A = 0 / late over early at A = 1 | 14.0 / 13.7 dB | 13.6 / 14.1 dB | 15.0 / 12.7 dB |
| Half-energy time of the reflections | 1.4 ms | 7.3 ms | 14.6 ms |
| Echo density 0.9 | 17-18 ms | 31-33 ms | 76-81 ms |

## Tuning decisions

- **Echo-density target 60 ms** (gate default 150 ms). The first ~20 ms are the room's identity:
  distinct, sparse reflections (echo density ~0.45-0.5), then they fuse. The perceptual mixing
  time of the 1x room (Lindau, Kosanke & Weinzierl 2012: 20 V/S + 12 ms) is ~28 ms (0.3x: 17 ms,
  2x: 43 ms); the measured 0.9 crossing is 31-33 ms at 1x and tracks size the same way. Slower
  than the plate (18 ms, instantly diffuse), never a hall-style slow build.
- **16 lines, not 8.** With eight 19-42 ms lines (0.26 s of delay, a mode every ~4 Hz) the tail
  rang at RT60 3 s: spectral fine structure 12-14 dB std below 1 kHz, peaks 22-27 dB above the
  median (Hall: ~7 dB, Gaussian noise: 5.6 dB). Sixteen 21-66 ms lines (0.73 s with the allpasses)
  bring it to 6.5-8.1 dB, Hall-like. The extra lines cost ~0.5 % CPU.
- **Tank scales as sqrt (Size) below 1x.** A fully scaled booth tank (0.22 s of delay) rings
  hard (13-15 dB std) at any long Decay. The reflections carry the size (half-energy time scales
  1.4 / 7.3 / 14.6 ms), so the tank shrinks less: 0.3x -> 0.55x tank (std 8-10 dB above 200 Hz;
  below that a small room *is* modal).
- **Neutral walls are a little dark** (-10.7 dB at 8 kHz on the reflections): real first
  reflections lose highs to surfaces. The first try (1.8-16 kHz) was -13 dB, too dull for a
  middle setting.
- **No flutter, by construction and measured.** Order <= 2 means no long regular trains;
  non-commensurate dimensions, per-image jitter and the allpass-diffused tank do the rest. The
  detector (in the tests) is the largest autocorrelation, at 8-80 ms lag (wall spacings of
  1.4-14 m), of the 1 ms envelope with its decay and its 21 ms moving average removed: a
  synthetic 17 ms flutter scores 0.92, hard walls 0.10-0.17 at every size (Hall: 0.27).
- The high crossover sits x1.25 above the shared Tone mapping: a room's surfaces dominate its
  highs, and it keeps the 2 kHz octave of the mid band on the Decay (mid RT -7 % -> -5 %).
- CPU: the 96 reflection taps were 1.35 % as an array of structs; as struct-of-arrays groups
  summed straight from the buffer with integer reads at rest they cost ~0.2 %.

## Measured (48 kHz, defaults)

| Decay | RT125 | RT250 | RT mid | RT 4k | RT 8k | Echo density 0.9 | Wet level | L/R rho | CPU |
|---|---|---|---|---|---|---|---|---|---|
| 3 s | 3.05 | 3.00 | 2.87 | 2.27 | 1.73 | 31 ms | -2.9 dB | -0.01 | 1.3 % |
| 1 s | 0.96 | 0.99 | 0.94 | 0.79 | 0.63 | 32 ms | -2.9 dB | -0.04 | 1.3 % |

- Gate RT60: 1 s -> 0.94, 2.5 s -> 2.39, 6 s -> 5.73; 0.3 s -> 0.30.
- Echo density 0.9 at 44.1 / 48 / 96 / 192 kHz: 34 / 32 / 33 / 35 ms.
- Early part (0-60 ms): L/R rho 0.00-0.10, mono sum -2.6 to -3.0 dB; +3.7 dB at 63 Hz re mid
  (first-order reflections keep their polarity: a little natural room warmth).
- Click crest under fast automation: -68 dB (static -79). Freeze: -7.9 -> -8.2 dB over 60 s,
  per band <= 0.2 dB. CPU from the `[.perf]` test (Release); two overlapping Rooms (algorithm
  change) stay under 3 %.

## References

- J. B. Allen, D. A. Berkley, *Image method for efficiently simulating small-room acoustics*
  (JASA 1979).
- A. Lindau, L. Kosanke, S. Weinzierl, *Perceptual evaluation of model- and signal-based
  predictors of the mixing time in binaural room impulse responses* (JAES 2012).
- J.-M. Jot, A. Chaigne, *Digital delay networks for designing artificial reverberators* (AES 1991).
- S. J. Schlecht, E. A. P. Habets, *Time-varying feedback matrices in feedback delay networks
  and their application in artificial reverberation* (JASA 2015): modulation against modal ringing.
- J. S. Abel, P. Huang, *A Simple, Robust Measure of Reverberation Echo Density* (AES 2006).
