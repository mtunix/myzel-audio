# Plate

Bright, dense, instantly diffuse. A Dattorro figure-of-eight plate tank, extended for an
instant onset, exact decay and a playable Density/Metal pair. Fastest-diffusing algorithm.

## Topology

```
in L -> bandwidth LP (one-pole, 3.5-20 kHz from Tone/Metal) -> AP AP AP AP AP AP -> l
in R -> bandwidth LP                                         -> AP AP AP AP AP AP -> r
        (the two chains are rotated by ~37 degrees after every stage: [l r] <- R [l r])

        +------------------------------------------------------------------------------+
        v                                                                              |
 0.71 l + -> AP1 (mod, -dd1) -> D1 -> shelves -> + -> AP2 (mod, dd2) -> AP3 (-dd3) -> D2 -> shelves --+
                                                ^ 0.71 r                                              |
 0.71 r + -> AP1 (mod, -dd1) -> D1 -> shelves -> + -> AP2 (mod, dd2) -> AP3 (-dd3) -> D2 -> shelves --+
        ^                                       ^ 0.71 l                                       (cross)
        +------------------------------------------------------------------------------+

out L/R: 10 signed taps each (Dattorro's 7 + 3 early ones), mostly from the opposite half
```

Lengths are Dattorro's (samples at 29761 Hz) scaled to the host rate and by Size: AP1 672/908,
D1 4453/4217, AP2 1800/2656, AP3 + D2 = Dattorro's D2 (487 + 3233 / 383 + 2780), so the loop
(725 ms at 1x) and with it the modal density are exactly Dattorro's.

- **Decay: exact exponential window.** Every delay element (input diffusers, tank allpasses,
  delays) carries gamma^D with gamma = 10^(-3 / (fs RT60)): replacing each z^-D by
  (gamma z^-1)^D multiplies the impulse response by gamma^n exactly. Output taps carry
  gamma^position. So the mid band decays at the set RT60 whatever the diffusion does, including
  the allpass recirculations (which bypass a classic per-half decay gain) and short decays
  (Decay 0.3 s measures 0.30 s; a classic Dattorro bottoms out near 0.9 s here because its
  allpasses ring on their own).
- **Low End / Tone:** first-order shelves (250 Hz / Tone's crossover), *two per half* (after D1 and
  after D2), each covering its share of the loop. One shelf per 362 ms half saturated: at full
  dark a first-order shelf aiming at -53 dB per pass only reaches ~-9 dB at 4 kHz.
- **Freeze:** every decay *rate* is scaled by (1 - freeze) (log domain), so each band's loop gain
  is gamma_band^(L (1 - freeze)) <= 1 for every freeze < 1 and exactly 1 at freeze = 1;
  modulation depth goes to 0 and every length is an integer at rest (exact Lagrange reads), so
  the frozen loop is lossless. Blending each gain linearly towards 1 is *not* safe here: a
  boosting low shelf (Low End 2x) times a blended mid gain exceeded 1 (found by the square-wave
  gate: peak 6e36).
- **Size:** each length glides (GlideValue, one-pole + slew limit) to round(nominal x Size), so
  at rest every read is integer and exact (no interpolation loss accumulating in the loop).
- **Mod:** AP1 up to 1.0 ms (Dattorro's excursion is ~0.54 ms; ~0.35 ms at the default Mod),
  AP2 up to 0.45 ms, 50 % sine + 50 % smooth random, 0.61-0.97 Hz x Mod Rate.
- Every control-driven gain and coefficient (diffusion coefficients, gammas, shelves, crossovers,
  bandwidth, tap gains, modulation offsets) is a per-sample ramp that restarts exactly at its
  previous target, so a constant value is held bit-exactly.

## Character controls

| Control | Effect |
|---|---|
| Param A, **Density** | 0 -> 0.5: input diffusion 0.30/0.25 -> 0.75/0.625 (Dattorro), decay diffusion 1 0.35 -> 0.70. 0.5 -> 1: input diffusers stretch to 1.8x (a softer, more blurred attack), input diffusion -> 0.80/0.70, AP3 fades in (0 -> 0.35). Advanced Diffusion scales the input diffusion (sqrt, 0.75 = nominal). |
| Param B, **Metal** | Decay diffusion 2 0.6 -> 0.5 -> 0.25 and decay diffusion 1 x1.1 -> x0.9 (less smearing in the loop: the modes ring), high-band ratio exponent 2.3 -> 1 -> 0.44 and high crossover x0.6 -> x1.6 (less damping), input bandwidth +-0.3 oct. |

Measured (tests in `tests/algorithms/plate`):

| | 0 | 0.5 | 1 |
|---|---|---|---|
| Density: echo density 20-60 ms | 0.68 | 0.97 | - |
| Density: envelope roughness 20-300 ms (dB) | 3.65 | 1.04 | 0.67 |
| Metal: RT60(8 kHz) / RT60(mid) | 0.43 | - | 0.88 |
| Metal: > 6 kHz energy re tail at 1-2 s | -27.9 dB | - | -5.4 dB |

## Tuning decisions

- **Instant onset.** Plain Dattorro reaches echo density 0.9 only at ~235 ms here (its four long
  input allpasses give a few dozen distinct echoes in the first 30 ms, kurtosis 120 -> 10 over
  60 ms, and every output tap replays that spiky onset). Three changes fix it, all measured:
  1. Two short stages in front (37/59 and 43/53 samples at 29761 Hz) and an orthogonal L/R
     rotation between stages (each stage now doubles the echo paths): 0.9 at 34 ms.
  2. Four-point injection (each half's start gets its own side, its middle the other side):
     the energy is spread over the loop from the first pass. Before, the envelope bloomed by
     ~8 dB over 150 ms and then fell away (energy concentrated in a small part of the loop).
  3. Dattorro's 379/277 input stages shortened to 199/149: the long pair spread the onset over
     ~40 ms, which left echo density hovering at 0.8-0.86 from 12-24 ms, so the 0.9 crossing
     wandered 24-32 ms between sample rates. Now it is 16-21 ms at 44.1-192 kHz.
- **Three extra taps per side** that the injected signal reaches within 8-15 ms. Dattorro's taps
  are reached at 6, 9, 52, 64, 67, 80, 100 ms (left), so the level stepped up as they joined (a
  ~5 dB "second hit" at ~70 ms). Roughness at the default went 1.40/1.22 -> 1.04/1.04 dB. Taps
  arriving at ~23 ms were tried first and rejected: a second onset in mid-build made echo
  density dip below 0.9 at 96/192 kHz.
- A plate is brighter than a hall at Tone = flat: high-band ratio exponent 0.8, crossover x1.25,
  both fading out towards full Dark (so Dark still passes the < 0.6 gate: measured 0.47).
- Size 0.3x shrinks the loop to 218 ms: the tail gets a little ringy (fine-structure std 12 dB
  at 200-500 Hz) - a small, metallic plate, which suits the idiom. 1x and 2x are as smooth as Hall.

## Measured (48 kHz, defaults)

| Decay | RT125 | RT250 | RT mid | RT 4k | RT 8k | Echo density 0.9 | Wet level | L/R rho | CPU |
|---|---|---|---|---|---|---|---|---|---|
| 3 s | 3.01 | 3.00 | 2.88 | 2.45 | 1.97 | 18 ms | -3.0 dB | 0.00 | 0.95 % |
| 1 s | 1.01 | 1.04 | 0.99 | 0.88 | 0.73 | 18 ms | -3.0 dB | 0.00 | 0.95 % |

- Gate RT60: 1 s -> 0.99, 2.5 s -> 2.42, 6 s -> 5.77 (mid band includes 2 kHz, which already feels
  the high shelf); 0.3 s -> 0.30, 0.5 s -> 0.49.
- Echo density 0.9: 16 / 18 / 18 / 21 ms at 44.1 / 48 / 96 / 192 kHz; 12 ms at 0.3x, 48 ms at 2x.
- Tail spectral fine structure (std of the detrended 1 Hz-resolution spectrum, 0.4-1.4 s;
  Gaussian noise = 5.6 dB): 6.3-6.7 dB in every band from 200 Hz to 8 kHz (Hall: 5.7-7.1).
- Click crest under fast automation: -64 dB (static -83). Freeze: +-0.2 dB over 60 s, per band
  <= 0.2 dB. CPU from the `[.perf]` test (Release).

## References

- J. Dattorro, *Effect Design Part 1: Reverberator and Other Filters* (JAES 1997): tank, lengths,
  taps, coefficients.
- J.-M. Jot, A. Chaigne, *Digital delay networks for designing artificial reverberators* (AES 1991):
  per-band decay; the gamma^D substitution for exact decay.
- J. S. Abel, P. Huang, *A Simple, Robust Measure of Reverberation Echo Density* (AES 2006).
- G. Luff, *Let's Write a Reverb* (ADC 2021): multi-channel diffusion by orthogonal mixing.
