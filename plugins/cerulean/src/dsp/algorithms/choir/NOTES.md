# Choir

Vowel-like voices emerging from the tail: a hall whose decay is sung back by up to four formant
"singers", each hearing its own part of the tank.

## Topology

```
in L/R -> bandwidth LP (one-pole, 4-18 kHz from Tone)
       -> 6-stage Schroeder allpass diffuser per side (2.7-17.5 ms x Size)
       -> 16-line FDN (shared Fdn<16>, 31-127 ms x Size, mean 70 ms, Hadamard, 3-band decay)
       -> plain tail: Hadamard rows 3 (L) / 5 (R) ------------------------- x 0.45 ---+
       -> voice k (k = 0..3): Hadamard rows 6+2k / 7+2k (its own orthogonal taps)     |
            -> delay 4-25 ms (x sqrt Size) + vibrato + slow detune drift              +-> x 2.2 -> out
            -> 4 parallel formant band-passes (vowel x tract scale), make-up gain     |
            -> pan / width, level (fractional voices fade in), 1/sqrt(sum levels^2) --+ x 0.89
```

The formant bank is outside the loop: Decay, Low End, Tone and Freeze are the hall's, exactly
(the shared Fdn), and the voices only colour what the tail is doing. The 0.45 / 0.89 blend is
power-complementary: mostly voices, but enough plain tail that it is still a reverb (the valleys
between formants are filled, transients keep their space).

### Formants (`FormantBank.h`)

- Table A-E-I-O-U: F1-F3 from Peterson & Barney (1952) male averages, F4, bandwidths and levels
  as in the usual singing-voice formant tables (F1 strongest, /i/ with a strong F2-F3 cluster,
  /u/ nearly F1 alone).

  | Vowel | F1 | F2 | F3 | F4 | levels (dB) |
  |---|---|---|---|---|---|
  | A | 730 | 1090 | 2440 | 3400 | 0 -4 -16 -24 |
  | E | 530 | 1840 | 2480 | 3500 | 0 -10 -14 -24 |
  | I | 270 | 2290 | 3010 | 3600 | 0 -14 -18 -26 |
  | O | 480 | 820 | 2450 | 3300 | 0 -6 -24 -30 |
  | U | 300 | 870 | 2240 | 3300 | 0 -12 -28 -34 |

- Morph: frequencies and bandwidths interpolated in log frequency, levels in dB, continuous over
  0..4, so a sweep glides like a tongue rather than cross-fading two filter sets.
- Simper SVF band-passes at unity peak gain, both channels of a voice sharing coefficients.
  `g`, `k` and every gain ramp linearly per sample within the sub-block, followed by a 2 ms
  one-pole, so the coefficient trajectory has no corners at sub-block boundaries (a fast vowel
  sweep with corners measured -74 dB above 14 kHz; smoothed it is below the static render).
- Make-up gain from the bank's power gain on pink noise (`sum g_i^2 (pi/2) (BW_i / F_i) / ln 1000`),
  so the vowel changes the colour, not the loudness (measured: all five vowels within 1 dB).

### Voices

| Voice | Tract scale | Delay | Vibrato | Pan / width |
|---|---|---|---|---|
| 1 | 1.00 (tenor) | 4.1 ms | 5.1 Hz | centre / 0.35 (a fairly narrow solo voice) |
| 2 | 1.08 (alto) | 11.3 ms | 4.7 Hz | -0.55 / 0.7 |
| 3 | 0.93 (bass) | 17.9 ms | 5.6 Hz | +0.55 / 0.7 |
| 4 | 1.15 (soprano) | 25.1 ms | 4.4 Hz | centre / 1.0 |

Each voice's taps are orthogonal Hadamard rows of the tank, so the voices are decorrelated
"singers" of the same tail. With Mod: vibrato up to 0.35 ms peak (~+-20 cents at 5 Hz) and a
slow detune drift up to 1.6 ms (~0.4 Hz, ensemble spread). The tract scales stay within 0.93-1.15:
wider (0.88-1.27 was tried) smeared the formants of four voices so much that the vowel blurred.

## Character controls

| Control | Effect |
|---|---|
| Param A, **Vowel** | `maps::vowelPosition`: continuous A-E-I-O-U morph of every voice's formants |
| Param B, **Voices** | `maps::voiceCount` 1..4; voice k's level is `clamp(count - k, 0, 1)`, all normalised by `1/sqrt(sum level^2)` so more voices thicken and widen without getting louder |
| Mod (shared) | Also makes the vowel wander on its own: whole choir +-1.2 vowel positions (smooth random, 0.17 Hz x Mod Rate) plus +-0.25 per voice (0.23-0.39 Hz), reflected at A and U. At the default Mod 0.35 that is a gentle +-0.4 |
| Freeze | The Fdn freezes (lossless, no modulation); every voice modulator's rate goes to 0 with Freeze, so vibrato, detune and vowel drift *hold where they are* (no jump back to the knob's vowel) and the frozen choir is static |

## Measurements (48 kHz, `CeruleanTests_choir "[.choir-measure]"` and the tests)

Formant peaks of the steady output (pink noise, 1 voice, Mod 0, 1/12-octave analysis):

| Vowel | Peaks found |
|---|---|
| A | 756, 1133, 2543 Hz |
| E | 535, 1798, 2543 Hz |
| I | 267-283, 2265, 3024 Hz |
| O | 476, 849 Hz |
| U | 283-300, 899 Hz |

F2 glides monotonically from A to E: 1070 -> 1273 -> 1429 -> 1604 -> 1800 Hz in quarter steps.

Voices (vowel A, steady pink noise): L/R correlation 0.51 (1) -> 0.43 (1.5) -> 0.32 (2) -> 0.30
(2.5) -> 0.25 (3) -> 0.22 (3.5) -> 0.16 (4); level within 1.1 dB; 1/12-octave bins within 6 dB of
the strongest one 7 -> 11 (the formants of four tract lengths spread: a thicker sound).

Vowel drift (vowel E-I, 1 voice, 300 Hz re 530 Hz balance over 1-s windows): std 0.67 dB at Mod 0
(the noise floor of the measurement), 7.5 dB at Mod 1, 0.36 dB at Mod 1 once frozen.

Gates (48 kHz, defaults: vowel I, 2.5 voices, Mod 0.35):

| RT 1 / 2.5 / 6 s | RT125/mid (LE 2x / 0.5x) | RT 4k dark / bright | Echo density 0.9 | Wet level | L/R rho | Mono sum | CPU (base / worst) |
|---|---|---|---|---|---|---|---|
| 0.95 / 2.32 / 5.53 | 1.64 / 0.83 | 0.70 / 2.95 s | 53 ms (44.1-192 kHz: 53-54) | -2.8 dB | 0.33 | -1.8 dB | 1.65 % / 2.0 % (4 voices, Mod 1) |

The mid-band RT reads 5-8 % short, the same bias as Hall (the 2 kHz band feels the high shelf;
the formants are outside the loop and do not change the decay). Click checks: generic gate
3rd-difference crest -62 dB automated vs -84 dB static (limit -50 dB); vowel jumping A/U every
150 ms leaves the band above 14 kHz at -114.9 dB re RMS (static -109.6 dB).

## Tuning decisions

- Formants outside the loop: a formant bank in the loop would make the RT depend on the vowel
  and ring at the formants; outside, Choir is an honest hall with voices on top.
- One shared 16-line tank, voices on orthogonal taps: four decorrelated singers for the price of
  a few sums, and the voices' content is exactly the tail (they sing what you played).
- 6 diffuser stages at 0.74 sqrt(Diffusion): with 4 the echo density hovered around 0.9 between
  45 and 60 ms (the formant resonators' ringing inflates the measure), and the crossing time
  jumped between sample rates; with 6 it crosses cleanly at 53-54 ms at every rate.
- Voice delays scale with sqrt(Size), so the choir spreads with the room but stays a choir.

## Recommended shared changes (not made: outside this directory)

- A per-sample ramped SVF (`FormantBank`'s linear-ramp + one-pole coefficient path) could serve
  any algorithm that sweeps resonant filters.

## References

- G. E. Peterson, H. L. Barney, *Control methods used in a study of the vowels* (JASA 1952).
- J. Sundberg, *The Science of the Singing Voice* (1987): singer's formant levels, bandwidths.
- A. Simper, *Linear trapezoidal integrated SVF* (Cytomic, 2013).
- J.-M. Jot, A. Chaigne (AES 1991); G. Luff, *Let's Write a Reverb* (ADC 2021): the tank.
