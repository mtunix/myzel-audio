# Clean: Basalt's own feed-forward compressor

The default model: a transparent, feed-forward compressor whose knobs mean exactly what they say.
There is no reference unit. Clean is **Basalt's design** on the digital compressor of Giannoulis,
Massberg and Reiss (2012), with the crest-factor Auto release of their 2013 paper. It uses the
shared core (`../../dynamics/NOTES.md`) and overrides nothing there; this file specifies the four
parts `PLAN.md` (*Models*) asks for and Clean's gates.

Status: M0 specification, implemented in M1 (2026-11-17) as `CleanModel` (`CleanModel.h`), checked
at unit level by `BasaltTests_dynamics` (`tests/dynamics/CleanTests.cpp`) and gated through the
processor by `BasaltMeasure` (*Measured in M1*). M1 found the release longer than the knob when the
RMS half is in the blend, which M0 had inferred away; *Ballistics*, *Release with the RMS half*
says what changed, and `PLAN.md` gates the floor it gives (Settled 53). Marks as in the core's NOTES: **source**,
**measured (simulation)** (Python/NumPy/SciPy, double precision, 48 kHz, set-ups given with the
numbers, scripts not kept), **measured (M1)** (the engine, by the tests named), **Basalt's
design**, **inference**, **not verified**.

## Sources

| Source | What Clean takes | Read |
|---|---|---|
| Giannoulis, Massberg, Reiss, *Digital Dynamic Range Compressor Design—A Tutorial and Analysis*, JAES 60(6), 399–408, 2012 | eq. 4 (soft knee), eq. 7 (time constant), eq. 10 (RMS), eq. 17 (smooth decoupled detector), eq. 23 (log domain after the gain computer), §5 (the recommendation: feed-forward, log domain, smooth decoupled) | read in full (the authors' copy) |
| Giannoulis, Massberg, Reiss, *Parameter Automation in a Dynamic Range Compressor*, JAES 61(10), 716–726, 2013 | the crest factor from a peak and an RMS detector with equal 200 ms constants (eq. 7, §2.1.1), release scaled by 2/C² (eq. 8), the listening results on release | read in full (the authors' copy); the 2014 erratum to eq. 3 **not seen** |
| `../../dynamics/NOTES.md` | conventions, look-ahead window, Range, stereo, key EQ, the measurement definition, the release calibration h(ρ) | Basalt |

## 1. Detector

Feed-forward, in the log domain after the gain computer (Giannoulis 2012, eq. 23; **source**).
Per detector channel, per sample:

```
m[n]  = β·m[n−1] + (1 − β)·k²[n]                         RMS state, β = e^(−1/(τ_RMS·fs))
L_P   = 20·log10|k[n]|                                    peak level (instantaneous)
L_R   = 10·log10(2·m[n])                                  RMS level, a sine reads its peak
r[n]  = min( (1 − b)·r(L_P) + b·r(L_R), Range )           blend of reductions, in dB
```

with r(·) the eq. 4 reduction (`x − G(x)`), after the look-ahead window on |k| and m
(`../../dynamics/NOTES.md`, *Look-ahead*). This is the "RMS/peak blend in the log domain" of
`PLAN.md`'s table (**Basalt's design**):

- **Blending reductions, not levels.** The instantaneous peak level is −∞ at every zero crossing,
  so a blend of levels in dB is meaningless before a hold. Blending the two reductions and letting
  the smooth decoupled detector's first stage hold the peak of the blend gives, on a sine,
  `(1 − b)·r(peak) + b·r(RMS)`, and since both levels read the sine's peak, exactly r(peak): the
  static curve on sines does not depend on the blend. On the constant-magnitude carrier the RMS
  half reads 3.01 dB higher (core, *Conventions*), so there the curve is
  `(1 − b)·r(L) + b·r(L + 3.01)`: up to b·3.01·(1 − 1/R) dB above r(L), 1.13 dB at b = 0.5, R 4.
- **The blend follows Attack.** b = 0 (pure peak) for Attack ≤ 1 ms, rising linearly in log
  Attack to b = 0.5 at 10 ms and above:
  `b = 0.5·clamp(log10(Attack / 1 ms), 0, 1)`.
  A fast Attack is for catching peaks, and a short-window RMS would read a sine's instantaneous
  power (up to +3 dB at its crests); a slow Attack is for level, where the RMS half lets a dense
  passage weigh as much as a single spike. One knob, no hidden mode.
- **The RMS window follows Attack:** τ_RMS = Attack/4. Measured (simulation, below): with a fixed
  5 ms window the attack read 1.8 … 7.9× the knob at 0.1 ms and 1.07 … 1.17× at 10 ms, depending
  on depth and ratio; with τ_RMS = Attack the spread was 1.10 … 1.33×; with Attack/4, 1.03 …
  1.08×, close enough to calibrate out (*Ballistics*).
- **Key, stereo, look-ahead** as the core: Internal or External key after the key EQ; Linked
  (Link %), Unlinked, Mid, Side, Mid/Side; the look-ahead window on both level paths.

What the blend costs, measured (simulation): steady reduction on sines minus eq. 4 at the sine's
peak, R 4, 10 dB over, hard knee, Release 1 s:

| Attack | b | 100 Hz | 1 kHz |
|---|---|---|---|
| 0.1, 1 ms | 0 | −0.008 | −0.001 |
| 2 ms | 0.15 | +0.264 | +0.021 |
| 5 ms | 0.35 | +0.341 | +0.013 |
| 10 ms | 0.5 | +0.227 | +0.008 |
| 30 ms | 0.5 | +0.033 | +0.002 |
| 100 ms | 0.5 | 0.000 | 0.000 |

At the defaults (Attack 10 ms, Release 150 ms, Knee 6 dB, R 4) the 1 kHz error is ≤ 0.007 dB from
6 dB under to 24 dB over the threshold, and the 100 Hz error ≤ +0.22 dB. The low-frequency
over-read is the RMS window's ripple at twice the frequency, held by the detector's first stage
(inference from the shape: it vanishes as τ_RMS grows past the period). It is reported, not gated
(*Gates*).

## 2. Gain computer

Giannoulis 2012, eq. 4 (**source**), as the core specifies, with nothing of Clean's own:

- **Threshold** → T, −60 … 0 dB on the detector scale.
- **Ratio** → R, 1 … 20 and ∞ (1/R = 0 exactly). Clean is feed-forward, so every ratio is exact
  (eq. 19 in Giannoulis: the feed-forward slope reaches −1 without difficulty, **source**).
- **Knee** → W, 0 … 24 dB, the quadratic of eq. 4 centred on T.
- **Range** → the cap on the blended static reduction, before the ballistics.
- The curve does not change with level or time. The transfer-curve display draws eq. 4 with the
  Range cap; the live dot sits at the current blended level.

## 3. Ballistics

Smooth decoupled (Giannoulis 2012, eq. 17) on the blended reduction, in the log domain
(**source** for the structure):

```
y1 = max( r[n], α_R·y1 + (1 − α_R)·r[n] )
yL = α_A·yL + (1 − α_A)·y1                 reduction applied = yL
```

**Mappings** (**Basalt's design**), so that the core's measurement definition reads the knobs:

- **Attack:** τ_A = Attack / κ(b), with κ(b) = 1 + 0.118·b (1 at pure peak, 1.059 at b = 0.5).
  κ undoes the RMS half's lag; it is the mean measured/set ratio over the steps below.
- **Release:** τ_R = τ_A·h(Release/τ_A), the core's exact cascade solution (`dynamics/NOTES.md`,
  *Feed-forward ballistics*). The shortest release is the cascade's floor, 1.01·τ_A =
  1.01·Attack/κ(b): 1.01 × Attack at b = 0, 0.954 × Attack at b = 0.5. Shorter settings give it.
  M1 subtracts the RMS half's lag δ first and raises the floor by it (*Release with the RMS half*).
- **Look-ahead** only moves when the gain computer sees the level (*Look-ahead* in the core): the
  attack starts LA early with the same shape, and the release starts when the audio event ends.
- **Knob changes** (the core's *Clicks*, M1): Attack, Release and Auto release glide over 20 ms
  in log time, b, κ, τ_A, τ_RMS, δ and the release poles following per sample (Auto's factor as
  factor^w while w glides between 0 and 1); a Threshold, Ratio, Knee, Range or Link change runs a
  second smooth decoupled detector for the new setting from the first one's state and blends the
  two reductions over 20 ms. Without the glide an Attack jump from 300 to 0.02 ms in mid-attack
  completed the attack within a sample, and b's own step (0.5 → 0) moved the target by up to
  b·3.01·c dB on the carrier at once.

Measured (simulation): attack on the constant-magnitude carrier, measured/set **before** κ, for
steps to D = 6, 10, 20, 10, 30 dB at R = 2, 4, 4, 20, 20 (hard knee):

| Attack | b | measured/set | mean (= κ) | spread |
|---|---|---|---|---|
| 0.02 ms | 0 | 1.018 (all) | 1.018 | 0 % (under one sample) |
| 0.1 ms | 0 | 1.003 | 1.003 | 0 % |
| 1 ms | 0 | 1.000 | 1.000 | 0 % |
| 2 ms | 0.15 | 1.009 … 1.027 | 1.019 | ±0.9 % |
| 5 ms | 0.35 | 1.022 … 1.061 | 1.042 | ±1.8 % |
| 10 … 300 ms | 0.5 | 1.031 … 1.084 | 1.059 | ±2.5 % |

After κ the attack stays within ±2.5 % of the knob over these steps (inference: the deeper the
step, the sooner the RMS half's reduction passes 63 %, so deep steps read short and shallow ones
long). Measured (M1): 0.977 … 1.039 of the knob at Attack 10 … 300 ms over the same steps, and
within 4.4 % over the core's grid (worst at ∞:1, Attack 300 ms).

### Release with the RMS half (M1)

M0 inferred that the RMS half "decays faster than any release Clean allows, so it does not move
the measured release". **Measured (M1), that is wrong.** After the step to silence the peak half's
reduction drops at once, but the RMS half's falls only linearly in dB, at 4.343/τ_RMS dB per ms,
and the hold relaxes towards it instead of towards 0. On the core's grid with the release
calibrated by h(ρ) alone, the release read **1.04 … 1.07 × the knob at Release = 5 × Attack, 1.14 …
1.26 × at 1.5 ×, and 1.23 … 1.45 × at the floor** (Ratio ∞ … 2, b = 0.5): the gate's ±10 % failed
for Release under about 3 × Attack.

The lag is the area under the RMS half's fall over the depth (inference, first order in the
cascade): `δ = b·c·ΔR²·τ_RMS / (2·4.343·D)`, ΔR = D/c + 3.01·(1 − b) the RMS reading over
Threshold on the carrier, c = 1 − 1/R. At the reference step (D = 10 dB) it predicted 0.190,
0.238 and 0.333 × Attack at ∞, 4 and 2:1 against 0.195, 0.245 and 0.350 measured at
Release = 5 × Attack (0.21, 0.27, 0.40 at 1.5 ×).

**Basalt's design (M1):** Clean subtracts δ from the release before the h(ρ) solve, as κ undoes
the attack's lag, `τ_R = τ_A·h((Release − δ)/τ_A)`, with Auto's release in place of Release when
Auto is on; Ratios under 2 take Ratio 2's δ (the reference step needs 30 dB over Threshold at
1.5:1, far from real use). **The shortest release becomes 1.01·τ_A + δ** (`minimumReleaseMs`):
1.14 … 1.29 × Attack at b = 0.5 (∞ … 2:1) instead of 0.954 ×. 0 at pure peak (Attack ≤ 1 ms).

- Measured (M1), the core's grid: release within **7.4 %** of the knob or the new floor (worst at
  Ratio 2 on the floor; ≤ 5.8 % above it), attack within 4.4 %.
- **It is exact at the reference depth only**, because the RMS half's fall grows with the level
  over Threshold. Measured (M1), release/knob at 3, 6, 10 and 20 dB of reduction: Ratio 2,
  Release 5 × Attack: 0.961, 0.979, 1.004, 1.073; Release 15 × Attack: 0.987, 0.992, 1.000,
  1.021; ∞:1, Release 5 × Attack: 0.981, 0.989, 1.001, 1.032. That depth dependence is the RMS
  half's, reported, not gated.
- Rejected: the blend share falling as Release nears Attack (exact at every depth near the floor,
  but Release would change the detector, a hidden coupling the blend was designed to avoid); a
  table of the exact calibration over (Release/Attack, Ratio, b) (exact at the reference only,
  like δ, at the cost of an offline generator); leaving the release long (the gate fails); a
  shorter RMS window (it reads a sine's instantaneous power, up to +3 dB at its crests, which is
  why the window follows Attack).
- M3's listening decides whether the blend stays (Settled 34's fallback is pure peak, which has
  no δ).

**Distortion,** measured (simulation): 1 kHz sine at 6 dB static reduction, R 4, hard knee,
harmonics 2–10 against the fundamental:

| Attack \ Release | 5 ms | 50 ms | 150 ms | 1 s |
|---|---|---|---|---|
| 1 ms | −60.8 dBc | −82.1 | −91.7 | −108.2 |
| 10 ms | −65.1 | −105.6 | −116.6 | −133.6 |
| 30 ms | −78.7 | −108.8 | −124.7 | −143.0 |

With Release below Attack the first stage no longer holds the peaks (τ_R → 0) and the gain follows
the waveform: −65 dBc, and the mean reduction falls to 5.0 dB of the static 6. That is the physics
of a release shorter than a period, not a defect, and `PLAN.md`'s THD gate excludes it (*Changes
to PLAN.md*).

## Auto release

`autoRelease` On (the default) replaces the fixed release with the crest-factor-adaptive release
of Giannoulis et al. 2013, with Release as its base (`PLAN.md`, *Controls*).

- **Crest factor** (2013, eq. 7, **source**): on the key after the key EQ,
  `p = max(k², α_C·p + (1 − α_C)·k²)`, `q = α_C·q + (1 − α_C)·k²`, C² = p/q, both with
  τ_C = 200 ms (the paper's value, "informal testing", §2.1.1). Equal constants make the two
  detectors release together, so p ≥ q and C² ≥ 1.
- **Guard** (**Basalt's design**): C² is 0/0 at reset and in digital silence, and the core's
  denormal flush would zero q before p (p ≥ q), sending C² to ∞ and the release to Release/8 in
  the middle of a long release. So C² is held at its last value while q < 10⁻²⁰ (−200 dB), p and
  q are flushed together (both to 0 once q < 10⁻³⁰), and C² starts at 2 (the sine anchor,
  Release_auto = Release) after a reset. A key that is not finite never reaches the detectors
  (the core's *Key path*: before that rule one +∞ sample gave C² = ∞/∞ = NaN and crashed the host
  in the h table, the M1 review).
- **Warm-up** (M1, review): Clean warming up unheard before a switch (the core's *Clicks*, the
  incoming path's state) starts its crest detectors from rest and divides the mean square's
  missing weight out when it is heard, so C² reads the warm-up's own peak against its own mean;
  `settleMs` gives them at least 50 ms with Auto release on. A crest detector left to fill from
  rest instead would read C² near 1/(1 − α^n) (about 16 × a sine's after 12.5 ms) and run the
  release near Release/8 for the first few hundred milliseconds after a switch. Linked mode feeds both detectors
  max(k_L², k_R²), one C² for the pair (the core's bit-identity rule); the other stereo modes have
  one per detector channel.
- **The law.** The paper divides each maximum time constant by C² and multiplies by 2 so that a
  sine (C² = 2) gets the maximum (eq. 8, **source**): τ_R = 2·τ_Rmax/C² − τ_A. Clean keeps the
  direction and the sine anchor, at half the strength in log terms (**Basalt's design**):

  ```
  Release_auto = Release · clamp( sqrt(2 / C²), 1/8, sqrt(2) )
  τ_R[n]       = τ_A · h(Release_auto / τ_A)         the core's exact calibration replaces "− τ_A"
  ```

- **Why the square root.** In the paper Release is not a user control (τ_Rmax is fixed at 1 s);
  in Basalt the user sets it, and Auto must stay recognisably "that release". Measured
  (simulation, the eq. 7 detector at 48 kHz, median over 1 s after 1 s of settling):

  | Signal | C² median (p5 … p95) | 2/C² (paper) | sqrt(2/C²) (Clean) |
  |---|---|---|---|
  | sine 50 / 100 / 1000 Hz | 1.98 / 1.99 / 2.00 | 1.01 / 1.01 / 1.00 | 1.01 / 1.00 / 1.00 |
  | constant magnitude (square) | 1.00 | 2.00 | 1.41 |
  | white noise | 12.6 (9.5 … 20.2) | 0.16 | 0.40 |
  | pink noise | 11.3 (8.6 … 15.2) | 0.18 | 0.42 |
  | decaying 60 Hz bursts, 2 per second | 7.7 (7.6 … 15.9) | 0.26 | 0.51 |
  | impulse train, 10 Hz | ≈ 3800 | 0.0005 | 1/8 (clamp) |

  With the paper's law, broadband material (pink noise as a stand-in for a dense mix) would
  release at 0.18 × the knob: a 150 ms setting would act like 27 ms. The square root gives
  0.42 ×, still clearly faster after transients and up to 1.41 × slower under sustained,
  square-like level, which is the program dependence the paper argues for (short after
  transients, long under steady state, §2.1).
- **The clamp** keeps an isolated click (the eq. 7 crest factor is largest on the first sample of
  a transient, the paper's eq. 11) from collapsing the release: never below Release/8, never above
  √2 × Release (7.1 s at the 5 s maximum, inside `PLAN.md`'s "Auto may go longer").
- **Update rate:** C² and τ_R are recomputed every 16 samples (0.33 ms at 48 kHz; the crest
  detectors' 200 ms constant makes the change between updates negligible, inference); the
  coefficient is held between updates. h is the core's table.
- **During a release into silence** both crest detectors decay by the same α_C, so C², and with it
  the release, stay at the value they had when the signal stopped (inference from eq. 7, exact),
  until q falls under the guard's 10⁻²⁰ (about 8 s after a −20 dB signal stops), after which the
  guard holds the same value. The Auto release gate uses this.
- The paper's listening study (2013, §3.1) found the crest approach's release slow on soft vocals
  and fast choices preferred on slap bass (median 26 ms by professionals); Clean's Release knob
  remains the user's answer to that, Auto only bends it.

## 4. Make-up and gain law

- The gain cell is an ideal multiplier on the delayed audio, `y = x·10^(−yL/20)`, in float, with
  the core's exact 1.0 at zero reduction.
- **Auto Gain** inverts the static curve at the core's reference: `M = min(x_ref − G(x_ref),
  Range)` with x_ref = −18 dB on the detector scale. Because both halves of the blend read a sine
  at its peak, the blend does not enter: M is eq. 4's reduction at −18 dB. Examples (inference,
  eq. 4): Threshold −18, Ratio 4, Knee 6 → M = 0.56 dB; Threshold −30, Ratio 4, Knee 6 →
  M = 9.0 dB; Threshold −10 → M = 0.
- Make-up applies to the wet path only, after the gain cell and before Mix (`PLAN.md`).

## Gates

Clean's rows of `PLAN.md`'s gates, on top of the core's table (`dynamics/NOTES.md`, *Gate
table*):

| Gate | Clean |
|---|---|
| Identity | as the core; also with Auto release on (the crest detectors never touch the audio) |
| Static curve | the core's grid at 1 kHz and on the constant-magnitude carrier, R up to ∞, Attack 10 ms (b = 0.5) and 0.1 ms (b = 0), Release 1 s: ±0.25 dB of eq. 4 on the sine, and on the carrier of `(1 − b)·r(L) + b·r(L + 3.01)` (*Detector*; at b = 0 that is r(L)). Levels on the detector scale. 100 Hz reported (expected ≤ +0.35 dB, the table in *Detector*). Pink noise: the steady mean reduction within ±0.25 dB of `BasaltMeasure`'s own double-precision transcription of *Detector* to *Ballistics* run on the same noise, at Attack 10 and 100 ms |
| Ballistics | the core's definition and grid, against Attack and Release directly (κ, δ and h are internal): ±10 % (expected: attack ±4.4 %, release ±7.4 %, *Measured in M1*); Release under the floor measured against 1.01·Attack/κ(b) + δ (`CleanModel::minimumReleaseMs`); look-ahead 3 and 10 ms: the shift rule of the core |
| Auto release | release into silence after a settled step, Release 50, 150, 500 ms: a 1 kHz sine burst releases in 1.00 × Release, the constant-magnitude carrier in 1.41 ×, a pink-noise burst in `sqrt(2/C²)` × Release with C² read from the reference crest detector at the step: each ±10 %; an impulse train never below Release/8; Release 5 s on the carrier: 7.07 s ±10 %, with the tail's rate unchanged 20 s after the step (the guard); the first release after a reset uses Release (C² = 2) |
| Distortion | THD+N, 1 kHz, 6 dB reduction: reported for every Attack; gated ≤ −80 dBc at Attack ≥ 10 ms **and Release ≥ 50 ms** (simulation: −105.6 dBc at 10/50, −108.8 at 30/50; measured in M1: −105.0 and −105.5, *Measured in M1*) |
| Stereo, Range, Clicks, Real-time | the core's rows |
| Learn | feed-forward: the solve replays *Detector* to *Ballistics* exactly on the captured envelope; `PLAN.md`'s ±1 dB in 90 % of captures |
| Auto Gain | the core's static M; `PLAN.md`'s ±1.5 LU corpus median |

## Measured in M1

**Measured (M1)**, the engine at 48 kHz, `BasaltTests_dynamics` (`tests/dynamics/CleanTests.cpp`):

| What | Result |
|---|---|
| Static curve, the core's grid (T −60/−30/−10, R 1.5 … ∞, W 0/6/24, Range 40/10, T − 20 … T + 30), Attack 10 and 0.1 ms, Release 1 s | 1 kHz sine: worst **0.068 dB**, at the hard knee exactly at Threshold, ∞:1, Attack 10 ms (below); carrier: within 0.0000 dB of `(1 − b)·r(L) + b·r(L + 3.01)` |
| 100 Hz sine, R 4, 10 dB over, hard knee, Attack 2/5/10/30/100 ms | +0.264, +0.341, +0.227, +0.032, −0.000 dB: the M0 table to the third decimal |
| Attack after κ, the M0 steps (6 … 30 dB, R 2 … 20) | 0.992 … 1.011 (2 ms), 0.983 … 1.027 (5 ms), 0.977 … 1.039 (10, 30, 300 ms) of the knob |
| Ballistics grid (Attack 0.02 … 300 ms × Release 5 ms … 5 s × R 2/4/20/∞) | attack within 4.4 %, release within 7.4 % of the knob or the floor (*Release with the RMS half*) |
| Auto release, Attack 1 and 10 ms, Release 50 … 500 ms | sine 0.997 … 1.000 ×, carrier 1.414 … 1.415 × Release |
| Auto release, pink noise (Release 150 and 500 ms) | C² 10.5 at the step: 0.434 and 0.436 × Release against √(2/C²) = 0.437 |
| Auto release, 10 Hz impulses, Release 500 ms | C² 3778: 0.1250 × Release, the clamp |
| Auto release, carrier, Release 5 s | 7.071 s; the tail's rate 0.14165 /s at 5 s and at 20 s (the guard holds C² through silence) |
| Look-ahead 3 and 10 ms, carrier, Attack 0.1 and 10 ms | attack side = the LA 0 trace shifted, release side identical: 0 dB difference |
| Link 100 %, 10 s of two unrelated bursty noises, look-ahead 2 ms, three settings | both channels' reductions identical to the bit; Link 0 % identical to Unlinked to the bit, Auto release included |
| Idle: Ratio 1, Range 0, a sine under the knee | exactly 0 every sample; after a 20 dB burst exactly 0 within 15.3 … 16.5 × Release |
| A seed at 10 dB, Attack 300 ms, the level states run by `follow()` | stays at 10.000000 dB: no swell, no re-attack |
| A switch between two Clean engines mid-attack at Attack 300 ms, Auto on, after the 375 ms warm-up (`[chain][switch]`) | within 8.3·10⁻⁶ dB of an engine that ran throughout |
| Knob jumps at Attack 0.02 ms, the key 14 dB over Threshold, every 0.31 s while the reduction still releases (*a knob jump neither steps nor bends the reduction*) | largest step per sample and second difference: Threshold −60 ↔ 0 dB 0.052 / 1.9·10⁻⁴ dB, Ratio 1 ↔ ∞ 0.023 / 8.4·10⁻⁵, Knee 0 ↔ 24 dB 0.002 / 7.6·10⁻⁶, Range 0 ↔ 40 dB 0.017 / 6.3·10⁻⁵ (the raised cosine's own curvature is about 2·10⁻⁴) |
| CPU, stereo, 48 kHz, look-ahead 5 ms, pink noise, with the gain conversion and the matrix (`[.perf]`) | 0.29 % of a core at the defaults (Attack 10 ms, Auto on), 0.23 % at Attack 0.5 ms with Auto off; after the review's changes (the gain's exp in float among them) 0.274 and 0.209 % |

- **The RMS half over-reads a sine by its ripple:** a one-pole on k² ripples at 2f, and its
  reading tops the sine's peak by up to 10·log10(1 + 1/√(1 + (4πf·τ_RMS)²)): 0.14 dB at 1 kHz and
  1.15 dB at 100 Hz with Attack 10 ms (τ_RMS 2.5 ms). The hold keeps that top, hence the 0.068 dB
  above (b·c of it) where the peak half reads 0. **The identity gate's "input below T − W/2"
  therefore needs that margin on sines with b > 0** (the test uses 0.5 dB at 1 kHz); the carrier
  has none.
- **Through the processor** (`BasaltMeasure all`, Normal and Live, 44.1, 48 and 96 kHz, the core's
  *Through the processor* for the full list): every gated row passes. Static worst 0.068 dB;
  attack within 4.4 % (worst ∞:1, Attack 300 ms), release within 7.5 % of the knob or the floor
  (worst Ratio 2, Attack 300 ms, Release 5 ms: 415.1 ms against the floor's 386.0 ms); Auto
  release: sine 0.997 … 1.000 ×, carrier 1.409 … 1.414 ×, pink noise within 4.3 % of √(2/C²)
  (C² 13.9 at the step), impulse train 0.125 ×, Release 5 s 7.071 s with the tail's rate equal at
  5 and 20 s. Against `PLAN.md`'s older floor 1.01·Attack/κ the 24 rows under the floor read
  1.23 … 1.45 × (reported). The raw samples of every step (`BasaltMeasure ballistics --dump`)
  give the same crossings when measured independently (M1 run: 7 steps, to 10⁻⁴ ms).
- **Settling** (`settleMs`, M1 chain): with b > 0 the RMS half needs 5·τ_RMS (1.25 × Attack) from
  rest, and with Auto release on the crest detectors at least 50 ms; a model or a lane switched
  to waits that long, plus the look-ahead window, before it is heard (`../../dynamics/NOTES.md`,
  *The chain (M1)*); at pure peak with Auto off nothing but the window.
- **Distortion through the processor** (`BasaltMeasure distortion`, M1, after the review: R 4,
  hard knee, a 1 kHz sine at 6 dB of static reduction, THD+N over 50 periods after 25 attack
  constants; THD of harmonics 2–10 within 1 dB of it in every cell):

  | Attack \ Release | 5 ms | 50 ms | 150 ms | 1 s |
  |---|---|---|---|---|
  | 1 ms | −60.8 dBc | −82.0 | −91.7 | −108.2 |
  | 10 ms | −65.1 | −105.0 | −116.6 | −133.0 |
  | 30 ms | −78.7 | −105.5 | −124.1 | −140.7 |

  THD repeats the simulation in *Distortion* above within 0.6 dB, except at Attack 30 ms: −105.5
  against −108.8 dBc at Release 50 ms and −141.5 against −143.0 at 1 s. The simulation predates δ,
  which shortens the internal release constant ((Release − δ) before h, *Release with the RMS
  half*), and a shorter hold ripples more. Gated (Attack ≥ 10 ms,
  Release ≥ 50 ms): 20 rows, worst −95.9 dBc at Attack 100 ms (its Release below Clean's floor
  runs at the floor, 119 ms). At 44.1 kHz the sampled crest of a 1 kHz sine moves from period to
  period, and THD+N reads up to 40 dB above THD at fast settings: gated worst −86.6 dBc (Attack
  10 ms, Release 500 ms), still inside the gate.

## What Clean leaves out

- No auto attack: the 2013 paper also automates attack (eq. 8) and knee (eqs. 14–17) and offers
  spectral-flux control (§2.1.3). Clean keeps Attack and Knee as the user's, so Learn and the
  transfer curve stay predictable.
- No hold control, no detector-mode switch, no adaptive make-up (`PLAN.md`, Settled 10).
- No colour: Character is separate (`PLAN.md`, *Character*).

## Changes to PLAN.md

M0 proposed four (the Clean row of *The reference behaviour*, the Distortion gate from Release
50 ms, the Clean Auto rows of the Ballistics gate, Auto's √2 × Release under *Controls*);
`PLAN.md` carries them. M1 adds one: Clean's release floor in the Ballistics gate is
1.01·Attack/κ + δ (*Release with the RMS half*; `PLAN.md`, Settled 53), and the rows under it
also report the floor without δ.

## Not verified

- The blend's audible merit over pure peak detection is a design judgement, not a measurement;
  M3's listening pass decides whether b's top (0.5) or the 1–10 ms ramp moves.
- κ(b), δ and the 100 Hz over-read were measured on synthetic steps and tones only (*Measured in
  M1*); M3 measures the release on the corpus.
- The 2013 paper's erratum (eq. 3).
