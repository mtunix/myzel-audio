# Halo

Shimmer: octaves (or fourths, fifths, an octave plus a twelfth) rising into the tail. The pitch
shifter lives only inside the feedback loop, so the first arrival is the plain reverb and every
trip through a shifted line climbs one more interval.

## Topology

```
in L/R -> bandwidth LP (one-pole, 4-18 kHz from Tone)
       -> 6-stage Schroeder allpass diffuser per side (2.9-18.6 ms x Size)
       -> injected into a 16-line FDN (Hadamard rows 1, 2)
          16 lines, 37-144 ms x Size (mean 82 ms), Hadamard mixing, 3-band decay filters
          lines 1, 6, 10, 14 (short, middle, long):
              v = sqrt(1 - a) y + sqrt(a) Shift(y)          a = Amount x Size (<= 1)
              Shift = 1-2 grain voices -> low cut (80 Hz, 2-pole) -> high cut (5.2 kHz, 2-pole)
       -> outputs: Hadamard rows 3 (L) and 5 (R) of the line outputs x 0.48 ---------------> out
```

The tank is the shared `Fdn<N>`, driven through its `read()` / `write()` halves (an insert point
this algorithm introduced) so the shifters sit between the line outputs and the mixing matrix.
The shifted path's high cut follows Tone below flat (5.2 kHz down to ~2.8 kHz at fully dark):
a darker shimmer, and the high band's decay stays dark with the climb feeding it.

### The shifter (`GrainShifter.h`)

- Delay-line granular shifter: grains of 85 ms (+-10 %) start every ~28 ms (+-25 %), about three
  overlap. Window `(1 - x^2)^2` (Hann-like, zero slope at the ends). Read with the shared cubic
  Lagrange `readCubic`. Each shifted line has its own seeded grain clock, so the four shifters
  never line up.
- **Aligned grains (WSOLA-style).** A new grain may start anywhere within +-5 ms of its nominal
  position; it takes the lag at which the input best matches what the previous grain is reading
  (coarse search on a ~12 kHz grid over a 6 ms window, refined at full rate). On tonal material
  consecutive grains then add in phase instead of beating; on diffuse material there is no clear
  match and the choice is as random as plain jitter. Cost: ~10 k multiply-adds per grain start.
- **Correlation-aware normalisation.** The grain sum is divided by `sqrt(sum_i w_i^2 + 2 sum_{i<j}
  w_i w_j rho_ij)`, with `rho` the measured correlation between consecutive grains (chained for
  grains further apart). That is unit power gain for coherent (tonal) and incoherent (diffuse)
  input alike, so the loop gain does not depend on what is being played.
- **Interval changes crossfade at grain level**: every grain keeps the ratio it started with, so
  old grains finish at the old interval while new ones fade in at the new one (one grain length,
  85 ms). The second voice (+19) fades in/out over 60 ms. No discontinuity anywhere.
- The shifted path is charged the decay of its own mean latency (`decayGain(latency)`), so a
  pass through a shifter decays like any other pass of that length.

## Character controls

| Control | Effect |
|---|---|
| Param A, **Interval** | `maps::haloIntervalIndex`: -12, +5, +7, +12, +12/+19. The last runs a second grain voice; the two are mixed at equal power |
| Param B, **Amount** | Energy share `a` of the shifted signal on the four shifter lines. 0 = the plain lush reverb (exactly: the shifters are not even rendered), 1 = those lines fully shifted. Scaled by Size, so the climb rate (intervals per second) is the same at every Size |
| Freeze | `a -> a (1 - freeze)`: the shifters fade out of the loop, which becomes the plain, lossless Fdn (gains 1, no modulation, integer lengths). The climb stops; the held cloud keeps the colour it had |

## RT60: keeping Decay honest while energy migrates

Shifting does not lose energy, it moves it: with +12, what was at 500 Hz is at 1 kHz one shifted
pass later. Measured per octave band, the mid band (500 Hz - 2 kHz) empties faster than the loop
gain says, so uncompensated, the mid-band RT60 came out 12-47 % short (6 s Decay: -29 % at Amount
0.5, -44 % at 1 for +12; -12 similar).

Model: shift events per second `lambda = (4/16) a / T_pass` (T_pass = mean line length + the
shifted share of the shifter latency); migration loss `M = kappa(interval) lambda (Decay/2.5 s)^gamma`
(dB/s, plus `a^0.4` for -12, which also loses real energy at the 80 Hz low cut). The loop runs
at `RT_eff = 60 / (60/Decay - M)`, capped at 6 x Decay. Only the mid band and the band the climb
moves energy *away from* are stretched; the band it moves energy *into* (air for upward
intervals, lows for -12) keeps its nominal RT, so whatever piles up there still dies on time.
The stretch is smoothed over ~80 ms (it jumps with the interval).

Calibrated (`kappa` = 3.15, 1.4, 1.7, 2.4, 2.9 dB per shift; `gamma` 0.35 for -12, 0.2 others)
from impulse responses: `CeruleanTests_halo "[.halo-measure]"`, test "mid-band RT60 per interval and
amount" (mean of the 500/1k/2k octave bands, both channels, T30). Result (48 kHz, Size 1x):

| Interval | Amount | Decay 1 s | 2.5 s | 6 s |
|---|---|---|---|---|
| any | 0 | 0.95 | 2.39 | 5.59 |
| -12 | 0.5 / 1 | 0.98 / 1.02 | 2.45 / 2.44 | 6.00 / **5.14** |
| +5 | 0.5 / 1 | 0.96 / 0.96 | 2.40 / 2.40 | 6.01 / 5.73 |
| +7 | 0.5 / 1 | 0.96 / 0.95 | 2.42 / 2.36 | 5.87 / 5.82 |
| +12 | 0.5 / 1 | 0.96 / 0.95 | 2.37 / 2.32 | 5.99 / 5.95 |
| +12/+19 | 0.5 / 1 | 0.96 / 0.94 | 2.37 / 2.36 | 6.26 / 6.21 |

The -5 % at Amount 0 is the same measuring bias Hall has (the 2 kHz band already feels the high
shelf). Known limits, by design:
- -12 at full Amount with long Decay (or small Size) runs up to ~15 % short: the octave-down
  climb really does lose energy at the low cut, and the 6x cap stops the compensation there.
- Beyond Decay ~10 s with high Amount the cap is reached and the climb itself sets how long a
  note stays in its register (the total tail still lasts; it rises and fades into the air).
- A single low note's broadband level decays ~15 % slower than Decay with upward shimmer (its
  energy keeps arriving in the mid band from below); the IR-based mid-band RT is on target.

## Measurements

Climb (220 Hz note, 0.5 s, Decay 6 s; spectral centroid of 1-s windows from 0.5 s, Hz):

| Interval / Amount | 0.5-1.5 s | 1.5-2.5 | 2.5-3.5 | 3.5-4.5 | 4.5-5.5 |
|---|---|---|---|---|---|
| Amount 0 | 216 | 218 | 216 | 218 | 217 |
| +12 / 0.5 | 267 | 707 | 916 | 1039 | 1322 |
| +12 / 1 | 400 | 1104 | 1224 | 1244 | 1738 |
| +7 / 0.5 | 304 | 408 | 669 | 915 | 1238 |
| -12 / 0.5 | 176 | 160 | 177 | 159 | 198 |

The climb saturates around 1-1.7 kHz: the 5.2 kHz high cut absorbs each octave as it arrives
instead of letting it whistle. Tone-burst test: octave bands peak in order (440 Hz at 0.7 s,
880 Hz at 1.25 s, 1760 Hz at 1.3 s); in the first 90 ms the octave is 32 dB below the note.

Flutter (held 440 Hz, +12, Amount 0.5, envelope of the 880 Hz partial, part above ~8 Hz):
**~4 % RMS** with aligned grains; jitter-only grains measured ~39 % (a random-phasor grain sum,
mod index ~0.5 at the grain rate). The remaining modulation is slow (< 8 Hz), like the
unshifted tail's own chorus.

Gates (48 kHz, base +12 / Amount 0.5):

| RT 1 / 2.5 / 6 s | RT125/mid (LE 2x / 0.5x) | RT 4k dark / bright | Echo density 0.9 | Wet level | L/R rho | Mono sum | CPU (base / worst) |
|---|---|---|---|---|---|---|---|
| 0.96 / 2.37 / 5.99 | 1.36 / 0.73 | 1.12 / 2.80 s | 77 ms (44.1-192 kHz: 76-77) | -3.1 dB | -0.03 | -3.1 dB | 1.7 % / 2.1 % (+12/+19, Amount 1) |

Click checks: switching -12/+5 every 200 ms (both dark) leaves the band above 14 kHz at
-105.8 dB re RMS (static: -108.8 dB). The generic gate's 3rd-difference detector reads the climb's
legitimate high partials as "clicks" (static +12 at Amount 0.5: -32 dB, +7: -58 dB), hence the
+12 base in the test Options.

## Tuning decisions

- **4 of 16 lines shifted**: with Amount 0.5 the octave arrives in ~0.7 s and the tail keeps
  rising for ~3 s. Shifting all lines climbed several octaves per second (whistle territory).
- **Grain 85 ms**: long enough for smooth, low-sideband shifting of a reverb tail; latency in the
  loop is harmless (it only lengthens the shifted passes, and their decay is charged for it).
- **Fixed 80 Hz / 5.2 kHz band limits** on the shifted path only: the unshifted reverb keeps Tone
  and Low End exactly; the shimmer's sparkle stops where whistles start.
- **6 diffuser stages at 0.78 sqrt(Diffusion)**: a soft onset, and an echo density that rises
  cleanly through 0.9 at the same time at every sample rate (with 4 stages it hovered around
  0.85-0.9 for 20 ms and the crossing time jumped between rates).
- Every control-dependent gain ramps per sample: dry/shift shares land *exactly* on their target
  (`ExactRamp`), so Amount 0 and Freeze 1 are bit-exact plain loops.

## Recommended shared changes (not made: outside this directory)

- (Done at integration: `Fdn<N>` now has the `read()` / `write()` insert point.)
- `LinearRamp`: an option to land exactly on the target at the end of the ramp (`ExactRamp`).
- PLAN.md lists a granular pitch shifter as a shared block: `GrainShifter.h` (aligned grains,
  correlation-aware normalisation) is ready to move to `src/dsp/common`.

## References

- W. Verhelst, M. Roelands, *An overlap-add technique based on waveform similarity (WSOLA)*
  (ICASSP 1993): grain alignment.
- J.-M. Jot, A. Chaigne, *Digital delay networks for designing artificial reverberators* (AES 1991).
- S. Costello, Valhalla DSP blog: notes on shimmer (pitch shifting in the feedback path).
- G. Luff, *Let's Write a Reverb* (ADC 2021): diffuser + Hadamard FDN structure.
