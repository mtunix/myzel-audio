# Spring

The boingy guitar-amp tank: drips, chirps, splash. A parametric model after Välimäki, Parker &
Abel (dispersive stretched-allpass cascades inside a delay loop), built as 1-3 springs with two
pickups each, a weak high-frequency "sizzle" loop and a soft, anti-aliased drive into the tank.

## Topology

```
in L/R -> low cut (2nd order, 95 Hz) -> Drip emphasis (+0..9 dB above 1.5 kHz)
       -> transducer bandwidth (2nd order LP, 6.5 kHz) -> dwell: soft clipper, +1..8 dB, ADAA
       |
       |  per spring s (1..3; input weights L/R 0.5/0.5, 0.65/0.35, 0.35/0.65):
       |
       +-> (+) -> dispersion A(z^K)^72 -> transit (28.7 / 34.4 / 39.6 ms x Size) --> far pickup
       |    ^                                                                      |
       |    |                                     dispersion A(z^K)^72 <-----------+
       |    |                                            |
       |    +-- smear: 2 allpasses (Diffusion) <- decay filter <- transit (same length)
       |    +---------------------------------------------------------------> near pickup
       |
       +-> 4th-order HP 3.2 kHz -> sizzle loop (27.4 ms x Size, 3 allpasses g 0.62, decay)
                                   -> L: loop return, R: tap at 0.79 (each LP 7 kHz)
pickups -> L/R: far end to one side, near end to the other, cross-feed 0.30/0.22/0.17 (1/2/3 springs)
        -> 4th-order Butterworth at 0.92 fc (removes the stretched allpass images) -> + sizzle -> out
```

- **Dispersion**: A(z) = (a + z^-1)/(1 + a z^-1) stretched to A(z^K), K = round(fs / 2 x 4.6 kHz)
  (K = 5 at 44.1/48 kHz -> fc 4.41/4.8 kHz; 10 at 96 kHz; 21 at 192 kHz). With a > 0 the group
  delay rises towards fc: high frequencies arrive later, the drip chirp. 72 stages per transit
  (144 per round trip) in two coefficient groups (40 + 32), see Drip below. The four SIMD lanes
  carry the three springs (`DispersionCascade.h`: TDF-II, one multiply-add on the critical path
  per stage, states stored phase-major).
- **Two pickups per spring**: the far end hears transits 1, 3, 5... (after half the dispersion),
  the near end 2, 4, 6... (after all of it). Physically the spring's two ends; for the image, two
  decorrelated views of one spring (they interleave in time instead of cancelling), so even a single
  spring has some width and sums to mono cleanly.
- **Decay**: per-round-trip band gains from `computeBandGains`, with the loop length taken at 1 kHz:
  both transits + the cascade's group delay there (analytic) + the smear allpasses. Recomputed every
  sub-block from the gliding lengths and ramped per sample.
- **Low End / Tone**: the shared 3-band decay filter in each loop (Low End at 250 Hz; Tone moves
  the high crossover and ratio, `maps::toneToHighDecay`). The tank is band-limited by its input
  (95 Hz HP, 6.5 kHz LP) and output (image cut at 4.4 kHz, sizzle LP 7 kHz), yet 125 Hz and 4 kHz
  keep enough energy for both bands to be shaped and measured.
- **Freeze**: band gains to exactly 1, modulation depth to 0, the drive crossfaded out (input is cut
  by the chain anyway), every delay and allpass length (including the Drip compensation offset)
  rounded to an integer. Lengths glide in double (`PreciseGlide.h`) and each block's per-sample
  ramp starts exactly where the last one ended, so the frozen reads are exact. Every block in the
  loop is lossless (stretched allpasses, Schroeder allpasses, integer delays).

## Character controls

| Control | Effect |
|---|---|
| Param A, **Springs** | 1-3 springs (`maps::springCount`), each with its own transit and dispersion scale (1.0/0.92/1.07). Springs fade in/out over 30 ms. Output is normalised by 1/sqrt(active) so the level stays put; cross-feed narrows with count (correlation 0.53 / 0.40 / 0.32). |
| Param B, **Drip** | The surf "drip". Three things together: (1) dispersion: group 1 coefficient 0.06 -> 0.55 over 0..0.5, group 2 joins 0 -> 0.55 over 0.5..1; (2) treble emphasis into the tank, 0 -> +9 dB shelf above 1.5 kHz; (3) dwell, +1 -> +8 dB into the soft clipper. Output gain compensates the emphasis. 0 = a smooth, subtle tank; 1 = splashy, chirpy, driven. |

Tested (`tests/algorithms/spring/SpringTests.cpp`): the 3.8 kHz band arrives -1.4 / +3.1 / +6.8 ms
relative to 700 Hz at Drip 0 / 0.5 / 1 (first arrival, one spring), and +18.6 ms after three
transits; 700 Hz timing moves < 0.1 ms with Drip; the 3 kHz/500 Hz energy ratio rises
5.1 -> 8.3 -> 10.9 dB; energy arriving 35-46 ms (springs 2 and 3) relative to spring 1's arrival
is -24 / -2.5 / +0.4 dB for 1 / 2 / 3 springs (Drip 0).

## Tuning decisions

- **Drip mapping**: per stage, the in-band (700 Hz -> 3.8 kHz) group-delay spread of a stretched
  first-order allpass peaks at a ~ 0.55 and *falls* beyond it; larger a only piles delay into the
  last few hundred Hz under fc, which the image cut removes (first version: a up to 0.72 measured
  4.0 -> 4.8 ms from Drip 0.5 -> 1, i.e. barely any change). So above Drip 0.5 more stages join
  instead (group 2 goes from plain K-sample delays to a = 0.55): the in-band spread grows evenly
  over the whole knob.
- **Drip keeps the spring's length**: dispersion also changes the cascade's DC delay (more
  dispersion = less delay at DC, up to +-120 samples). The transits absorb the difference to
  Drip 0.5, so echo spacing (Size's job) doesn't move and Drip automation doesn't bend the bass.
- **Stereo width**: a real tank is mono and narrow. Target: clearly narrower than the halls, wider
  with more springs, never decorrelated: tail correlation 0.53 (1 spring), 0.40 (2, default),
  0.32 (3), mono sum -1.2 .. -1.8 dB (gate: > -4). Achieved with far/near pickups on opposite
  sides plus a cross-feed of 0.30/0.22/0.17, and springs 2/3 leaning L/R at the input.
- **Lengths**: transits 28.7/34.4/39.6 ms (round trips ~74/87/98 ms at low frequencies, incl. the
  cascades' DC delay and the smear):
  long-decay-tank territory, no small common ratios so the boings never lock together.
- **Smear** (Diffusion): two Schroeder allpasses per round trip (2.9-5.8 ms), g = 0.6 sqrt(Diffusion)
  (0.52 at the default). The coil/mount coupling that turns later chirps into a wash while the
  first few stay distinct. Diffusion 0 gives a bare, very "sproingy" tank.
- **Sizzle**: the modes above fc. A single weak loop (output gain 0.21-0.35, Drip 0-1) of three short allpasses, fed
  only above 3.2 kHz, output-tapped *after* its 27 ms line (a tank has no output before the transit
  time; the first version leaked sizzle at t = 0). Gives the splash on top of the drips.
- **Image cut**: 4th-order Butterworth at 0.92 fc. The stretched allpass mirrors its chirp above
  fc (fc..2fc sweeps downwards); those images would sound metallic. They circulate in the
  (lossless) loop and are removed only at the output, so Freeze stays exact.
- **Drive**: Padé-tanh soft clipper with first-order antiderivative anti-aliasing (`SoftDrive.h`,
  closed-form antiderivative, double precision). No oversampling needed: the input is already
  band-limited to 6.5 kHz, and ADAA took a 0 dBFS 5 kHz tone at full Drip from -5 dB to -41 dB of
  aliasing at 3 kHz (at -10 dBFS it is below the measurement floor). Gentle: a -20 dBFS tone at full
  Drip keeps its 3rd harmonic at -46 dB; compression at -10 dBFS is 0 dB.
- **Size**: transits, smear and sizzle scale and glide (220 ms one-pole + slew limit). The input
  is scaled by sqrt(Size) because a loop's steady-state energy at fixed RT60 goes as 1/length:
  wet level -4.6 / -3.8 / -3.2 dB at Size 0.3x / 1x / 2x (was +0.6 / -3.8 / -6.2 dB).
- **Mod**: smooth random wobble on each transit, up to 0.3 ms at ~0.9 Hz x Mod Rate (spread per
  line). Only decorrelates and loosens the loops; springs don't chorus.
- **Ramps**: every control-dependent gain and coefficient is a `LinearRamp` (emphasis, dwell, drive
  mix, both dispersion groups, smear, spring fades, input and pickup weights, sizzle, output);
  delay lengths and the Drip offset are interpolated per sample.
- **CPU**: the springs run in passes over the sub-block. The back transit is always longer than a
  sub-block, so the springs' inputs for the whole block are known before the cascades run; each
  cascade then processes four consecutive samples interleaved (they touch different phase rows,
  so they're independent). 2.25 % -> 0.91 % of a core, bit-identical output at any sub-block size
  (tested at 1/7/16/32).

## Gate options

None changed from the defaults. The echo-density target (0.9 by 150 ms) is met at ~40 ms even
though a spring is not diffuse: Abel-Huang counts samples beyond one standard deviation, and a
swept sinusoid has more of those than Gaussian noise, so each dispersed chirp reads as "dense".
The spring's sparse echo pattern is tested separately (Springs test above).

## Measured (48 kHz, defaults unless stated: Springs 0.5 = 2, Drip 0.5, `[.measure]`)

| Setting | RT125 | RT250 | RT1k | RT4k | RT mid | Echo density 0.9 | L/R rho | Wet level |
|---|---|---|---|---|---|---|---|---|
| Decay 3 s, 2 springs | 2.87 | 3.09 | 2.87 | 2.37 | 2.84 | 40 ms | 0.39 | -3.6 dB |
| Decay 1 s, 2 springs | 1.01 | 1.03 | 0.99 | 0.87 | 0.97 | 39 ms | 0.42 | |
| Decay 3 s, 1 spring, Drip 0 | 2.86 | 3.04 | 2.89 | 2.22 | 2.83 | 69 ms | 0.54 | -4.3 dB |
| Decay 3 s, 3 springs, Drip 1 | 2.92 | 3.06 | 2.90 | 2.58 | 2.85 | 36 ms | 0.30 | -4.4 dB |

- Gates: RT60 0.97 / 2.37 / 5.62 s for Decay 1 / 2.5 / 6 s (the 2 kHz band already feels the high
  shelf, as in Hall); Low End 2x -> RT125/mid 1.55, 0.5x -> 0.70; RT 4 kHz dark 0.69 s, bright
  3.33 s; frozen level 3 s -> 60 s: -11.02 -> -10.96 dB (range 0.17 dB), 4 kHz band -0.02 dB over
  30 s; click crest -67 dB automated (static -86, limit -50); sample-rate RT60 within 0.8 %.
- Wet level over Springs x Drip corners: -3.4 .. -5.0 dB. Pink-noise response (out - in): 63 Hz
  -9.6, 125 -3.3, 250 -1.0, 500 +0.1, 1k +0.7, 2k +1.3, 4k -1.2, 8k -13.9, 16k -33.5 dB: a
  ~100 Hz - 5 kHz tank.
- CPU: 0.91 % of one core (48 kHz stereo, `[.perf]`, Release); budget 3 %.

## References

- V. Välimäki, J. Parker, J. S. Abel, *Parametric Spring Reverberation Effect* (JAES 2010):
  stretched allpass chirp cascades in a delay loop, low/high chirp paths.
- J. S. Abel, D. P. Berners, S. Costello, J. O. Smith, *Spring Reverb Emulation Using Dispersive
  Allpass Filters in a Waveguide Structure* (AES 2006).
- J. Parker, S. Bilbao, *Spring Reverberation: A Physical Perspective* (DAFx 2009): dispersion
  below the transition frequency, group delay rising with frequency.
- J. Parker, *Efficient Dispersion Generation Structures for Spring Reverb Emulation* (EURASIP 2011).
- J. Parker, V. Zavalishin, E. Le Bihan, *Reducing the Aliasing of Nonlinear Waveshaping Using
  Continuous-Time Convolution* (DAFx 2016): first-order ADAA.
