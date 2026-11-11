# Hall

A big, smooth, gently modulated concert hall. The reference algorithm: the others are measured
against the same gates (`tests/support/EngineGates.h`).

## Topology

```
in L/R -> bandwidth LP (one-pole, 4-18 kHz from Tone)
       -> early-reflection cluster (14 taps per side, 7-85 ms x Size) ---------------------+
       -> 4-stage Schroeder allpass diffuser per side (3-15 ms x Size x Build)            |
       -> injected into a 16-line FDN with orthogonal +-1 patterns (Hadamard rows 1, 2)   |
          16 lines, 33-131 ms x Size, Hadamard mixing, per-line 3-band decay filters      |
       -> outputs: Hadamard rows 3 (L) and 5 (R)  x 0.42 --------------------------------> + -> out
```

- **Decay**: per-line gains `g = 10^(-3 L / (fs RT60))`, recomputed from the current (gliding)
  lengths every sub-block and ramped per sample, so the RT60 is exact whatever Size is doing.
- **Low End / Tone**: the decay filter's second-order shelves (250 Hz; Tone moves the high
  crossover 1.5-9 kHz and the high-band RT ratio 0.12-0.97, see `maps::toneToHighDecay`).
- **Mod**: up to 1.6 ms peak per line, 65 % smooth random + 35 % sine, rates spread 0.75-1.25x
  around 0.45 Hz x Mod Rate. Enough to break up modal ringing and give the BigSky-style
  "expensive hall" shimmer, not enough to sound like a chorus at the default 35 %.
- **Freeze**: all band gains go to exactly 1, the modulation stops where it is (no pitch bend
  while freezing) and the lengths round to whole samples (exact Lagrange reads): lossless. Every
  octave band holds within 0.3 dB over 35 s (tested per band at 250 Hz, 1 kHz and 4 kHz).

## Character controls

| Control | Effect |
|---|---|
| Param A, **Early** | Level of the early-reflection cluster (0 = none, ^1.3 taper to 0.75) |
| Param B, **Build** | Input diffuser lengths 0.5x-3x and coefficient +0-0.06: low = an instant dense wall; high = the late field swells in over ~100 ms (10 % energy arrival moves later by > 15 ms, tested) |

## Tuning decisions

- 16 lines rather than 8: the echo density gets to 0.9 in ~85 ms with the diffusers, and the
  modal density is high enough that 30 s+ tails don't ring.
- Tank lengths avoid small common factors (`tankMs` in the .cpp), mean ~76 ms.
- The diffuser coefficient follows `0.7 sqrt(diffusion)` (+ Build): at the default 75 % it is 0.61,
  smooth on drums without smearing a picked guitar.
- Every gain or coefficient that changes with a control is a `LinearRamp`, and Decay is smoothed
  in the log domain by the chain. Stepping them once per 32-sample sub-block measured as a
  -29 dB click crest when jumping Decay; ramped and log-smoothed it is -71 dB (a static sine is -85).

## Measured (48 kHz, defaults, `CeruleanMeasure hall`)

| Decay | RT125 | RT250 | RT mid | RT 4k | RT 8k | Echo density 0.9 | Wet level | L/R rho | CPU |
|---|---|---|---|---|---|---|---|---|---|
| 3 s | 2.94 | 3.03 | 2.95 | 2.33 | 1.52 | 85 ms | -3.0 dB | 0.00 | 1.3 % |

With the first-order shelves the shared decay filter started with, the mid band read 7 % short
(2.80 s at 3 s); the second-order shelves adopted at integration put it within 2 %.

## References

- J.-M. Jot, A. Chaigne, *Digital delay networks for designing artificial reverberators* (AES 1991).
- G. Luff, *Let's Write a Reverb* (ADC 2021): diffuser + Hadamard FDN structure.
- J. Dattorro, *Effect Design Part 1* (JAES 1997): bandwidth filter and allpass diffusion values.
