# Billow

A reverb that keeps building after the note: it blooms for the bloom length, then decays at
exactly the Decay setting.

## Topology

```
in L/R -> bandwidth LP (one-pole, 9 kHz x 2^(0.9 Tone), ramped)
       -> 6-stage Schroeder allpass diffuser per side (3-20 ms x Size, Diffusion) = d
          |
          +-- x 0.35 (direct) ------------------------------------------------------------+
          |                                                                              v
          +-> bloom network B: 8-line FDN, 97-204 ms x Size, Hadamard,      x bloom    M: 16-line FDN,
              decays at RT_B = bloom length x (6 + 6 Feedback) (<= 40 s),  envelope   34-135 ms x Size,
              highs at 0.6 x RT_B from 5 kHz, random-walk mod              x level -> decays at Decay
                                                                                         |
       out L/R <- Hadamard rows 3 / 5 of M's lines x 0.12 <------------------------------+

bloom envelope: open while the input plays (fast peak envelope within 20 dB of the recent peak,
                and above -80 dBFS) and for one bloom length after it stops; while open it rises
                linearly over the bloom length (from wherever it is), then closes over
                0.25 x bloom length (80-500 ms).
```

Both FDNs are the shared `Fdn` (which adopted this algorithm's second-order shelving and freeze
handling at integration, see below).

**Why gated, not a plain cascade.** A cascade of two decaying tanks (bloom tank -> main tank) is
linear and also rises then falls, but after its peak it decays at the rate of the *slower* tank.
A 4 s bloom feeding a 1 s Decay could then never decay at 1 s. With the gate, B pours energy into
M while the envelope is open (the tail grows); once it closes only M rings, so the decay after the
bloom *is* the Decay setting, for every Bloom/Feedback/Decay combination (measured below).

**Why it is bounded.** B's loop gain is always < 1 (it is an ordinary decaying FDN, RT_B <= 40 s),
the envelope is <= 1, and M is a stable FDN. No gain anywhere exceeds unity in a loop; the bloom
is a bounded input into a stable tank. Worst case measured (Feedback 100 %, Bloom 4 s, Decay 60 s,
Size 2x, Mod 100 %, 0 dBFS 100 Hz square held for 60 s): peak 8.5, settles at +3.7 dB RMS. The same
square chopped into bursts that reopen the bloom every 0.5 s: peak 10.0, settles at +4.2 dB RMS.
(Any reverb at Decay 60 s gains ~+15 dB on steady input; the gate's limit is a peak of 64.)

**Freeze.** The bloom level ramps to 0 with freeze (it would otherwise keep pouring into the
lossless tank and grow it); M follows the Fdn freeze contract. B keeps decaying on its own and is
disconnected. Freezing in the middle of a 4 s, Feedback 100 % bloom: the frozen level stays within
-0.5 / +0.2 dB of the level right after freezing (1 s windows) for 30 s.

## Character controls

| Control | Effect |
|---|---|
| Param A, **Bloom** | Bloom length `maps::bloomLengthMs` (150 ms - 4 s): the envelope's rise time and how long it stays open after the input stops; also scales B's memory |
| Param B, **Feedback** | How much the bloom feeds on itself: B's decay (6x .. 12x the bloom length) and the bloom level (+8 dB over the range). Subtle swell .. huge build |

The bloom level also follows Decay a little ((3 s / Decay)^0.3, 0.4 .. 2.5): a short Decay holds
less of the bloom in the tank, so it gets more, and the rise stays similar across Decay.

Measured rise of the energy peak (20 ms frames) above the onset (first 100 ms), and when it peaks:

| Decay | Bloom | Feedback 0 | Feedback 50 % | Feedback 100 % | Peak at |
|---|---|---|---|---|---|
| 1 s | 0.15 s | +3.8 dB | +10.3 dB | +15.8 dB | 0.24 s |
| 1 s | 0.77 s | +4.5 dB | +11.3 dB | +16.8 dB | 0.76 s |
| 1 s | 4 s | +4.0 dB | +11.0 dB | +15.7 dB | 4.04 s |
| 3 s | 0.77 s | +4.2 dB | +11.0 dB | +16.5 dB | 0.74-0.88 s |
| 3 s | 4 s | +4.3 dB | +11.4 dB | +16.0 dB | 4.02 s |
| 10 s | 0.77 s | +4.2 dB | +10.4 dB | +15.8 dB | 0.74-0.88 s |
| 10 s | 4 s | +5.5 dB | +12.5 dB | +17.1 dB | 4.02 s |

(At Feedback 0 with a 4 s bloom and Decay <= 3 s the gentle swell peaks a little early, 2.9-3.2 s:
B has decayed too far by then to keep growing.)

## Tuning decisions

- **Presence gating, not onset gating.** The first version opened the bloom on note onsets only
  and closed it one bloom length later. A held note then bloomed and sank back ~20 dB while still
  held (pumping on every pad). Now the bloom stays open while the input plays: held notes keep
  blooming, and every note keeps building for one bloom length after it ends. The presence test
  is relative (within 20 dB of the recent peak), so it behaves the same at any input level; the
  -80 dBFS floor keeps hiss and dither from holding it open. Steady input is bounded by B's
  steady-state gain (<= +13 dB at RT_B 40 s).
- **Linear envelope** (energy ~t^2). A squared or smoothstep shape started so slowly that the
  direct reverb dipped first ("dip, then swell"); linear grows monotonically from the note on.
- **B's memory 6-12x the bloom length.** Shorter (2-4x) and B had decayed by >10 dB before the
  envelope peaked, so the bloom peaked early and Feedback 0 did not bloom at all.
- **B's high damping is fixed** (0.6 x RT_B from 5 kHz), not tied to Tone. Tied to Tone, a dark
  setting left a bloom with no highs at all; the bloom then carried only mids, which leaked into
  the 4 kHz octave band and masked the dark tank's fast high-band decay (the Tone gate read RT 1.7 s
  at 4 kHz instead of ~0.4 s). Tone now shapes the main tank and the input, as for the direct path.
- **6-stage input diffuser, 34-135 ms tank.** With 4 stages and a 40-158 ms tank, the echo density
  hovered at 0.85-0.9 from 55 to 100 ms, so its 0.9 crossing moved 26 ms between 48 and 96 kHz.
  Now it crosses at 52-59 ms at every rate.
- **Second-order shelving decay filters** (written here as a local `ShelvedFdn`, now the
  shared `Fdn` + `DecayFilter`): with the shared first-order shelves the decay
  after the bloom read 8-9 % short at 6-20 s; now 1-4 %. The Fdn also stops the
  modulation where it is on Freeze (no pitch bend) and restarts each block from the exact previous
  delay target (a frozen length stays exactly integer).
- Every gain or coefficient a control moves is a `LinearRamp` (bandwidth, diffusion, bloom level,
  band gains, the high crossover's SVF coefficients); the bloom envelope itself is per sample.

## Measured (48 kHz, defaults: Bloom 0.77 s, Feedback 50 %)

RT60 measured after the bloom has closed:

| Decay | RT125 | RT250 | RT mid | RT 4k | RT 8k | Echo density 0.9 | Wet level | L/R rho | CPU |
|---|---|---|---|---|---|---|---|---|---|
| 1 s | 1.01 | 0.99 | 1.00 | 0.87 | 0.78 | 50 ms | | 0.03 | |
| 3 s | 2.99 | 3.07 | 2.97 | 2.46 | 1.96 | 52 ms | -3.3 dB | 0.01 | 1.7 % |
| 10 s | 9.73 | 10.05 | 9.87 | 7.85 | 5.69 | 55 ms | | 0.00 | |

Decay after the bloom, across the range (test "after the bloom, the tail decays at exactly Decay"):
Bloom 4 s / Feedback 100 % / Decay 1 s: 0.99 s; 4 s / 50 % / 3 s: 2.94 s; 0.77 s / 100 % / 2 s: 1.97 s;
0.15 s / 0 % / 6 s: 5.81 s; 0.77 s / 50 % / 10 s: 9.71 s; 4 s / 100 % / 20 s: 19.25 s.

Gates: whole-IR RT60 1.03 / 2.48 / 5.90 s at Decay 1 / 2.5 / 6 s; dark/bright RT(4 kHz) 0.80 / 2.98 s;
mono sum -3.3 dB; click crest -64 dB static, -60 dB automated (Billow's level swings with the
bloom, which raises the crest ratio; the peaks sit at the loudest part of the bloom and are the
220 Hz test tone's own curvature, not clicks); frozen level -0.18 dB from 3 s to 60 s (range
0.8 dB, the slow beating of a static network), per band within 0.07 dB.

Memory: ~1.4 MB per instance at 48 kHz, ~5.6 MB at 192 kHz.

## Test options

`extraTailSeconds = 4.5`: the tail grows for up to one bloom length (4 s) plus release before it
decays, so every gate window that expects a decaying tail starts that much later. No check is
disabled or relaxed otherwise; the decay *after* the bloom is checked separately (above).

## References

- J.-M. Jot, A. Chaigne, *Digital delay networks for designing artificial reverberators* (AES 1991).
- G. Luff, *Let's Write a Reverb* (ADC 2021): diffuser + Hadamard FDN structure.
- A. Simper, *Linear trapezoidal integrated SVF* (Cytomic tech note, 2013): the shelving filter.
- Sean Costello's Valhalla DSP blog: notes on swelling / "bloom" style reverbs (ideas only).
