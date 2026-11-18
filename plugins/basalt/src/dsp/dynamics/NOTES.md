# Dynamics core: detector, gain computer, ballistics

The shared side chain every Basalt model runs on (`PLAN.md`, *Models*, *Feedback models*, *Signal
flow*, *Latency*): the key path and key EQ, the stereo matrix, level detection, look-ahead, the
gain computer, Range, ballistics and the feedback loop. A model's `NOTES.md`
(`../models/<model>/NOTES.md`) chooses among what is specified here and overrides only what it
names. Product names appear only here and in `PLAN.md` (root `NOTICE`).

Status: M0 specification, implemented in M1 (2026-11-17) in this folder and checked at unit level
by `BasaltTests_dynamics` (`tests/dynamics/`), then wired into the plug-in (*The chain (M1)*) and
gated through it (*Measured in M1*, *Through the processor*); *Implementation (M1)* records what
the code decided where this file left it open or the gates proved it wrong, and *Measured in M1*
what the engine reads. Three independent reviews of M1 followed; what they changed is in
*Implementation (M1)* and *The chain (M1)*, marked "(review)". Each statement is marked:

- **source**: read in the cited document, at the equation or section given;
- **measured (simulation)**: computed in M0 with Python/NumPy/SciPy in double precision at
  48 kHz unless stated, by the method given next to the numbers (scripts in the M0 session's
  scratchpad, not kept; every set-up is described well enough to repeat). M1 repeats them through
  the real engine in `BasaltMeasure`;
- **measured (M1)**: the C++ engine in this folder at 48 kHz, by the unit tests named next to the
  numbers (`BasaltTests_dynamics -s` prints them);
- **Basalt's design**: our choice, with the reason;
- **inference**: reasoning from sources, not measured;
- **not verified**: looked for and not found, or not read.

## Sources

| Source | What it gives | Read |
|---|---|---|
| D. Giannoulis, M. Massberg, J. D. Reiss, *Digital Dynamic Range Compressor Design—A Tutorial and Analysis*, JAES 60(6), 399–408, 2012 | static curve (eqs. 2–4), one-pole time constant (eqs. 5–8), RMS (eq. 10), peak detectors (eqs. 11–17), feedback vs feed-forward (§3.1, eqs. 18–19), the alternate digital feedback (§3.1.1, Fig. 6), detector placement (§3.2, eqs. 20–23), comparisons (§4), recommendations (§5) | read in full, 2026-11-17, the authors' copy once posted on eecs.qmul.ac.uk (that server now fails TLS verification; OpenAlex lists no open-access copy). `PLAN.md` already marked it read |
| D. Giannoulis, M. Massberg, J. D. Reiss, *Parameter Automation in a Dynamic Range Compressor*, JAES 61(10), 716–726, 2013 | the compressor model (eqs. 2–6), crest factor (eq. 7), auto attack and release (eq. 8), auto knee, auto make-up, the listening study | read in full, 2026-11-17, the authors' copy once posted on c4dm.eecs.qmul.ac.uk. The erratum to eq. 3 (Feb. 2014) **not seen** |
| F. Floru, *Attack and Release Time Constants in RMS-Based Feedback Compressors*, AES 104th Convention, preprint 4703, 1998 | log-domain feed-forward and feedback transfer functions; the feedback divides the RMS time constant by k + 1 (§2.2, eq. 32 and §3.1.2); ratio = k + 1 (eqs. 45–46, Table 2) | read: §2–§3 |
| THAT Corporation, *Design Note 01A: The Mathematics of Log-Based Dynamic Processors*, doc. 600047 rev. 02, 2009 | feedback ratio as 1 + G; infinite compression needs infinite G; high feedback ratios prone to instability | read: *Feedback Processor Math*, *Available Ratios* |
| J. S. Abel, D. P. Berners, *On Peak-Detecting and RMS Feedback and Feedforward Compressors*, AES 115th Convention, paper 5914, 2003 | feedback as feed-forward with a level-dependent time constant | abstract only (via `PLAN.md`); Giannoulis 2012 cites it for eq. 10's equivalence to eq. 5 |
| O. Kröning, K. Dempwolf, U. Zölzer, *Analysis and Simulation of an Analog Guitar Compressor*, Proc. DAFx-11, 205–208, 2011 | the delay-free loop broken by one sample | not re-read in this run (`PLAN.md` marks it read) |
| D. Lemire, *Streaming Maximum-Minimum Filter Using No More than Three Comparisons per Element*, Nordic Journal of Computing 13(4), 328–339, 2006, arXiv:cs/0610046 | the running-maximum filter for the look-ahead window | abstract only |
| MathWorks, Audio Toolbox, *Dynamic Range Control* (documentation page) | a widely used alternative definition: attack and release as 10 %–90 % times | read; secondary, cited only to contrast the definition |
| A. Simper, *Linear Trap Integrated SVF* (Cytomic, 2013/2016) | bell response from the SVF's band-pass output | via Halite's `src/dsp/detector/NOTES.md` (read there); not re-read |

## Conventions

- **Levels** are in dB relative to digital full scale on the **detector scale**: a sine reads its
  peak level on every detector. The peak detector reads `20·log10|k|`; the RMS detector reads
  `10·log10(2·m)`, `m` the mean square, so a sine of peak A reads 20·log10 A on both (**Basalt's
  design**, the AES17 dBFS convention; AES17 itself **not opened** in this run). Threshold,
  Learn's levels and Auto Gain's reference are on this scale.
- **The constant-magnitude carrier** (±A alternating every sample, the gates' test signal,
  *Ballistics*) is not a sine: its mean square is A², a sine's A²/2. It reads **20·log10 A on a
  peak detector and 20·log10 A + 3.01 dB on an RMS detector** (10·log10(2A²)). Gate levels are set
  on the detector's own reading: a carrier "at Threshold" for an RMS detector has amplitude
  T − 3.01 dB, and a blend of the two (Clean) expects the blend of the two readings
  (`../models/clean/NOTES.md`). Only a sine reads alike on every detector.
- **Reduction** `r` is in dB and never negative; the gain applied is `g = 10^(−r/20)`. Upward's lift
  is a negative reduction (its NOTES).
- **Time constant** of a one-pole `s[n] = α·s[n−1] + (1−α)·u[n]` is τ with `α = e^(−1/(τ·fs))`, the
  time to reach 1 − 1/e = 63.2 % of a step (Giannoulis 2012, eqs. 5–7; **source**). The 10–90 %
  rise of the same filter is τ·ln 9 = 2.20 τ (eq. 8; **source**).
- **Sample rate:** the whole side chain runs at the host rate (`PLAN.md`, *Decisions*).
  Coefficients are recomputed from times and the rate, never stored as numbers.
- **Precision:** detector states, coefficients and the gain computer in double; the audio path
  multiplies in float (as Halite).

## The side chain, in order

```
key ─ key EQ ─ stereo matrix ─┬─ level (peak |k|, RMS m) ─ look-ahead window max ─ gain computer
                              │        ─ link ─ Range ─ ballistics ─ r → g
                              └─ crest-factor detectors (models with a crest-based Auto release)

feedback models: window max ─ (− previous reduction) ─ inverted gain computer ─ link ─ ballistics ─ Range ─┐
                                        ▲                                                                   │
                                        └───────────────────────────── one sample ─────────────────────────┘
```

The audio is delayed by the constant latency; the key is delayed by that latency minus Look-ahead
(*Look-ahead*). Every stage is per detector channel (*Stereo*) until the link.

## Key path and key EQ

- **Key:** Internal takes the plug-in's input before Character (Pre placement does not reach the
  key); External takes the side-chain bus. A mono key feeds both detector channels; a mono
  main bus with a stereo key sums the key to mono, (L + R)/2 (**Basalt's design**).
- **The key is taken sane** (M1, review): a NaN or an infinity reads as 0 and the key is clamped to
  ±10⁶ (+120 dBFS, so its square stays finite) before the key EQ, for every source; the audio and
  the dry path pass whatever comes in (`saneKey`, Halite's `ShapeLane::sane`). Measured before the
  rule (the review, through the processor, defaults with Threshold −30): one +∞ sample crashed the
  host (C² = ∞/∞ = NaN, the h table indexed with NaN); one NaN left the RMS half and, with a key
  EQ stage on, all compression off for good (+9.0 and +19.5 dB over the clean render a second
  later); +∞ with Auto release off left the output NaN for good, even at Ratio 1. `releaseRatio`
  also reads a NaN as its floor, so no model can index the table with one.
- **Key EQ** (**Basalt's design**; `PLAN.md` gives ranges, not slopes): `myzel::dsp::TptSvf`
  (`common/dsp/Filters.h`, double), in this order:
  - **High-pass** 20 Hz … 2 kHz, 12 dB/octave, Butterworth (one SVF, Q = 1/√2). The lowest
    position is Off: the stage is skipped, not set to 20 Hz.
  - **Low-pass** 2 … 20 kHz, 12 dB/octave, Butterworth. The highest position is Off. At host rates
    where 20 kHz exceeds 0.49·fs the `prewarp` clamp applies; Off is still a skip.
  - **Bell** 40 Hz … 16 kHz, ±18 dB, Q 0.3 … 6: an SVF set with `q = Q·A` and output
    `x + k·(A² − 1)·bp`, A = 10^(gain/40), k = 1/(Q·A) (Simper's mixing). At 0 dB it is skipped.
  12 dB/octave is the common side-chain filter slope and keeps the key's phase shift small near
  the corner; a steeper filter is a later option, not a frozen value.
- **Listen › Key** plays the key after the key EQ (both detector channels as L/R; for Mid, Side
  and Mid/Side, the encoded M and S as L/R).
- **Key EQ changes glide** (M1, *Implementation (M1)*): frequencies and Q in log, the bell's gain
  in dB, over 20 ms on a C¹ cubic, the SVF coefficients following per sample while they move; a
  pass filter switched on or off crossfades between the filtered and the plain key over the same
  20 ms (from rest when it comes on). A jump that left the coefficients to the next block measured
  −48.1 dB on the click gate at Attack 10 ms (*Key EQ jumps*, below).

## Stereo

Detector channels and gain channels per `stereo` mode (**Basalt's design**):

| Mode | Detector channels | Gains applied |
|---|---|---|
| Linked | L, R; reductions linked by Link (below) | g_L to L, g_R to R |
| Unlinked | L, R independent | g_L, g_R |
| Mid | M = (L + R)/2 | g_M to M, S untouched; L = M + S, R = M − S |
| Side | S = (L − R)/2 | g_S to S, M untouched |
| Mid/Side | M and S independent | g_M to M, g_S to S |

- **Link** (Linked only; dimmed otherwise) blends each channel's static reduction with the louder
  channel's, in the log domain after the gain computer: `r_c = (1 − λ)·r_c + λ·max(r_L, r_R)`,
  λ = Link/100. Because the gain computer is monotonic, the max of the reductions is the
  reduction of the louder channel: the console's "stereo dominant" at 100 %
  (`../models/vca/NOTES.md`). Link 0 % is Unlinked exactly. A Link change fades with the curve
  (*Clicks*).
- **Bit-identical gains at Link 100 %:** both channels feed identical reductions to identical
  ballistics states, so from a reset (and whenever the states are equal) the gains are equal to
  the bit. Every per-channel quantity that steers ballistics (an Auto release's crest factor, the
  opto's light history) is computed once from the linked pair in Linked mode, never per channel,
  or the gains would differ.
- **Feedback models** link inside the loop: each channel's demand is computed from its own output
  estimate, then linked as above; at Link 100 % both channels see the same state, so the estimate
  stays consistent.
- **Mid/Side idle:** when every applied gain is exactly 1.0 the matrix is skipped and the output is
  the input (*Idle*); the float round trip M + S is not exact in general, so the short circuit is
  what the Stereo gate's "M/S round trip exact at idle" tests.

## Level detection

### The detector taxonomy (Giannoulis 2012)

Equation numbers are Giannoulis 2012's; `x_L` is the detector's input, `y_L` its output, α_A,
α_R from eq. 7. All rows **source**, the last column as the paper reports it.

| Detector | Equation | Behaviour and artefacts |
|---|---|---|
| RMS | eq. 10, one-pole on the square | equivalent to a one-pole on the level save a scaling of τ (Abel & Berners via §2.3.1). Basalt's RMS reads `10·log10(2m)` (*Conventions*) |
| Analog peak (RC charge and discharge) | eqs. 11–12 | the peak estimate converges to about τ_R/(τ_R + τ_A) of the true peak (eq. 13), and a short release also speeds the attack: correct only when τ_R ≫ τ_A |
| Decoupled | eq. 14: instant-attack peak with release τ_R, then a one-pole τ_A | level-correct, but the attack shapes the release: measured release ≈ τ_A + τ_R |
| Branching | eq. 15: charge with α_A while the input exceeds the state, else discharge with α_R | the release is the intended τ_R; on a plateau the release stops abruptly |
| Smooth branching | eq. 16: the release branch relaxes to the input instead of to zero | uses the full release, but the slope of the envelope is discontinuous when the branch switches (Fig. 8 inset) |
| Smooth decoupled | eq. 17: eq. 14 with the release relaxing to the input | continuous envelope and full release. Smoothing lowers THD, and the decoupled forms distort slightly less than the branching ones (Fig. 9); at high modulation rates its effective ratio sits further from the set ratio than the non-smooth decoupled form's (Fig. 10) |

Placement (§3.2, **source**): in the linear domain before the gain computer (eq. 20) the
envelope lags the attack and is discontinuous at the threshold; biasing it at the threshold
(eq. 21) fails with a soft knee; in the linear domain after the gain computer (eq. 22) the release
is a constant rate in dB, longer for deeper reduction. **In the log domain after the gain computer
(eq. 23) the detector smooths the reduction itself; the release is exponential in dB and its time
does not depend on the depth.** The paper recommends feed-forward, the log-domain placement and
the smooth decoupled detector (§5).

### What Basalt computes

Per detector channel, per sample (**Basalt's design**, on the above):

- **Peak level** `L_P[n] = 20·log10|k[n]|` (instantaneous; the hold comes later).
- **RMS level** `L_R[n] = 10·log10(2·m[n])`, `m[n] = β·m[n−1] + (1 − β)·k²[n]`, β from the
  model's RMS time constant.
- Levels below −200 dB are floored at −200 dB (no log of zero; far below any Threshold).
- A model's detector is a choice of these plus its ballistics; a model with a physical cell (the
  opto) computes the cell's state from the key instead and documents it.

## Look-ahead

`PLAN.md`, *Latency*: the latency is constant, and Look-ahead only chooses how early the detector
sees the audio.

- **Delay:** the audio is delayed by D = 10 ms (plus Character's integer delay) in Normal. The key
  is delayed by D − LA, so the detector at output time n sees the audio sample that leaves at
  n + LA. Live: D = 0, LA = 0, Look-ahead dimmed.
- **The window:** the detector does not just see the advanced sample; it sees the **whole window**
  between now and LA ahead. Each level signal (peak magnitude, RMS mean square) passes a running
  maximum over the last LA·fs + 1 key samples before the gain computer (**Basalt's design**).
  Since the gain computer is monotonic this equals the maximum reduction the upcoming window
  needs. Consequences, measured below:
  - the attack starts LA before the event and has the same shape as with LA = 0;
  - the release starts when the event has left the audio, not LA early (an advanced detector
    without the window releases LA too soon and under-compresses the end of every loud note).
- **Running maximum:** the monotonic wedge of Lemire 2006 (amortised under three comparisons per
  sample), in a ring sized in `prepare()` for 15 ms at the actual rate (`windowCapacity()`: 10 ms
  of look-ahead plus the feedback hold, *Feedback*), never resized on the audio thread. With
  LA = 0 and no hold the window is one sample, which passes each value through (M1 keeps pushing
  it, so a Look-ahead change finds the wedge current).
- **Changing Look-ahead** glides the key tap to its new position over 50 ms (linear, fractional
  read with linear interpolation on the key only), and the window length follows the glide
  (**Basalt's design**). The audio never moves. A jump would hand the gain computer a level step
  that a fast Attack passes as a gain step (*Clicks*).
- **The tap settles on whole samples** (M1): the target is round(LA·fs), within half a sample of
  the knob (11 µs at 44.1 kHz). A settled fractional tap reads the key through the linear
  interpolator, a low-pass that nulls a Nyquist-rate key at half a sample: measured (M1),
  Look-ahead 3 ms at 44.1 kHz (132.3 samples) read the carrier 5.46 dB low and moved the release
  side of the shift gate by 5.46 dB; on a 10 kHz key the same tap reads −1.9 dB (computed).

Measured (simulation): a 1 kHz tone burst 15 dB over Threshold, 200 ms long, feedback loop of
*Feedback* (Ratio 4, Attack 5 ms, Release 100 ms), audio delayed by 10 ms, LA = 0 and 3 ms:

| | LA = 0 | LA = 3 ms |
|---|---|---|
| attack starts, relative to the audio onset | +0.04 ms | −2.96 ms |
| attack-side gain vs the LA = 0 gain shifted by 3 ms | | identical (max difference 0 dB, double) |
| release starts, relative to the audio end | +4.92 ms (the 5 ms hold) | +4.92 ms |
| release-side gain vs LA = 0 | | identical (0 dB) |

The same holds by construction for the feed-forward models: the window acts on the level before
any state, so it changes only *when* the gain computer sees a level, never the ballistics.

## Gain computer

### The static curve

Giannoulis 2012, eq. 4 (**source**), threshold T, ratio R, knee width W, all in dB, input level
x on the detector scale:

```
G(x) = x                                   if 2(x − T) < −W
       x + (1/R − 1)·(x − T + W/2)² / (2W)  if 2|x − T| ≤ W
       T + (x − T)/R                       if 2(x − T) > W
```

- The reduction is `r(x) = min(x − G(x), Range)`.
- W = 0 is the hard knee (eq. 3). R = ∞ (Ratio's top step) uses 1/R = 0 exactly
  (`Parameters.h`, `inverseRatio`); the curve is then flat above the knee. Ratio 1 gives r ≡ 0
  exactly (1/R − 1 = 0), which the idle short circuit relies on.
- **Knob mapping:** Threshold → T, Ratio → R (feed-forward: any value, ∞ included), Knee → W
  (0 … 24 dB, centred on T as in eq. 4), Range → the clamp. A model may reshape the curve (the
  vari-mu's rising ratio, Upward's mirror); its NOTES give its formula and how these four knobs
  enter it.
- **Local slope** for the transfer-curve display and the feedback compensation:
  dG/dx = 1 below the knee, `1 + (1/R − 1)(x − T + W/2)/W` inside it, 1/R above.

### The feedback form

A feedback model computes its reduction from the output level y. For the closed loop to have
exactly the static curve G (eq. 4) in terms of the input, the feedback computer is
`r_fb(y) = G⁻¹(y) − y` (**Basalt's design**, derived here; for the hard knee it is Giannoulis's
eq. 18, `(R − 1)(y − T)` above threshold, and the loop gain R − 1 is Floru's k, ratio = k + 1,
eqs. 45–46, **source**). G is strictly increasing for finite R, so it inverts in closed form:

```
G⁻¹(y) = y                                  if y ≤ T − W/2
         T − W/2 + (−1 + √(1 + 4a·v)) / (2a)  if T − W/2 < y ≤ T + W/(2R),
                                              a = (1/R − 1)/(2W), v = y − T + W/2
         T + R·(y − T)                       above
```

At the knee's top 1 + 4a·v = 1/R² > 0, so the root is always real (inference, checked
numerically). With a constant gain the loop then settles on eq. 4 exactly, knee included
(measured below).

## Range

Range (0 … 40 dB) caps the reduction (or Upward's lift). **Where** it sits matters (**Basalt's
design**, measured):

- **Feed-forward:** on the static reduction, before the ballistics: `r = min(r(x), Range)`. The
  ballistics then smooth into and out of the cap, and the static curve is `min(x − G(x), Range)`.
- **Feedback:** on the detector state after each update, `s = min(s, Range)`, not on the loop's
  demand. The static curve is the same, `min(x − G(x), Range)` (inference: when r(x) > Range the
  loop rests at s = Range with y = x − Range, and the demand there exceeds Range, so the cap holds).
  Clamping the demand instead saturates the loop on every large overshoot: measured (simulation,
  constant-magnitude step, Attack 5 ms, Range 40), the attack took **3.98×** the set time at
  Ratio 20 (12 dB over), **10.3×** at 50, **31.3×** at 50 with 30 dB over; clamping the state:
  **1.000×** in all four.
- Range 0 means no reduction at all: the gain is exactly 1 and the idle short circuit applies.

## Ballistics

### What the knobs mean: the measurement definition

The knobs mean *measured* times, the same on every model (`PLAN.md`, *Feedback models*). This is
the definition the Ballistics gate uses unless a model's NOTES replaces it for that model
(**Basalt's design**):

- **Signal:** a constant-magnitude carrier, ±A alternating every sample (the Nyquist square).
  |k| and k² are constant, so neither a peak nor an RMS detector ripples, and every detector
  sees a clean level step. DC-free. Its level is the detector's own reading (*Conventions*: an
  RMS detector reads it 3.01 dB above its amplitude), and D is set on that reading. Models whose
  key path is band-limited by design (none in v1) use a 1 kHz sine and measure on the gain's
  per-period maximum.
- **What is measured:** the reduction r[n] in dB as the engine applies it (BasaltMeasure reads the
  gain trace, not the audio). Knee 0 dB, Range 40, Look-ahead 0, Auto release off unless the test
  is about Auto.
- **Attack:** the carrier steps from digital silence (fully released, r = 0) to a level whose
  static reduction is D = 10 dB. Attack time = time from the step until r first reaches
  (1 − 1/e)·D_final = 63.2 % of the settled reduction, D_final read after settling.
- **Release:** after the reduction has settled at D (held ≥ 5 × (Attack + Release), and at least
  10 s for a model with light or charge history), the carrier steps to digital silence. Release
  time = time from the step until r first falls to D/e = 36.8 % of D.
- **Time origin and interpolation:** the step is applied at input sample n0. The output sample
  produced from input n0 + j sits at t = (j + 1)/fs, and the state before the step at t = 0
  (r = 0 for an attack, r = D for a release). Each crossing is interpolated linearly between the
  two samples around it, the first one between t = 0 and t = 1/fs. A one-pole of τ then reads τ to
  the interpolation's error: within 0.7 % from 4 samples up, 2.1 % from 2 samples, up to 11 %
  below (computed; 1.06 × at 0.02 ms and 44.1 kHz, 1.02 × at 48 kHz). Every M0 table used this
  convention. Counting the first output sample as t = 0 instead reads one sample short (0.077 ms
  for a 0.1 ms one-pole at 44.1 kHz).
- **Why 63 %:** it is the time constant itself (eq. 7), so a one-pole with τ measures τ, and it is
  independent of D for an exponential in dB, which is what a log-domain detector produces (§3.2).
  A 10–90 % definition would read 2.20 τ for the same filter (eq. 8); the MathWorks Audio Toolbox
  uses that one, so numbers from other tools are not comparable without the factor.
- **Gate:** within ±10 % of the knob over the Attack range 0.02 … 300 ms and the Release range
  5 ms … 5 s on a log grid (PLAN), or within ±0.25 sample where that is wider (times under
  2.5 samples: the interpolation's error, above). Release shorter than the model's minimum
  (below) is measured against that minimum.
- **Program-dependent and level-dependent behaviour** (a feedback model's knee region, VCA RMS's
  depth-dependent attack, the opto's history) is tested by each model's own rows, not by this
  definition.

### Feed-forward ballistics: smooth decoupled, calibrated

The default feed-forward ballistics are eq. 17 in the log domain after the gain computer (eq. 23):
`y1 = max(r, α_R·y1 + (1 − α_R)·r)`, `y_L = α_A·y_L + (1 − α_A)·y1`, reduction = y_L
(**source** for the structure; Giannoulis 2013 uses the same, eq. 5).

- **Attack** is exact: the instant-attack first stage passes a step at once, and the second stage
  is the one-pole τ_A. Measured (simulation, peak path only, every D and R tried): measured/set =
  1.000 from 1 ms up, 1.003 at 0.1 ms, 1.018 at 0.02 ms (under one sample).
- **Release** is the two-pole cascade τ_R then τ_A, so the measured release is longer than τ_R
  (Giannoulis 2012: ≈ τ_A + τ_R; Giannoulis 2013 subtracts τ_A, eq. 8). Basalt solves it exactly
  instead (**Basalt's design**): with ρ = Release / τ_A, the internal release constant is
  `τ_R = τ_A · h(ρ)`, where h solves "the cascade's step response falls to 1/e at ρ". The
  closed form of the cascade's remaining fraction is
  `(τ_R·e^(−t/τ_R) − τ_A·e^(−t/τ_A)) / (τ_R − τ_A)`. Measured (simulation):

  | ρ = Release/τ_A | 1.01 | 1.1 | 1.25 | 1.5 | 2 | 3 | 5 | 10 | 30 | 100 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | h(ρ) = τ_R/τ_A | 0.0100 | 0.0952 | 0.2235 | 0.4287 | 0.8652 | 1.8294 | 3.8676 | 8.9397 | 28.982 | 98.995 |

  Discrete check (eq. 17 at 48 kHz with the solved τ_R): 149.99 ms for 150 ms at Attack 10 ms,
  4999.99 ms for 5 s at Attack 300 ms, 11.99 ms for 12 ms at Attack 10 ms. The paper's
  subtraction (τ_R = Release − τ_A) reads up to **7.3 % long** (at ρ = 2; 5.7 % at ρ = 1.5 and 3,
  0.6 % at ρ = 10). h is tabulated (M1: h/(ρ − 1) on 256 points over log ρ from 1 to 10⁴, linear
  interpolation, *Implementation (M1)*; above 10⁴, h = ρ − 1) and evaluated when Attack, Release or the rate changes, or per sample when
  an Auto release moves the target.
- **Minimum release:** the cascade cannot release faster than τ_A (h → 0). The floor is
  ρ = 1.01, a release of 1.01·τ_A: 1.01 × Attack where τ_A = Attack (a model that scales τ_A, as
  Clean's κ, states its own figure). Giannoulis 2012 notes the same guarantee ("the
  release time can never be shorter than the attack time", §2.3.3, **source**). The gate checks
  those settings against 1.01·τ_A.
- **A caution kept for the gates:** when τ_R → 0 the first stage stops holding peaks, and on a
  sine the second stage averages the waveform's reduction instead of its peak (the feedback
  simulation's variant SD below shows −1.3 dB at Ratio 20 when this happens). The static gate
  therefore runs with Release 1 s, and settings with Release ≤ 2 × Attack are reported, not
  gated, for static accuracy.

### Feedback ballistics

See *Feedback*: smooth branching (eq. 16) inside the loop, the attack coefficient scaled by the
set ratio, the release as set less a short hold.

## Feedback

`PLAN.md`, *Feedback models*, asked M0 to check the implementation by simulation. Done; the
recommended loop is specified first, then the evidence.

### The loop (Basalt's design)

Per detector channel, per sample, s the reduction state (dB, ≥ 0):

1. **Pre-hold on the key** (outside the loop): the peak level `L_w[n]` = running maximum of |k|
   over H = h + LA samples (*Look-ahead*), h = min(Release/10, 5 ms) at the host rate. A model
   whose detector is RMS-type uses its RMS level here instead and may set h = 0.
2. **Output estimate** (Giannoulis 2012, §3.1.1, **source** for the idea): the current key level
   with the previous gain applied, `y = L_w[n] − s[n−1]`. It works the same with an external key
   (the estimate is then the key's level after the gain), which is what §3.1.1 points out.
3. **Demand** `c = r_fb(y) = G⁻¹(y) − y` (*The feedback form*); linked across channels as in
   *Stereo*.
4. **Smooth branching** (eq. 16): if c > s, `s = α_A'·s + (1 − α_A')·c`; else
   `s = α_R'·s + (1 − α_R')·c`; then `s = min(s, Range)`.
5. **Coefficients:** `1 − α_A' = (1 − e^(−1/(Attack·fs))) / R_set` (Floru's division by the
   ratio undone, so the closed-loop attack pole is e^(−1/(Attack·fs)) at the set ratio);
   `α_R' = e^(−1/((Release − h)·fs))`, uncompensated. R_set is the set ratio, capped (below).

Why this shape, from the simulation:

- **Release uncompensated:** when the input falls below threshold the estimate falls below the
  knee, the demand is 0 and the loop is open, so the release is the detector's own constant. Any
  compensation by the ratio would make the release R times too slow there.
- **Branching, not decoupled:** a decoupled detector puts a peak hold on the loop's *demand*, and
  the demand overshoots by the loop gain at every onset (from a released state, (R − 1) times the
  overshoot). Holding it latched hundreds of dB of reduction (variant DH below). Decoupled with
  the τ_A stage compensated instead (variant SD) drags R·Attack into every release.
- **Pre-hold, outside the loop:** smooth branching alone under-reads sines (the eq. 13 level
  problem, closed-loop: −0.7 … −1.6 dB, variant SB). Holding the key's peak over h before the loop
  makes the static curve exact on sines above 1/(2h), and costs h of release delay, which step 5
  subtracts.

### Results (measured, simulation)

Set-up: 48 kHz, double precision, T = −30 dB, hard knee unless stated, the loop above or the
named variant; static error = steady mean reduction − eq. 4's reduction at the sine's peak level,
12 dB over threshold, in dB (ripple peak-to-peak in brackets).

**Static curve, recommended loop (pre-hold + smooth branching):**

| Attack, Release | 50 Hz, R = 4 / 20 / 50 / 100 | 100 Hz, R = 4 … 100 | 1 kHz, R = 4 … 100 |
|---|---|---|---|
| 0.02 ms, 50 ms | −0.06 / −0.14 / −0.16 / −0.18 (0.3 … 0.8) | 0.000 | 0.000 |
| 0.8 ms, 50 ms | −0.08 / −0.19 / −0.22 / −0.24 (0.3 … 0.8) | 0.000 | 0.000 |
| 1 ms, 150 ms | −0.03 / −0.08 / −0.09 / −0.10 | 0.000 | 0.000 |
| 10 ms, 150 ms | −0.14 / −0.33 / −0.37 / −0.39 | 0.000 | 0.000 |
| 30 ms, 1 s | −0.07 / −0.17 / −0.20 / −0.21 | 0.000 | 0.000 |

On the constant-magnitude carrier the error is 0.0000 dB for every variant and ratio tried up to
100, knee 0 and 6 dB: **the loop reaches eq. 4 exactly**, the ratio being k + 1 as Floru and
eq. 18 say. Below 1/(2h) (100 Hz at h = 5 ms) the gain moves within the cycle and the steady
reduction under-reads; that in-cycle modulation is the feedback models' sound at fast settings
and is reported, not gated.

**The variants rejected** (same set-up, 1 kHz unless stated):

| Variant | Static error | Ballistics |
|---|---|---|
| SD: smooth decoupled, τ_A × R, release solved with h(ρ) | −1.33 dB at R = 20, Attack 10 ms, Release 150 ms (R·Attack > Release, so the hold collapses); +1.3 … +5.1 dB at Attack 0.02 ms, Release 5 ms, R ≥ 20 | release never shorter than R·Attack |
| SB: smooth branching, no pre-hold | −0.68 (R 2) … −1.65 dB (R 100) at Attack 10 ms, Release 150 ms | as recommended |
| DH: decoupled hold on the demand | ≤ 0.005 dB at slow settings | a step from released latches the overshooting demand: 390 … 1900 dB transient reduction at R 20 … 200 |

**Ballistics, recommended loop** (constant-magnitude carrier, Attack 5 ms, Release 200 ms, the
*Ballistics* definition, D = 10 dB):

| Ratio | Attack measured/set | Overshoot | Release (to silence)/set | Partial release 24 → 6 dB over, /set |
|---|---|---|---|---|
| 1.5 | 1.000 | 0.000 dB | 1.000 | 0.692 |
| 2 … 50 | 1.000 | 0.000 dB | 1.000 | 0.652 |
| knee 6 dB, 2 … 50 | 1.000 | 0.000 dB | 1.000 | 0.558 … 0.678 |

- **Floru's law, checked:** with the attack coefficient *not* compensated, the measured attack is
  0.4990, 0.2484, 0.1232, 0.0480 of the detector's constant at R = 2, 4, 8, 20 (1/R = 0.5, 0.25,
  0.125, 0.05). The loop divides the time constant by the ratio while it is closed.
- **Release:** to below threshold the loop is open and the release is the detector's (1.000). A
  partial release (the level drops but stays over) runs open-loop until the estimate meets the
  knee, then closes and snaps to the new value: about 0.65 of the set time to 63 %, with no
  exponential tail. This is the level-dependent time constant of Abel & Berners (abstract) in
  Basalt's loop, and it stays as the feedback models' character.
- **Knee region:** the compensation uses the set ratio, so where the curve's local slope is
  lower (inside the knee) the closed loop is slower than the set Attack. Kept as character; the
  Ballistics gate measures at Knee 0, and the knee-region behaviour is reported.

**Stability** (44.1 kHz, Attack 0.02 ms, Release 5 and 50 ms, steps 30 dB over and partial
release 30 → 10 dB over): no overshoot, no oscillation up to R = 1000 (largest undershoot −0.004 dB
at R = 1000). The attack pole is e^(−1/(Attack·fs)) at any ratio by construction. In the release
branch while the loop is closed the pole is 1 − (1 − α_R')·R, negative beyond R ≈ (Release − h)·fs
(198 at 5 ms, 44.1 kHz); the branch switching kept it bounded in every case tried. On white noise
(−10 dBFS, T −30, Release 5 ms) the sign changes of the gain's slope rise from 3.6 % of samples
(R 20), 3.7 % (R 50) and 3.8 % (R 100) to 5.5 % (R 200, 400): the loop starts to chatter above 100.

### The ratio cap

**Recommended: feedback models cap Ratio at 50**, and the ∞ position means 50 on the curve and in
the display (**Basalt's design**). Reasons:

- the static curve is exact (constant magnitude) and within 0.000 dB on sines ≥ 100 Hz to R = 100,
  so accuracy does not set the cap;
- at 50 the curve is limiter-like (20 dB over → 0.4 dB out) and the loop is as quiet on noise as at
  20; above 100 it chatters (above), and above ≈ 200 the closed release branch is past its
  monotone limit at the shortest Release and 44.1 kHz;
- a cap at 20 would make the ∞ position identical to the 20 position.

A feedback model's NOTES may set a lower cap (the FET family's 20:1 and all-buttons). `PLAN.md`
says ∞:1 needs infinite loop gain (DN01A, Floru, Giannoulis); in this loop R = ∞ with the
compensation turns the attack branch into an integrator that does reach y = T, but the release
branch near equilibrium then chatters at the rate of the release coefficient, so ∞ is not offered.

### Look-ahead in feedback models

**Kept.** The loop runs on the advanced, windowed key exactly as without look-ahead; only the
audio delay differs. Measured (simulation, *Look-ahead* above): the attack side is the LA = 0
trajectory shifted by LA, identical in double precision, and the release side is identical in
audio time. The output estimate is then not the true output but the key with the previous gain,
LA ahead; the static curve is unchanged (it does not depend on time) and so are the measured
ballistics (they are measured relative to the key's step). `PLAN.md`'s fallback (dim Look-ahead
like Live) is not needed.

### What a feedback model's NOTES choose

- the level fed to the pre-hold (peak, RMS, a cell) and h (0 … 5 ms; h = 0 keeps more in-cycle
  modulation and moves the static gate's lowest frequency up);
- its cap (≤ 50);
- a model wanting Floru's RMS-in-the-loop (the RMS time constant divided by the ratio) places the
  RMS on the estimated output `|k|·10^(−s/20)` inside the loop instead of on the key; Giannoulis's
  §3.1.1 literally applies the previous gain to the input sample, so this is the paper's form.
  Static accuracy and ballistics must then be re-simulated in that model's NOTES.

## Make-up and Auto Gain

- **Gain** (−24 … +24 dB) is added after the dynamics, on the wet path.
- **Auto Gain** (`PLAN.md`, *Auto Gain*): the static make-up `M = r_static(L_ref)`, the model's
  static reduction at the reference level L_ref = −18 dB on the detector scale (a sine peaking at
  −18 dBFS), with the current Threshold, Ratio, Knee and Range; Gain adds to it. For eq. 4,
  M = 0 when T ≥ L_ref + W/2. Feedback models use the same formula, since their static curve is
  eq. 4 exactly. A model whose curve changes with level gives its own r_static.
- `PLAN.md` says "−18 dBFS RMS"; on the detector scale that is a sine peaking at −18 dBFS (plain
  RMS would put it at −15 dBFS peak). M3 confirms the level against the corpus (PLAN).
- Auto Gain changes are smoothed like Gain (*Clicks*).

## Idle and the exact 1.0

- The reduction is snapped to exactly 0 when it falls below 1·10⁻⁶ dB (a linear deviation of
  1.2·10⁻⁷, −139 dB) and its target is below it too (*Implementation (M1)*), and a gain of exactly
  0 dB short-circuits to 1.0 (`PLAN.md`, *Signal flow*).
  The snap is below the click gate by 79 dB (inference).
- The gain computer gives exactly 0 below T − W/2, at Ratio 1 and at Range 0, so the identity gate
  needs no tolerance.
- Detector states are flushed to zero below 10⁻³⁰ (denormals).

## Clicks

`PLAN.md`'s click gate (no step artefact above −60 dB, Halite's method) for the core
(**Basalt's design**, revised and measured in M1, *Implementation (M1)*):

- **Threshold, Ratio, Knee, Range and Link** fade over 20 ms (`CurveFade`): a change starts a
  second set of ballistics for the new setting from the state of the running one, both follow
  their own static target, and the two reductions are blended on a raised cosine; at the end the
  new set carries on alone. A change during a fade waits for its end, the latest winning. The
  first design, a linear 20 ms ramp of the four parameters, measured −37 to −54 dB at Attack
  0.02 ms: the ramp's corners, Range's clamp and the knee meet a 20 µs attack as corners in the
  gain, and so does the instant first stage of the decoupled detector wherever a rising target
  meets a hold that is still releasing. Blending after the ballistics leaves only the raised
  cosine's own curvature. Link joined the fade after the review (M1): changed at once it was not
  smoothed anywhere (−44.7 dB at Attack 10 ms on a one-sided key, the review), and glided before
  the ballistics it still met a releasing hold on the quieter channel (−53.8 dB at Attack
  0.02 ms); in the fade it reads −72.3 dB. A feedback model (M2) runs two loop states the same
  way (`FeedbackLoop` is copyable for it).
- **Continuous automation** of the faded knobs (measured in the M1 review with a standalone
  driver on `CleanModel` against a fade-free reference given the same per-block Threshold: a
  −20 dBFS carrier, Ratio 4, hard knee, Release 150 ms, Threshold set once per 64 samples): the
  fades follow 12–19 ms late, and the reduction moves in 20 ms raised-cosine segments. A ±10 dB
  Threshold LFO at 0.5 and 2 Hz deviates by up to 0.52 and 2.06 dB at Attack 0.02 ms (0.36 and
  1.44 dB at Attack 10 ms), ±3 dB at 8 Hz by 1.28 dB; aligned by the best shift (16–19 ms) the
  deviation is 0.14–0.44 dB. No click: the reduction's largest spectral line between 30 Hz and
  1 kHz is 0.047 dB (48–50 Hz), and the gain's third difference stays at or below −123 dB. If M3's
  listening finds automation sluggish, a shorter fade while automation is continuous is the
  option.
- **Attack, Release and Auto release** glide in ln(ms) over 20 ms (`SmoothGlide`, `timeGlideMs`),
  every coefficient that follows them (Clean: b, κ, τ_A, τ_RMS, δ, the release poles; Auto's
  factor as factor^w, w gliding from 0 to 1) recomputed per sample while they move. Changed at
  once, they completed a pending attack within a sample: Attack 300 → 0.02 ms in mid-attack read
  −27.2 dB (Clean's b also moves the target there); Release changed at once in mid-release reads
  −58.2 dB at Attack 0.02 ms (the click gate's release rows, *Through the processor*).
- **Gain, Output, Auto Gain's make-up:** two 8 ms one-poles, as Halite's Output.
- **Look-ahead:** the 50 ms key-tap glide (*Look-ahead*).
- **Key EQ:** the 20 ms glides of *Key path and key EQ*. At Attack 0.02 ms a key EQ move still
  reaches the gain as the attack it causes (the gain follows its key); the click gate reports those
  rows there and gates them at 10 ms.
- **Stereo mode, key source, model:** the 30 ms crossfade of `PLAN.md` with both paths running.
- **The incoming path's state** on a model or VCA detector switch (revised after the M1 review):
  only the model heard runs. The incoming one starts from rest and follows the key unheard for
  its `settleMs` plus the look-ahead window (*Model interface*); its one-pole level states (the
  RMS mean squares, the crest detectors' mean squares) divide out their start from rest
  (`WarmUpWeight`: after n samples a one-pole holds 1 − αⁿ of a steady level), so they read the key
  as if they had always run. The incoming ballistics state that carries the reduction (y_L, the
  loop's s, VCA RMS's y) is then seeded with the reduction applied, and the two crossfade; a model
  whose state is not in dB seeds it through its own law (the Opto's conductance,
  G = e^(r/(q·w)) − 1, in its NOTES). Memories (VCA Auto's C₂, the Vari-mu and Diode bridge
  memories, the Opto's S) start empty. Without the seed, a slow Attack lets the reduction fall
  during the fade and re-attack over Attack: a swell of the full reduction at Attack 300 ms
  (inference). Measured (M1, `[chain][switch]`): two Clean engines in one lane, Attack 300 ms,
  Auto release on, a carrier 16 dB over Threshold; switched mid-attack after its 375 ms warm-up,
  the incoming engine stays within 8.3·10⁻⁶ dB of one that ran throughout. The first M1 design kept
  every engine's key-only states running all the time; that cost a whole follower per engine
  (Clean's `follow()` measured 0.09–0.17 % of a core in the review, so M2's six would take 0.5 %),
  and it is gone. The click gate runs every switch at Attack 10 and 300 ms.

## Cost

Per detector channel and sample: one log10 (peak) or one log10 of the mean square, the eq. 4
branch (one square), two one-pole updates, the window's amortised < 3 comparisons, one exp10 for
the gain; a feedback model adds the inverse curve (one square root inside the knee). h(ρ) is a
table. No allocation: the window ring and every state are sized in `prepare()` at the actual
rate. Measured (M1): Clean with the gain conversion and the matrix, not the chain, 0.23 % (Attack
0.5 ms, Auto off) to 0.29 % (the defaults) of a core (`../models/clean/NOTES.md`, *Measured in
M1*). The whole chain, after the review's changes (only the heard model and the heard key lines
run, the key EQ per chunk, the gain's exp in float): 0.284 % at the defaults and 0.365 % with
look-ahead 10 ms, three key EQ stages and the side chain on (*Through the processor*); `PLAN.md`'s
budget is stated per configuration since (*Changes to PLAN.md*).

## Gate table (the core's share)

What each `PLAN.md` gate checks for the shared core; a model's NOTES add its own rows.

| Gate | Core check |
|---|---|
| Identity | Ratio 1, Range 0, or input below T − W/2: output bit-identical to the input delayed by the reported latency; every stereo mode (the M/S short circuit), Look-ahead 0, 5, 10 ms, Normal and Live, block sizes 1 … 4096 and variable, key Internal and External |
| Static curve | 1 kHz sine and the constant-magnitude carrier (levels on the detector's own reading, *Conventions*), levels T − 20 … T + 30 dB in 1 dB steps; T −60, −30, −10; R 1.5, 2, 4, 10, 20, ∞ (feedback: to the cap); W 0, 6, 24; Range 40 and 10; Attack 10 ms, Release 1 s, Auto off: steady mean reduction within ±0.25 dB of `min(x − G(x), Range)`. Feedback models add 100 Hz (gated) and 50 Hz (reported) |
| Ballistics | the *measurement definition* above, Attack 0.02, 0.1, 1, 10, 100, 300 ms × Release 5, 50, 150, 500 ms, 5 s, R 2, 4, 20 (feed-forward also ∞; feedback to the cap): ±10 %; look-ahead 3 and 10 ms: attack side equals the LA = 0 trace shifted by LA within 10⁻⁶ dB, release side equal in audio time |
| Range | at R ∞ (feedback: cap), 30 dB over, Range 10: steady reduction 10 dB ±0.05; attack time with Range engaged within ±10 % of the unclamped one (the state clamp) |
| Stereo | Link 100 %: g_L and g_R bit-identical over a 10 s stereo excerpt from reset; Link 0 % equals Unlinked to the bit; M/S round trip exact at idle |
| Clicks | the jumps listed under *Clicks*, at Attack 0.02 ms and 10 ms; model and detector switches also at 300 ms |
| Real-time | no allocation, lock or I/O in any of the above (`AllocationTests`) |

## Model interface

`Model.h` (**Basalt's design**, M1). Every model, M2's five and 1.1's three, implements
`dynamics::DynamicsModel` in its own folder (`src/dsp/models/<m>/`); the chain owns the instances,
runs one (two during a 30 ms switch) and never knows which.

- **In:** a `ModelParams` snapshot per block, in the knobs' units (`Curve` with Threshold, 1/Ratio,
  Knee and Range as targets, Attack, Release, Auto release, Floor, the VCA detector, and the link:
  `linked` with λ), and a `KeyBlock`: one or two detector channels of key in double, already through
  the key EQ, the stereo matrix and the look-ahead tap, with the tap's look-ahead per sample (it
  glides), from which a model sizes its own windows.
- **Out:** the reduction in dB per detector channel and sample, positive down, negative for
  Upward's lift. `gainFor()` turns exactly 0 into exactly 1.0f, and `applyGains()` (`Stereo.h`)
  applies the gains through the matrix.
- **Calls:** `prepare(rate)` allocates (windows sized by `windowCapacity()`), `reset()` releases
  everything, `seed(r)` starts the ballistics at the applied reduction (Settled 33), `process()`
  produces, `follow()` runs only the key-only states (mean squares, crest detectors, windows,
  the curve fade and glides) from `reset()` while the model warms up unheard before a switch,
  its one-pole level states normalised for their start from rest (`WarmUpWeight`, *Clicks*);
  `staticReduction(p, level)` gives the settled curve for the display, Auto Gain (`autoGainDb()`)
  and Learn's display, `reduction(c)` the state now, `settleMs(p)` how long `follow()` needs from
  `reset()` before the key-only states read the key (Clean: 5·τ_RMS with the RMS half in, and at
  least 50 ms with Auto release, the crest detectors' mean square reading a 40 Hz period or two;
  default 0), which the chain waits, plus the look-ahead window, before it seeds and fades in a
  model or a lane (*The chain (M1)*). Only the model heard runs otherwise.
- **Rules:** no latency of its own; nothing allocated, locked or logged outside `prepare()`;
  coefficients follow the snapshot without moving the state; Threshold, Ratio, Knee, Range and
  Link through `CurveFade` (a second ballistics state while it fades), Attack, Release and Auto
  release through `SmoothGlide`, and any other level a model reads (Upward's Floor) glides the
  same way, so it never needs a fade of its own; whatever steers the ballistics computed once for
  a linked pair.
- **Still to add:** Learn (M3) replays the model over its capture, per millisecond the peak and
  the mean square per channel (`PLAN.md`, *Learn*), which no key can stand for; M3 adds that
  entry point to the interface for every model, with its gate.
- **The blocks** a model builds from: `GainComputer.h` (eq. 4, slope, inverse, demand, the fade),
  `Glide.h` (the time knobs' and the key EQ's glides, the look-ahead's linear glide),
  `Detectors.h` (RMS, crest factor, the warm-up weight), `RunningMax.h` (look-ahead window,
  pre-hold), `Ballistics.h` (smooth decoupled, h(ρ)), `Feedback.h` (the loop of *Feedback*, steps
  2–5, with the cap and hold helpers; the model brings step 1, and may bring its own demand, the
  reduction its estimate subtracts, and read the linked demand: Vari-mu's rising curve, its
  r = max(s, m, ℓ) and its memories, `../models/varimu/NOTES.md`), `Stereo.h`, `KeyPath.h` (key
  EQ, key delay, `saneKey`).

## Implementation (M1)

Where this file left a choice open, or turned out to need one (**Basalt's design**, M1):

- **The snap waits for the target.** A state snaps to 0 when it *and its target* are under
  10⁻⁶ dB (`snapState`). Snapping the state alone, as *Idle* reads, also caught the first steps of
  a slow attack towards a small reduction and held it at 0 for good: the feedback loop sat at 0
  instead of 0.020 dB 1 dB above the knee's foot at Ratio 50, Knee 24 (Attack 10 ms), and Clean at
  Attack 300 ms did the same for any target under about 0.014 dB. Released detectors still land on
  exactly 0 (Clean: 15–17 × Release after a 20 dB burst, *Measured in M1*).
- **h(ρ) is tabulated as h/(ρ − 1) on 256 points** over log ρ from 1 to 10⁴, linear
  interpolation: within 0.034 % of the bisection solve (`solveReleaseRatio`). 64 points, as
  *Feed-forward ballistics* proposed, read up to 0.54 % off near ρ = 1.25.
- **The look-ahead window grows by at most one sample per sample** and shrinks at once
  (`RunningMax`): the wedge's expiry point then never moves backwards, so the maximum is exact for
  any sequence of lengths, glides included. Its ring is sized in `prepare()` for 15 ms at the
  actual rate (`windowCapacity()`), not a fixed 192 kHz.
- **The look-ahead per sample** reaches the model as the tap's glided value; the window is
  round(LA) + 1 samples (feedback: + the hold).
- **Link 0 % is Unlinked to the bit** with Auto release too: Linked steers its pair's ballistics from
  one detector (Clean's crest factor on max(k_L², k_R²)) only while Link > 0. At 0 % each channel
  steers itself, as Unlinked does. Every crest detector runs while the model is heard, so changing
  Link never starts one cold.
- **A mono bus ignores Stereo:** one detector channel, its gain on the one channel.
- **The feedback inverse** clamps 1/R at 10⁻³ (Ratio 1000) so it never divides by 0; models cap
  far below (*The ratio cap*).
- **Key EQ:** a stage that is Off or at 0 dB is skipped and restarts from rest when it comes back;
  coefficient changes keep the state.
- **dB from log2** (`20·log10(2)·log2 x`), the cheaper libm call; below 10⁻¹⁰ (10⁻²⁰ for 2m) the
  level is exactly −200 dB.
- **Knob changes, from the gates** (M1, after the processor's click and ballistics gates; numbers
  at 48 kHz, Normal and Live alike, the click gate's 3rd-difference crest):

  | Change | Before | After |
  |---|---|---|
  | Threshold −60 ↔ 0 dB, Attack 0.02 ms | −39.1 dB (linear ramp) | −68.9 dB (curve fade) |
  | Ratio 1 ↔ ∞, Attack 0.02 ms | −51.5 dB | −71.0 dB |
  | Range 0 ↔ 40 dB, Attack 0.02 ms | −42.7 dB | −72.3 dB |
  | Attack 0.02 ↔ 300 ms | −27.2 dB (coefficients at once) | −78.3 dB (20 ms glide) |
  | Key LPF 2 kHz ↔ Off, Attack 10 ms | −48.1 dB (per-block coefficients) | −72.3 dB (glides) |
  | Key Bell Freq 40 Hz ↔ 16 kHz, +18 dB, Attack 10 ms | −56.4 dB | −78.8 dB |

  - **Curve fade, not a smoother parameter ramp:** a C¹ ramp of the four parameters would still
    meet Range's clamp and the hard knee as corners, and the decoupled detector's instant first
    stage turns any rising target into a corner where it meets a releasing hold. Rejected: a
    reduction-domain blend before the ballistics (measured −37.3 dB on Threshold at 0.02 ms, the
    hold corner), parameter ramps of any shape, and a queue of fades (three curves at once).
  - **The cost** stays out of the steady state: a block with no fade and no glide runs a loop
    without their checks. Clean at the defaults measured 0.279 % of a core against 0.284 % before
    these changes, alternating the two builds (`BasaltMeasure perf`, 48 kHz, load average 3).
  - **Key EQ:** glides as *Key path and key EQ*; a flat key EQ (every stage off or at 0 dB, at
    rest) is skipped per chunk.
- **From the reviews** (M1, after three independent reviews; numbers at 48 kHz):
  - **The key is taken sane, `releaseRatio` reads NaN as its floor** (*Key path and key EQ*).
  - **Link fades with the curve, Auto release glides** (*Clicks*): Link 0 ↔ 100 % with the
    channels 26 dB apart read −44.7 dB at Attack 10 ms changed at once (the review) and −53.8 dB at
    Attack 0.02 ms glided before the ballistics; in the fade −71.9 and −72.3 dB.
  - **Only the model heard runs; a warm-up normalises its start from rest** (*Clicks*, *Model
    interface*). `follow()` runs from `reset()` only.
  - **The key's magnitude for a model's own level path** (M2): VCA, FET and Vari-mu each read |k|
  and each kept a copy of the same guard; it is `saneMagnitude` in `KeyPath.h` now (NaN as 0,
  past ±10⁶ as 10⁶), the arithmetic unchanged, so the models' tests read the same bits.
- **The gain's exp in float** (`gainFor`): `std::exp` of the float argument, within one float
    step of the double one; 3 % of the chain's cost at the costliest setting.
  - **The key EQ runs a chunk at a time**, its stages fused into one loop over both channels while
    every stage is at rest (the two channels' recursions run side by side), per sample while a
    glide moves; the same arithmetic in the same order per sample, so the output is unchanged.
  - **`FeedbackLoop` takes a model's demand, estimate and linked-demand output** (*Model
    interface*, the blocks).

## The chain (M1)

`../chain/` wires the core into the plug-in along `PLAN.md`'s *Signal flow* (**Basalt's design**,
M1, checked by `BasaltTests "[chain]"`, `tests/ChainTests.cpp`):

- **Delays.** Normal delays the audio by D = round(10 ms · fs) (441 samples at 44.1 kHz, 480 at
  48 kHz) plus Character's integer delay (`characterDelaySamples`, 0 until M3), whatever Look-ahead
  says; Live by 0. That is what the host is told, and the dry path is the same delayed audio, so
  Mix, Bypass and Listen line up. The gain cell multiplies that delayed audio, so the key is read
  at the whole latency less the look-ahead and Listen › Key at the whole latency (review: the key
  had been read at the look-ahead's share alone, which a Character delay would have misaligned).
  M3 decides where Character sits around the gain cell (`../character/NOTES.md`, placement) and
  keeps this rule for what the gain cell sees. Tested with a stand-in Character delay of 76
  samples (`prepare (…, characterDelay)`): every output is the one without it delayed by 76
  samples, to the bit, Listen › Key and Delta included; Live unchanged.
- **Key lines.** One per source (internal: the input, before Character; external: the side chain),
  each the key EQ into a delay line of the whole latency. A line runs only while a lane hears it;
  one that starts again starts from rest, and the lane that switches to it follows the key the
  latency longer, which fills it (review: the side chain's line, EQ included, ran unheard
  whenever the bus was on). The detector reads at the latency − LA(t), LA gliding over 50 ms;
  Listen › Key at the latency. External with the side chain off hears the input; on but silent,
  silence (as Halite, its Settled 143).
- **Lanes.** A lane is one key configuration (source, stereo mode, bus width) with a model per
  engine; only the heard engine runs.
  - **A model or VCA detector switch** stays in the lane: the incoming engine starts from rest,
    follows the key unheard for its `settleMs` plus the look-ahead window (*Clicks*, the incoming
    path's state), is seeded with the applied reduction, and the two reductions are blended in dB
    over a 30 ms raised cosine (`../models/vca/NOTES.md`, *Switching detectors*). A change during
    the warm-up restarts it, or cancels it when it returns to the engine heard; one during the
    blend waits for its end.
  - **A key source or stereo mode switch** starts the other lane. It follows the key unheard for
    the engine's `settleMs` plus the look-ahead window (Clean: 5·τ_RMS, up to 375 ms at Attack
    300 ms; at least 50 ms with Auto release), is then seeded with the applied reduction (channel
    by channel when the channels mean the same, the same mode or Linked ↔ Unlinked; otherwise
    every channel the larger) and the audio crossfades over 30 ms, each lane's gains on its own
    matrix. Measured: Attack 300 ms, a carrier 14 dB over Threshold on both key channels,
    Linked → Unlinked → Mid → Linked: the reduction stays within **0.0003 dB**; seeded without the
    warm-up it dipped **0.37 dB**, the RMS half starting empty.
  - **A lane that comes in Linked starts linked** (review): its seed is the outgoing reductions
    linked, (1 − λ)·r_c + λ·max, so at Link 100 % both channels start on the same reduction and
    their gains are identical to the bit from the moment they are heard. Seeded channel by channel
    instead, Unlinked → Linked left the two channels apart for up to 2.75 s below Threshold (the
    review, on `CleanModel`: 132 223 samples differing). Tested: Unlinked → Linked, the channels
    6 dB apart, then a release below Threshold: 0 samples differ once the lane is heard.
  - One change at a time: a change during a fade waits for it; one during the warm-up restarts or
    cancels it.
- **Decisions fall on the same samples whatever the host's blocks** (review): the chain works in
  chunks of at most 256 samples, takes every decision (a switch, a warm-up's end, a fade's end)
  at a chunk's start, and ends each chunk where the next decision is due. Before, a warm-up
  counted down per chunk, so a switch's seed and fade fell on the first chunk boundary after it,
  which followed the host's blocks (the review: Linked → Mid/Side, outputs at blocks 1 and 4096
  differing by up to 1.06·10⁻³, −59.5 dBFS). Tested: switches of stereo mode, key source, Listen,
  latency, Threshold, Attack and Link at one sample, some during another's warm-up or fade, give
  the same bits at blocks of 1, 7, 448, 1344 and a mixed cycle; with the settings still, at 1 …
  4096 and variable.
- **The dispatch (M2):** an engine per v1 model, the VCA's two detectors as two (`VcaModel` with
  its detector fixed, PLAN.md Settled 73), so a detector switch is the lane's model switch; 1.1's
  three run Clean's engine and the status line notes the model asked for (`ChainStatus`). In M1
  only Clean was built and every model ran on it.
  The in-lane switch is tested on two Clean engines: switched mid-attack at Attack 300 ms after
  its warm-up, the incoming one carries on within 8.3·10⁻⁶ dB of one that ran throughout, and an
  engine not heard does no work at all until it is asked for.
- **After the gain cell**, in order: Gain plus Auto Gain's make-up (two 8 ms poles; the make-up is
  `autoGainDb` of the engine heading in, at the target curve); Mix against the aligned dry path
  (two 8 ms poles); Listen (30 ms crossfade: Output, Key, or **Delta = the aligned input − the gain
  cell's output**, before Gain and Mix); Output (two 8 ms poles); Bypass (a 20 ms raised cosine to
  the aligned input); the latency fade (10 ms out, switch, 10 ms in).
- **The idle identity:** every stage leaves the samples alone at exactly unity (Mix at exactly
  100 %, gains of exactly 1, no fade), so they pass as copies, denormals included; at Mix exactly
  0 % the output is the dry path to the bit.
- **Meters** (`../chain/Meters.h`, for M4): In (the aligned input) and Out per channel, Reduction
  and Lift per detector channel, each the peak since the display last took it, so no block's peak
  is lost between two reads; the status line's model heard, latency and Auto Gain make-up.
- **Cost** (*Through the processor*): 0.284 % of a core at the defaults, 0.365 % at the costliest
  ordinary setting.

**Key EQ jumps at Attack 10 ms (measured, M1):** *Key path* said a key EQ change moves only the
detector's input and the ballistics smooth the consequence. On the click gate's constant-magnitude
key (a Nyquist-rate carrier) that was not enough: Key LPF 2 kHz ↔ Off moved the key by tens of dB
within a sample, and the 10 ms attack turned it into an 11 dB gain move whose onset read
**−48.1 dB**; Bell Freq 40 Hz ↔ 16 kHz at +18 dB **−56.4 dB** (the SVF's transient as its
coefficients jumped); the same EQ held still, −78.9 dB. With the 20 ms glides they read
**−72.3** and **−78.8 dB**. A Nyquist key cannot show the high-pass or the bell (a TPT filter is
exactly 0 dB there), so after the review those rows run on tones inside their bands (*Through the
processor*). At Attack 0.02 ms the gain follows its key by design; those rows are reported, not
gated.

## Measured in M1

**Measured (M1)**, 48 kHz, `BasaltTests_dynamics`:

| What | Result | Test |
|---|---|---|
| eq. 4 against the paper's form, T −60/−30/−10, R 1.5…20, W 0/6/24 | within 10⁻⁹ dB; slope against the numerical derivative within 10⁻⁵ | *Gain computer: eq. 4* |
| G⁻¹(G(x)) − x, R 1 … 1000, W 0 … 24 | worst 7.1·10⁻¹² dB | *the closed-form inverse* |
| h(ρ) against this file's table | the solve within the table's rounding; the 256-point table within 0.034 % of the solve | *h(rho) matches* |
| Smooth decoupled release (unit cascade, Attack 0.1 … 300 ms, Release 5 ms … 5 s) | worst 0.21 % of the knob (or of the 1.01·τ_A floor) | *the cascade releases in the set time* |
| Running maximum, random lengths 1 … 400 and fades, 200 000 samples | exact | *Running maximum* |
| Feedback loop, static, carrier, R 1.5 … 50 and ∞ (cap 50), W 0/6/24, Range 40/10, T − 20 … T + 30 | worst 0.0001 dB (3 s per 1 dB step where the knee slows the loop) | *the loop settles on eq. 4* |
| Feedback loop, static, 1 kHz and 100 Hz sines, R 4/20/50, W 0/6 | worst 0.0007 dB at both | same |
| Feedback loop, ballistics, Attack 0.02 … 300 ms × Release 5 ms … 5 s × R 2/4/20/50, Knee 0 | attack within 0.34 % from 0.1 ms (0.02 ms within the ±0.25-sample rule), release 0.00 % | *attack and release mean the knobs* |
| Feedback loop without the compensation, R 2/4/8/20 | 0.4990, 0.2484, 0.1232, 0.0480 of the detector's constant: M0's figures to four places | *Floru* |
| Range on the loop's state: ∞, 30 dB over, Range 10; R 20 and 50 with demands past Range 40 | 10.00 dB; attacks at the knob (±10 %) | *Range sits on the state* |
| Look-ahead 3 ms, feedback loop, 1 kHz burst (the set-up under *Look-ahead*) | attack side = the LA 0 trace shifted, release side identical: 0 dB difference | *look-ahead shifts the attack* |
| Allocation once prepared (every block, Clean, key EQ, key delay, glide, window, loop, fade, gains, matrix) | none | *Real-time: allocate nothing* |
| Denormals without flush-to-zero, 150 s of silence after Attack 300 ms, Release 5 s | none; the reduction ends exactly 0 | *nothing goes denormal* |

Clean's own figures are in `../models/clean/NOTES.md`, *Measured in M1*.

**Through the processor** (`BasaltMeasure all` and `perf`, the chain of *The chain (M1)*, Normal
and Live, the M1 gate run after the reviews, 2026-11-17; the same verdicts at 44.1, 48 and 96 kHz,
figures at 48 kHz unless named; every run exits 0):

- **Identity:** 226 gated rows bit-identical: Clean's slot, Normal and Live, every stereo mode,
  look-ahead 0/5/10 ms, key Internal and External, mono and stereo, blocks 1 … 4096 and variable.
  The eight slots without their own engine yet run Clean's: their 1744 rows read 0 as well and
  are reported "not exercised" (review: counted as passes, they made Clean's 226 look like 1970).
- **Static (Clean):** 439 pass, worst 0.068 dB (1 kHz, hard knee at Threshold, ∞:1, Attack
  10 ms); 32 reported (100 and 50 Hz).
- **Ballistics (Clean):** 261 pass: attack within 4.4 % (0.996 … 1.044 ×; 1.057 × at 44.1 kHz for
  Attack 0.02 ms, under 2.5 samples), release within 7.4 % (0.967 … 1.074 ×) of the knob or of
  Clean's floor 1.01·Attack/κ + δ; the look-ahead shift exact at 3 and 10 ms; every Auto release
  row. The attack steps now hold max(5·(Attack + Release), 10·Attack): with 5·(Attack + Release)
  alone a short Release left D_final 0.2–0.5 % low and the attack read up to 0.9 % short (the
  review). 24 rows below the floor are reported against the floor without δ, 1.01·Attack/κ,
  which they read 1.23 … 1.45 × (clean/NOTES.md, *Release with the RMS half*).
- **Clicks (Clean):** 86 gated rows pass (43 per latency), worst −66.0 dB (Look-ahead 0 ↔ 10 ms at
  Attack 0.02 ms; −64.1 dB at 44.1 kHz). After the review each knob runs on a key it can reach:
  the side chain's channels 26 dB apart (Link −72.3 dB at Attack 0.02 ms, −71.9 at 10 ms; Stereo
  −75.0), a key at Threshold for Knee (−77.7 dB), tones inside the key EQ's bands at Attack 10 ms
  (HPF on 1 kHz −73.7, Bell Gain on 1 kHz −67.6, Bell Freq on 16 kHz −72.0, Bell Q on 2 kHz
  −73.3 dB; Key LPF stays on the Nyquist carrier, −72.3), and a key gated 150 ms on and off with
  Release and Auto release moved 10 ms into each release, measured in a window around each move
  (Release −75.0, Auto release −77.7 dB; the same windows with nothing moving −77.8). 98
  reported: Character (M3), the model and detector switches, the no-swell rows, Floor (Upward,
  M2) and Target (Learn's aim only) not exercised; the key EQ rows at Attack 0.02 ms (−23 to
  −48 dB: a 20 µs attack follows its key); the baselines (−78.9 dB on the carriers; at Attack
  0.02 ms −52.3, −58.3 and −68.1 dB on the 1, 2 and 16 kHz tones and −73.6 dB on the internal
  sines, the model following its key).
  - Halite's measure divides the peak third difference by the RMS of the whole render, so a
    render that sits most of its time at another level reads the click against that level: the
    tone rows held with Release 5 s spent most of the render 24 dB down and read −56.7 dB from the
    normalisation alone; at Release 150 ms the same change reads −67.6 dB. The scenarios keep the
    level they are measured at.
- **Stereo:** 20 pass, Link 0 % = Unlinked to the bit with Auto release on and off.
- **Distortion (Clean, new after the review):** THD+N of a 1 kHz sine at 6 dB of static reduction
  (R 4, hard knee, Auto off), the fundamental fitted over 50 whole periods after 25 attack
  constants: 20 gated rows (Attack ≥ 10 ms, Release ≥ 50 ms) pass, worst −95.9 dBc (Attack
  100 ms on Clean's release floor, 119 ms); 22 reported. THD (harmonics 2–10) beside it repeats
  the M0 simulation within 0.6 dB, and reads up to 3.3 dB higher at Attack 30 ms, where δ shortens
  the hold (`../models/clean/NOTES.md`, *Measured in M1*). At 44.1 kHz THD+N reads up to
  40 dB above THD at fast settings (worst gated −86.6 dBc, Attack 10 ms, Release 500 ms): a 1 kHz
  sine has 44.1 samples a period, so its sampled crest moves from period to period, and the held
  peak carries sidebands around the fundamental that harmonics alone do not show.
- **Cost** (`BasaltMeasure perf`, 48 kHz stereo, 512-sample blocks, through the processor, load
  average 3): **0.284 %** of a core at the defaults (Clean, Linked, Normal), 0.279 % Unlinked,
  0.159 % Mid, 0.162 % Side, 0.273 % Mid/Side, 0.282 % Live; **0.365 %** Linked and 0.345 %
  Mid/Side with look-ahead 10 ms, key HPF, LPF and bell on and the side chain on (the review read
  0.32–0.35 % for parts of that and 0.405 % for all of it before the changes under *From the
  reviews*). Reported at other rates: 0.259 / 0.331 % at 44.1 kHz, 0.550 / 0.703 % at 96 kHz.

**Through the processor, M2** (`BasaltMeasure all --model <m>`, 48 kHz, 2026-11-17): every
model on its own engine, the per-model figures in `PLAN.md` (*Milestones*, M2) and in each model's
NOTES (*Through the processor (M2)*). What concerns the core:

- **Switches between engines:** every model to every other and VCA Peak ↔ RMS, at Attack 0.02, 10
  and 300 ms, Normal and Live: no step above −67.6 dB (Halite's method; Upward ↔ the others the
  worst), and no swell: at Attack 300 ms the reduction never leaves the band of the two settled
  reductions ±0.5 dB (0.000 dB outside it, every pair), VCA's detector switch on a 1 kHz sine at
  Attack 10 and 300 ms too. The warm-ups are each model's `settleMs` plus the look-ahead window.
- **Gains above 1** (Upward): Mix, Auto Gain's make-up (a cut, −ℓ(−18)), Listen › Delta and the
  Lift meter take them as they are (`ChainTests`, *Upward's lift passes Mix …*).
- **Range on a feedback loop's state** gives a trajectory that is the unclamped one until Range,
  so a step past Range reads 63 % of Range at 0.25–0.32 × the knob (FET and Vari-mu, R ∞, 30 dB
  over, Range 10). The gate for the loops is therefore that the clamp does not slow an attack
  whose demand passes Range (R 20 to 9 dB under Range 10: 10.0 ms for 10 ms), PLAN.md Settled 76.
- **The look-ahead glide on a Nyquist-rate key** reads it through the linear interpolator, which
  nulls it at half a sample (*Look-ahead*): the windowed peak models hold over the dips, but VCA
  RMS's 0.044 ms mean square at Attack 0.02 ms follows them and its release limiter lets the
  reduction fall about 2 dB during the 50 ms glide, which the click gate reads at −11.7 dB
  (`../models/vca/NOTES.md`, *Through the processor (M2)*).

## Changes to PLAN.md

M0 proposed eight changes (the measured feedback loop and its cap, the Ballistics and Static curve
definitions, the look-ahead window and Range in *Signal flow*, Auto Gain's reference, the key EQ's
slopes, the references); `PLAN.md` carries them. M1 adds three, now in `PLAN.md` (Settled 51 on):

- ***Measurement and quality gates*, Ballistics:** Clean's floor is 1.01·Attack/κ + δ
  (`../models/clean/NOTES.md`, *Release with the RMS half*); the rows under it also report the
  floor without δ.
- ***Measurement and quality gates*, Clicks:** curve knobs and Link fade over 20 ms with the new
  setting's ballistics beside the old; Attack, Release and Auto release glide in log time; key EQ
  changes glide; each knob runs on a key it can reach; key EQ rows are gated at Attack 10 ms and
  reported at 0.02 ms, where the gain follows its key.
- ***Performance budget*:** gated at 48 kHz, where it is stated; the cost scales with the rate
  (Clean 0.55 % at 96 kHz); ≤ 0.3 % at the ordinary settings and ≤ 0.4 % with the whole key path
  engaged (look-ahead 10 ms, every key EQ stage, the side chain).

## Not verified

- AES17's wording of the dBFS convention (not opened); Basalt's convention stands on its own.
- The 2013 erratum to eq. 3. The copy read prints the same structure as the 2012 eq. 4, but its
  text extraction was garbled there, so whether the erratum changes it is open.
- Abel & Berners 2003 beyond its abstract.
- Everything under *Results* is a simulation of these equations; *Measured in M1* repeats the core
  tables on the engine at unit level, `BasaltMeasure` repeats them through the plug-in.
