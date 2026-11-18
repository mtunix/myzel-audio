# Opto: the electro-optical leveller

The gentle one: a light panel driven by the signal shines on a photoresistor that shunts the audio,
so the curve is soft by nature, the attack follows the light, and the release has two stages whose
second depends on how long and how hard the cell was lit. Behaviour reference: the **LA-2A**
levelling amplifier (Universal Audio's reissue manual, P/N LA2A-M01 rev. 1.3) and its **T4** cell
(an electroluminescent panel and a cadmium-sulphide photoresistor), with photoresistor datasheets
(**NSL-32SR2**, **VTL5C9/VTL5C10**) for the cell's laws and Wright & Välimäki's grey-box fit to
LA-2A recordings for numbers the manual does not give. Product names appear only here and in
`PLAN.md` (root `NOTICE`).

Opto is feed-forward and computes a cell's state from the key instead of the core's ballistics
(`../../dynamics/NOTES.md`, *What Basalt computes*: "a model with a physical cell (the opto)
computes the cell's state from the key instead and documents it"). Key path, key EQ, stereo
modes, look-ahead window, Range and the idle short circuit are the core's.

Status: M0 specification, implemented in M2 (2026-11-17) as `OptoModel` in this folder and checked
at model level by `BasaltTests_opto` (`tests/models/opto/`); not yet wired into the chain.
*Implementation (M2)* records what the code decided where this file left it open or the
measurements proved it wrong (the light's constant at short Attack, Range on the light, Ratios
under 1.5), and *Measured in M2* what the engine reads. Marks as in the core's NOTES: **source**,
**measured (simulation)** (Python/NumPy/SciPy, double precision, 48 kHz unless stated, the set-up
next to the numbers, scripts not kept), **measured (M2)** (the engine, by the tests named),
**Basalt's design**, **inference**, **not verified**.

## Sources

| Source | What Opto takes | Read |
|---|---|---|
| Universal Audio, *Model LA-2A Leveling Amplifier* manual, P/N LA2A-M01, revision 1.3 (third-party copy; planning library `uala2a.pdf` and `uala2ad.pdf`, the same text in two files) | specifications (p. 1): a "very fast" attack; about 0.06 s to 50 % release and 0.5 to 5 s to complete release, depending on the previous reduction; up to 40 dB of reduction; Limit/Compress (p. 3); block diagram Figure 3 (p. 7) and its text (p. 8); the T4 and the divider, Figure 5 (pp. 8–9); the two-stage decay and the cell's memory (p. 10); the side-chain section (p. 11) | read in full, Figures 3 and 5 on the rendered pages |
| Advanced Photonix, *NSL-32SR2 Optocoupler* datasheet, rev. 12-02-15 (`nsl32sr2b.pdf`) | an LED-lit CdS/CdSe cell: on resistance ≤ 40 Ω at 20 mA, 140 Ω typical at 1 mA; rise 5 ms typical (dark to light, conductance to 63 % of its final value); decay 5 ms typical (to 100 kΩ after 16 mA is removed); dark resistance 1 MΩ min, 5 MΩ typical 10 s after the light; the resistance-versus-current graph | read in full, the table and the graph on the page image. `nsl32sr2.pdf` in the library is an "access denied" HTML page, not the datasheet |
| Excelitas, *Low Cost Axial Vactrols VTL5C9, 5C10* datasheet (catalogue pp. 55–56, distributor's copy; `vtl.pdf`) | turn-on to 63 % of final R_on: 4.0 ms (5C9), 1.0 ms (5C10) typical; turn-off to 100 kΩ: 50 ms (5C9), 1.5 s (5C10) maximum; slope R@0.5 mA / R@5 mA: 7.3 and 3.8; transfer curves given after 24 h of light adaptation, response times after 24 h at 40 mA (notes 2, 3) | read in full (the typical-curve graphs carry no legible numbers) |
| A. Wright, V. Välimäki, *Grey-Box Modelling of Dynamic Range Compression*, Proc. DAFx20in22, 304–311, 2022 (CC BY) | a feed-forward grey-box model trained on the SignalTrain LA-2A recordings in Compress mode (§5); learned curves (Fig. 7) and detector constants (Fig. 8); the time constant shrinking when the input is large (Fig. 10, §6) | read in full |
| F. Eichas, U. Zölzer, *Modeling of an optocoupler-based audio dynamic range control circuit*, Proc. SPIE 9948, 2016 | an optocoupler model | record only (via `PLAN.md`) |
| J. Parker, S. D'Angelo, *A Digital Model of the Buchla Lowpass-Gate*, DAFx-13 | a vactrol model | abstract only (via `PLAN.md`) |
| R. Simionato, S. Fasciani, JAES 73(3), 2025 | the opto's time variance | abstract only (via `PLAN.md`) |
| `../../dynamics/NOTES.md` | detector scale, RMS level, stereo, look-ahead, Range, idle, the attack definition | Basalt |

No datasheet for the T4 itself was obtained (**not verified**); the photoresistor datasheets stand
in for the cell's laws, and every number taken from them is a law's shape, not a T4 value.

## Topology: feed-forward, and the manual's contradiction

**Feed-forward** (**source**, the manual's figures and the text beside them):

- p. 8: after the input transformer the signal feeds the side chain and the gain-reduction circuit
  side by side.
- Figure 3 (p. 7): the Peak Reduction control takes the input transformer's output in parallel
  with the optical attenuator; nothing returns from the output stages to the side chain.
- Figure 5 (p. 9): the side chain leaves the input transformer through its own resistor ("to gain
  reduction control"), while the photocell shunts the attenuator node after the series resistors.

**But** the *Side-Chain Circuit* section (p. 11) calls the unit "a feed-back style compressor",
because the side chain's signal is affected by the reduced signal. The manual contradicts its
own figures. The only coupling the figures show
is the cell loading the input transformer's secondary through the 68 kΩ series resistor, which
would move the side chain's level very little (**inference**, not measured). Basalt follows the
figures and `PLAN.md`: **feed-forward**. Wright & Välimäki's model, feed-forward in structure (§2),
fits the LA-2A recordings closely (Table 1, **source**), which is consistent with that choice but
does not prove the circuit (*Proposed changes*, 1).

## 1. Detector: light and cell

Per detector channel, per sample (**Basalt's design**, each step grounded below):

```
m  = β·m + (1 − β)·k²                         light: mean square, τ_m = min(5 ms, 1.5 × Attack) (β = e^(−1/(τ_m·fs)))
u  = 10·log10(2m)                             level on the detector scale (a sine reads its peak)
G* = min( max( e^((u − T)/w) − ε, 0 ), G_max )  the conductance the light would settle the cell at
                                              ε = e^(−6), w = (Knee + 12 dB)/5.5
memory (Auto release on):
  S* = σ·G*
  S  ← S + (1 − a_c)(S* − S)   if S* > S      charging, τ_sc = 3 s
  S  ← S + (1 − a_d)(S* − S)   otherwise      discharging, τ_sd = min(16·Release, 25 s)
  (Auto release off: S = 0)
fast part:
  F* = max(G* − S, 0)
  F  ← F + (1 − a_on)(F* − F)   if F* > F     rise, τ_on from Attack (*Ballistics*)
  F  ← F + (1 − a_ff)(F* − F)   otherwise     first-stage fall, τ_ff from Release
clamp: S ≤ σ·G_max, F ≤ G_max − S, G_max = e^(Range/(q·w)) − 1
reduction r = q·w·ln(1 + F + S),  q = 1 − 1/R (R = ∞: q = 1)
```

What each step stands on:

- **The light** follows the drive "very fast" (p. 1); the panel was chosen because neon and
  incandescent sources "took time to light up" (p. 9, **source**). Basalt gives the light no
  dynamics of its own beyond a 5 ms mean square, which turns the waveform into a level the cell can
  integrate. 5 ms is the same order as a photoresistor's own rise (NSL-32SR2: 5 ms to 63 %;
  VTL5C9: 4 ms; VTL5C10: 1 ms, **source**) and keeps the 1 kHz static error under 0.02 dB
  (*Gain computer*).
- **Light to conductance is a power law**, a straight line on log–log axes: the NSL-32SR2's graph
  (**source**, read from the page image) runs from about 1.1 kΩ at 0.1 mA to 125 Ω at 1 mA, 33 Ω at
  10 mA and 19 Ω at 40 mA, a slope of about 0.9 decades per decade falling to about 0.4; the
  VTL5C9 and 5C10 state slopes of 7.3 and 3.8 over a decade of current (0.86 and 0.58 decades per
  decade). In dB of level that is `G ∝ e^((u − T)/w)`, w setting the slope. The exponent varies by
  part and by light level, so it cannot fix a ratio (*Gain computer*); Basalt ties it to the Knee
  knob instead.
- **The divider** (Figure 5, **source**): the cell is the bottom leg of a divider, so the gain is
  `1/(1 + R_s·G)` and the reduction `20·log10(1 + R_s·G) = 8.686·ln(1 + G')` in dB with
  G' = R_s·G. Basalt's `q·w·ln(1 + G)` is that law with its scale generalised to q·w so Ratio and
  Knee can be set independently; the bare divider is the case q·w = 8.686 dB (**inference**).
- **ε**, the panel's onset: below a minimum drive an electroluminescent panel gives no light
  (**inference**, not from a datasheet read). It makes the reduction exactly 0 below
  `T + w·ln ε = T − 6w` (19.6 dB under Threshold at the default Knee), so the identity gate and the
  core's idle short circuit hold; above the threshold it moves the curve by under 0.01 dB.
- **Two-stage release with memory** (**source**, p. 10): once the light goes, the cell releases
  quickly (40–80 ms) to about half, and the rest can take "as much as several seconds"; how long it
  takes depends on how long and how brightly the cell was lit, so the release is slower after long
  or deep compression. The VTL5C datasheet specifies its curves and response times only after
  24 h of a stated light history (notes 2, 3, **source**), the same effect seen from the part's
  side. Basalt's S is that memory: it charges towards a share σ of the conductance while lit
  (slowly, τ_sc: "how long"), in proportion to the light ("how bright"), and holds the cell's
  conductance up after the fast part F has fallen.
- **The fast part** carries the attack (it fills the gap above the memory, so the attack does not
  wait for S) and the first release stage.
- **Stereo** (core): in Linked mode the *levels* are linked before the cell,
  `u_c ← (1 − λ)·u_c + λ·max(u_L, u_R)`; at Link 100 % one cell's computation drives both channels,
  so the gains are bit-identical and the light history is shared, as the core requires.
- **Look-ahead** (core): the window maximum acts on m.

| Constant | Value | Basis |
|---|---|---|
| τ_m | 5 ms; min(5 ms, 1.5 × Attack) below Attack 10/3 ms (M2) | Basalt's design, the photoresistors' rise times for scale; shortened where the light alone would take the whole Attack (*Implementation (M2)*) |
| ε | e^(−6) | Basalt's design (panel onset) |
| σ | 0.1 | Basalt's design, fitted to p. 1 and p. 10 (*Ballistics*) |
| τ_sc | 3 s, fixed | Basalt's design: exposure time belongs to the programme, not to the knob |
| τ_sd | 16 × Release, at most 25 s | Basalt's design, fitted to "0.5 to 5 seconds" at the 60 ms mark |

## 2. Gain computer: the cell's static curve

With the light and the cell settled (F + S = G*):

```
r_s(u) = min( q·w·ln(1 + max(e^((u − T)/w) − ε, 0)), Range )
q = 1 − 1/R,   w = (Knee + 12 dB)/5.5
```

- **Shape:** a softplus. Zero below T − 6w; at the threshold `q·w·ln(2 − ε)` = 0.693·q·w (1.70 dB
  at the defaults, the same as eq. 4's (1 − 1/R)·W/8 = 1.69 dB with W = Knee + 12); the local slope
  of the output is a logistic step from 1 to 1/R centred on T; the asymptote is the hard knee at T.
- **Knee** sets the width: the local slope goes from 10 % to 90 % of its change over 2·w·ln 9 =
  0.80·(Knee + 12) dB, the same as eq. 4's quadratic knee of width Knee + 12 dB (whose slope changes
  linearly). The cell is never hard: Knee 0 gives a 9.6 dB span, Knee 24 a 28.8 dB span
  (**Basalt's design**; the bare divider at 4:1 would be w = 11.6 dB, a 51 dB span).
- **Ratio** is the asymptotic ratio; ∞ (q = 1) is exact, the model being feed-forward.
- **The opto's ratios** (`PLAN.md`: "from the photocell model"). The manual gives only "a low
  compression ratio" for Compress and "a higher compression ratio" for Limit (p. 3, **source**).
  The photocell does not fix one: in a feed-forward cell the ratio is set by the light-to-
  conductance exponent, which the datasheets show varies from part to part (slope 7.3 vs 3.8) and
  along one part's curve (0.9 to 0.4 decades per decade), so a "physical" opto ratio would be an
  arbitrary choice of part (**inference**). The one measurement of the unit read is Wright &
  Välimäki's fit to Compress-mode recordings (Fig. 7, **source**): hard-knee curves of 2:1 to 4:1
  (thresholds −19 to −51 dB on their scale, Peak Reduction 60 to 100), or soft-knee curves of 5:1
  to 10:1 with knee widths of 19 to 27 dB. **Basalt's design:** the Ratio knob is the asymptotic
  ratio and the default 4 with the cell's knee (secant slope 3:1 from T to T + 20 dB) is the
  Compress-type sound; Limit-type settings start at 10:1, a value no source gives (**not
  verified**). UI marks: "Compress" at 4, "Limit" at 10.
- **Range** clamps the conductance (*Detector*), so the cell's state never holds more than the
  reduction shown, and its release starts from what was applied.

Measured (simulation, 1 kHz sine, Ratio 4, Knee 6, T = −30 dB, Attack ≈ 10 ms, Release 1 s,
Auto off, levels T − 20 … T + 30 dB in 5 dB steps): steady mean reduction within **0.016 dB** of
r_s. Pink noise is not gated (the 5 ms light reads noise's level fluctuations; the core's rule).

## 3. Ballistics

### Attack

**Attack** is the core's measured attack (constant-magnitude carrier, silence to 10 dB of
reduction, time to 63.2 %), **floored at 2 ms** (**Basalt's design**).

- The manual gives no figure ("very fast", p. 1); the 10 ms often quoted is not in it (`PLAN.md`).
  Wright & Välimäki's learned detectors (Fig. 8, **source**) put the LA-2A's attack time constant
  at 3.6 to 14 ms (switching one-pole models: 14.0 ms with a 78.8 ms release; 3.6 ms with 63.5 ms).
  **Basalt marks 10 ms**, the shared knob's default, inside that range.
- **Calibration:** the measured attack comes from the light's 5 ms mean square and the cell's rise
  together, so τ_on is solved from Attack. F's response to the time-varying G*(t) has no closed
  form, so the solve integrates numerically and does not belong on the audio thread
  (**Basalt's design**): τ_on/Attack is a **constant table** over log Attack (2 … 300 ms), Ratio
  and Knee, generated offline by the double-precision transcription (the reference step
  integrated exactly per sample at 192 kHz, bisection on τ_on to 10⁻⁴ of Attack), committed
  with its generator, and read by trilinear interpolation once per block (per sample while
  Attack glides). M2: the transcription is `tests/models/opto/Reference.h`, the generator the
  hidden test case `"[.generate]"` in `tests/models/opto/AttackTableTests.cpp`, the table
  `OptoAttackTable.h` here (*Implementation (M2)*, the grid). The table depends on no
  host rate (the light and F are continuous-time one-poles), so one table serves every rate. A
  test re-solves random points against the table (±1 % of Attack), and `[.perf]` confirms the
  per-block cost. M2 sets the grid. Measured (simulation, 1 kHz sine, Ratio 4, Knee 6):

  | τ_on | → 0 | 2 ms | 5 ms | 10 ms | 20 ms | 50 ms | 100 ms | 300 ms | 1 s |
  |---|---|---|---|---|---|---|---|---|---|
  | measured attack | 1.78 ms | 3.46 | 5.02 | 6.96 | 10.04 | 17.73 | 29.56 | 76.48 | 240.9 ms |

  Hence the floor: the light alone takes 1.8 ms at the defaults.
- **Bigger steps attack faster** in dB: the log of a conductance rising linearly reaches 63 % of a
  deeper target sooner. Reported, not gated; Wright & Välimäki observe the same in their learned
  model, whose time constant becomes very small for large inputs (§6, **source**).

### Release

**The Opto's Release is the time to 50 % of the reduction** (**Basalt's design**: it replaces the
core's 63 % for this model, so the knob means what the manufacturer's figure means: "approximately
0.06 seconds for 50 % release", p. 1). Measured from the moment the key goes silent after 10 dB of
reduction, with Auto release off. **Basalt marks 60 ms.**

- **First stage:** `τ_ff = (Release − Δ) / ln(G₀/G₁)`, with `G₀ = e^(10/(q·w)) − 1`,
  `G₁ = e^(5/(q·w)) − 1` (the conductances at 10 and 5 dB) and `Δ = τ_m·w/4.343`, the time the
  light's level takes to fall by w after the key stops. Release is floored at Δ + 1 ms.
- Measured (simulation, carrier, Auto off, before Δ): t50 = Release + 3.75, 2.50, 7.50 ms at
  (Ratio, Knee) = (4, 6), (2, 0), (20, 24), constant from Release 150 ms to 2 s; τ_m·w/4.343 gives
  3.77, 2.51, 7.54 ms. At Release 30 ms the offset is 0.6 ms larger, at 10 ms 1.8 ms larger, so the
  gate starts at 30 ms.
- **Depth slows the release without any memory:** with the divider's log law the conductance must
  fall further to halve a deeper reduction. Auto off, Release 60 ms: t50 = 64.0 ms after 10 dB,
  117.7 ms after 20 dB (simulation, before Δ). This is the manual's "slower if … the amount of
  compression is large" coming out of the cell law itself.

### Auto release: the memory

`autoRelease` On (default) runs the memory S; Off sets S = 0 and the Opto becomes the bare cell
(single-stage, depth-dependent by the log law only).

Measured (simulation, 48 kHz, carrier, Ratio 4, Knee 6, Attack ≈ 10 ms, Release 60 ms, held at the
level for the stated reduction, then silence; "full" = time until the reduction falls to 10 % of
its held value; τ_ff without the Δ correction, so the t50 column reads about 4 ms long):

| Held | Auto | t50 | Full release |
|---|---|---|---|
| 10 dB for 0.5 s | off | 64.0 ms | 0.14 s |
| 10 dB for 0.5 s | on | 67.0 ms | **0.55 s** |
| 10 dB for 10 s | on | 99.4 ms | **2.31 s** |
| 20 dB for 0.5 s | on | 159 ms | 3.60 s |
| 20 dB for 10 s | on | 1.69 s | 5.36 s |

Against the manual (p. 1): about 0.06 s to 50 % at moderate reduction, and complete release in 0.5
to 5 s depending on the previous reduction. `PLAN.md`'s gate asks the full release after 10 s at
10 dB to be at least 2 × that after 0.5 s: **4.2 ×**. The same constants were tried with σ = 0.04 …
0.1 and τ_sc = 2 … 3 s (simulation at a 2 kHz control rate, the level fed directly); σ = 0.1 and
τ_sc = 3 s span the manual's 0.5 … 5 s most closely while keeping t50 near 60 ms after short holds.
After 10 s at 20 dB the memory holds more than half the reduction, so t50 grows to 1.7 s; that is
the "slower" the manual describes, kept.

Release scales the memory's discharge (τ_sd = 16 × Release, so the full release scales with the
knob); the memory's charging time stays 3 s at every Release.

## 4. Make-up and gain law

- **The cell** is a shunt on a divider (Figure 5): the reduction law is `20·log10(1 + R_s·G)`,
  generalised to `q·w·ln(1 + G)` (*Detector*). The tube stages' colour belongs to Character
  (`PLAN.md`, *Models*).
- **Auto Gain** inverts the static curve at the reference level: `M = r_s(L_ref)` (core). Because of
  ε it is exactly 0 when the threshold lies more than 6w above L_ref.
- The applied gain is `10^(−r/20)`; the core's snap to an exact 1.0 applies (F decays towards 0, and
  reductions under 10⁻⁶ dB snap).

## 5. Gate table

| Gate | Opto |
|---|---|
| Identity | Ratio 1; Range 0; or input below T − 6w (the ε onset): bit-identical as the core, both latencies |
| Static curve | 1 kHz sine and the carrier (its level on the mean-square reading, 3.01 dB above its amplitude; core *Conventions*), T − 20 … T + 30 dB in 1 dB steps, T −60, −30, −10, R 1.5, 2, 4, 10, 20, ∞, Knee 0, 6, 24, Range 40 and 10, Attack 50 ms, Release 1 s, Auto off: steady reduction within ±0.25 dB of r_s. 100 Hz reported (the 5 ms light ripples at 200 Hz) |
| Ballistics, attack | carrier, silence → 10 dB, Knee 0 and 6, R 2, 4, 20: 63 % time = Attack ±10 % for Attack 2, 5, 10, 30, 100, 300 ms; below 2 ms it reads 2 ms ±10 % |
| Ballistics, release | Auto off, 10 dB then silence: t50 = Release ±10 % for Release 30, 60, 150 ms, 0.5, 2, 5 s, at R 2, 4, 20 and Knee 0, 6, 24 |
| Ballistics, history (`PLAN.md`'s Opto row) | Auto on, R 4, Knee 6, Release 60 ms: full release (to 10 %) after 0.5 s and after 10 s at 10 dB within ±10 % of `BasaltMeasure`'s double-precision transcription of these equations (with the Δ correction) on the same stimulus, and their ratio ≥ 2 at every Release 30 ms … 1 s and R 2 … 20. M0's 0.55 s and 2.31 s (a variant without Δ, levels fed at a 2 kHz control rate) are the expected values, reported beside the measurement, not targets |
| Ballistics, depth | Auto off, Release 60 ms, R 4, Knee 6: t50 after 20 dB / t50 after 10 dB within ±10 % of the transcription's ratio (expected about 1.84, the log law, simulated before the Δ correction) |
| Distortion | reported: THD+N of a 1 kHz sine at 6 dB reduction, Attack 2, 10, 50 ms, Release 60 ms, 0.5 s |
| Clicks | the core's list; Ratio and Knee jumps re-solve τ_on and τ_ff without moving F or S |
| Stereo | the core's; at Link 100 % the two channels' F and S are bit-identical over a 10 s excerpt |
| Learn | feed-forward with memory: Learn runs the cell on the captured per-millisecond envelope (`PLAN.md`); the 5 ms light on a 1 ms envelope is the approximation M3 measures |
| Auto Gain | `r_s(L_ref)` ±0.01 dB against the formula; the corpus gate as `PLAN.md` |
| Goldens | a vocal at the defaults, a bass at Release 60 ms, a mix bus at Ratio 2 |

## Implementation (M2)

Where this file left a choice open, or turned out to need one (**Basalt's design**, M2; each
measured in *Measured in M2*):

- **The light shortens at short Attack: τ_m = min(5 ms, 1.5 × Attack).** With τ_m fixed at 5 ms
  the light alone (τ_on → 0) reads 2.41 … 2.71 ms at Ratio ≥ 10 with a hard knee (1.84 ms at the
  defaults, which is all M0 checked), so Attack 2 ms and the floor's "below 2 ms reads 2 ms
  ±10 %" could not be met (+28 % at R ∞, Knee 0; computed on the transcription). From Attack
  10/3 ms up nothing changes; at the 2 ms floor τ_m is 3 ms and the light alone takes at most
  0.81 of Attack, which leaves τ_on room everywhere. Δ (the release's light delay) follows τ_m.
  Rejected: a floor per Ratio and Knee (up to 2.7 ms; a knob whose minimum moves with Ratio) and
  τ_m shortened at every Attack (more 1 kHz ripple at ordinary settings).
- **Range caps the light's conductance too:** G* ≤ G_max before the cell, the state clamps kept.
  With the state clamps alone a cell rising into Range stopped dead, a corner in the reduction
  (second difference 1.6·10⁻³ dB on a Threshold jump at Attack 10 ms, three times Clean's figure);
  capped at the target it rounds off. The static curve and the "never holds more than shown"
  guarantee are unchanged; a release still starts from the cap, once the light has fallen to it
  (20 dB of light over the cap is 20·τ_m/4.343 = 23 ms at τ_m 5 ms).
- **The τ_on table:** ln(τ_on/Attack) on 41 × 17 × 9 nodes: log Attack from 2 ms in steps of
  ln(5/3)/4 (to 331 ms; the light's corner at 10/3 ms is a node), 1/q from 1 (∞:1) to 3 (1.5:1)
  and 1/w uniform (Knee 24 … 0 dB), trilinear. ln(τ_on/Attack) is close to 3.68·(1/q)·(1/w) at long
  Attack (the log law), which the bilinear (1/q, 1/w) cell reproduces. Measured (48 kHz, 600
  random points, before committing): within 0.25 % of Attack; 0.41 % with 31 Attack nodes, 5.2 %
  with Knee uniform. The values span 0.20 (∞:1, Attack 2 ms) to 153 (1.5:1, hard knee, 300 ms):
  at low ratios a conductance rising linearly reaches 63 % of 10 dB in dB very early, so τ_on is
  far above Attack and the last tenths of a dB come slowly (*Measured in M2*, static).
- **Ratios under 1.5 take Ratio 1.5's τ_on and τ_ff** (q floored at 1/3 in the time laws only;
  the curve keeps the set ratio). A 10 dB reduction needs 30 dB of light over Threshold at 1.5:1
  and 90 dB at 1.1:1, so the knobs' reference step does not exist there, and the closed form's
  ln(G₀/G₁) runs to infinity as q → 0. Clean does the same below Ratio 2. Rejected: extending
  the table to q → 0 (τ_on → ∞).
- **Linked stereo:** the levels are linked before the cells as *Detector* says; each channel keeps
  its own cell, so at Link 100 % both cells see the same conductance and stay bit-identical from a
  reset (one computation drives both while they are equal), and at Link 0 % Linked is Unlinked to
  the bit. Between, each cell's memory follows its own linked level.
- **Curve changes** fade as the core's: the new law's cell starts from a copy of the running
  cell's F and S (conductances, not moved), the two reductions blended over 20 ms. A Ratio jump
  at a steady level therefore reaches the new curve in the fade's 20 ms (G* does not depend on
  q), a Knee or Threshold jump through the cell's own attack or release.
- **Auto release glides** as the time knobs: switched on, the memory starts charging from empty;
  switched off, its share hands over to the fast part over 20 ms (S·a/a_prev stays, the rest moves
  to F), so the conductance never jumps, and F then releases it at τ_ff.
- **The seed** goes through the divider's law: F = e^(r/(q·w)) − 1, S = 0 (memory empty, Settled
  33). `follow()` runs the light and its look-ahead window from reset with the core's warm-up
  weight; `settleMs` is 5·τ_m.
- **The snap:** a cell whose light is dark (G* = 0) and whose reduction is under 10⁻⁶ dB is set to
  exactly 0; with Auto on that comes when the memory has discharged that far (*Measured in M2*).
- **The order per sample:** light, window, link, then S, then F against G* − S (the S just
  updated), then the clamps; as *Detector* lists them.

## Measured in M2

**Measured (M2)**, the engine at 48 kHz unless named, `BasaltTests_opto` (`tests/models/opto/`);
the transcription is `tests/models/opto/Reference.h`, written from this file:

| What | Result | Test |
|---|---|---|
| Static curve, the gate's grid (T −60/−30/−10, R 1.5 … ∞, Knee 0/6/24, Range 40/10, T − 20 … T + 30 in 1 dB steps, Attack 50 ms, Release 1 s, Auto off) | 1 kHz sine: worst **0.0195 dB** (T −60, R 2, Knee 0, Range 10, at the cap); carrier: worst **0.0046 dB**. Every row within ±0.25 dB | *the static curve on 1 kHz sines and the carrier* |
| This file's point (1 kHz, R 4, Knee 6, T −30, Attack 10 ms, Release 1 s) | worst +0.040 dB against r_s (M0's simulation read 0.016 dB; not reproduced: the fast part rises at τ_on ≈ 20 ms and falls at τ_ff ≈ 460 ms, so it leans towards the crests of the light's 2 kHz ripple; M0's set-up is not recorded closely enough to say which differs); 100 Hz +0.373 dB, reported | *100 Hz and the NOTES' own point* |
| Attack, carrier, silence → 10 dB, Knee 0/6, R 2/4/20, Attack 2 … 300 ms (and 0.02 … 1 ms against the 2 ms floor) | within **0.89 %** of the knob or the floor (worst R 20, Knee 0, Attack 0.02 ms: 1.982 ms); D_final 10 dB within 10⁻⁴ | *Attack means the 63 % time …* |
| The table against a fresh solve, 60 random points (192 kHz) | worst **0.159 %** of Attack; at the nodes within 10⁻⁵ | *the committed attack table re-solves …* |
| Attack and Release at 44.1 and 96 kHz (R 4/20, Knee 0/6) | within 2.4 % of the knobs | *one table serves every host rate* |
| Release (t50), Auto off, R 2/4/20 × Knee 0/6/24 × Release 30 ms … 5 s | within **8.03 %** (Knee 24, R 2, Release 30 ms: 32.41 ms, the closed form's second-order error this file predicted); from 60 ms within 1.8 %; identical to the transcription | *Release means the time to 50 % …* |
| Depth, Auto off, Release 60 ms, R 4, Knee 6 | t50 60.29 ms after 10 dB, 110.58 ms after 20 dB: **1.834**, the transcription's 1.834 (M0 1.838 without Δ) | *depth slows the release …* |
| History, Auto on, R 4, Knee 6, Release 60 ms | full release **0.550 s** after 0.5 s and **2.313 s** after 10 s (× 4.21); transcription 0.550 / 2.313 s; M0 expected 0.55 / 2.31 s | *the cell's memory lengthens …* |
| History ratio, Release 30 ms … 1 s | R 2.5: 2.37 … 2.41; R 3: 2.92 … 2.98; R 4: 4.13 … 4.30; R 10: 9.08 … 9.55; R 20: 9.12 … 9.75; each within 10⁻³ of the transcription | same |
| **History ratio at R 2 (the gate's ≥ 2, not met)** | **1.86 … 1.88** at every Release, engine and transcription alike: 10 dB at 2:1 and Knee 6 takes 20 dB of light over Threshold, so 0.5 s already charges the memory to 3.4 dB and the full release after it is 2.0 s at Release 60 ms. Knee 0 misses up to R 4 (1.40 at R 2, 1.87 at R 4); Knee 24 passes everywhere (4.3 … 10.2) | *at Ratio 2 the memory's history ratio stays under 2* (`[!shouldfail]`) |
| Auto table (R 4, Knee 6, Release 60 ms; M0 without Δ) | t50 63.0 / 93.8 / 150.3 / 1685.8 ms, full 0.550 / 2.313 / 3.596 / 5.359 s for 10 dB 0.5 s / 10 dB 10 s / 20 dB 0.5 s / 20 dB 10 s (M0: 67.0 / 99.4 / 159 / 1690 ms, 0.55 / 2.31 / 3.60 / 5.36 s; the t50s 4 ms shorter by Δ, as M0 said) | *the release table of NOTES' Auto release* |
| Bigger steps attack faster (R 4, Knee 6) | Attack 10 ms: 16.1 / 10.0 / 4.30 / 2.05 ms for 5 / 10 / 20 / 30 dB; Attack 100 ms: 205 / 100 / 26.3 / 9.4 ms | *bigger steps attack faster, reported* |
| Identity: Ratio 1, Range 0, a key 0.25 dB (sine) or 0.01 dB (carrier) under T − 6w, linked, look-ahead 5 ms, Auto on and off | exactly 0 every sample, gain exactly 1 | *idle is exactly 0* |
| Exact 0 after a 15 dB burst | 0.26 s (Release 30 ms, Auto off); 42.7 s (150 ms, Auto on) and 285 s (1 s, Auto on): the memory discharges over 16 × Release before it is under 10⁻⁶ dB | same |
| Range 10, the light 30 dB over | the reduction never above 10 dB; Auto off t50 83.2 ms = Release + the light's 23.0 ms fall to the cap | *Range caps the cell …* |
| Auto Gain | r_s(−18 dB) within 0.01 dB, exactly 0 when T − 6w ≥ −18 dB | *Auto Gain reads r_s at the reference* |
| Link 100 %, 10 s of two bursty noises, look-ahead 2 ms, three settings | reductions, F and S bit-identical every block; Link 0 % = Unlinked to the bit | *Link 100 % gives both channels the same cell …* |
| Look-ahead 3 and 10 ms on the carrier | attack side = the LA 0 trace shifted, release side identical: 0 dB with Auto off; with Auto on the release side differs by 0.010 / 0.032 dB (the cell is lit the look-ahead longer) | *look-ahead starts the attack early …* |
| Seed at 10 dB, Attack 300 ms, after `follow()` for 5·τ_m | Auto off: 10.000000 dB throughout; Auto on: 10.000 … 10.026 dB (the empty memory charges and F follows it down at τ_ff: F + S runs τ_ff·dS/dt over G* meanwhile) | *a seed starts the cell …* |
| A switch mid-attack (Attack 300 ms, 16 dB) against an engine that ran throughout | Auto off within 7·10⁻¹⁴ dB; Auto on within 0.012 dB (the memory starts empty) | same |
| Knob jumps every 0.31 s on a key toggling 30 dB between them, Attack 2 and 10 ms | largest step per sample ≤ 0.023 dB, second difference ≤ 2.2·10⁻⁴ dB (Attack 2 ↔ 300 ms; Threshold 7·10⁻⁵, Ratio 7.5·10⁻⁵, Knee 1.7·10⁻⁵, Range 5.6·10⁻⁵, Release 3·10⁻⁷, Auto release 1.7·10⁻⁶) | *a knob jump neither steps nor bends …* |
| Block sizes 1, 7, 64, 4096 and a mixed cycle, with every knob changed at one sample | bit-identical | *block sizes change nothing* |
| Allocation (process, follow, seed, staticReduction, random settings and block sizes, mono and stereo) | none | *allocates nothing once prepared* |
| Denormals without flush-to-zero, 3 s of noise then 150 s (Auto off) and 700 s (Auto on) of silence at Attack 300 ms, Release 5 s | none; the reduction, F and S end exactly 0 | *nothing goes denormal …* |
| Keys at ±10⁶ and what `saneKey` makes of NaN and ∞, every corner of Threshold, Ratio (1, 1.0001, 4, ∞), Knee, Range (0, 0.01, 40), the time knobs, Auto, Link 50 % | finite, ≥ 0, ≤ Range | *no key or setting the chain can hand it gives a NaN* |
| THD+N, 1 kHz at 6 dB, R 4, hard knee (reported) | Attack 2 ms: −85.5 / −101.7 dBc at Release 60 / 500 ms; 10 ms: −100.9 / −111.1; 50 ms: −116.6 / −121.3 | *THD+N … reported* |
| CPU, stereo, 48 kHz, look-ahead 5 ms, pink noise, with the gain conversion and the matrix (`[.perf]`, load average 4–5) | **0.242 %** of a core at the defaults (Linked 100 %, Auto on); 0.231 % Attack 2 ms Auto off; 0.322 % Link 50 %; 0.305 % Unlinked; 0.309 % Mid/Side; 0.435 % with Threshold moving every block (two cells fading). `PLAN.md`'s budget 0.5 % | *CPU, stereo at 48 kHz* |

- **Low ratios settle slowly upwards.** At 1.5:1 with a hard knee τ_on is 152 × Attack (7.6 s at
  Attack 50 ms), so after a 1 dB step up the last 0.01 dB takes about 30 s; the static test
  therefore steps downwards there, where τ_ff (145 ms at Release 1 s) settles it, the steady
  state being the same. `ref::stepSettleS` gives the time per step; `BasaltMeasure` needs it.
- **What `BasaltMeasure` compares against** (`tests/models/opto/Reference.h`):
  `staticReduction` (r_s), `levelForReduction` (the carrier for a depth; its amplitude 3.01 dB
  below the level), `expectedAttackMs` (Attack floored at 2 ms), `expectedReleaseMs` (Release
  floored at Δ + 1 ms, as t50), `releaseAfterHold`, `fullReleaseS` and `depthRatio` (the history
  and depth rows on the transcription's cell, τ_on from `solveAttackConstantMs` or the engine's
  table), `historyRatioFloor` with M0's expected values, and `stepSettleS`.

## Through the processor (M2)

`BasaltMeasure all --model opto` (48 kHz, Normal and Live, 2026-11-17), against
`tests/models/opto/Reference.h` (τ_on solved on it at 48 kHz):

- **Identity:** 226 rows bit-identical, the noise under T − 6w at Threshold 0, Knee 24.
- **Static:** 217 pass (Attack 50 ms, the staircase stepping the way the cell settles faster),
  worst 0.033 dB; 100 Hz reported up to 0.29 dB, 50 Hz 0.56 dB.
- **Ballistics:** attack 0.991–1.000 × max(Attack, 2 ms) over R 2, 4, 20 × Knee 0, 6, held eight
  τ_on so D_final has settled; release t50 1.000–1.080 × the expectation (Knee 24, R 2, 30 ms the
  worst, as predicted); history equal to the transcription (R 4, Release 60 ms: 0.550 and 2.313 s,
  × 4.21); depth 1.834 against 1.834. **The history ratio fails at Ratio 2** (1.863–1.880 over
  Release 30 ms … 1 s, ≥ 2 gated), equal to the transcription; proposed change 10 stands.
- **Clicks:** every model switch (−77.1 dB) and no-swell row pass; Key Bell Gain at Attack 10 ms
  reads −59.98 dB against −60 (a 36 dB key jump through τ_on), the one failing row with Live's.
- **Stereo:** Link blends the levels before the cells, so Link 50 % puts the quiet channel's cell
  at the mean of the two levels in dB: within the gate.
- **Cost:** 0.253 % of a core at the defaults, 0.336 % with the whole key path, 0.395 % Mid/Side
  with it.

## Proposed changes to PLAN.md

1. ***The reference behaviour*, Opto row, side chain:** keep **feed-forward**, and add "the
   manual's figures (Fig. 3, p. 7; Fig. 5, p. 9) and p. 8 show it; its p. 11 calls the unit
   'feed-back style', contradicting them; Basalt follows the figures".
2. **Opto row, NSL-32SR2:** "The decay figure was not legible, so it is unverified" → "decay 5 ms
   typical to 100 kΩ after 16 mA is removed; rise 5 ms to 63 %; 140 Ω at 1 mA, ≤ 40 Ω at 20 mA (rev.
   12-02-15, read)". The same correction in *References*.
3. **Opto row, ratio:** "no figures published, so M0 sets them from the photocell model" → "the
   photocell does not fix a ratio (its exponent varies by part and light level); Wright & Välimäki
   2022 fit Compress-mode recordings at 2–4:1 hard-knee or 5–10:1 with a 19–27 dB knee. Basalt: Ratio
   is the asymptotic ratio of the cell's soft curve, marks Compress 4, Limit 10".
4. **Opto row, attack:** add "Wright & Välimäki 2022 fit 3.6–14 ms; Basalt marks 10 ms (the knob's
   default), floor 2 ms".
5. **Opto row, release:** add "the Opto's Release knob is the time to 50 % (the manual's measure);
   Auto release on = the cell's memory, off = the bare cell".
6. ***Measurement and quality gates*, Ballistics, Opto:** define "full release" as the time to 10 %
   of the held reduction; gate the history against `BasaltMeasure`'s transcription of these
   equations (±10 %) next to the ratio ≥ 2, reporting M0's 0.55 s and 2.31 s (Release 60 ms,
   Ratio 4, Knee 6) as expected values.
7. ***Controls*, Release:** "Auto release may go longer where its reference does (the 670-type
   25 s)" also covers the Opto's memory (up to 25 s of discharge constant).
8. ***References*, Opto:** add the VTL5C catalogue pages (55–56) and the NSL-32SR2 figures; Wright &
   Välimäki: add "learned attack time constants 3.6–14 ms, release time constants 63.5 and 78.8 ms (Fig. 8); Compress-mode
   curves (Fig. 7)".
9. **(M2) *The reference behaviour*, Opto row, attack:** "the light's constant shortens to
   1.5 × Attack below 10/3 ms, so the 2 ms floor holds at every Ratio and Knee".
10. **(M2) *Measurement and quality gates*, Ballistics, Opto:** the history ratio ≥ 2 holds from
    Ratio 2.5 at Knee 6 (2.37 … 9.75 measured) but not at Ratio 2 (1.86 … 1.88), by the specified
    equations themselves. To decide: accept it (deep light at low ratios charges the memory even
    in half a second: the "how bright" half of the manual's dependence) and gate from 2.5, or
    change the memory's law. Not decided here; the gate stands and the row is recorded as failing.

## Not verified

- Any T4 datasheet or measurement; the panel's light-versus-voltage law and the onset ε.
- Limit mode's ratio (no source gives a figure; SignalTrain's Limit recordings were not used by
  Wright & Välimäki).
- The manual's "feed-back style" sentence beyond the contradiction recorded above.
- Eichas & Zölzer 2016 (record only), Parker & D'Angelo 2013 and Simionato & Fasciani 2025
  (abstracts only).
- Everything under M0's marks is a simulation of these equations; *Measured in M2* repeats it on the
  engine at model level. Through the processor (`BasaltMeasure`) once the chain runs the Opto.
