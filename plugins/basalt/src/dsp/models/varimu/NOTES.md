# Vari-mu: the variable-gain tube leveller

The smooth one for buses and masters: a gain stage whose gain falls as its control voltage rises,
in a feedback loop, with a ratio that starts gentle and climbs towards limiting as the level
rises, and a release that slows down when the programme stays loud. Behaviour references: the
**670** stereo limiter (Fairchild Recording Equipment Corp., instruction manual DS-670-659 of
December 1959 with its schematic, and the *Technical Bulletin Model 660, 670*), the **Manley
Stereo Variable-MU** limiter/compressor (owner's manual), and the **6386** remote-cutoff twin
triode both were built on (JJ datasheet). The model is always written *Vari-mu*: "Variable Mu" is
a live registration (`PLAN.md`). Product names appear only here and in `PLAN.md` (root `NOTICE`).

Vari-mu runs on the shared core's feedback loop (`../../dynamics/NOTES.md`, *Feedback*) with its
own static curve, an attack calibration for that curve's rising loop gain, an attack floor, a cap
of 20 and a three-tier Auto release.

Status: M0 specification, implemented in M2 (2026-11-17) in this folder (`VariMuCurve`, the curve
and its inverse; `VariMuModel`, the model) and checked at model level by `BasaltTests_varimu`
(`tests/models/varimu/`, whose `Reference.h` is the transcription `BasaltMeasure` compares with).
*Implementation (M2)* records what the code decided where this file left it open or the
measurements proved it wrong; *Measured in M2* what the engine reads. Not yet wired into the
chain. Marks as in the core's NOTES: **source**, **measured (simulation)** (Python/NumPy/SciPy,
double precision, 48 kHz unless stated, the set-up next to the numbers, scripts not kept),
**measured (M2)** (the C++ model at 48 kHz unless stated, by the tests named next to the numbers;
`BasaltTests_varimu -s` prints them), **Basalt's design**, **inference**, **not verified**.

## Sources

| Source | What Vari-mu takes | Read |
|---|---|---|
| Fairchild Recording Equipment Corp., *Instruction Manual, Model 670 Stereo Limiter*, December 1959 ("supersedes all previous issues"), feature page marked DS-670-659 (archive copy; image-only, planning library `f670man.pdf` and the page images `f670p-*.png`, `f670q-*.png`) | features (p. 1): full limiting within a ten-thousandth of a second, 2:1 compressor with a threshold 5 dB below normal programme level or 30:1 peak limiter 10 dB above it; general description (p. 2): six release steps, 0.3 to 25 s, two automatic, and two amplifiers per channel (the gain stage and the one that makes the control voltage); specifications (p. 4): attack 0.2 ms in positions 1, 2, 6 and 0.4 ms in 3, 4, 5, release per position measured "from 10 db of limiting", compression ratio from 1:1 to 1:20 above a predetermined level, factory-set to +2 dBm, lateral/vertical operation; threshold adjustments (p. 8); the graph *Input vs. Output Curves* (dated December 1959, superseding March 1959); normal operation (pp. 9–10); the IM-distortion graph (3/59) | read: every page, from the images |
| The same manual's last sheet: *670 Dual Limiter Schematic*, drawing number legible as **A-96039** (the last digit uncertain in the scan) | side-chain topology, the rectifier and the timing network's structure | read for topology (the scan at full resolution); component values only where named below, as legible |
| Fairchild, *Technical Bulletin Model 660, 670* (`f670bul.pdf`, image `f670b-1.png`) | full limiting within a five-thousandth of a second; release 0.3 to 25 s in six positions, three of them dependent on the programme; 2:1 compressor 5 dB below average level, 30:1 limiter 10 dB above normal level | read |
| Manley Laboratories, *Stereo "Variable-MU" Limiter/Compressor Owner's Manual* (undated, third-party copy; `manleyvm.pdf`) | front panel (p. 6): Compress 1.5:1 soft knee, Limit 4:1 sharper knee, the ratio rising beyond 12 dB of limiting up to 20:1, recovery 0.2, 0.4, 0.6, 4, 8 s, attack 25 / 50 / 70 ms; operational notes (p. 10): the knee softens with more limiting, like a compressor followed by a limiter; the gain-control chain is a feedback circuit; the stereo link combines the DC control signals, not the audio; technical notes (p. 11): 5670 tubes now, 6386 in early units; mastering version (p. 12): attack 15–90 ms in 11 steps; specifications (p. 14): a vacuum-tube rectifier in the side chain. The *Gain Reduction Curves* page is a blank placeholder | read in full |
| JJ Electronic, *6386 LGP* datasheet (distributor's copy; `t6386.pdf`) | a twin triode with "exponential transfer characteristics"; µ = 18, S = 3 mA/V at Ua = 100 V; sections matched within 3 dB at Ua = 150 V, Ug = −2 … −30 V; the transfer graph Ia = f(−Ug) for Ua = 100 … 300 V | read, the graph on the page image |
| P. Raffensperger, DAFx-12 (670 wave-digital model) | feed-forward and feedback options in his code | abstract only (via `PLAN.md`); nothing here rests on it |
| `../../dynamics/NOTES.md` | the loop, Range on the state, the measurement definition, the ratio cap, the pre-hold | Basalt |

## Topology: feedback, from the schematic

**Feedback** (**source**):

- **670, drawing A-96039:** the push-pull gain stage (the 6386s, paralleled pairs) drives its
  output transformer. That transformer's secondary feeds the output terminals through the mode switch and,
  through a resistive pad, the control amplifier's input transformer. From there: the front-panel AC
  THRESHOLD control (a dual potentiometer), a 12AX7, a 12BH7, a push-pull 6973 pair, a transformer,
  a full-wave diode bridge, and the timing network switched by the TIME CONSTANT switch, whose
  voltage biases the 6386 grids. The side chain is taken after the gain stage (our reading of the
  drawing). The manual's p. 2 says only that each half has two amplifiers, the gain stage and one
  that produces the DC control voltage; it does not say where the second one is fed from.
- **Manley** (p. 10): the manual calls the gain-control chain a feedback circuit.

`PLAN.md` asked M0 to read the 670's side chain from the schematic; it is feedback (*Proposed
changes*, 1).

**The detector in the hardware is a peak rectifier**, not RMS (**source**: the 670's diode bridge
charging the timing capacitors; the Manley's "vacuum tube audio rectification"). With the 670's
0.2–0.4 ms attack it follows peaks; with the Manley type's 25–70 ms attack the capacitor charges
over many cycles and reads closer to an average (**inference**). `PLAN.md`'s "RMS-like" is
corrected to that (*Proposed changes*, 2).

**Stereo in the references** (**source**): the 670 limits left/right or "vertical and lateral
components" through matrixing networks (pp. 2–3), which is Basalt's Mid/Side mode; the Manley links
by combining "the 'DC' control signals not the audio" (p. 10), which is the core's Link of
reductions after the gain computer.

## 1. Detector

The core's feedback loop on the **peak** level with the core's pre-hold
`h = min(Release/10, 5 ms)` (the references are low-distortion levellers: the 670 "absence of
distortion" (p. 1), the Manley's 0.1–0.2 % at typical settings (p. 14), so the in-cycle modulation
the FET keeps is not wanted here; **Basalt's design**). Estimate, demand from the inverted curve,
Range on the state, Link as the core; the attack branch and the Auto release are this model's
(*Ballistics*).

## 2. Gain computer: the rising ratio

### What the references show

- **670** (**source**, p. 4 and the graph): the ratio varies from 1:1 to 1:20 above a predetermined
  level.
  The graph *Input vs. Output Curves* shows five settings; curve 3 is the factory adjustment.
  **Measured (digitised)** from the scan at 200 dpi (axes calibrated on the heavy grid lines, the
  curve traced column by column, reading accuracy about ±0.1 dB; the "straight amplifier" line sits
  0.4 dB under unity and the reduction is taken from it, (input − 0.4) − output; the fit below
  computes the reduction from the traced points by that rule):

  | Input (dBm) | +3.2 | +5.3 | +7.4 | +9.0 | +12.2 | +15.9 | +19.1 | +22.8 | +24.4 |
  |---|---|---|---|---|---|---|---|---|---|
  | Output (dBm) | +2.40 | +3.25 | +3.87 | +4.23 | +4.87 | +5.49 | +5.85 | +6.01 | +6.01 |
  | Reduction (dB) | 0.4 | 1.6 | 3.1 | 4.4 | 6.9 | 10.0 | 12.9 | 16.4 | 18.0 |
  | Local ratio over the step before | | 2.5 | 3.4 | 4.5 | 5.0 | 6.0 | 9 | 23 | ∞ (flat) |

  The curve leaves unity near +2.5 dBm (the "+2 dbm" predetermined level) and its local ratio
  climbs continuously from about 2.5:1 to beyond 20:1 within about 20 dB of input.
- **670 threshold controls** (p. 8): the DC THRESHOLD (internal) sets the input level for 3 dB of
  limiting and the AC THRESHOLD (front panel, the side chain's gain) the level for 10 dB; their
  combination sets "the slope and position of the curve".
- **Manley** (**source**, p. 6, p. 10): Compress 1.5:1 with a soft knee, Limit 4:1 with a sharper
  knee; beyond 12 dB of limiting the ratio rises, up to 20:1; the knee softens as limiting grows,
  so the unit acts like a compressor followed by a limiter.
- **Both stop at 20:1.**

### Basalt's curve (**Basalt's design**)

A base soft-knee curve (eq. 4, the shared knobs) plus a rise towards 20:1 centred U_r above the
threshold:

```
d(x)   = d₄(x; T, R, W) + φ(R)·(1/R − 1/20)·w_r·ln(1 + e^((x − T − U_r)/w_r))      R < 20
d(x)   = d₄(x; T, 20, W)                                                            R ≥ 20 (and ∞)
r_s(x) = min(d(x), Range)
d₄     = x − G(x) of eq. 4 (core);  U_r = 16 dB;  w_r = 2 dB;  φ(R) = min(1, (R − 1)/0.5)
```

- **Threshold** T, **Knee** W and **Ratio** R are the base curve's, as everywhere; R is the ratio
  before the rise. **Cap 20**, the references' maximum; the ∞ step reads 20:1 with no rise.
- **The rise** adds slope until the local ratio reaches 20:1: the local output slope goes from 1/R
  to 1/20 along a logistic step centred at T + 16 dB, 88 % complete by T + 20 dB. It is the
  Manley's "compressor followed by a limiter".
- **φ(R)** fades the rise in between Ratio 1 and 1.5 so that Ratio 1 is still the identity (the
  core's idle rule needs r ≡ 0 at Ratio 1).
- `y(x) = x − d(x)` is strictly increasing (slope ≥ 1/20), so the feedback form
  `c(y) = G⁻¹(y) − y` exists; the model inverts y(x) per sample and detector channel by Newton
  from that channel's previous solution (M2: 1.1 curve evaluations a solve on a moving estimate,
  within 2·10⁻⁸ dB of the exact inverse; *Implementation (M2)*). The table on x − T this file
  offered was not needed.

**Why U_r = 16, w_r = 2** (measured, simulation): the family was fitted to the 670's curve 3 with
T, R, W free, for U_r = 10 … 18 and w_r = 1 … 3:

| U_r, w_r | fitted T, R, W | rms / max error |
|---|---|---|
| 12, 2 | +3.25 dBm, 4.3, 4.9 dB | 0.046 / 0.104 dB |
| 14, 2 | +3.50 dBm, 5.1, 6.2 dB | 0.077 / 0.172 dB |
| **16, 2** | **+3.75 dBm, 6.0, 7.4 dB** | **0.114 / 0.235 dB** |

and checked against the Manley's statement at Ratio 4, Knee 6 (local ratio against the reduction):

| U_r, w_r | 6 dB | 10 dB | 12 dB | 14 dB | 16 dB | 20 dB | 24 dB |
|---|---|---|---|---|---|---|---|
| 14, 2 | 4.2 | 5.8 | 8.6 | 12.8 | 16.5 | 19.5 | 19.9 |
| **16, 2** | 4.1 | 4.8 | 6.3 | 9.6 | 13.8 | 18.9 | 19.9 |
| 18, 2 | 4.0 | 4.3 | 5.0 | 7.0 | 10.6 | 17.7 | 19.7 |

16 dB puts the Manley's rise where its manual puts it (beginning past 12 dB of reduction) and still
reproduces the 670's factory curve within a quarter of a dB with Ratio 6 and Knee 7.4; 12 dB fits
the 670 better but rises from about 9 dB of reduction at 4:1. `PLAN.md`'s gate is the Manley-type
rise, so 16.

**Settings the references map to** (a guide for presets, not a mode): the 670's factory curve:
Ratio 6, Knee 7.4, Threshold 1.25 dB above the level where that curve leaves unity (the fit);
Manley Compress: Ratio 1.5, Knee 12; Manley Limit: Ratio 4, Knee 3.

## 3. Ballistics

### Attack

**The core's measured attack** (carrier, silence to 10 dB of reduction, 63.2 %), 1:1, **floored at
0.1 ms**.

- **One knob spans both types:** the 670's 0.2 and 0.4 ms and the Manley's 25, 50, 70 ms (mastering
  version 15–90 ms) all lie inside 0.1 … 300 ms and are marked on the knob. There is no model
  default: switching model never moves a knob (`PLAN.md`, *Models*), so the shared 10 ms sits
  between the two types. The factory presets choose: the Manley type's 50 ms for bus and master
  levelling, the 670 type's 0.4 ms for peak control (**Basalt's design**). The 670 couples attack
  to the release position (0.2 ms with positions 1, 2, 6; 0.4 ms with 3, 4, 5); Basalt does not.
- **The references' definitions are not Basalt's:** the 670's full limiting within a
  ten-thousandth (manual) or a five-thousandth (bulletin) of a second describes full effect, not a
  63 % time; the Manley gives bare figures. The marks are positions, not calibrations.
- **Calibration for the rising loop gain:** the core divides the attack coefficient by the set ratio
  so that the closed loop's pole is the knob's. Here the loop gain changes along the curve, and a
  step from silence first meets the high loop gain of the 20:1 region (the uncompressed estimate
  lies far up the curve), so it runs faster than the end point's pole. Basalt therefore uses
  `1 − α_A' = (1 − e^(−1/(τ·fs))) / R_ref`, with **R_ref the curve's local ratio at the reference
  point** (where d = 10 dB) and **τ = Attack / Φ(R, W)**, Φ measured on the reference step.
  Measured (simulation, the core's loop on levels, continuous-time regime, Attack 30 ms):

  | Φ | W 0 | W 6 | W 12 | W 24 |
  |---|---|---|---|---|
  | R 1.5 | 0.500 | 0.500 | 0.500 | 0.500 |
  | R 2 | 0.290 | 0.290 | 0.290 | 0.290 |
  | R 4 | 0.266 | 0.266 | 0.266 | 0.266 |
  | R 8 | 0.448 | 0.448 | 0.448 | 0.379 |
  | R 20 | 0.994 | 0.994 | 0.994 | 0.455 |

  **M2 measured it otherwise** (*Implementation (M2)*): Φ = R_ref / (κ(R)·R_∞), R_∞ = 1/(1/R − A)
  the curve's asymptotic local ratio (20 from Ratio 1.5 on), where a step from silence starts,
  and κ a correction measured on the engine and tabulated over log Ratio 1 … 20 (20 points,
  0.887 … 1). Φ itself falls from 0.91 to 0.50 between Ratio 1.4 and 1.5 as the rise fades in,
  and from 1 to 0.45 between Knee 21 and 24 at Ratio 20 as the reference point enters the knee;
  the 16 × 7 bilinear Φ table proposed here read Ratio 1.2 at 1.99 × the knob and Ratio 20,
  Knee 21 at 1.16 ×. κ varies by at most 0.8 % with Knee and is tabulated on Ratio alone; Φ(20, 0)
  is 1 by construction.
- **Calibrated, at 44.1 kHz** (simulation; measured/Attack at 10, 20, 30 dB steps; overshoot over
  the settled reduction):

  | Attack | R 1.5, W 12 | R 4, W 6 | R 8, W 6 | R 20, W 0 |
  |---|---|---|---|---|
  | 0.02 ms | 0.82 / 0.82 / 0.82 | 0.71 / 0.70 / 0.69, **overshoot 0.24 / 1.88 / 2.83 dB** | 0.79 / 0.76 / 0.76 | 1.06 (all depths) |
  | 0.05 ms | 0.88 / 0.86 / 0.86 | 0.81 / 0.73 / 0.73 | 0.86 / 0.81 / 0.81 | 1.02 |
  | 0.1 ms | 0.94 / 0.92 / 0.92 | 0.90 / 0.81 / 0.81 | 0.93 / 0.87 / 0.87 | 1.01 |
  | 0.2 ms | 0.97 / 0.95 / 0.95 | 0.95 / 0.85 / 0.85 | 0.97 / 0.91 / 0.91 | 1.00 |
  | 10 ms | 1.00 / 0.98 / 0.98 | 1.00 / 0.89 / 0.89 | 1.00 / 0.94 / 0.94 | 1.00 |

  At the reference step (10 dB) the knob holds within 5 % from 0.2 ms up, and within 10 % at
  0.1 ms. **Hence the floor:** below 0.1 ms the rising loop gain overshoots (the one-sample-delay
  estimate meets a demand that grows 19 dB per dB) and, with the release slow, holds the overshoot.
  Uncalibrated at 48 kHz, a 0.02 ms attack held about 25 dB of reduction after a 10 dB step until
  the release brought it down, with or without a per-sample limit on the step (simulation).
  The floor costs nothing in the references (fastest 0.2 ms).
- **Alternative kept on file:** solving the loop without the one-sample delay (bisection on
  `s = s₀ + b·(c(L − s) − s₀)` each sample in the attack branch) showed no overshoot at 0.02 ms in
  the same tests (simulation). It costs about 30 curve evaluations a sample; the core's loop with
  the floor is the recommendation.
- **Deeper steps attack faster** (0.73 … 0.98 × Attack at 20 and 30 dB above): the level-dependent
  time constant of a feedback compressor (Abel & Berners 2003, abstract, via the core), kept as
  character and reported.

### Release

**The core's measured release** (to 36.8 % after silence), 1:1 over 5 ms … 5 s; the loop is open
below threshold, so the release is the detector's own constant.

- **The 670's positions** (p. 4, "from 10 db of limiting"): 0.3, 0.8, 2, 5 s for positions 1–4. The
  end point is not stated. Reading the timing network on drawing A-96039 (a base resistor and
  capacitor, with the switch adding parallel resistors for faster positions and parallel capacitors
  for slower ones; our reading of the switch's two decks is uncertain in the scan), the network's
  time constants come out at roughly a third to a half of the published figures, which suggests the
  figures are times to near-full recovery rather than time constants (**inference**, **not
  verified** on a unit). Basalt therefore puts no detents at the 670's release figures; the Manley's
  0.2, 0.4, 0.6, 4, 8 s are likewise undefined. Presets after the 670 type use about a third of its
  figures.
- **Auto release off** is a single exponential release: the 670's fixed positions and the Manley.

### Auto release: three tiers from the 670's automatic positions

**Source** (p. 4): position 5 is automatic, 2 s after individual peaks and 10 s after multiple
peaks; position 6 is 0.3 s after individual peaks, 10 s after multiple peaks and 25 s under a
consistently high programme level. The manual does not say how many peaks, how long or how close
together make "multiple". On drawing A-96039 the automatic
positions connect series resistor–capacitor branches to the timing node: capacitors that charge only
through a resistor, so short peaks barely reach them and a sustained level fills them (structure,
**inference**).

**Basalt's design:** the reduction is the maximum of three followers, the loop's state s and two
memories:

```
r   = max(s, m, ℓ)                                    the reduction the loop and the audio use
m  ← m + (1 − e^(−1/(0.3 s·fs)))·(s − m)   if s > m and the demand exceeded s within the last 25 ms
     m·e^(−1/(min(5·Release, 25 s)·fs))    otherwise
ℓ  ← the same with charging 5 s and release min(12.5·Release, 25 s)
```

M2 made the rule exact (*Implementation (M2)*): "the demand exceeded s" is the linked demand at
or above the loop's state before the update, within 10⁻⁶ dB (the loop attacks or holds, as opposed
to releasing; at equilibrium the two are equal to rounding); "within the last 25 ms" counts samples
from the last one that did; the memories never exceed Range and snap to exactly 0 under 10⁻⁶ dB.

- Tiers 1 : 5 : 12.5: position 5's 2 s → 10 s, and position 6's multiple → consistent 10 s → 25 s.
  At **Release 2 s** they are 2, 10 and 25 s: position 5 and position 6's long tiers. Position 6's
  0.3 s for single peaks is Release 0.3 s, whose tiers are then 1.5 and 3.75 s, not position 6's 10
  and 25 s; Basalt has one ladder, scaled by the knob.
- **The 25 ms rule** (the memories charge only while the side chain is calling for reduction, held
  over one cycle of 40 Hz) keeps the fast state's own release tail from filling them. Without it, a
  single 50 ms peak released in 7.5 s instead of 2 s (simulation).
- **25 s cap**, the 670's longest (`PLAN.md`, *Controls*: "Auto release may go longer where its
  reference does").

Measured (simulation, delay-free loop on levels at a 2 kHz control rate, Ratio 4, Knee 6, Attack
0.4 ms, Release 2 s, 10 dB of reduction, then silence; release to 36.8 %):

| History | Release measured |
|---|---|
| one 50 ms peak | 1.99 s |
| one 200 ms note | 3.55 s |
| 3 peaks of 50 ms, 0.4 s apart | 3.23 s |
| 5 such peaks | 6.05 s |
| 8 such peaks | 7.75 s |
| 2 s sustained | 9.93 s |
| 30 s sustained | 24.80 s |

These come from a variant (delay-free, a 2 kHz control rate), not the specified one-sample loop, so
M2 gates against `BasaltMeasure`'s transcription of the equations and reports these as expected
values (*Gate table*). The engine on the specified loop (M2, *Measured in M2*): 2.000, 3.759,
3.753, 6.460, 8.045, 9.995 and 24.948 s, the transcription the same to the millisecond.

**"Multiple peaks" falls short of the 670's figure** (**Basalt's design**, a recorded deviation).
The memories charge on the *time* the side chain calls for reduction, not on a count of peaks, so
several short peaks fill only part of the 5 × tier: 3, 5 and 8 peaks of 50 ms, 0.4 s apart, release
in 1.6, 3.0 and 3.9 × Release (M2 on the specified loop: 1.88, 3.23 and 4.02 × at Release 2 s;
1.21, 2.09 and 2.46 × at 0.3 s, whose tiers empty faster between the peaks), where the 670's
position 5 gives 10 s (5 × its 2 s) after "multiple peaks". The 5 × tier is reached by about 2 s of reduction. Kept, because:

- the manual names no count, length or spacing for "multiple", so no peak count can be read from
  it;
- the 670's automatic positions charge series RC branches through a resistor (the drawing, as
  read above), which integrate time under reduction as Basalt's memories do (**inference**);
- the release still grows with every added peak, monotonically, which is the behaviour the
  reference describes.

Rejected: charging the 5 × memory per side-chain event, or much faster, so that 3 to 8 peaks reach
5 × Release. A per-event charge needs a peak counter and an arbitrary count at which "several"
becomes 5 ×; a fast charge brings single long notes there too (a 64 ms charge, enough for three
50 ms peaks, fills to 96 % during one 200 ms note; arithmetic).

## 4. Make-up and gain law

- **The cell:** a remote-cutoff triode whose anode current falls roughly exponentially with grid
  bias ("exponential transfer characteristics", **source**). Read from the 6386's transfer graph at
  Ua = 300 V: about 50 mA at −2 V, 24 at −10 V, 11 at −20 V, 6 at −30 V, 3.5 at −40 V and 2 mA at
  −50 V, i.e. 4.7 to 6.8 dB per 10 V. For an exponential law the transconductance follows the
  current, so the stage's gain in dB is nearly linear in the control voltage, flattening slightly at
  deep bias (**inference**). That is why the timing network can be modelled in dB, as Basalt does,
  and why the gain is applied as an ideal `10^(−r/20)`. The tube stages' colour belongs to
  Character.
- **Auto Gain** inverts the static curve at the reference level, `M = r_s(L_ref)`, in closed form
  (the model's d(x)).

## 5. Gate table

| Gate | Vari-mu |
|---|---|
| Identity | Ratio 1 (φ = 0); Range 0; or input below both T − W/2 and T − 13 dB: the base curve is 0 there and the rise term under 10⁻⁶ dB at every Ratio, so the core's snap makes the gain exactly 1. Bit-identical as the core, both latencies |
| Static curve | 1 kHz sine and the carrier, T − 20 … T + 40 dB in 1 dB steps, R 1.5, 2, 4, 6, 10, 20, ∞, W 0, 6, 12, 24, Range 40 and 10, Attack 10 ms, Release 1 s, Auto off: within ±0.25 dB of r_s. 100 Hz gated as the core |
| Rising ratio (`PLAN.md`'s Vari-mu row) | Ratio 4, Knee 6, carrier: local ratio from 1 dB steps (secant) ≤ 5 at 8 dB of reduction, ≥ 12 at 16 dB, ≥ 17 at 20 dB; at Ratio 20: 20 ±1 everywhere above the knee |
| Ballistics, attack | carrier, silence → 10 dB, Knee 0, 6, 24, R 1.5, 4, 8, 20: 63 % time = Attack ±10 % for Attack 0.2, 0.4, 1, 10, 25, 50, 70, 300 ms; ±15 % at 0.1 ms; Attack below 0.1 ms reads 0.1 ms. Overshoot ≤ 0.05 dB for steps to 10, 20, 30 dB at every Attack, 44.1 and 48 kHz |
| Ballistics, release | Auto off: the core's definition, Release 5 ms … 5 s, ±10 % |
| Ballistics, Auto (`PLAN.md`'s Vari-mu Auto row) | Auto on, 10 dB, Release 0.3 and 2 s: after one 50 ms peak = Release ±10 %. After 3, 5 and 8 peaks of 50 ms 0.4 s apart, after 2 s sustained and after 30 s sustained: within ±10 % of `BasaltMeasure`'s transcription on the same stimulus (expected, from M0's variant: 1.6, 3.0, 3.9 and 5 × Release, and min(12.5 × Release, 25 s)); the release grows with the number of peaks |
| Distortion | reported: THD+N of a 1 kHz sine at 6 dB of reduction, Attack 0.4 and 50 ms, Release 0.3 and 2 s |
| Clicks | the core's list; Ratio and Knee jumps also move R_ref and Φ (coefficients only, never the state) |
| Stereo | the core's; Mid/Side as the 670's lateral/vertical, Link as the Manley's control-voltage link |
| Learn | feedback: the loop, with the memories, runs on the captured envelope (`PLAN.md`, *Learn*); M3 measures the gap to the audio |
| Auto Gain | `r_s(L_ref)` ±0.01 dB against the formula; the corpus gate as `PLAN.md` |
| Goldens | a mix bus at Ratio 4, Attack 50 ms, Auto on; a drum bus at Attack 0.4 ms, Release 0.3 s |

## Implementation (M2)

Where this file left a choice open, or the engine proved it wrong (**Basalt's design**, M2):

- **Files.** `VariMuCurve.h/.cpp`: d(x), its slope and curvature, the static curve, the per-sample
  inverse; `VariMuModel.h/.cpp`: the model on `dynamics::DynamicsModel`, the core's
  `FeedbackLoop` with this model's demand, estimate and linked-demand output (`Feedback.h`), its
  memories, the curve fade and the time glides. Nothing in the core changed.
- **The rise's softplus is tabulated:** cubic Hermite on ln(1 + e^u) and its slope every 1/16 over
  ±30 widths, built at load time; below −30 widths the rise is exactly 0 (1.8·10⁻¹³ dB there),
  above +30 it is u. Measured: d within 6.1·10⁻⁹ dB of the formula, its slope within 1.5·10⁻⁷. The
  exact `exp` and `log1p` cost 0.383 % of a core against 0.329 % (`[.perf]`, defaults), kept as
  headroom for the chain's key path. The interpolant's own slope feeds Newton, so each step sees
  the function it solves.
- **The inverse, per detector channel:** each channel's solve starts from its own last solution,
  extrapolated along the inverse's slope and, within a dB, its curvature (x' = 1/G', x'' = d''/G'³);
  Newton then runs from either side (G is concave, so a step from above lands under the root) until
  |G(x) − y| ≤ 10⁻⁷ dB, and one more step is taken without evaluating it (its error is the step's
  square times the curvature, under 10⁻⁸ dB; 2·10⁻⁶ dB where it crosses a hard knee's corner).
  `FeedbackLoop` asks for the demands once per channel in channel order, which the model uses to
  pick the channel's solver; another order would still solve, only slower. Measured: 1.11 curve
  evaluations a solve on a moving estimate (at most 5), |G(x) − y| ≤ 4·10⁻¹³ dB on random levels
  and curves, the demand within 1.8·10⁻⁸ dB of the transcription's regula falsi. Rejected: two
  remembered solutions picked by the nearer level, which both channels of a linked pair collapsed
  into one (3.7 evaluations a solve on pink noise, 0.96 % of a core); the first version, exact
  softplus with the same nearer-level cache, read 1.27 %.
- **The attack calibration** (*Attack*): `1 − α_A' = (1 − e^(−Φ/(Attack·fs)))/R_ref` with
  Attack ≥ 0.1 ms, R_ref = 1/G'(x_ref) at d(x_ref) = 10 dB by bisection, and
  Φ = R_ref/(κ(R)·R_∞). A step from silence first meets the curve's top, whose local ratio is
  R_∞ = 1/(1/R − A), 20 from Ratio 1.5 on; the loop's effective compensation R_ref/Φ came out
  within 0.887 … 1 of it everywhere, smooth in Ratio, so κ = R_ref/(Φ·R_∞) is what is tabulated:
  measured at Knee 0 on the reference step (silence to the carrier at 10 dB of static reduction,
  Attack 30 ms, 48 kHz, `[.calibrate]`), 20 points over log Ratio 1 … 20, linear between them,
  κ(1) = 1 (the curve is one line), κ(20) = 1.0000 as measured. Recalibrated when Ratio or Knee
  changes (once per curve fade, on the audio thread: two bisections of about 50 curve evaluations,
  bounded, no allocation), never moving the state. Rejected: Φ on a 16 × 7 table over Ratio 1.5 … 20
  and Knee (*Attack*, measured up to 1.99 × the knob below Ratio 1.5 and 1.16 × at Knee 21 to 22),
  and Φ against the reference point's depth inside the knee (Knee − W*, W* = 2·(x_ref − T) on the
  hard knee), which fixed the knee but not the low ratios. κ's dependence on Knee, at most 0.8 %
  (Ratio 20, Knee 24), is left out.
- **The Auto release memories:** the loop's estimate subtracts r = max(s, m, ℓ) of the previous
  sample; the memories charge on the linked demand (`FeedbackLoop`'s output), so a linked pair's
  memories stay bit-identical. "Calling for reduction" is c ≥ s_prev − 10⁻⁶ dB, held 25 ms
  (round(25 ms·fs) samples). Memories are clamped at Range (a Range fade's new loop starts from the
  old one's memories) and snap to exactly 0 under 10⁻⁶ dB. Auto release off: nothing charges and the
  tiers' release glides from 5 × and 12.5 × to 1 × Release over 20 ms in log time (5^w, 12.5^w as
  the glide's w falls), so a memory that holds the release when Auto goes off bends into the knob's
  release instead of dropping; once empty, r = s exactly.
- **Switching** (Settled 33, *Model interface*): `seed()` starts both loops' states at the applied
  reduction with empty memories; `follow()` runs the pre-hold windows, the curve fade and the
  glides; `settleMs()` is the pre-hold h, to which the chain adds the look-ahead window.
- **A curve change** runs a second loop, a copy of the running one with its memories and solvers,
  on the new curve's calibration and Link, and blends the two reductions on the core's 20 ms raised
  cosine, as Clean's ballistics.
- **The key it takes:** the chain passes it sane; the model's pre-hold reads a NaN as silence and an
  infinity as +120 dBFS on its own too.
- **Goldens** at model level: the reduction for two deterministic stereo keys, a mix bus (pink noise
  under a slow swell with accents; Threshold −24, Ratio 4, Knee 6, Attack 50 ms, Release 0.3 s,
  Auto on) and a drum bus (synthetic kick, snare and hat at 120 BPM; Threshold −20, Ratio 4,
  Knee 6, Attack 0.4 ms, Release 0.3 s), every fourth sample over 2 s
  (`tests/models/varimu/golden/`, `BASALT_UPDATE_GOLDEN=1 … "[golden]"`, within 10⁻³ dB). The
  gate table's corpus goldens through the processor come with the chain.

## Measured in M2

**Measured (M2)**, 48 kHz unless stated, `BasaltTests_varimu`:

| What | Result | Test |
|---|---|---|
| d(x) and d'(x) against the transcription (`Reference.h`), Ratio 1 … ∞, Knee 0 … 24, −90 … +130 dB | 6.1·10⁻⁹ dB, slope 1.5·10⁻⁷ | *the model's d(x) is the NOTES' formula* |
| Ratio 4, Knee 6: local ratio at 6, 10, 12, 14, 16, 20, 24 dB of reduction | 4.06, 4.76, 6.32, 9.58, 13.85, 18.89, 19.86 (this file's table to its rounding) | *the NOTES' fits, repeated* |
| The 670's factory curve at Ratio 6, Knee 7.4, T +3.75 dBm (the fit's rounded values) | rms 0.127, max 0.227 dB (M0's unrounded fit: 0.114 / 0.235) | same |
| Static curve, carrier and 1 kHz, the gate table's grid (R 1.5, 2, 4, 6, 10, 20, ∞ × W 0, 6, 12, 24 × Range 40, 10, T − 20 … T + 40) | worst 3.8·10⁻⁶ dB on both | *the carrier and 1 kHz sines* |
| Static curve, 100 Hz (gated), 50 Hz (reported), T −60 and −10 | 4.1·10⁻⁶ dB; 0.130 dB; 3.7·10⁻⁶ dB | *100 Hz gated* |
| Rising ratio, Ratio 4, Knee 6, secants of the settled 1 dB steps at 8, 16, 20 dB of reduction (gate ≤ 5, ≥ 12, ≥ 17) | 4.17, 12.84, 18.56 (the transcription's secants the same; tangents 4.22, 13.85, 18.89) | *the ratio rises with level* |
| Ratio 20 above the knee, secants | 20.000 | same |
| Attack, the reference step, R 1.5, 4, 8, 20 × W 0, 6, 24 × Attack 0.2 … 300 ms (gate ±10 %; 0.1 ms ±15 %; below, 0.1 ms) | worst 4.40 % at 48 kHz, 4.73 % at 44.1 kHz; below 0.1 ms the floor (0.094 … 0.101 ms) | *the reference step reads the knob* |
| Attack between κ's nodes: Ratio 1.05, 1.1, 1.2 … 20 (25 log steps), ∞ × Knee 0 … 24 (15) × Attack 1 and 30 ms | worst 0.84 % | *between the table's nodes* |
| Overshoot, steps to 10, 20, 30 dB, every Attack from 0.02 ms, 44.1 and 48 kHz (gate ≤ 0.05 dB) | 0.0000 dB | *the reference step* |
| Deeper steps (20, 30 dB) from Attack 1 ms | 0.88 … 1.00 × Attack | same |
| Release, Auto off, R 1.5, 4, 20 × Attack 0.1, 10, 300 ms × Release 5 ms … 5 s | worst 0.00 % | *Auto off, the knob 1:1* |
| Range 40 against none, 60 dB over | identical to the bit until the state reaches Range, then 40.00 dB | *Range sits on the loop's state* |
| Auto Gain against r_s(−18 dB) | within 10⁻² dB (exact to the table's 10⁻⁸) | same |
| Link 100 %: both channels' reductions; Link 0 % against Unlinked; 10 s of bursty noise, Auto on and off | bit-identical; bit-identical | *stereo* |
| Seed at Attack 300 ms; warm-up for settleMs plus the look-ahead window, then seeded, Auto off, Link 70 % | stays at the seed within 10⁻⁶ dB; within 1.4·10⁻¹⁴ dB of a model that ran throughout | *switch* |
| Block sizes 1, 7, 64, 448 with settings moving at multiples of 448; 1, 4096 and random with settings still | bit-identical | *the same bits at every block size* |
| Allocation once prepared (random settings, blocks, follow and seed) | none | *nothing allocates* |
| Silence after the 25 s tier charged, no flush-to-zero | no subnormal; exactly 0 after 411 s (Release 5 s), 27.9 s (150 ms), 0.9 s (5 ms) | *nothing goes denormal*, *idle* |
| NaN, +∞, −∞ in the key | every reduction finite and within Range; the clean trace again 1.3 s later | *a NaN or an infinity* |
| Cost: model, gain conversion and matrix, stereo, look-ahead 5 ms, pink noise at 20 dB mean reduction (`[.perf]`, two runs, load 2.4) | 0.329 … 0.336 % (defaults, Auto), 0.333 … 0.346 % (Attack 0.4 ms), 0.310 % (Ratio 1.5, Auto off), 0.317 … 0.330 % (Unlinked), 0.214 … 0.223 % (Ratio 20, closed form); budget 0.5 % | *CPU, stereo at 48 kHz* |

**The attack against M0's calibrated table** (measured/Attack for steps to 10 / 20 / 30 dB at
44.1 kHz; overshoot 0.000 dB in every cell, 48 kHz within 0.01 of these):

| Attack | R 1.5, W 12 | R 4, W 6 | R 8, W 6 | R 20, W 0 |
|---|---|---|---|---|
| 0.02 ms (reads 0.1 ms) | 4.72 / 4.63 / 4.63 | 4.50 / 4.03 / 4.03 | 4.64 / 4.37 / 4.36 | 5.03 |
| 0.05 ms (reads 0.1 ms) | 1.89 / 1.85 / 1.85 | 1.80 / 1.61 / 1.61 | 1.86 / 1.75 / 1.75 | 2.01 |
| 0.1 ms | 0.94 / 0.93 / 0.93 | 0.90 / 0.81 / 0.81 | 0.93 / 0.87 / 0.87 | 1.01 |
| 0.2 ms | 0.97 / 0.96 / 0.96 | 0.95 / 0.85 / 0.85 | 0.97 / 0.91 / 0.90 | 1.00 |
| 10 ms | 1.00 / 0.98 / 0.98 | 1.00 / 0.89 / 0.89 | 1.00 / 0.94 / 0.94 | 1.00 |

M0's simulation of the same, cell for cell within 0.01; the floor removes its 0.24 … 2.83 dB of
overshoot at 0.02 ms.

**κ(Ratio)**, Ratio = 20^(i/19), i = 0 … 19 (`[.calibrate]`, residual 4.4·10⁻⁷ of 30 ms):
1, 1.0000, 0.9956, 0.9755, 0.9560, 0.9319, 0.9089, 0.8931, 0.8869, 0.8892, 0.8974, 0.9090,
0.9222, 0.9357, 0.9489, 0.9613, 0.9726, 0.9828, 0.9920, 1.0000. The Φ it gives at M0's points
(*Attack*, M0's simulation in brackets): R 1.5 0.502 (0.500), R 2 0.292 (0.290), R 4, W 6 0.268
(0.266), R 8, W 6 0.451 (0.448), R 20, W 0 1 (0.994), R 20, W 24 0.454 (0.455).

**Auto release** (Ratio 4, Knee 6, Attack 0.4 ms, 10 dB, then silence; release to 36.8 %), the
engine and `Reference.h`'s transcription of this file's equations on the same carrier (gate:
one 50 ms peak = Release ±10 %, the rest ±10 % of the transcription, growing with the count):

| History | Release 0.3 s | Release 2 s | Transcription | M0's variant (2 s) |
|---|---|---|---|---|
| one 50 ms peak | 0.300 s (1.00 ×) | 2.000 s (1.00 ×) | the same to the ms | 1.99 s |
| 3 peaks of 50 ms, 0.4 s apart | 0.364 s (1.21 ×) | 3.753 s (1.88 ×) | the same | 3.23 s |
| 5 peaks | 0.627 s (2.09 ×) | 6.460 s (3.23 ×) | the same | 6.05 s |
| 8 peaks | 0.737 s (2.46 ×) | 8.045 s (4.02 ×) | the same | 7.75 s |
| 2 s sustained | 1.503 s (5.01 ×) | 9.995 s (5.00 ×) | the same | 9.93 s |
| 30 s sustained | 3.746 s (12.49 ×) | 24.948 s (12.47 ×) | the same | 24.80 s |
| one 200 ms note (reported) | 0.581 s (1.94 ×) | 3.759 s (1.88 ×) | the same | 3.55 s |

After one 50 ms peak at Release 2 s the 5 × memory holds 2.11 dB a second later, the 12.5 × one
0.15 dB: the 25 ms rule keeps the fast state's tail out of them.

**Clicks at model level** (largest step and second difference of the reduction in dB, a key 14 dB
over Threshold −18, knobs jumping between their ends every 0.31 s, Auto off): Threshold −40 ↔ 0,
Ratio 1 ↔ ∞ and 1.5 ↔ 20, Knee 0 ↔ 24, Range 0 ↔ 40 at Attack 0.1 and 10 ms: steps ≤ 0.063 dB,
second differences ≤ 2.6·10⁻⁴ dB (the raised cosine's order, as Clean's). Attack 300 → 0.1 ms
mid-attack: 0.031 dB, 3.0·10⁻⁴; Release 5 s → 5 ms mid-release: 0.023 dB, 9.3·10⁻⁵; Auto off while
the 5 × memory holds the release: 7·10⁻⁴ dB, 1.0·10⁻⁶. Two corners are the model's own, not a
knob's, and reported:

- **A memory taking over the release:** where the loop's state falls under a charged memory the
  release slope changes within a sample, 1.2·10⁻³ dB (1 s at 14 dB over, Release 150 ms, Auto on,
  10 ms into the release), under the loop's own corner where its release starts after the
  pre-hold, 1.5·10⁻³ dB, on the same stimulus.
- **The state reaching Range** (the core's Range on the state, Settled 29): on the rising curve a
  key 56 dB over Threshold asks for hundreds of dB (the 20:1 top inverted), and the state arrives at
  Range 40 with its slope: 3.1 dB per sample at Attack 0.1 ms (after 0.12 ms), 0.024 at 10 ms,
  7.8·10⁻⁴ at 300 ms. This is why the Threshold row jumps to −40, not −60: at −60 it read a second
  difference of 0.022 dB at Attack 10 ms. FET's loop does the same at its cap; the processor's
  click gate decides whether it matters (*Not verified*).

**Distortion** (THD+N of a 1 kHz sine at 6 dB of static reduction, Ratio 4, hard knee, Auto off and
on alike): at 48 kHz under −200 dBc, since the 5 ms pre-hold reads the sampled peak exactly and the
gain holds still; at 44.1 kHz, where the sampled crest moves from period to period, −106.5 and
−122.9 dBc at Attack 0.4 ms (Release 0.3, 2 s), −114.4 and −125.4 dBc at 50 ms.

## Through the processor (M2)

`BasaltMeasure all --model vari-mu` (48 kHz, Normal and Live, 2026-11-17), against
`tests/models/varimu/Reference.h`:

- **Identity:** 226 rows bit-identical.
- **Static:** 509 pass, worst 0.085 dB (1 kHz, R 1.5, hard knee), 100 Hz gated and passing; the
  rising ratio's secants 4.19, 12.84, 18.56 at 8, 16, 20 dB (the transcription 4.17, 12.84,
  18.56); at Ratio 20 every secant above the knee within 0.002 of 20.
- **Ballistics:** attack 0.913–1.008 × max(Attack, 0.1 ms) over R 1.5, 4, 8, 20 × Knee 0, 6, 24
  (0.02 ms against the 0.1 ms floor, ±15 %), no overshoot in 138 steps to 10, 20 and 30 dB;
  release 1.000 × the knob; every Auto row equal to the transcription (Release 2 s: 2.00, 3.75,
  6.46, 8.05, 9.99, 24.95 s; Release 0.3 s: 0.30, 0.36, 0.63, 0.74, 1.50, 3.75 s), growing with
  the peaks.
- **Clicks:** every model switch (−75.6 dB) and no-swell row pass. **18 knob rows fail**, all at
  Attack 10 ms or in releases: Threshold −60 ↔ 0 dB −49.4 dB (the state reaching Range at speed,
  *Implementation (M2)*), Ratio 1 ↔ ∞ −59.1 dB, the key EQ rows −40.9 (Bell Freq) to −54.1 dB (a
  key jump past the calibration point attacks at the rising loop gain, faster than Attack), and
  Release 5 ↔ 5000 ms in the releases −51 to −54 dB.
- **Cost:** 0.379 % of a core at the defaults, 0.412 % with the whole key path.

## Proposed changes to PLAN.md

1. ***The reference behaviour*, Vari-mu row, side chain:** "the 670's side chain from the schematic
   in M0 (Raffensperger's model offers both)" → "**feedback**: on the 670 schematic (drawing
   A-96039) the control amplifier is fed from the gain stage's output transformer; the Manley's
   manual calls its chain 'a feedback circuit'".
2. **Vari-mu row, detector:** "RMS-like" → "a peak rectifier charging the timing network (670
   schematic; the Manley's 'vacuum tube audio rectification'); with the Manley type's slow attack it
   reads closer to an average".
3. **Vari-mu row, ratio:** add "the 670's factory curve (graph of December 1959, digitised) leaves
   unity near +2.5 dBm and climbs from about 2.5:1 to beyond 20:1 within about 20 dB of input";
   note that the 2:1/30:1 statement is in the manual's feature page too, not only the bulletin.
   Basalt's curve: the base soft knee plus a rise to 20:1 centred 16 dB above Threshold (2 dB wide).
4. **Vari-mu row, attack:** add "Basalt: Attack 1:1 spanning both types (0.2–0.4 ms and 25–70 ms
   marked), floor 0.1 ms; the feedback loop's rising gain needs an attack calibration Φ(Ratio, Knee)".
5. **Vari-mu row, release:** add "the 670's 'from 10 dB of limiting' figures do not state an end
   point; on the schematic the network's time constants are a third to a half of them (inference).
   Auto release: max of the loop and two memories at 5 × and 12.5 × Release (cap 25 s), charging
   over 0.3 s and 5 s while the side chain calls for reduction".
6. ***Feedback models*:** add "a model whose loop gain rises with level (Vari-mu) needs its attack
   compensated at the reference point and calibrated (Φ), and an attack floor (0.1 ms): below it
   the one-sample-delay estimate overshoots by up to 2.8 dB at 44.1 kHz (simulation). A delay-free
   per-sample solve removes the overshoot at about 30 curve evaluations a sample; it is the fallback".
7. ***Measurement and quality gates*, Ballistics, Vari-mu:** "the Manley-type rise above 12 dB of
   reduction" → the numbers in *Gate table* (≤ 5 at 8 dB, ≥ 12 at 16 dB, ≥ 17 at 20 dB at Ratio 4,
   Knee 6); Vari-mu Auto: one 50 ms peak = Release ±10 %; several peaks (3, 5, 8), 2 s and 30 s
   sustained against the transcription, the release growing with the count; record that several
   peaks reach 1.6 … 3.9 × Release, not the 670's 5 × (*Auto release*).
8. ***References*, Vari-mu:** the 670 manual is dated December 1959 (the *Input vs. Output* graph
   December 1959, superseding March 1959; the IM graph 3/59); the schematic's drawing number
   A-96039 (as legible); the Manley's page references above; the 6386 transfer graph read.

### M2 (2026-11-17)

1. ***The reference behaviour*, Vari-mu row, and Settled 40:** "the attack calibrated (Φ)" →
   "the attack compensated at the curve's local ratio where its reduction is 10 dB and calibrated by
   Φ = R_ref/(κ·R_∞), κ measured over Ratio" (*Implementation (M2)*).
2. ***Feedback models*, a loop gain that rises with level:** the same wording.
3. ***Measurement and quality gates*, Vari-mu Auto:** "Several short peaks reach only 1.6–3.9 ×
   Release" → "1.9–4.0 × at Release 2 s on the specified loop (M0's variant: 1.6–3.9 ×)".
4. ***Milestones*, M2's status line:** Vari-mu's figures from *Measured in M2*.

## Not verified

- The 670's release figures' end point, and the component values of the timing network (read from
  a scan, the switch decks' wiring uncertain).
- The schematic's drawing number's last digit.
- The 670's attack definition; the Manley's attack and release definitions.
- Whether the Manley's ratio rise applies in Compress as well as Limit ("of limiting", p. 6).
- Raffensperger 2012 beyond its abstract.
- Everything under *Measured in M2* is the model at unit level; `BasaltMeasure` repeats it through
  the processor once the chain runs Vari-mu, with `tests/models/varimu/Reference.h` as its
  transcription. In particular the processor's click gate on the two corners above, Learn's replay
  of the loop (M3), and the corpus goldens.
