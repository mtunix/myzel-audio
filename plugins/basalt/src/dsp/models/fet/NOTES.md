# FET: the feedback peak limiter

The fast one: a peak limiter whose side chain listens to its own output, with attack times down to
tens of microseconds, a threshold that rises with the ratio, and an extreme fifth setting at the
top of the Ratio knob. Behaviour reference: the **1176LN** limiting amplifier (Universal Audio's
reissue manual, P/N 65-00046), with the **1178** dual limiter's manual (UREI, 1980) for the theory
of operation and the threshold figures, and the **2-1176** manual (P/N 65-00047) as a second
reading of the all-buttons setting. Product names appear only here and in `PLAN.md` (root
`NOTICE`).

FET runs on the shared core's feedback loop (`../../dynamics/NOTES.md`, *Feedback*). It overrides
four things there: the ratio cap (20, with the ∞ step as **All**), a threshold that moves with
the ratio, a shorter pre-hold, and the Auto release (dimmed).

Status: M0 specification, implemented in M2 (2026-11-17) in this folder (`FetModel`) and checked
at unit level by `BasaltTests_fet` (`tests/models/fet/`, with `Reference.h`, this file's equations as
pure functions for `BasaltMeasure`). *Implementation (M2)* records what the code decided where
this file left it open or turned out incomplete, *Measured in M2* what the engine reads. Not yet
wired into the chain (the integrator's step). Marks as in the core's NOTES: **source**,
**measured (simulation)** (Python/NumPy, double precision, 48 kHz unless stated, the set-up next
to the numbers, scripts not kept), **measured (M2)** (the C++ model, by the unit tests named next
to the numbers; `BasaltTests_fet -s` prints them), **Basalt's design**, **inference**, **not
verified**.

## Sources

| Source | What FET takes | Read |
|---|---|---|
| Universal Audio, *Model 1176LN Solid-State Limiting Amplifier* manual, P/N 65-00046, © 2009 (the PDF's metadata title: "1176LN Manual_v3-090908-DC"; a dealer's copy, planning library `kmr1176.pdf`) | attack 20–800 µs and release 50 ms–1.1 s (pp. 9, 21, 38); ratios 4, 8, 12, 20, a higher ratio also raising the threshold (pp. 9, 20–21); soft knee (p. 21); all-buttons (pp. 9, 22); stereo doubles the fastest attack (p. 27); the circuit description: Figure 1 block diagram (p. 29), the FET as the bottom of a divider (Figure 4, p. 31), the unit called "feedback style" (p. 31), Figure 5, a partial schematic of the gain-reduction control circuit (p. 32), and its description (p. 33) | read in full, Figures 1 and 5 on the rendered pages |
| UREI, *Model 1178 Dual Peak Limiter* manual, 1980 (a scan with OCR; `ua1178.pdf`) | specifications (p. 2): attack under 20 µs, defined on full ("100 %") recovery, adjustable to 800 µs; release 50 ms to 1.1 s, defined on 63 % recovery; the threshold-versus-ratio table; operation (§3); theory of operation (§4.3–§4.6, pp. 17–18) | read: §1–§4, the specification table checked on the page image. The schematic (Figure 6-2) **not read** |
| Universal Audio, *Model 2-1176 Dual 1176LN Limiting Amplifier* manual, P/N 65-00047 (metadata "2-1176 Manual 090910-DC"; `g4m1.pdf`) | the same ranges; the ALL ratio position, with the meter described as going wild and often resting at maximum (front panel, item 7) | read: front panel, all-button section |
| F. Floru, AES preprint 4703, 1998 | ratio = k + 1 (eqs. 45–46); the loop divides the time constant by k + 1 | read (§2–§3; also the core) |
| E. Gerat, F. Eichas, U. Zölzer, AES 143rd Conv., paper 9852, 2017 | a grey-box 1176LN model | abstract only (via `PLAN.md`); nothing here rests on it |
| `../../dynamics/NOTES.md` | the loop, Range on the state, the measurement definition, the ratio cap rule | Basalt |

The planning library's `ua1176ln.pdf` is not the manual: it is an HTML bot-check page saved under a
`.pdf` name. `kmr1176.pdf` is the 1176LN manual itself.

## Topology: feedback, from the circuit description

**Feedback** (**source**, three independent statements):

- 1176LN manual, p. 29: the control voltage comes from a level-sensing circuit that monitors the
  output; Figure 1 shows the gain-reduction control circuit fed from the preamp's output, before
  the output level control.
- p. 31 calls the unit a "feedback style" compressor because its side chain samples the level
  after the gain reduction. p. 33 places the tap at the preamplifier's output, just before the
  volume potentiometer (R23). Figure 5 (p. 32) is the partial schematic of that circuit, its
  input labelled as coming from the preamp output.
- 1178 manual §4.3–§4.4 (p. 17): the FET attenuator sits between the input amplifier and the
  preamplifier, and the preamplifier's output feeds both the Output Level control and the ratio
  switch, which drives the gain-reduction amplifier.

`PLAN.md` says the 1176LN manual "does not say" and that M0 reads the schematic. The manual does say
it, and the only schematic it carries is Figure 5's partial one, which is enough for the topology
(*Proposed changes*, 1).

**How the hardware makes ratio and threshold** (Figure 5 and p. 33, **source**; reading the drawing,
**inference**): the ratio switch has two decks. One picks a tap of a resistor ladder from the
preamp's output, which sets the level sent to the side chain (p. 33); the other sets the bias on
the rectifier's diodes, which sets the threshold (p. 33). A higher button takes more side-chain level, which is more loop gain and
so a higher ratio (Floru: ratio = k + 1), and a higher bias, which raises the threshold. The 1178
does the same with currents summed into its control amplifier (§4.6). This is why the threshold
moves with the ratio, and Basalt keeps it (*Gain computer*).

**Detector in the hardware** (**source**, p. 33 and 1178 §4.6): a full-wave rectifier (two diodes
driven in antiphase) charges a capacitor through the attack control and discharges it through the
release control. A peak detector with separate charge and discharge paths: Giannoulis's branching
type (**inference**). The release therefore does not depend on the loop: the capacitor discharges
at its own rate whatever the signal.

## 1. Detector

The core's feedback loop on the **peak** level (`../../dynamics/NOTES.md`, *The loop*), per
detector channel:

1. **Pre-hold** of |k| over H = h + LA samples, with **h = min(Release/10, 1 ms)** (**Basalt's
   design**; the core's default is 5 ms). The shorter hold leaves the gain free to move within the
   cycle below 500 Hz. That in-cycle modulation is part of the reference's sound (the 1178's
   specification, p. 2, has low-frequency distortion rising as the release gets faster) and
   `PLAN.md` keeps it
   as ballistics (*Models*: "A fast FET … modulates the waveform within a cycle, and that is
   ballistics, measured and kept"). The static gate then runs at 1 kHz (≥ 1/(2h)); 100 Hz is
   reported (simulation, *Gain computer*: within 0.03 dB at Attack 0.8 ms, Release 1.1 s, since
   the release holds the reduction between peaks there). **Corrected in M2:** at the static
   gate's own Attack 10 ms, Release 1 s, 100 Hz reads up to −0.41 dB (All; −0.36 dB at 20:1,
   −0.20 dB at 4:1) and 50 Hz up to −0.60 dB (*Measured in M2*): between the held peaks the
   estimate falls, the closed loop releases towards the lower demand R times faster than the knob
   (a partial release, core *Results*), and the attack wins it back only at the knob's speed
   (**inference** from the loop's equations; the figures grow with Attack and Ratio as it says).
   The 0.03 dB holds at fast attacks only.
2. **Estimate** `y = L_w − s[n−1]`, **demand** `c = G⁻¹(y; T_eff) − y` (*Gain computer*), smooth
   branching, Range on the state: the core's loop unchanged.
3. **Stereo:** the core's modes and Link. The hardware links two units through an adapter, which
   doubles the fastest attack, 20 to 40 µs (p. 27, **source**). Not kept: Basalt's link is exact and costs no speed (**Basalt's design**).
4. **All** (the ∞ step) adds a key lag (*All*).

## 2. Gain computer

The core's eq. 4 in its feedback form, with the threshold offset by the ratio:

```
T_eff = T + ΔT(R)
ΔT(R) = 0                                   R ≤ 4
        piecewise linear in log2 R through (4, 0), (8, +4), (12, +5), (20, +6) dB
r_s(x) = min( x − G(x; T_eff, R, W), Range )         static reduction, eq. 4
c(y)   = G⁻¹(y; T_eff, R, W) − y                     the loop's demand (core, *The feedback form*)
```

- **The offsets are the 1178's** (**source**, spec p. 2, checked on the page image): the input
  level at the minimum limiting threshold (±2 dB) is −30, −26, −25, −24 dB (0 dB = 0.775 V) at 4:1,
  8:1, 12:1, 20:1, i.e. +4, +5, +6 dB above the 4:1 threshold. The same table gives the output at
  threshold as +7, +8, +9, +10 dBm (with the output control set for about 10 dB of reserve), a
  3 dB spread against the input's 6 dB; the input column is the threshold, so Basalt uses it
  (**inference**). The 1176LN manual states the coupling without figures. The 1178's §1.1 presents
  it as a two-channel version of the 1176LN with similar performance (**source**); the figures
  themselves are **not verified** on a 1176LN.
- **Threshold** is the 4:1 threshold. Below 4:1 the offset stays 0 (**Basalt's design**: the
  hardware has no lower ratio, and extrapolating the 4→8 slope would put the threshold 8 dB under
  the knob at 1:1, where it means nothing anyway).
- **Ratio:** 1 … 20, continuous, the feedback loop's ratio exactly (the core's loop reaches eq. 4).
  **Cap 20**: the top button and the knob's last continuous value. The ∞ step is **All**.
- **Knee:** the Knee knob as eq. 4's W. The manual says only that the unit uses a soft knee
  (p. 21, **source**); no width is published (**not verified**), so the
  shared default of 6 dB stands.
- **Range** on the state (core).
- The transfer-curve display draws eq. 4 at T_eff and marks T (the knob) where they differ.

Measured (simulation, the core's loop as specified here: pre-hold h = min(Release/10, 1 ms),
smooth branching, T_eff; sine at its peak level on the detector scale, Attack 0.8 ms, Release
1.1 s, Knee 6, T = −20 dB, 48 kHz): steady reduction against eq. 4 at T_eff over −30 … 0 dB in
3 dB steps, worst error **0.000 dB at 1 kHz** for R = 4, 8, 12, 20, and **−0.020, −0.025, −0.027,
−0.029 dB at 100 Hz**. (A first M0 run on a variant, a branching detector with no pre-hold,
read 0.113 … 0.115 dB at 1 kHz: the hold is what makes the curve exact.)

## All: the ∞ step (`PLAN.md`, Open 4)

**What the sources say** (**source**, 1176LN p. 22 and front panel p. 9): with all four buttons in,
the ratio lands between 12:1 and 20:1; the bias points change across the circuit and with them the
attack and release times; distortion rises because the attack lags on initial transients, which
the manual likens to a "reverse look-ahead"; the curve keeps shifting. The 2-1176 adds that the
meter goes wild and often rests at maximum (front panel, item 7).

**Recommendation for Open 4: All is FET's reading of Ratio's ∞ step**, not a Ratio position of its
own and not a model of its own (**Basalt's design**):

- A feedback model cannot reach ∞:1 and caps it (`PLAN.md`, *Feedback models*); FET's cap is 20,
  which the continuous range already reaches, so ∞ would otherwise repeat 20:1. Giving the step
  this meaning changes no frozen list and no normalised value.
- Rejected: **a Ratio position after ∞** would change Ratio's frozen mapping in M0 for one model and
  mean nothing in the other eight; **a model of its own** would add a tenth entry to the frozen
  `model` list (moving every normalised value) and duplicate FET for one setting of it.
- In every other feedback model the ∞ step reads as that model's cap (core, *The ratio cap*); in
  the feed-forward models it is ∞:1.

**What All does** (**Basalt's design** on the sources above):

| Part | All | Why |
|---|---|---|
| Ratio | 20 (loop gain 19) | the top of the published 12–20 range |
| Threshold | T_eff = T (the 4:1 threshold, no offset) | "bias points change all over": with both decks' sections in parallel, the side chain gets the highest tap while the threshold leaves the higher buttons' raised settings. Basalt takes the lowest, so All compresses harder than 20:1 at the same Threshold, as the 2-1176's meter resting at maximum suggests (**inference** from Figure 5 and that sentence; the direction of the bias change is not readable from the partial drawing) |
| Key lag | the key reaches the detector **L = 1 ms** later than the audio (a negative look-ahead): the first millisecond of a transient passes unreduced, then the loop catches up at the set Attack | the manual's attack lag on initial transients, its "reverse look-ahead". The length is not published (**not verified**); 1 ms is a starting value for M3's voicing, which may set it in 0.25 … 2 ms and records the choice here |
| Attack, Release | the knobs, as at 20:1 | the changed times are not quantified anywhere read |
| Distortion | none added by the model | `PLAN.md`: harmonics belong to Character. The lag and the fast loop produce their own in-cycle modulation, which is kept. A preset may pair All with Character's Edge |

- **The lag in the delay line:** with Look-ahead LA, the key's delay becomes D − LA + L, which fits
  the core's 15 ms ring. In Live (D = 0) the key is delayed by L; delaying the key is always
  possible, so All works in both latencies. **M2:** the model delays the key's magnitude itself,
  before the pre-hold, so the chain needs nothing for All (*Implementation (M2)*); the effect is
  the same delay.
- **Switching into or out of All** ramps T_eff over the core's 20 ms and glides the key tap by L
  over 50 ms, as the core glides Look-ahead (*Clicks*).
- **Display:** the Ratio read-out and the status line say "All"; the transfer curve draws 20:1 at T.

## 3. Ballistics

**Attack** and **Release** are the core's measured times (constant-magnitude carrier, silence to
10 dB of reduction and back; attack to 63.2 %, release to 36.8 %), mapped **1:1** over the whole
knob ranges (**Basalt's design**: the knobs mean the same in every model, `PLAN.md` *Feedback
models*):

- **Attack 0.02 … 300 ms.** The 1176LN's 20–800 µs (pp. 9, 21) is marked on the knob (the UI shades
  it). Slower settings are allowed: the loop handles them, and the FET's character is its curve
  and its loop as much as its speed. The attack coefficient is the core's,
  `1 − α_A' = (1 − e^(−1/(Attack·fs))) / R_set`, with R_set = min(Ratio, 20), and 20 for All.
  Rejected: clamping the knob to 20–800 µs in this model, which would make the shared default
  (10 ms) mean 0.8 ms here.
- **Release 5 ms … 5 s**, the release coefficient as the core (the loop is open below threshold).
  The 1176LN's 50 ms–1.1 s is marked. The core's definition (to 36.8 %, i.e. 63 % recovered) is
  exactly the 1178's "for 63 % recovery" (**source**), so Basalt's 1.1 s means what the 1178's
  1.1 s means.
- **The 1178's attack figure** (under 20 µs) is defined on full recovery (**source**), which does
  not define a time constant; Basalt uses its own 63 % definition there too.
- **Auto release: dimmed.** No source read documents a program-dependent FET release, or a release
  that slows with reduction depth (`PLAN.md`'s open point): the 1176LN manual calls only "the amount
  of 1176LN gain reduction" program dependent (glossary, p. 36), and the 1178 gives a single
  63 % figure. Following `PLAN.md`, nothing is specified; the knob dims in FET and the status line
  says why. The feedback loop's own level dependence (a partial release snaps when the loop closes,
  about 0.65 of the set time to 63 %; the knee region attacks slower) stays as character (core,
  *Results*).
- **At 44.1 and 48 kHz** an attack of 0.02 ms is under one sample; the core's time origin and
  interpolation apply (*What the knobs mean*), with ±0.25 sample allowed under 2.5 samples.

Measured (simulation, the loop as specified here: pre-hold h = min(Release/10, 1 ms), smooth
branching with the attack coefficient divided by the set ratio; 48 kHz; constant-magnitude carrier
stepping from silence to the level for 10 dB of reduction at Ratio 4, hard knee; attack at
Release 300 ms, release at Attack 0.8 ms; the core's time origin and interpolation):

| Attack set | 0.02 ms | 0.05 ms | 0.2 ms | 0.8 ms | 10 ms | 100 ms |
|---|---|---|---|---|---|---|
| measured (63 %) | 0.0204 | 0.0511 | 0.2003 | 0.8001 | 9.999 | 99.95 |

| Release set | 5 ms | 50 ms | 300 ms | 1.1 s |
|---|---|---|---|---|
| measured (to 36.8 %) | 4.979 | 49.98 | 299.98 | 1099.98 ms |

All within 2.1 % (the shortest attacks are under three samples, where the interpolation reads
long, core *What the knobs mean*). **Range on the demand instead of the state**, measured on the
first M0 variant (a branching detector with no pre-hold), Ratio 20, Attack 0.2 ms: the attack read
**0.720 ms** (3.6 × slow) with the demand clamped at 40 dB and **0.199 ms** with the state clamped;
the core measured the same rule on its own loop (1.000 × with the state clamped).

## 4. Make-up and gain law

- **The cell:** the FET is a voltage-controlled resistor forming the bottom of a divider with a
  series resistor (Figure 4, p. 31, **source**); the "LN" circuit exists to keep it in its linear
  region (p. 31). Basalt applies an ideal gain `g = 10^(−r/20)` (core). The FET's own distortion
  belongs to Character (`PLAN.md`, *Models*).
- **Auto Gain** inverts the static curve at the reference level, `M = r_s(L_ref)` with T_eff
  (core, *Make-up and Auto Gain*). For All, T_eff = T and R = 20.
- **Gain** is make-up after the loop, outside it: the loop's estimate never includes Gain or
  Output, as the hardware's tap sits before its output control (p. 33).

## 5. Gate table

The core's rows apply (`../../dynamics/NOTES.md`, *Gate table*), with these FET values and rows:

| Gate | FET |
|---|---|
| Identity | Ratio 1; or input below T_eff − W/2 (All: below T − W/2); Range 0. Bit-identical as the core, both latencies, with and without All's key lag |
| Static curve | 1 kHz sine and the carrier, T − 20 … T + 30 dB in 1 dB steps, R 1.5, 2, 4, 8, 12, 20 and All, W 0, 6, 24, Range 40 and 10, Attack 10 ms, Release 1 s: steady reduction within ±0.25 dB of `min(x − G(x; T_eff), Range)`. 100 Hz and 50 Hz reported (h ≤ 1 ms leaves the gain moving within the cycle) |
| Threshold coupling | hard knee, carrier on a 0.01 dB level sweep: the knee, found by extrapolating the reduction above it linearly to 0 dB, at T + 0, + 4, + 5, + 6 dB (±0.05) for Ratio 4, 8, 12, 20; at Ratio 5.66 (between 4 and 8 on log2) at T + 2 dB; at All at T |
| Ballistics | the core's definition, Knee 0, Attack 0.02, 0.05, 0.2, 0.8, 10, 100, 300 ms × Release 5, 50, 300 ms, 1.1, 5 s, R 4, 20 and All: ±10 % (All measured from the end of the lag) |
| All's lag | carrier step from silence to 10 dB of reduction: the reduction stays below 0.1 dB for the first L (1 ms ± 1 sample), then reaches 63 % of the settled value Attack later ±10 % |
| Knee region | reported: Knee 12, R 4, attack measured/set for steps ending inside the knee (slower than the knob, core) |
| Distortion | reported, not gated (`PLAN.md`): THD+N of a 1 kHz sine at 6 dB of reduction for Attack 0.02, 0.8, 10 ms × Release 50 ms, 1.1 s, R 4, 20 and All, and of a 100 Hz sine at Release 50 ms |
| Clicks | the core's list, plus Ratio 20 ↔ All (T_eff ramp, lag glide) at Attack 0.02 ms: no step artefact above −60 dB |
| Stereo, Real-time | as the core |
| Learn | feedback: the loop runs on the captured envelope (`PLAN.md`, *Learn*); M3 measures the gap to the audio |
| Auto Gain | `r_s(L_ref)` with T_eff, ±0.01 dB against the formula; the corpus gate as `PLAN.md` |
| Goldens | R 4 at Attack 0.8 ms and Release 50 ms on drums; All on drums; R 20 on a vocal |

## Implementation (M2)

Where this file left a choice open, or the code found it incomplete (**Basalt's design**, M2,
2026-11-17; `FetModel.h`, `FetModel.cpp`):

- **The loop is the core's `FeedbackLoop` unchanged**, with eq. 4's inverse as the demand, the
  state as the estimate's subtraction and Range on the state. FET brings step 1 (the peak
  magnitude, All's lag line, the pre-hold `RunningMax` over h + LA + 1 samples), the curve
  (`loopCurve`: T + ΔT, 1/R capped at 1/20; All: T, 1/20) and the coefficients.
- **ΔT** is computed from the knob's 1/R (`thresholdOffsetDb`), linear in log2 R through the four
  points, 0 at and below 4:1, +6 dB at the cap. All is 1/R exactly 0 (Ratio's ∞ step,
  `inverseRatio`), read as 20:1 at T.
- **The hold subtracted from the release is the window's, in whole samples**, round(h·fs)/fs, not
  h itself: the measured release is then exactly the knob (1.0000 × at 44.1, 48 and 96 kHz,
  where M0's simulation read 4.979 ms for 5 ms).
- **The coefficients are taken once at Ratio 1** (`unitCoefficients`) whenever the time knobs
  move; a curve's attack step is that one times its 1/R. The two loops of a curve fade each take
  their own curve's 1/R, so a fade between 4:1 and All runs each loop at its own speed. Taking the
  core's `coefficientsFor` per loop and per sample during fades measured 0.38 % of a core with a
  fade always running; the product reads the same bits at 0.29 %.
- **All's lag lives in the model.** A delay line of the key's magnitude (the core's `KeyDelay`,
  sized for 2 ms, M3's voicing range) sits before the pre-hold, set to round(L·fs) samples (48 at
  48 kHz, 44 at 44.1 kHz, 1 ms ± half a sample) and gliding linearly over the core's 50 ms
  (`lookaheadGlideMs`) when All comes or goes. The model interface allows no latency of its own,
  and delaying the key is not latency: the audio is untouched. With look-ahead the pre-hold
  window stays h + LA + 1 samples on the delayed key, so the attack side is the LA = 0 trace
  shifted by LA and the release side is the same in audio time, All included (measured, below).
  Rejected: the chain reading the key D − LA + L late (the chain would have to know one model's
  setting; the result is the same).
- **Between samples the lag line reads the louder neighbour**, max(|k[n − ⌊ℓ⌋]|, |k[n − ⌊ℓ⌋ − 1]|),
  while the lag glides. The pre-hold then never sees a magnitude no sample had, and the glide
  only moves its window's edge by a sample. Linear interpolation reads up to 0.037 dB under the
  crest between samples (computed: the midpoint between a crest sample and its neighbour, 997 Hz
  at 48 kHz), which a 20 µs attack passes into the gain: Ratio 20 ↔ All on a 997 Hz
  sine at Attack 0.02 ms measured a second difference of 6.5·10⁻³ dB with it and 1.17·10⁻³ dB with
  the louder neighbour, the same as the key held at 20:1 (*Measured in M2*). Settled, the lag is
  a whole number of samples and reads one sample.
- **The key's magnitude is taken sane in the model too:** a NaN reads 0, anything past 10⁶
  (+120 dBFS) reads 10⁶, as the chain's `saneKey`, so a model driven without the chain cannot
  hold a NaN in its pre-hold window (the running maximum's wedge keeps one until it expires).
- **Cost measures:** the held peak's level is cached while the window's maximum repeats, and in a
  steady block (no fade, no glide) a held peak below the knee's foot skips the log (the demand is
  0 there whatever the state).
- **settleMs** is the pre-hold plus All's lag plus one sample (1.02 ms at the defaults, 2.02 ms at
  All); after `follow()` for that plus the look-ahead window and a seed, the model carries on as
  one that ran throughout, to the bit (*Measured in M2*). The memories the core lists (none in
  FET) start empty.
- **Auto release is not read** (dimmed): Auto on and off give the same bits.
- **Gate set-ups the gate table left open:** the static staircase holds each 1 dB step for
  0.1 s plus six of the closed loop's local constants, Attack·R·G'(x) (inside a 24 dB knee at 20:1
  up to 20 × Attack), then averages 40 ms (two periods at 50 Hz); the threshold coupling sweep
  holds each 0.01 dB step 2 ms at Attack 0.2 ms and fits the reductions between 1 and 8 dB; the
  ballistics hold the attack max(5·(Attack + Release), 10·Attack) (Settled 70); THD+N fits the
  fundamental over 50 periods after 25 attack constants plus two releases plus 0.5 s.

## Measured in M2

**Measured (M2)**, `BasaltTests_fet`, 48 kHz unless stated (2026-11-17). Every gated row passes.

| Gate | Result | Test |
|---|---|---|
| Static curve: 1 kHz sine and carrier, T −60/−30/−10, R 1.5, 2, 4, 8, 12, 20, All, W 0/6/24, Range 40/10, T − 20 … T + 30 dB, Attack 10 ms, Release 1 s | worst **0.0004 dB** on both (T −60, All, W 24); gate ±0.25 | *the static curve at T_eff* |
| M0's static set-up (Attack 0.8 ms, Release 1.1 s, W 6, T −20, −30 … 0 dB) | 1 kHz −0.0000 dB; 100 Hz **−0.0193, −0.0248, −0.0269, −0.0283 dB** at 4, 8, 12, 20:1 (M0: −0.020, −0.025, −0.027, −0.029, gated within 0.0015); 50 Hz −0.042 … −0.056 dB | *100 and 50 Hz reported* |
| Threshold coupling, hard knee, 0.01 dB sweep on the carrier | knee at T **+0.0000, +4.0000, +5.0000, +6.0000 dB** for 4, 8, 12, 20:1; +2.0032 dB at 5.66:1 (log2 5.66 is not exactly 2.5); +0.0001 dB at All; +0.0000 at 2 and 1.5:1; slopes 1 − 1/R to 10⁻⁵; gate ±0.05 | *the threshold moves with the ratio* |
| Ballistics: Attack 0.02 … 300 ms × Release 5 ms … 5 s, R 4, 20, All, Knee 0, 10 dB | attack within **2.1 %** (0.02 ms: 1.021 ×, under one sample), release **1.0000 ×**; at 44.1 kHz attack 1.057 × at 0.02 ms (0.88 sample, inside ±0.25 sample), at 96 kHz 1.009 ×; settled depth 10 dB to 10⁻³; gate ±10 % | *attack and release are the knobs* |
| M0's ballistics table (R 4, attack at Release 300 ms, release at Attack 0.8 ms) | attack 0.0204, 0.0511, 0.2003, 0.8001, 10.000, 100.00 ms for 0.02 … 100 ms (M0: 0.0204, 0.0511, 0.2003, 0.8001, 9.999, 99.95); release 5.000, 50.000, 300.000, 1100.000 ms (M0: 4.979, 49.98, 299.98, 1099.98) | *the M0 simulation's ballistics table* |
| All's lag, carrier step to 10 dB, Attack 0.02, 0.2, 0.8, 10 ms | first reduction at **1.0000 ms** (44.1 kHz: 0.9977 ms, 44 samples); 0.1 dB reached at 1.0003 … 1.1005 ms; 63 % **0.0204, 0.2003, 0.8001, 10.000 ms** after the lag (44.1 kHz 0.0211 … 10.000, 96 kHz 0.0202 … 10.000) | *All's key lag* |
| Look-ahead 3 ms, 1 kHz burst 21 dB over, R 4, 20, All | attack side = the LA 0 trace shifted, release side identical: **0 dB** difference, All included | *look-ahead starts the attack early* |
| Range: 30 dB over T_eff, Range 10, R 20 and All | 10.000 dB; demands past Range 40 (12 and 30 dB over) attack at the knob (±10 %) | *Range sits on the loop's state* |
| Auto Gain and the display's curve against `Reference.h` (T −60 … 0, R 1 … 20 and All, W 0 … 24, Range 0/10/40) | **0** dB difference | *the static reduction the display and Auto Gain read* |
| Identity: Ratio 1; Range 0 (also All); levels under T_eff − W/2 at 8, 12, 20:1 and under T − W/2 at All, both above T − W/2 where ΔT > 0; carrier and 997 Hz sine; look-ahead 0/5/10 ms; blocks 1/64/4096 | every sample exactly 0 (a gain of exactly 1) | *idle is exactly 0* |
| Stereo, 10 s of pink noise, R 4, 20, All | Link 100 %: both channels identical to the bit; Link 0 % = Unlinked to the bit; Link 50 % between them | *Link 100 %* |
| Seed (Settled 33): Attack 300 ms, seeded at 10 dB on the key that gives it | stays within 10⁻⁶ dB of 10 dB, R 4, 20, All | *a seed starts the loop* |
| Warm-up (Settled 56): `follow()` for settleMs + look-ahead, then seeded with the running engine's reduction, linked stereo, Link 70 % | identical to an engine that ran throughout, **to the bit**, R 4, 20, All, Release 5 and 150 ms | *warmed up for settleMs* |
| Block size: blocks 1, 7, 64, 448, 4096, settings still and moving at shared block starts | the same bits | *the output does not depend on the block size* |
| NaN and ±∞ in the key | the same bits as 0 and ±10⁶ there; the reduction finite and within Range | *a key that is not a number* |
| Real-time | nothing allocated in `process`, `follow`, `seed`, `reset`, `staticReduction`, `settleMs` once prepared (random settings, blocks, mono/stereo, look-ahead moving); no subnormal after 150 s of silence at Attack 300 ms, Release 5 s, the state exactly 0 | *allocates nothing*, *nothing goes denormal* |

**Reported, not gated:**

- **100 and 50 Hz** (T −30, Release 1 s, the worst over W 0/6/24 and Range 40/10): at Attack 0.8 ms
  100 Hz −0.005, −0.009, −0.025, −0.038, −0.043, −0.048, −0.057 dB for 1.5, 2, 4, 8, 12, 20:1 and
  All; at the gate's **Attack 10 ms −0.041, −0.078, −0.198, −0.290, −0.326, −0.355, −0.409 dB**.
  50 Hz: −0.012 … −0.111 dB at 0.8 ms, **−0.085 … −0.601 dB** at 10 ms. `PLAN.md`'s Static row
  gates feedback models at 100 Hz; with FET's 1 ms hold (Settled 38) 8:1 and above at Attack 10 ms
  would fail it (*Proposed changes*, 7). The figures read low: the in-cycle partial release, *1.
  Detector*.
- **Knee region** (Knee 12, R 4, T −30, Release 300 ms; attack measured/set for steps from
  silence ending at T − 4, −2, 0, +2, +4, +6, +10 dB): **3.52, 2.97, 2.36, 1.60, 1.07, 1.00, 1.00 ×**
  at Attack 0.8 ms (3.49 … 1.00 × at 10 ms). The linearised closed loop at the settled level,
  Attack·R·G'(x), predicts 3.50, 3.00, 2.50, 2.00, 1.50, 1.00, 1.00 ×: close inside the knee's
  lower half, pessimistic near its top, where most of the step runs above the knee.
- **In-cycle modulation, THD+N at 6 dB of static reduction** (hard knee, T −30, double-precision
  gain on the sine), Attack 0.02 / 0.8 / 10 ms:

  | | 1 kHz, 48 kHz | 997 Hz, 48 kHz, Release 50 ms / 1.1 s | 1 kHz, 44.1 kHz, Release 50 ms / 1.1 s | 100 Hz, Release 50 ms |
  |---|---|---|---|---|
  | R 4 | below −286 dBc | −82.8 / −106.3, −83.2 / −107.4, −88.3 / −109.0 | −78.1 / −102.2, −78.3 / −102.7, −83.1 / −103.8 | −38.7, −40.4, −45.7 |
  | R 20 | below −285 dBc | −71.3 / −92.0, −71.6 / −92.5, −79.4 / −95.2 | −67.9 / −86.9, −67.8 / −87.3, −74.4 / −90.0 | −36.8, −38.6, −44.3 |
  | All | below −289 dBc | −71.5 / −91.9, −71.6 / −92.6, −79.3 / −95.2 | −67.9 / −86.9, −67.8 / −87.3, −74.4 / −90.0 | −37.2, −39.9, −44.5 |

  At 48 kHz a 1 kHz period is exactly 48 samples, so the 49-sample hold always holds the same
  sampled crest and the gain does not move: the gate table's 1 kHz row reads the arithmetic's
  floor. 997 Hz at 48 kHz and 1 kHz at 44.1 kHz show the crest moving from period to period;
  100 Hz shows the in-cycle modulation the 1 ms hold keeps, −37 to −46 dBc at Release 50 ms.
- **Knob jumps** (carrier 14 dB over T −18, R 4, W 6, a jump every 0.31 s, Attack 0.02 and 10 ms):
  largest step 0.002 … 0.077 dB a sample, second difference inside the 20 ms fades **≤ 2.2·10⁻⁴ dB**
  (Threshold −60 ↔ 0, Ratio 1 ↔ All, 20 ↔ All, 4 ↔ 20, Knee 0 ↔ 24, Range 0 ↔ 40), gated
  < 5·10⁻⁴. After the fades, at Attack 10 ms Threshold −60 ↔ 0 shows **2.9·10⁻³ dB**: the loop
  reaching Range 40 with its demand at 42 dB, where the state's clamp stops a slope of
  (42 − 40)/τ_A at once. A plain step without any knob moving gives the same corner
  (2.6·10⁻³ dB, step 0.087 dB). This is the core's Range on the state (*Range*: clamping the demand
  instead slowed the attack 4–31 ×), shared by every feedback model; reported for the click gate
  through the processor.
- **Ratio 20 ↔ All** (T_eff ramp and lag glide), measured from 0.1 s: 997 Hz sine at Attack
  0.02 ms step 0.010 dB (the T_eff fade's own slope), second difference 1.17·10⁻³ dB, as with the
  key held at 20:1 or All (1.17·10⁻³); pink noise at 0.02 ms 1.54 dB and 1.54 dB (the gain follows
  its key, as held); at Attack 10 ms 3.7·10⁻⁵ dB on the sine (held 1.2·10⁻⁵) and 8.5·10⁻³ on noise
  (held 7.6–8.9·10⁻³). Gated against the held keys (×1.05 plus 5·10⁻⁴).
- **Time knob jumps:** Attack 300 → 0.02 ms mid-attack and Release 5 s → 5 ms mid-release glide
  (largest step 0.022 dB, second difference 1.5·10⁻⁴ dB; gated < 0.05 and < 10⁻³).
- **Cost** (`[.perf]`, the model plus the gain conversion and the matrix, not the chain; stereo,
  48 kHz, 512-sample blocks, 20 s of pink noise into compression, the fastest of five passes,
  load average about 4): **0.163 %** of a core at the defaults, Linked (34 ns a stereo frame);
  0.153 % with look-ahead 5 ms; 0.173 % at 20:1, Attack 0.02 ms, Release 50 ms, Unlinked;
  0.164 % at All, Attack 0.02 ms, look-ahead 10 ms; **0.294 %** with a curve fade running all the
  time (two loops). Budget 0.5 % (`PLAN.md`, *Performance budget*).

## Through the processor (M2)

`BasaltMeasure all --model fet` (48 kHz, Normal and Live, 2026-11-17), against
`tests/models/fet/Reference.h`:

- **Identity:** 226 rows bit-identical.
- **Static:** the 1 kHz and carrier grid (R 1.5 … 20 and All) passes, worst 0.0003 dB; the
  coupling knees at T + 0, 4, 5, 6 dB and 2.003 dB at 5.66:1 within 0.0001 dB. **100 Hz fails 42
  of 126 rows** that PLAN.md gates for feedback models: −0.26 to −0.41 dB from 8:1 up at Attack
  10 ms (−0.409 at All), as *Measured in M2* says; proposed change 7 stands for the user.
- **Ballistics:** the grid (Attack 0.02 … 300 ms × Release 5 ms … 5 s × R 4, 20, All) within
  0.9999–1.021 × the knob, All's measured from the end of its lag; the lag itself: no 0.1 dB
  before sample 48 and the first reduction at 48 at Attack 0.02, 0.2 and 10 ms; a demand past
  Range attacks at the knob (10.0 ms); Auto release changes nothing, bit for bit. The knee region
  is reported.
- **Clicks:** every row passes, Ratio 20 ↔ All −76.3 dB, every model switch −75.4 dB, no swell.
- **Cost:** 0.223 % of a core at the defaults, 0.269 % with the whole key path.

## Proposed changes to PLAN.md

1. ***The reference behaviour*, FET row, detector cell:** replace "the 1176LN manual does not say,
   and M0 reads its schematic" with "**feedback**: the 1176LN manual says so (p. 31), Figure 1
   (p. 29) and the partial schematic Figure 5 (p. 32) take the side chain from the preamp output
   after the FET; the 1178 (§4.4) likewise".
2. **FET row, ratio:** add the figures: "threshold +4, +5, +6 dB at 8:1, 12:1, 20:1 over 4:1 (1178
   specifications, ±2 dB)". **Release:** "50–1100 ms for 63 % recovery (1178)". **Dependence on
   reduction depth:** "none documented in the 1176LN, 1178 or 2-1176 manuals; not specified, and
   Auto release dims in FET".
3. ***Open* 4: settled.** "FET's all-buttons is its reading of Ratio's ∞ step (**All**): ratio 20,
   the 4:1 threshold, a 1 ms key lag (a negative look-ahead, M3 voices 0.25 … 2 ms). No frozen list
   changes." Add it to *Settled*.
4. ***Feedback models*:** "the FET family's 20:1 and all-buttons" → "FET caps at 20 and reads ∞ as
   All; the other feedback models read ∞ as their cap".
5. ***Measurement and quality gates*, Ballistics:** "VCA, FET: the threshold moving with ratio, if
   their NOTES keep it" → FET keeps it: first reduction at T + 0, 4, 5, 6 dB for 4, 8, 12, 20:1.
6. ***References*, FET:** the 1176LN manual is read in full including Figure 5's partial schematic;
   add the 2-1176 manual (P/N 65-00047) and the 1178's specification figures (*References* below).
7. **(M2) *Measurement and quality gates*, Static curve:** "the feedback models up to their cap and
   also at 100 Hz (50 Hz reported)" → "… also at 100 Hz (FET: reported, its 1 ms pre-hold keeps
   the in-cycle modulation, Settled 38; 50 Hz reported)". Measured: 100 Hz at the gate's Attack
   10 ms reads −0.29 … −0.41 dB from 8:1 up (*Measured in M2*). The alternative, the core's 5 ms
   hold, would make 100 Hz exact and take away the modulation the reference is kept for.
8. **(M2) *Measurement and quality gates*, Distortion:** FET's 1 kHz row at 48 kHz cannot move
   (48 samples a period inside a 49-sample hold); report 997 Hz beside it, as `BasaltMeasure`'s
   44.1 kHz row already does.

## Not verified

- The threshold offsets on an 1176LN (the figures are the 1178's).
- The 1176LN's full schematic (not in the manual); the 1178's schematic (in its manual, not read).
- The length of the all-buttons attack lag and the direction of its bias change.
- Any knee width for the 1176LN.
- Gerat et al. 2017 and Eichas et al. 2017 beyond their abstracts or records.
- The figures under *Gain computer* and *3. Ballistics* are simulations of these equations; *Measured
  in M2* repeats them on the engine at unit level, and `BasaltMeasure` repeats them through the
  plug-in once the chain runs FET (not done in this step).
