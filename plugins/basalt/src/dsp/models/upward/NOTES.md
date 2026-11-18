# Upward: lift below the threshold

Upward compression: material below Threshold is lifted toward it, loud material passes untouched.
Ratio sets how much (the lift ratio below Threshold), Range caps the lift, and nothing below Floor
is lifted, so silence and noise stay down (`PLAN.md`, *Models*, *Controls*, Settled 15). There is
no reference unit and no peer-reviewed source (`PLAN.md`): Upward is **Basalt's design** on the
shared core (`../../dynamics/NOTES.md`). Two open compressors with an upward mode were looked at
for behaviour only (*Prior art*).

Status: M0 specification, implemented in M2 (2026-11-17) as `UpwardModel` in this folder and
checked at model level by `BasaltTests_upward` (`tests/models/upward/`) against `Reference.h`
there, this file's equations as pure functions written apart from the engine (the static curve,
Auto Gain's curve, the expected times, the program-dependent figures; BasaltMeasure reads them
too). *Implementation (M2)* records what the code decided or corrected, *Measured in M2* what it
reads; through the processor once the chain runs it. Marks as in the core: **source**,
**measured (simulation)** (Python/NumPy/SciPy, double precision, 48 kHz, set-ups given with the
numbers, scripts in the M0 session's scratchpad, not kept), **measured (M2)** (the engine at
48 kHz, `BasaltTests_upward -s`), **Basalt's design**, **inference**, **not verified**.

## Sources

| Source | What it gives | Read |
|---|---|---|
| `../../dynamics/NOTES.md` | detector scale, RMS level, look-ahead window, Link, the snap to exactly 0, the measurement conventions, Auto Gain | Basalt |
| Giannoulis, Massberg, Reiss, JAES 60(6), 2012 | eq. 4, the quadratic knee mirrored here | via the core (read there) |
| THAT Corporation, *2252 IC RMS-Level Detector*, doc. 600032 rev. 03 | a power one-pole falls at a constant rate in dB, 10·log10(e)/τ dB/s | read (`../vca/NOTES.md`) |
| LSP Compressor manual, lsp-plug.in (manuals, *compressor_stereo*); LSP Plugins, LGPL-3.0 | upward mode: below the Boost threshold the gain is held at a constant amplification, so very quiet signals are not amplified without limit; Boosting mode sets the amount of boost and derives the threshold | read (the manual page); **no code read** |
| ZL Compressor manual, zl-audio.github.io (plugins, zlcompressor, manual); ZL Compressor, AGPL-3.0 (README) | Inflate is its upward mode; a Floor control sets the level where the signal returns to the uncompressed state; selecting Inflate sets its output clipper to 100 % | read (the manual page and README); **no code read** |

## Prior art, and what Upward does differently

- **LSP** holds the lift constant below its boost threshold (source): the lift stops growing, but
  noise and silence keep the full lift. **ZL** returns to unity below its Floor (source); the
  manual does not give the shape of that return (**not verified**).
- Basalt follows ZL's outcome (no lift below Floor), with the return shaped so the static curve
  stays monotonic and smooth (*2. Gain computer*), and with a constant-rate lift-in and an Auto that
  slows it after loud passages (*3. Ballistics*). Neither project's code was read; nothing is
  derived from it.

## 1. Detector

- **RMS, feed-forward** (`PLAN.md`): the core's RMS level `L_R = 10·log10(2m)`,
  `m[n] = β·m[n−1] + (1 − β)·k²[n]`, with **τ_RMS = 10 ms fixed** (**Basalt's design**), through
  the core's look-ahead window.
- **Why 10 ms:** the detector's rise is program-dependent (a power one-pole), so a large jump is
  seen within a fraction of τ: a step from T − 20 to T + 10 dB removes 63 % of the lift's target in
  0.18 ms (inference: 0.0175·τ). Its fall is 10·log10(e)/τ = 434 dB/s, which is faster than any
  useful lift-in, so the release limiter (below) sets the lift-in rate, not the detector. 20 ms
  halves the low-frequency ripple but doubles the attack for small steps (measured, simulation:
  a 6 dB step from T − 6 to T, Attack 0.5 ms: 6.8 ms at τ 10 ms, 13.1 ms at 20 ms).
- **Linked:** the louder channel wins, as for every model: `ℓ_c = (1 − λ)·ℓ(x_c) + λ·ℓ(max(x_L, x_R))`
  on the static lifts, Range and Floor included (M2, *Implementation (M2)*). Above the Floor fade
  this is the core's Link on r = −ℓ exactly (ℓ falls with level there, so the louder channel's
  lift is the smaller one); inside it the core's max(r_L, r_R) would take the quieter channel's
  smaller lift, and a source on one channel would never be lifted.

## 2. Gain computer

The lift ℓ (dB, ≥ 0) as a function of the detector level x, Threshold T, Ratio R, Knee W, Range,
Floor F (**Basalt's design**):

```
c     = 1 − 1/R                                  (1/R = 0 at ∞)
u     = T − x                                    how far below Threshold
q(u)  = 0                       if u < −W/2
        (u + W/2)² / (2W)       if |u| ≤ W/2       (the core's quadratic knee, mirrored)
        u                       if u > W/2
ℓ₀    = c · q(u)                                 lift: (1 − 1/R) dB per dB below Threshold
ℓ₁    = min(ℓ₀, Range)                           Range caps the lift
s     = clamp((x − F) / 12 dB, 0, 1)
w     = s² · (3 − 2s)                            Floor fade, 0 at and below F, 1 from F + 12 dB
ℓ(x)  = w · ℓ₁
gain  = 10^(+ℓ/20)                               (reduction r = −ℓ)
```

- **Ratio** is the lift ratio below Threshold: the output changes by 1/R dB per input dB there. At
  ∞ everything between Threshold and the Range cap comes out at T. Ratio 1 gives ℓ ≡ 0 exactly.
- **Knee** acts at Threshold only, the core's eq. 4 mirrored (width W centred on T).
- **Range** caps the lift with a hard minimum (a kink in the curve, as Range does downward).
- **Floor fade:** below F the lift is exactly 0 ("no lift below Floor", `PLAN.md`); above F + 12 dB
  the fade is 1; in between a C¹ smoothstep, so the gain never steps when the level crosses F
  (Floor's job) and the curve has no corner there. Width 12 dB is **Basalt's design**, to be voiced
  in M2 (6 … 18 dB).
- **The output never folds back.** Output level `x + ℓ(x)`; its slope is
  `1 + w'·ℓ₁ + w·ℓ₁'`, with w' ≥ 0, ℓ₁ ≥ 0 and −c ≤ ℓ₁' ≤ 0, so the slope is at least 1 − c = 1/R
  (0 at ∞): a quieter input never comes out louder than a louder one (inference; checked
  numerically on a 0.005 dB grid from −100 to 0 dB: smallest slope 0.25 at R 4, 0.05 at R 20,
  0 at ∞ with Range 10, as the bound says). Inside the fade the slope rises above 1 (an expander):
  largest 3.54 at R 20, Range 40, F −70, T −40 (measured, numerically; M2: 2.44 at T −50, 3.54 at
  T −40, 4.68 at T −30, and up to 1 + 5·Range/40 where the fade lies under the Range cap). That
  expansion is what keeps noise down.
- **Overlap:** if F + 12 dB > T the fade and the lift region overlap; the formula needs no special
  case, the lift just stays small. If T + W/2 ≤ F there is no lift anywhere (with a knee the
  lift reaches up to T + W/2, so T ≤ F alone is not enough).
- **Parameter ramps:** Floor glides over 20 ms on the time knobs' C¹ cubic, in dB (`SmoothGlide`,
  Settled 67); Threshold, Ratio, Knee, Range and Link fade with the curve (core, *Clicks*).

## 3. Ballistics

Attack is how fast the lift goes away, Release how fast it comes in (`PLAN.md`). Per channel
(linked as above), y the applied lift, ℓ the static lift:

```
if ℓ < y:  y = α_A·y + (1 − α_A)·ℓ                     lift goes away: one-pole, τ = Attack
else:      y = min(ℓ, y + 10 dB / (Release·fs·k))     lift comes in: at most 10 dB per Release·k
k = 1                                                  Auto off
k = 1 + 4·h,  h[n] = α_h·h[n−1] + (1 − α_h)·[L > T],   τ_h = 2 s     Auto on
```

- **Attack** is a one-pole on the lift (63.2 % of the change in τ_A), the core's convention. It is
  unaffected by depth.
- **Release is a constant rate**: Release reads as the time for 10 dB of lift to come in, the same
  law as VCA RMS (`../vca/NOTES.md`). A constant rate lets a long Release bring up a tail slowly
  without the fast-then-slow shape of an exponential, which lifts the first few dB of a tail
  quickly and is heard as a swell.
- **Why not the decoupled detector:** the core's smooth decoupled form in the lift domain takes the
  minimum of the lift at once, so on a rippling RMS it follows the level's ripple peaks and reads
  low at low frequencies. Measured (simulation, τ_RMS 10 ms, decoupled form, lift 9 dB on a sine):
  −0.47 dB at 50 Hz, −0.25 dB at 100 Hz, −0.07 dB at 1 kHz; and its release depends on the
  detector's fall (a 30 dB down-step read 200 ms for Release 150 ms). The rate limiter above reads
  +0.03 dB at 1 kHz (below).
- **Auto** (**Basalt's design**): the lift comes in up to 5 × slower after the level has spent time
  above Threshold (h is the fraction of the last ≈ 2 s spent there). A brief loud event inside a
  quiet passage leaves the lift-in nearly as set, so there is no hole after a single hit; the end
  of a loud section lifts the following quiet part slowly, so the room and noise do not swell up
  the moment the band stops. L is the linked level, so h is shared by both channels (core,
  *Stereo*). Release stays the base (`PLAN.md`).
- **Minimum release:** the lift cannot come in faster than the detector's level falls,
  (1 − 1/R)·434 dB/s: 10 dB in 23 ms/(1 − 1/R), 31 ms at 4:1. Shorter Release settings measure at
  that floor.
- **Minimum attack:** the detector's rise adds to Attack, about 0.2 ms on the gate's 30 dB jump,
  and not as a sum: the specified time (*Measured in M2*) is 0.188 ms at Attack 0.02 ms, 0.298 at
  0.1, 0.547 at 0.3, 0.761 at 0.5, 1.239 at 1 and 3.213 ms at 3 ms. "Attack + 0.25 ms" holds within
  2 % from 0.3 ms and reads 30 % long at 0.02 ms, so the gate below 3 ms compares with the
  specified time. Small jumps take longer (6.8 ms for 6 dB at Attack 0.5 ms, above; M2 6.821 ms).
  Look-ahead removes the lift before the transient arrives, which is the reliable way to keep
  transients from being lifted.
- **Onsets from silence:** the RMS starts from nothing and rises through Floor … T + W/2 before it
  reads a loud onset, and the lift comes in at the Release rate meanwhile, then goes away on
  Attack: up to 0.93 dB at a level exactly T + W/2 (Release 150 ms, hard knee), 0.006 dB 20 dB
  over, 2.5 dB at Release 5 ms with a 6 dB knee 3 dB over (measured (M2), the transcription
  alike); exactly 0 again within 180 ms. It is the specification's own behaviour; look-ahead puts
  it on the silence before the onset.
- **Exact zero:** the attack branch decays exponentially; the core's snap (below 10⁻⁶ dB → 0) makes
  the gain exactly 1 once the level has stayed above T + W/2 or below F.

Measured (simulation; the core's constant-magnitude carrier, Ratio 4, Knee 0):

| Attack, Release | Attack: T − 20 → T + 10, time to 36.8 % of the lift (15 dB) | Release: T + 10 → T − 60, Range 20, Floor −90, slope from 2 to 12 dB of lift |
|---|---|---|
| 0.5 ms, 50 ms … 2 s | 0.75 ms | 50.0, 150.0, 500.0, 2000.0 ms per 10 dB |
| 5 ms, 50 ms … 2 s | 5.19 ms | as above |
| 30 ms, 50 ms … 2 s | 30.19 ms | as above |

Auto, Release 150 ms, Attack 5 ms, the release test after the level stayed above T for 0.2 s or
6 s: **209 ms** and **606 ms** per 10 dB.

Static accuracy on sines, lift 9 dB (T − 12 dB, Ratio 4), mean lift in the last second (measured,
simulation):

| | Attack 1 ms, Release 150 ms | Attack 30 ms, Release 150 ms | Attack 30 ms, Release 1 s |
|---|---|---|---|
| 50 Hz | −0.20 dB | +0.42 dB | −0.01 dB |
| 100 Hz | −0.07 dB | +0.25 dB | +0.08 dB |
| 1 kHz | +0.02 dB | +0.03 dB | +0.03 dB |

At low frequencies the 10 ms RMS ripples at 2f and the asymmetric ballistics ride it: a fast
attack follows the ripple's lift dips, a slow attack with a fast lift-in follows its peaks. This is
in-cycle behaviour of an RMS at 10 ms, reported, not gated (*Gates*). Pink noise band-limited to
20 Hz … 20 kHz, 9 dB lift: +0.12 dB (5/150 ms), +0.17 dB (30/500 ms), −0.26 dB (30 ms/2 s): the
asymmetric ballistics make a noise reading depend on the release, so pink noise is reported, not
gated (the core's rule for non-analytic readings).

## 4. Make-up and gain law

- **Gain law:** gain 10^(+ℓ/20), the core's ideal cell. Upward boosts, so the output can exceed
  0 dBFS only where the input was already within Range of it; Basalt does not add a clipper
  (ZL forces one with Inflate, source); Output and Gain are the user's.
- **Auto Gain** (the core's rule, "bring the reference back to itself"): M = −ℓ(L_ref),
  L_ref = −18 dB on the detector scale. With Threshold above −18 dB this is a cut equal to the lift
  at the reference, so material at −18 dB keeps its level and the quiet parts rise toward it; with
  Threshold at or below −18 − W/2 it is 0.
- **Learn** (`PLAN.md`): the Threshold that gives a mean lift equal to Target over the quiet half
  of the capture (frames below the median short-term level) that lies above Floor, on the
  mean-square capture (`../vca/NOTES.md`, *Proposed changes*, 5). Frames below Floor are excluded
  because they are never lifted.

## Gates

The core's rows apply. Upward's own:

| Gate | Upward check |
|---|---|
| Identity | bit-identical when the level is ≥ T + W/2, or ≤ F (digital silence included), or Ratio 1, or Range 0; every stereo mode and latency. Over the knee once the detector has read the level: a render that starts from silence passes the RMS through the lift region first (*Onsets from silence*), so those rows compare from 200 ms on (M2) |
| Static curve | 1 kHz sine and the carrier (its level on the mean-square reading, 3.01 dB above its amplitude; core *Conventions*), levels F − 10 … T + 10 dB in 1 dB steps; T −50, −30, −10; R 1.5, 2, 4, 10, ∞; W 0, 6, 24; Range 40, 10; F −90, −70, −50; Attack 10 ms, Release 1 s, Auto off: mean lift within ±0.25 dB of ℓ(x). Plus **monotonic output**: on the carrier, output level non-decreasing in input level across the grid (no fold-back), and lift exactly 0 at x ≤ F. Reported: 50 and 100 Hz sines and band-limited pink noise |
| Ballistics, attack | carrier T − 20 → T + 10 dB (settled lift 15 dB at R 4), time to 36.8 % of the starting lift: ±10 % of Attack for Attack ≥ 3 ms; below, the specified time ±10 % (`Reference.h`, `attackSeconds`: the detector's rise; M2 corrected "Attack + 0.25 ms", 30 % long at 0.02 ms) |
| Ballistics, release | carrier T + 10 → T − 60 dB, Range 20, F −90, lift slope between 2 and 12 dB: 10 dB per Release ±10 % for Release ≥ 50 ms at R 4; below, the detector floor 23 ms/(1 − 1/R) ±10 % |
| Ballistics, Auto | Release 150 ms, Attack 5 ms: lift-in 209 ms and 606 ms per 10 dB after 0.2 s and 6 s above T, ±10 % |
| Floor | a level sweeping slowly (1 dB/s) through F … F + 12 dB: the gain's slope stays continuous (no step artefact above −60 dB, the click method) |
| Clicks | Floor jumps (−90 ↔ −30) at 10 dB of lift: no step artefact above −60 dB |
| Learn | the core's, on the quiet half above Floor: mean lift within ±1 dB of Target in 90 % of captures |

## Implementation (M2)

`UpwardModel` on the core's `DynamicsModel` (**Basalt's design**, M2, where this file left a
choice open or proved wrong in the code):

- **Per sample and detector channel:** k² into the 10 ms mean square, the look-ahead window's
  running maximum on it (the core's `RunningMax`), then ℓ. The level is compared as a power 2m
  with T + W/2 and F first, so the log runs only inside the lift region, and a NaN level lifts
  nothing. Then Link, then the ballistics of *3* on the lift y; the chain gets −y (exactly 0,
  never −0, when y is 0).
- **Linked on the levels' maximum** (*1. Detector*): the louder channel's lift, not the smaller
  lift. Rejected: max(r_L, r_R), which inside the Floor fade takes the quieter channel's lift:
  a mono source panned hard to one side, the other channel under Floor, would get min(15, 0) =
  0 dB of lift where the louder channel asks for 15 dB (inference; the engine's 15 dB on both
  channels is measured, `[upward][stereo]`). Link applies to the finished
  lifts, Range and Floor included (the fade needs each channel's own level): at Link 0 % and
  100 % where Range sits makes no difference, in between it blends two capped lifts where the
  core (Settled 54) caps the blend.
- **Auto's memory h** runs whether Auto release is on or off (it depends on the key and T only),
  so switching Auto on finds the history; per channel, and once from the louder channel for a
  linked pair while Link > 0 (core, *Stereo*). It is a memory, not a level state: it starts empty
  at `reset()`, charges during a warm-up and is not normalised for its start from rest (50 ms of
  key cannot stand for 2 s of history). Auto release on or off glides its weight 0 … 1 over
  20 ms: k = 1 + 4·h·a.
- **Seed:** `seed(r)` sets y = −r. A positive r (another model's reduction) is a lift below 0,
  which comes back at the Release rate like any lift-in: the gain rises as Upward's gain always
  rises, instead of jumping inside the 30 ms crossfade. `settleMs` is 5·τ_RMS = 50 ms.
- **Fades and glides:** the curve fade runs a second y with the new curve and blends the lifts
  (Clean's pattern); Attack and Release glide in ln ms, Floor in dB (Settled 67). A lift coming
  in at the Release rate lands on its target with a corner of that rate, 10 dB/(Release·fs) per
  sample (the constant-rate law's own; VCA RMS has the same).
- **Not in the core:** nothing was missing; the model uses `RmsDetector`, `RunningMax`,
  `CurveFade`, `SmoothGlide`, `snapState`, `flushLevel` as they are.
- **The chain** (for the integrator; nothing there needs changing): `gainFor` turns −y into a
  gain above 1, the lane blends reductions in dB of either sign, the meters' Lift already takes
  −r, and Auto Gain's make-up `staticReduction(−18)` is −ℓ(−18), a cut, as *4* asks. A lane switch
  seeding "the larger reduction" seeds the smaller lift, which then comes in at the Release rate.

## Measured in M2

**Measured (M2)**, the engine at 48 kHz, `BasaltTests_upward` (`tests/models/upward/`), against
`Reference.h` (the transcription of *1*–*3*, sample by sample, from rest):

| What | Result |
|---|---|
| `staticReduction` against ℓ(x), T −60 … 0, R 1 … ∞, W 0/6/24, Range 0/10/40, F −90 … −30, x −120 … +10 dB every 0.01 dB | identical (0 dB); exactly 0 at x ≤ F, x ≥ T + W/2, Ratio 1, Range 0 |
| Output slope, 0.005 dB grid, the gate's grid of settings | never below 1/R (no fold-back); the slope continuous at F and F + 12 dB |
| Static gate (T −50/−30/−10, R 1.5 … ∞, W 0/6/24, Range 40/10, F −90/−70/−50, F − 10 … T + 10 dB, Attack 10 ms, Release 1 s) | 1 kHz sine worst **0.029 dB** (T −30, ∞:1, hard knee, F −50, 17 dB under T); carrier worst 6·10⁻⁶ dB; the carrier's output never falls as the input rises; lift exactly 0 at and under Floor |
| Sines, lift 9 dB (T − 12, R 4), reported | Attack 1 / Release 150 ms: 50 Hz −0.199, 100 Hz −0.071, 1 kHz +0.016 dB; 30 / 150 ms: +0.424, +0.245, +0.026; 30 ms / 1 s: −0.005, +0.076, +0.025 (M0's table to 0.01 dB) |
| Pink noise high-passed at 20 Hz, reading T − 12, reported | +0.139 dB (5 / 150 ms), +0.195 (30 / 500 ms), −0.219 (30 ms / 2 s); M0, other noise: +0.12, +0.17, −0.26 |
| Attack (the gate's step), Attack 0.02 … 300 ms × R 1.5/2/4/10/∞ | equal at every ratio; within 7.1 % of the knob from 3 ms (3.213 ms at 3 ms, 10.204 at 10, 100.201 at 100); every row within 0.0001 % of the specified time; 6 dB step at Attack 0.5 ms 6.821 ms (M0 6.8) |
| Lift-in (the gate's step), Release 5 ms … 5 s × R 1.5/2/4/10/∞ × Attack 1/30 ms | within 0.06 % of max(Release, ln 10·τ_RMS/(1 − 1/R)) per 10 dB; the floor 69.1, 46.1, 30.7, 25.6, 23.0 ms at R 1.5, 2, 4, 10, ∞ |
| Auto, Release 150 ms, Attack 5 ms | **208.6** and **606.2 ms** per 10 dB after 0.2 s and 6 s above T (M0 209, 606); after one 20 ms hit 161.4 ms; after 60 s 626.0 ms (h decays during the lift-in, so 5 × is never quite reached); Auto off 150 ms whatever came before |
| Floor sweep 1 dB/s through F … F + 12, R 20 | the lift's second difference ≤ 1.0·10⁻⁶ dB, the core's snap leaving exactly 0 (the curve's own ≈ 7·10⁻¹⁰); a 100 Hz probe through the gains −104.6 dB (Halite's click measure) |
| Floor −90 ↔ −30 dB at 10 dB of lift | probe −67.8 … −66.1 dB at Attack 0.02 ms (the Floor glide's own curvature through the fade, 3.2·10⁻³ dB), −81.8 … −99.5 dB at Attack 10 ms, Release 50 ms … 1 s |
| Knob jumps at Attack 0.02 ms (key under Threshold, every 0.31 s) | Threshold, Ratio, Knee, Range: second difference ≤ 1.39·10⁻³ dB, the Release rate's landing (8.1·10⁻⁵ for Threshold, where nothing lands); probe −100.4, −75.4, −88.8, −69.8 dB; Attack, Release, Auto: nothing moves (−110 dB) |
| Onset from silence over the knee (*Onsets from silence*) | 0.93 dB at T + W/2, 0.69 at + 1 dB, 0.18 at + 6, 0.006 at + 20 (Release 150 ms); 2.47 dB at Release 5 ms, knee 6, + 3 dB; exactly 0 within 86 … 179 ms; the transcription alike |
| Idle | Ratio 1, Range 0, under Floor, silence: exactly 0 from the first sample; over the knee from 200 ms; after a lift, exactly 0 16.5 × Attack after the step over T (2.3 ms at 0.02 ms) |
| Link 100 %, 10 s of two bursty noises, look-ahead 2 ms, three settings | both channels identical to the bit; Link 0 % identical to Unlinked; a source on one channel lifted 15 dB on both (7.5 dB on the quiet one at Link 50 %) |
| Seed | its own lift held at Attack 300 ms (no swell); a 6 dB reduction rises to the 15 dB lift in a straight line at 10 dB per Release; `follow()` for 50 ms then a seed at 0 lifts in at the knob |
| Block sizes 1, 7, 64, 4096 and a mixed cycle, look-ahead, Linked and Unlinked, Auto on | identical to the bit |
| Look-ahead 3 and 10 ms, Attack 0.1 and 10 ms | attack side the LA 0 trace shifted, release side identical: 0 dB |
| Sane key (NaN, ±∞, ±10³⁰⁰ through `saneKey`); every knob's ends | finite, back to the lift within 2 s; a raw NaN past the guard leaves the gain finite (1 once released) |
| Denormals without flush-to-zero, 150 s of silence after Attack 300 ms, Release 5 s | none; lift, mean square and Auto's memory end exactly 0 |
| Allocation, random knobs per block, follow and seed, gains and matrix | none |
| THD+N, 1 kHz at 6 dB of lift, reported (Attack × Release 5 / 50 / 150 ms / 1 s) | 1 ms: −72.1, −72.1, −72.6, −81.8 dBc; 10 ms: −88.6 … −91.1; 30 ms: −98.1; 100 ms: −108.5 |
| CPU, stereo, 48 kHz, the model with `gainFor` and the matrix, 512-sample blocks (`[.perf]`, load average 3–4) | **0.167 %** of a core at the defaults with the key in the lift region, 0.199 % with look-ahead 5 ms, 0.138 % Unlinked with Auto off, 0.084 % with the key over Threshold (budget 0.3 %; Clean read 0.274 % on the same measure in M1, `../clean/NOTES.md`) |

## Through the processor (M2)

`BasaltMeasure all --model upward` (48 kHz, Normal and Live, 2026-11-17), against
`tests/models/upward/Reference.h`:

- **Identity:** 290 rows bit-identical: Ratio 1, Range 0, a key under Floor (−80 dBFS at Floor
  −70) from the first sample, loud noise over T + W/2 from 300 ms.
- **Static:** 1081 pass over T −50, −30, −10 × R 1.5 … ∞ × Knee × Range × Floor −90, −70, −50,
  levels from 14 dB under Floor (the carrier's amplitude; its reading is 3.01 dB higher), worst
  0.029 dB; the output rises with the input everywhere and the gain is exactly 1 at and under
  Floor. 100 and 50 Hz reported (up to 1.3 and 3.0 dB inside the lift region at Attack 10 ms,
  Release 1 s, Floor −90).
- **Ballistics:** attack 1.000–1.071 × the specified time (Attack ≥ 3 ms against the knob); the
  lift-in 1.000 × max(Release, the detector's floor) over R 1.5 … ∞, Release 5 ms … 5 s, Attack
  1 and 30 ms; Auto 208.6 and 606.2 ms after 0.2 and 6 s above Threshold.
- **Clicks:** Floor −90 ↔ −30 at 10 dB of lift −68.8 dB, the 1 dB/s Floor sweep passes, every model
  switch (−67.6 dB) and no-swell row passes. Fails: Key Bell Gain at Attack 10 ms −57.2 dB (Normal
  and Live), Look-ahead 0 ↔ 10 ms at Attack 0.02 ms −59.2 dB.
- **Stereo:** the loud channel over Threshold untouched when Unlinked (from 0.3 s, the onset), the
  quiet one's own lift; Linked takes the louder's lift.
- **Cost:** 0.181 % of a core at the defaults, 0.243 % with the whole key path.

## Proposed changes to PLAN.md

1. ***The reference behaviour*, Upward row:** Detector "RMS · feed-forward" → "RMS (10 ms) ·
   feed-forward"; Attack "how fast the lift goes away (one-pole)"; Release "how fast the lift comes
   in, a constant rate of 10 dB per Release; Auto slows it up to 5 × after loud passages". Prior
   art: "LSP (LGPL-3.0) holds its lift constant below a boost threshold; ZL (AGPL-3.0) returns to
   unity below a Floor; read for behaviour, no code".
2. ***Controls*, Floor:** add "the lift fades to 0 over the 12 dB above Floor (a smooth curve, so
   the gain never steps at Floor)".
3. ***Measurement and quality gates*, Static curve:** add for Upward "and the output never falls
   as the input rises (monotonic), and the lift is exactly 0 at and below Floor".
4. **(M2) *Measurement and quality gates*, Ballistics:** Upward's attack below 3 ms against the
   specified time (`tests/models/upward/Reference.h`), not Attack + 0.25 ms.
5. **(M2) *Measurement and quality gates*, Identity:** Upward over the knee once the detector has
   read the level (from 200 ms after an onset from silence).
6. **(M2) Settled 32 / 54 for Upward:** Link takes the louder channel's lift (by level), applied
   to the finished lifts.

## Not verified

- ZL's Floor shape and LSP's upward curve beyond their manuals' sentences (no code read, by
  intent).
- Every "measured (simulation)" figure is a simulation of these equations; M2 repeated them on
  the engine at model level (*Measured in M2*); `BasaltMeasure` repeats them through the
  processor once the chain runs Upward.
