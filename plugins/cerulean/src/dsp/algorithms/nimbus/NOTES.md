# Nimbus

A huge, dark, endlessly diffuse ambient cloud, voiced for long decays (10-60 s) and pads. The
idea comes from CloudSeed (several delay lines, each with allpass diffusers *inside* its feedback,
everything slowly modulated). The structure, values and code are our own.

## Topology

```
in L/R -> bandwidth LP (one-pole, 7.5 kHz x 2^(0.9 Tone) x 2^-(Distance-0.5), ramped)
       -> 8-stage Schroeder allpass pre-diffusion per side
          (4-39 ms x Size x 0.3..2.2 from Distance; coefficient from Diffusion, Distance, Density)
       -> injected into the cloud tank with orthogonal +-1 patterns (Hadamard rows 1, 2) ------+
                                                                                            |
          cloud tank (CloudTank.h), 16 lines, Hadamard mixing; per line:                    |
            line delay 62-212 ms x Size, random-walk modulated (up to 7 ms x Mod)           |
            -> 3 allpasses in the loop, 2-31 ms x Size, each random-walk modulated           |
               (up to 1 ms x Mod); coefficient 0..0.7 from Density                           |
            -> second-order shelving decay filter (common DecayFilter)                    |
       -> outputs: Hadamard rows 3 (L) and 5 (R) x 0.74                                     |
       + pre-diffused signal x 0.35 (1 - Distance)^2  (a close cloud starts at once) <------+
```

- **Decay**: per-line gains from the *total* loop length (line + its three allpasses), so the
  RT60 is exact for any Density (the allpasses are lossless; their average group delay is their
  length). Recomputed every sub-block from the gliding lengths, ramped per sample.
- **Low End / Tone**: second-order (Butterworth-type) shelves at 250 Hz and at the Tone crossover
  (x 0.9), high-band ratio x 0.8: Nimbus is darker in the loop than the global mapping.
  See "Tuning decisions" for why second order.
- **Mod**: every one of the 64 delays has its own seeded smooth random walk (no sines: nothing
  periodic to hear), line rates 0.15-0.29 Hz, allpass rates 0.26-0.48 Hz (x Mod Rate), spread so
  that no two coincide. Depth 7 ms (lines) and 1 ms (allpasses) at Mod 100 %.
- **Freeze**: the random walks *stop where they are* (rate x (1 - freeze)) instead of springing
  back to the base lengths; above freeze 0.5 every length rounds to an integer; band gains go to
  exactly 1 and the shelves are written as `x + (G - 1) hp + ...`, so they are exactly
  transparent. Lossless and static, and engaging Freeze never bends the pitch.
- **Size**: one glide for the whole tank, slew-limited so the longest line bends <= 12 %.

## Character controls

| Control | Effect |
|---|---|
| Param A, **Density** | In-loop allpass coefficient 0.7 sqrt(A) (0 = the lines are plain delays: a sparse cloud of echoes; 1 = a thick fog), and the pre-diffusion coefficient x (0.5 + A), so the sparse end stays audible at the default Diffusion |
| Param B, **Distance** | Pre-diffusion lengths 0.3x-2.2x (onset smeared over ~50-300 ms), +0.06 pre-diffusion coefficient, input low-pass one octave darker per full turn, and the direct "early" share fades out: the cloud arrives later, smeared and darker |

Measured (48 kHz, Decay 6 s unless noted, tested in `NimbusTests.cpp`):

| | Density 0 | 0.5 | 1 |
|---|---|---|---|
| Envelope roughness 0.1-1.5 s (10 ms frames) | 3.6 dB | 2.2 dB | 1.4 dB |
| Echo density 50-300 ms | 0.66 | 0.98 | 0.96 |
| Echo density 50-300 ms, Diffusion 0 | 0.10 | 0.37 | 0.43 |

| | Distance 0 | 0.5 | 1 |
|---|---|---|---|
| 10 % energy arrival (Decay 30 s) | 191 ms | 285 ms | 454 ms |
| 50 % energy arrival (first 2 s, Decay 30 s) | 701 ms | 799 ms | 983 ms |
| Share of onset energy (0-0.4 s) above 3 kHz | 0.58 | 0.55 | 0.48 |

## Tuning decisions

- **Second-order shelves** (first written here as a local `ShelvingDecayFilter`, now the shared
  `DecayFilter` used by every algorithm): with the earlier first-order shelves, the high shelf at 4.5 kHz leaks into the 1-2 kHz octaves
  (analytically ~16 % of the high band's extra damping at 2 kHz), so the mid-band RT60 read 9 %
  short at 10 s and 10 % short at 30 s, independent of modulation. The SVF shelf
  |H|^2 = (1 + G^2 w^4) / (1 + w^4) is monotonic and bounded by max(1, G) (loop-safe), and leaks
  with w^4 instead of w^2: now -3 % at 10 s, -4 % at 30 s, -7 % at 60 s.
- **Three in-loop allpasses per line**, 0.7 max coefficient: with two, Density's top end was barely
  distinguishable from the middle. The allpasses are short compared to the lines (2-31 vs
  62-212 ms) and all modulated, so their group-delay ripple (which would make some frequencies
  decay slower and ring) is averaged out. Spectral peakiness at Density 1 is the same as at 0.5.
- **Random walks only** for modulation. Lines up to 7 ms deep, slow (~0.2 Hz): the classic lush,
  slowly detuning ambient tail. Mod 0 is allowed (a static network) but rings measurably at 30 s
  (peakiness 4.6 dB vs 2.1 dB at the default), which is why the default Mod is not 0.
- **Lengths**: lines 62-212 ms (mean 122 ms, 1.6x Hall's), allpasses 2-31 ms, pre-diffusion
  4-39 ms; all chosen so that no two share a small common factor and line and allpass lengths
  are uncorrelated.
- **Dark by default**: input low-pass at 7.5 kHz (Distance 0.5), loop high ratio x 0.8 and
  crossover x 0.9: RT(8 kHz) is 0.49 x RT(mid) at Decay 3 s (Hall: 0.57).
- Every gain and coefficient that a control moves is a `LinearRamp` (bandwidth coefficient,
  pre-diffusion and in-loop coefficients, early gain, band gains, the high crossover's SVF
  coefficients); delay lengths are interpolated per sample.

## Measured (48 kHz, defaults)

| Decay | RT125 | RT250 | RT mid | RT 4k | RT 8k | Echo density 0.9 | Wet level | L/R rho | CPU |
|---|---|---|---|---|---|---|---|---|---|
| 1 s | 1.04 | 1.09 | 1.03 | 0.87 | 0.69 | 83 ms | | 0.01 | |
| 3 s | 2.98 | 3.01 | 2.97 | 2.24 | 1.47 | 70 ms | -3.5 dB | 0.00 | 2.4 % |
| 10 s | 10.0 | 10.1 | 9.72 | 6.88 | 4.30 | 71 ms | | 0.01 | |
| 30 s | | | 28.8 | | | | | | |

Gates: RT60 1.03 / 2.47 / 5.87 s at Decay 1 / 2.5 / 6 s (19.2 s at 20 s, 56.0 s at 60 s); mono
sum -3.0 dB; click crest -74 dB static, -65 dB automated; frozen level within 0.6 dB over 60 s,
per band within 0.23 dB; sample-rate RT60 within 0.1 % and echo density within 2 ms.

Long-tail quality, Decay 30 s, window 8-16 s (`TailMetrics.h`):

| | Spectral peakiness (99.5th pct, +-1/12 oct) | Echo density |
|---|---|---|
| Ideal diffuse tail (decaying white noise) | 1.1 dB | 1.0 |
| Hall, defaults | 2.5 dB | 1.02 |
| Nimbus, defaults (Mod 35 %) | 2.1 dB | 1.01 |
| Nimbus, Mod 0 | 4.6 dB | 1.01 |
| Nimbus, Mod 100 % | 1.6 dB | 1.01 |
| Nimbus, Mod 100 %, Rate x4 | 1.7 dB | 1.01 |

Freeze is static: the spectrum of a frozen cloud (Mod 100 %, Rate x4) drifts 0.7 dB rms between
4-8 s and 20-24 s; with the modulation left running in freeze it drifts 6.2 dB (and still passes
the energy-only freeze gate, hence the extra test).

Memory: ~1.8 MB per instance at 48 kHz, ~7 MB at 192 kHz (power-of-2 delay buffers for Size 2x).

## References

- CloudSeed (Valdemar Erlingsson, MIT): the idea of diffusers inside each delay line's feedback and heavy,
  slow random modulation. Read for ideas only; no code taken.
- J. Dattorro, *Effect Design Part 1* (JAES 1997): allpasses inside a decaying loop.
- J.-M. Jot, A. Chaigne, *Digital delay networks for designing artificial reverberators* (AES 1991).
- A. Simper, *Linear trapezoidal integrated SVF* (Cytomic tech note, 2013): the shelving filter.
- J. S. Abel, P. Huang, *A simple, robust measure of reverberation echo density* (AES 2006).
