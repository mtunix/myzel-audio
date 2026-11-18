# VCA: console bus and RMS compressors

A VCA compressor with two detectors, switched by `vcaDetector` (`PLAN.md`, *VCA detector*,
Settled 23). **Peak** (default) behaves as the console bus compressor of the SSL G-series: a
full-wave peak detector per channel, the louder channel in control, stepped attack and release, an
effective threshold that moves with the ratio, and a two-stage Auto release. **RMS** behaves as the
dbx 160A and the THAT 2252 detector inside it: true RMS with one time constant, so the attack
speeds up for larger level changes and the release is a constant rate in dB/s. Both share the gain
cell, the gain computer and the Ratio marks; detector and ballistics change. Product names appear
only here and in `PLAN.md` (root `NOTICE`).

Status: M0 specification, implemented in M2 (2026-11-17) as `VcaModel` in this folder, one
instance per detector, and checked at model level by `BasaltTests_vca` (`tests/models/vca/`, its
`Reference.h` the specification as pure functions for `BasaltMeasure`); *Implementation (M2)*
records what the code decided where this file left it open, *Measured in M2* what the engine
reads. Not yet wired into the chain (`../../chain/Engines.h`). The shared core
(`../../dynamics/NOTES.md`) holds the conventions, the detector scale, the look-ahead window,
Range, Link, the measurement definition and the release calibration h(ρ); this file uses them and
names what it overrides. Marks as in the core: **source** (read in the cited document),
**measured (simulation)** (Python/NumPy/SciPy, double precision, 48 kHz, set-ups given with the
numbers, scripts in the M0 session's scratchpad, not kept), **measured (M2)** (the C++ engine at
48 kHz, by the unit tests named), **Basalt's design**, **inference**, **not verified**.

## Sources

| Source | What it gives | Read |
|---|---|---|
| Solid State Logic, *XLogic G-Series Stereo Compressor Owner's Manual*, 82S6XL051A, rev. 0A, June 2005 | attack 0.1, 0.3, 1, 3, 10, 30 ms; release 0.1, 0.3, 0.6, 1.2 s and Auto; ratio 2, 4, 10; threshold and make-up continuous; the knee point moves with the ratio on purpose (a lower ratio lowers the effective threshold, to hold perceived loudness); with the internal side chain the loudest channel wins; external key mono | read in full |
| Solid State Logic, *500 Series G Comp Module User Guide*, rev. 2.0, June 2020 | the same ratio/threshold coupling; left and right rectified separately by a true-peak full-wave detector, the dominant (louder) channel sets the reduction through the selected time constants; a side-chain high-pass; the main VCA always in circuit | read in full |
| Solid State Logic, *THE BUS+ User Guide*, rev. 1.1, April 2022, solidstatelogic.com (`The_Bus_User_Guide_1.1.pdf`) | the bus compressor's side chain is a feedback loop fed from a feed-forward position (its F/B switch moves the feed after the main VCA instead); **Auto**, the same as on every earlier SSL bus compressor, is a two-stage release, 100 ms short and 12 s long, louder parts released fast and quieter parts slowly; Auto 2 is 50 ms / 6 s; THAT 2181 VCAs; a summed (instead of separately rectified) side chain as an alternative stereo mode | read: *Overview*, *Bus Compressor Controls*, *Bus Compressor Switches*, *Specifications*. New in M0; not in `PLAN.md`'s references |
| dbx, *160A Compressor/Limiter* specification sheet (dealer's copy) | true RMS detection; attack program-dependent, typically 15 ms for 10 dB, 5 ms for 20 dB, 3 ms for 30 dB; release program-dependent, typically 8 ms for 1 dB, 80 ms for 10 dB, 400 ms for 50 dB, a 125 dB/s rate; ratio 1:1 through ∞:1 to −1:1; threshold −40 … +20 dBu; OverEasy or hard knee; stereo strapping by true-RMS power summing; THD < 0.2 % typical up to 40 dB of compression at 1 kHz | read in full. The other dbx files in the planning library (`dbx160.pdf`, `dbx160acut.pdf`, `bh984286.pdf`) are failed downloads (a 404 page and two bot-check pages): **not obtained** |
| THAT Corporation, *2252 IC RMS-Level Detector*, doc. 600032 rev. 03 | a log-domain one-pole on the square: τ = C_T·V_T/I_T (≈ C_T·0.0259/I_T at 300 K), typical I_T 7.5 µA; attack and release locked together, separate adjustment impossible with RMS response; the release a straight line in dB, the attack (1 − e^(−t/τ)) steepened by the log; ripple at 2f falls as τ rises | read: *Theory of Operation*, *Time Constants*, electrical characteristics |
| THAT Corporation, *2180 Series Blackmer Pre-Trimmed IC VCAs*, doc. 600029 rev. 02, 2008 | gain exponential in the control voltage: ±6.1 mV/dB typical (6.0 … 6.2), so control voltages add in dB | read: electrical characteristics |
| THAT Corporation, *THAT4305 Analog Engine*, doc. 600067 rev. 02, 2015 | typical RMS time constant 32 ms (5 Hz); attack and release fixed in relation; feed-forward compressors cannot oscillate, reach ∞ with unity side-chain gain and make negative ratios easy; feedback compressors are limited to about 20:1 and their loop shortens the attack as the ratio rises; for fast peak-like gating, use the detector as a fast log rectifier and put the attack/release stage after it | read: *Feedforward Topologies*, *Feedback Topologies*, detector section |
| THAT Corporation, *THAT4301 Analog Engine*, doc. 600069 rev. 10, 2017 | 32 ms typical RMS constant; its compressor application is DN00A's hard-knee circuit | skimmed (searched for time constants and topology) |
| THAT Corporation, *Design Note 00A: Basic Compressor/Limiter*, doc. 600135 rev. 01, 2009 | a feed-forward hard-knee and soft-knee compressor; 2252 integration ≈ 30 ms; ratio 1/(1 − R) from the side-chain gain; the soft knee from an open-loop diode in the threshold detector; 6 mV/dB scaling; Floru's preprint 4054 for RMS time constants | read in full |
| THAT Corporation, *Design Note 01A: The Mathematics of Log-Based Dynamic Processors*, doc. 600047 rev. 02, 2009 | feed-forward ratio 1/(1 − G), feedback ratio 1 + G; negative ratios and ∞:1 possible only feed-forward | read in full |
| `../../dynamics/NOTES.md` | detector scale, smooth decoupled ballistics and h(ρ), Range before the ballistics, Link, look-ahead window, measurement definition, snap to exactly 0 | Basalt |
| Search-result summaries of three reviews (Sound On Sound, Bonedo, Tape Op) describing the classic bus compressor as a feedback design with the detector on an auxiliary VCA | consistent with the Bus+ guide | **search summaries only, not read**; nothing below rests on them |

Not found: an SSL document giving the knee shape, the size of the threshold–ratio coupling, the
attack and release definitions (time constants or measured times) or the Auto circuit. No
schematic was consulted.

## What the references say

### Peak: the console bus type

- **Steps** (source: XLogic, 500 guide): Attack 0.1, 0.3, 1, 3, 10, 30 ms; Release 0.1, 0.3, 0.6,
  1.2 s and Auto; Ratio 2, 4, 10. The Bus+ adds attack 6, 15, 20, 40 ms, release 0.05 and 0.15 s,
  ratios 1.3:1 to negative, and Auto 2 (source); Basalt takes the classic steps as marks and
  the Bus+'s Auto 2 as a second Auto mark.
- **Detector** (source: 500 guide): each channel rectified by a true-peak full-wave detector; the
  louder channel drives the reduction of both.
- **Threshold moves with Ratio** (source: XLogic and 500 guide, the same sentence in both): a lower
  ratio lowers the effective threshold, deliberately, to hold perceived loudness. Neither says by
  how much (**not verified**).
- **Auto** (source: Bus+ guide): two stages, 100 ms and 12 s; louder material released fast,
  quieter material slowly; Auto 2 50 ms and 6 s. `PLAN.md`'s "SSL does not document Auto" is out
  of date: the Bus+ guide documents it, and says it is the Auto of every earlier SSL bus
  compressor. Whether 100 ms and 12 s are time constants or measured times is not said
  (**not verified**).
- **Topology** (source: Bus+ guide): the side chain is a feedback loop, but the signal feeding it is
  taken from a feed-forward position; the Bus+'s F/B switch instead feeds it from after the main
  VCA, for a more relaxed compression. **Inference:** a feedback loop inside the side chain fed
  with the input means the detector sees the input with the loop's own gain applied, an output
  estimate built from the input. That is Giannoulis 2012 §3.1.1's digital feedback (the previous
  gain applied to the current input), which the core implements.

### RMS: the dbx/THAT type

- **One time constant** (source: 2252): the 2252 filters the square of the input in the log domain
  with one pole; attack and release are locked. Released level falls as a straight line in dB.
- **The 160A's constant** (inference from the sheet and the 2252): a constant release of 125 dB/s
  is a power one-pole decaying at 10·log10(e)/τ dB/s, so **τ = 4.343/125 s = 34.74 ms**. The 2252's
  typical circuit (C_T 10 µF, I_T 7.5 µA) gives τ = 10 µF × 0.0259 V / 7.5 µA = 34.5 ms; DN00A
  says ≈ 30 ms, the 4301 and 4305 sheets 32 ms. The sheet's three release figures (8 ms/1 dB,
  80 ms/10 dB, 400 ms/50 dB) are all exactly 125 dB/s.
- **Attack** (source: 160A): typically 15, 5, 3 ms for 10, 20, 30 dB.
- **Topology** (inference): the 160A offers −1:1; DN01A and the 4305 sheet say negative ratios
  (and ∞:1) are only possible feed-forward. So the 160A is feed-forward.
- **Stereo** (source: 160A): two units strap for stereo by true-RMS power summing.

## M0's reading of the topology, and the decision

The Peak detector's reference is a loop inside the side chain (above). Basalt nevertheless builds
**both detectors feed-forward** (**Basalt's design**, as `PLAN.md` already says):

- With a log-domain detector and a dB-linear VCA, a closed loop at ratio R divides the detector's
  time constant by R (Floru 1998; THAT 4305; the core measured 0.499/0.248/0.123/0.048 at R
  2/4/8/20) and otherwise has the same static curve. Basalt's Attack and Release are *measured*
  times (`PLAN.md`, *Feedback models*), so the main audible effect of the loop is calibrated away
  in either topology.
- What remains is second-order. Measured (simulation): a peak compressor with an analog-style
  branching detector (eq. 15), 1 kHz sine, levels T − 10 → T + 12 → T + 3 dB, Attack 10 ms, Release
  100 ms, the feedback loop with its detector constant multiplied by R against feed-forward:
  largest difference of the reduction 0.12 / 0.30 / 0.47 dB while attacking and 0.33 / 0.60 /
  0.77 dB while releasing, at R 2 / 4 / 10. The core lists the same residue for its loop (a
  partial release snaps at ≈ 0.65 of the set time; the knee region is slower).
- Feed-forward keeps ∞:1 (feedback caps at 50 in the core), lets Learn solve exactly on the
  captured envelope, and costs less.
- **A loop must never use a decoupled peak hold on its demand**: same simulation with eq. 14 in the
  loop, the hold latched the loop's onset demand, (R − 1)·(level − T), and over-compressed by up to
  29.7 dB at R = 10. The core rejects this variant too (DH); recorded here because it is the
  obvious way to "make the console's detector feedback".

M2 may A/B the core's feedback loop on the Peak detector by ear; switching costs one flag, since
the core implements both. Any change is a NOTES change before code.

## 1. Detector

Both detectors are feed-forward, on the key after the key EQ and the look-ahead window, levels on
the core's detector scale (a sine reads its peak).

### Peak

- **Level:** `L_P[n] = 20·log10|k[n]|` per channel, the core's peak level, through the look-ahead
  window's running maximum (core, *Look-ahead*). This is the full-wave rectifier (source: 500
  guide).
- **Sample peak, not inter-sample peak** (**Basalt's design**): the analog rectifier sees the
  continuous waveform; a sample peak under-reads a sine whose peak falls between samples by up to
  20·log10(cos(π f/fs)): −0.47 dB at 5 kHz, −2.01 dB at 10 kHz, −3.01 dB at 12 kHz (48 kHz);
  −0.56, −2.42, −3.66 dB at 44.1 kHz. Over several cycles the release holds the largest sample, so
  the shortfall is smaller on most material. Accepted to keep the key path shared and Live at zero
  latency; the rejected alternative is a 4× polyphase interpolated peak (≈ 48 multiply-adds per
  sample and channel, a few samples of key delay).
- **Stereo dominant:** the core's Link with λ = Link/100; at 100 % each channel's reduction is the
  louder channel's (core, *Stereo*). Nothing to override.

### RMS

- **Level:** the core's RMS level, `m[n] = β·m[n−1] + (1 − β)·k²[n]`,
  `L_R[n] = 10·log10(2·m[n])`, with **τ_RMS = Attack / 0.45442** (*3. Ballistics*). One pole on
  the square, exactly the 2252's arrangement (source), so attack and release of the detector are
  one constant.
- **Linked: power summing** (**Basalt's design**, from the 160A's strapping; overrides the core's
  louder-wins link for this detector only): with `stereo` Linked, the linked level is
  `L_Σ = 10·log10(m_L + m_R)` (twice the mean of the two mean squares, so identical channels read
  as one), and the core's link blends each channel's own reduction with r(L_Σ) instead of
  max(r_L, r_R): `r_c = (1 − λ)·r(L_c) + λ·r(L_Σ)`. A source panned hard to one side reads 3 dB
  below its louder-wins reading; a centred source reads the same. Gains stay bit-identical at
  100 % (one level for both channels).
- Look-ahead acts on the mean square (the core's window on m), so the RMS's rise is advanced, not
  sharpened.

## 2. Gain computer

Shared by both detectors (`PLAN.md`, *VCA detector*). The core's eq. 4 with one change, the
threshold that moves with the ratio:

```
T_eff = T − S / R                    S = 6 dB, R the set ratio (1/R = 0 at ∞)
r(x)  = min( x − G(x; T_eff, R, W), Range )      G: the core's eq. 4, W = Knee
```

- **The coupling** (**Basalt's design** on the SSL manuals' statement; the size is ours): T_eff
  sits 3 dB below Threshold at 2:1, 1.5 dB at 4:1, 0.6 dB at 10:1, 0.3 dB at 20:1 and on Threshold
  at ∞. At 1:1 the shift is 6 dB, but the curve is the identity there, so it changes nothing and
  the core's exact zero at Ratio 1 holds. The reductions at 6 dB over Threshold become 4.5, 5.6,
  5.9, 6.0 dB at 2, 4, 10, ∞ (without the coupling: 3.0, 4.5, 5.4, 6.0): lowering the ratio loses
  less level, which is what the manuals say the coupling is for. S is voiced by ear in M2 between
  3 and 9 dB; a change is an intended sound change (NOTES, goldens).
- **Why kept:** `PLAN.md` keeps such couplings "where the NOTES argue they define the sound". Both
  SSL manuals call it deliberate and give it a reason; it changes how the Ratio knob feels on a
  bus. Threshold remains the knee at ∞:1, so the knob's value and the curve agree at the top
  ratio, and the transfer-curve display draws the knee at T_eff (`PLAN.md`, *UI*).
- **Both detectors** use it, since the gain computer is shared. The 160A's sheet is silent on
  such a coupling (**not verified** either way).
- **Knee shape:** the core's quadratic (eq. 4), centred on T_eff, width Knee. The SSL documents
  give no knee (**not verified**); the 160A's OverEasy is a soft knee whose shape is not published.
  DN00A's soft knee comes from an open-loop diode, which never reaches exactly zero below the
  threshold; the quadratic does, which the identity gate needs (**Basalt's design**).
- **Ratio:** 1 … 20 and ∞, continuous, feed-forward, so ∞ is exact (1/R = 0). Marks at 2, 4, 10
  (the console steps). The 160A's negative ratios are not offered (`PLAN.md`).
- **Range** is the core's: on the static reduction, before the ballistics.

## 3. Ballistics

### Peak: manual release

The core's feed-forward ballistics unchanged: smooth decoupled (eq. 17) in the log domain, Attack
→ τ_A, Release → τ_R = τ_A·h(Release/τ_A), minimum release 1.01 × Attack, snap below 10⁻⁶ dB.

- **Marks** on the continuous knobs (detents in the UI, `PLAN.md`): Attack 0.1, 0.3, 1, 3, 10,
  30 ms; Release 100, 300, 600, 1200 ms. The knob values are the measured times of the core's
  definition (63.2 % of the reduction, recovery to 36.8 %). SSL does not say whether its figures
  are capacitor time constants or measured times, and with a feedback side chain a time constant
  would measure about 1/R as fast in attack (**not verified**). Basalt reads them as measured
  times.
- **Program dependence** with Auto off: none beyond the peak hold (the reduction holds the
  largest recent peak). Depth does not change the times (log-domain placement, core).

### Peak: Auto, the two-stage release (Basalt's design on the Bus+ figures)

The decoupled detector's first stage (the peak hold that releases with τ_R) becomes a
two-capacitor network in the dB domain, the textbook dual release: a small capacitor C₁ that the
rectifier charges, discharging through R₁₂ into a large capacitor C₂, which leaks to ground through
R₂. A VCA's control voltage is linear in dB (2180: −6.1 mV/dB, source), so capacitor voltages are
reductions in dB. Per channel (linked as the core's Link), with r[n] the static reduction:

```
d/dt v1 = −(v1 − v2) / τ1
d/dt v2 =  (v1 − v2) / (m·τ1) − v2 / τ2           (between samples)
v1 = max(v1, r[n])                                  (the rectifier tops the small capacitor up)
y  = α_A·y + (1 − α_A)·v1                           (the core's attack stage)
```

- **Relaxation:** both capacitors discharge toward 0 and the rectifier catches v1 at the target
  (the analog circuit), where the manual first stage (eq. 17) relaxes toward the target. For a
  release to below Threshold the two are the same, so the core's h(ρ) calibrates τ1 unchanged.
- **Constants:** τ1 = τ_A·h(Release/τ_A), the same calibration as manual, so a short burst
  releases at the set Release; **τ2 = min(120 × Release, 20 s)**; **m = C₂/C₁ = 10**.
- **Why 120 and the marks:** the Bus+'s two Autos are 100 ms / 12 s and 50 ms / 6 s, both a ratio
  of 120. With Auto on, Release 100 ms is the console's Auto and Release 50 ms its Auto 2. Reading
  the published pairs as the two stages' time constants is an interpretation (**not verified**).
  The 20 s cap keeps long base releases usable; above Release 167 ms the slow stage is 20 s.
- **What it does:** during sustained reduction C₂ charges from C₁ (time constant ≈ m·τ1·τ2/(τ2 + m·τ1),
  about 0.9 s at Release 100 ms) toward τ2/(τ2 + m·τ1) of the peak reduction (0.92). When the level
  drops, C₁ discharges quickly into C₂, the two share their charge, and the rest leaves slowly
  through R₂. A transient on its own leaves C₂ nearly empty and releases fast, except for a residue
  of about 1/(1 + m) of the reduction (charge sharing) that fades over the slow stage; sustained
  reduction releases mostly at the slow rate. Peaks above a held floor release fast down to it:
  "louder elements fast, quieter elements slowly" (source: Bus+).
- **Discretisation:** the 2 × 2 linear system is advanced exactly (matrix exponential of the
  system matrix over one sample, recomputed when Release or the rate changes), then the max. No
  allocation; four multiply-adds per sample and channel.
- **Switching Auto** keeps v1 (no step in the reduction). Off → on starts v2 at 0; on → off drops
  v2 and v1 then releases with τ1 alone.
- **Release reference:** with Auto, the hold does not always settle (a 20 ms hold at Attack 10 ms
  reaches 8.65 of 10 dB), so "release to 36.8 %" means 36.8 % of the reduction applied at the
  moment the signal stops, here and in the gates (**Basalt's design**).
- **Measured (simulation):** Attack 1 ms, Release 100 ms (τ2 = 12 s), a 10 dB static reduction held
  for 20 ms, 1 s or 5 s, then silence; reduction in dB after the step:

  | Held | +0.1 s | +0.3 s | +1 s | +3 s | +10 s | +30 s | release to 36.8 % | C₂ at the step |
  |---|---|---|---|---|---|---|---|---|
  | 20 ms | 4.05 | 1.40 | 1.02 | 0.88 | 0.52 | 0.11 | 0.112 s | 0.20 dB |
  | 1 s | 7.64 | 6.52 | 6.06 | 5.21 | 3.07 | 0.67 | 7.6 s | 6.14 dB |
  | 5 s | 9.49 | 9.15 | 8.66 | 7.44 | 4.38 | 0.96 | 12.3 s | 9.20 dB |

  Same with Attack 10 ms, Release 150 ms (τ2 = 18 s): 0.192 s after 20 ms (against the 8.65 dB
  applied when the signal stops; 0.165 s against the 10 dB target), 7.9 s after 1 s, 18.2 s after
  5 s. Attack 0.3 ms, Release 50 ms (Auto 2, τ2 = 6 s): 0.058 s, 5.5 s, 6.2 s.
- **THD** with Auto at Attack 10 ms, Release 100 ms, 1 kHz, 6 dB reduction at 4:1: the network's
  slow floor reduces the droop between peaks. M0 printed −97.8 dBc here, which is higher than the
  manual release's −107.2 and contradicts that reason; M2 reads **−126.9 dBc** once C₂ has charged
  (10 s) and −106.4 dBc after 25 attack constants, when it has hardly begun, on the engine and on
  an independent re-run of these equations alike (*Measured in M2*).

### RMS

**Attack** scales the RMS time constant, **Release** sets a constant recovery rate, **Auto**
dims (`PLAN.md`, *VCA detector*). Per channel (linked as in *1. Detector*):

```
τ_RMS = Attack / 0.45442                     the detector (1. Detector)
r_t   = r(L_R)                               the static reduction, 2. Gain computer
y     = r_t                          if r_t ≥ y        (attack: the detector alone)
y     = max(r_t, y − 10·Release⁻¹/fs) otherwise        (release: at most 10 dB per Release)
```

- **Attack mapping:** 0.45442 is the time, in units of τ, for a one-pole on the square to move the
  level from Threshold to 63.2 % of a 10 dB rise (1 − exp(−t/τ) solved for the level 6.32 dB up;
  0.19299 for 20 dB, 0.08105 for 30 dB; derived here, checked by simulation). So **Attack reads as
  the measured attack of a 10 dB step from Threshold**, and larger steps are faster by the factors
  0.42 (20 dB) and 0.18 (30 dB): the 160A's program dependence, by construction.
- **The 160A mark: Attack 15.79 ms, Release 80 ms.** There τ_RMS = 34.74 ms, the constant implied
  by 125 dB/s, and the release limiter's rate equals the detector's own fall at ∞:1, so the model
  is one time constant, as the 2252. Measured (simulation, the core's constant-magnitude carrier,
  Ratio ∞, Knee 0, steps from Threshold): attacks **15.75, 6.69, 2.79 ms** for 10, 20, 30 dB
  (formula 15.79, 6.71, 2.82); on a 1 kHz sine 15.75, 6.73, 2.77 ms.
- **Against the sheet:** 15.8 vs 15 ms (+5 %), 6.7 vs 5 ms (+34 %), 2.8 vs 3 ms (−6 %). No single
  pole meets all three: fitting one τ to 15/5/3 ms under any fraction of the final reduction
  (30 … 99 %) and starting level (at Threshold, 3 … 20 dB below, silence), the smallest worst
  error is **17 %** (measured, simulation; from silence, 75 % of the final reduction, τ = 15 ms,
  giving 12.6/5.9/3.1 ms). The sheet says "typically" and gives no definition. Basalt keeps the
  exact single pole, which is what the 2252 is, and gates against its own formula (*Gates*).
- **Release** is a recovery-rate limiter on the reduction: 10 dB per Release, constant in dB/s at
  every depth (the 160A's constant rate), so **Release reads as the time for 10 dB of recovery**
  (`PLAN.md`). Measured (simulation, carrier at T + D, then silence, Ratio ∞, slope from 90 % to
  10 % of D): **80.0 ms per 10 dB** at D = 6, 12, 24 dB with Release 80 ms; 500.0 ms at 500 ms.
- **The detector's floor:** the reduction cannot recover faster than the detector falls,
  (1 − 1/R)·4.343/τ_RMS dB/s, i.e. **10 dB in 5.067 × Attack / (1 − 1/R)**. Shorter Release
  settings recover at that floor: attack and release stay locked below it, as in the 2252
  (source). At the mark and ∞:1 the floor is exactly 80 ms. The UI shades the Release knob below
  the floor (M4).
- **Auto release dims**: a constant-rate release already lengthens with depth (8 ms for 1 dB,
  400 ms for 50 dB at 125 dB/s), and the 160A has no other program dependence.
- **Snap:** the limiter reaches the target in finite time, and the target reaches exactly 0 below
  T_eff − W/2, so the reduction is exactly 0 without needing the core's snap; the snap stays as a
  guard.

### Switching detectors

The 30 ms crossfade of a model switch (`PLAN.md`): both detectors and ballistics run during the
fade and the two reductions are blended in dB. Both detectors' level states (the RMS mean square)
run all the time, and the incoming ballistics state (y_L, or the RMS path's y) starts at the
reduction applied at the switch, with the Auto network's C₂ empty (core, *Clicks*), so a switch at
slow Attack neither swells nor re-attacks.

## 4. Make-up and gain law

- **Gain law:** the reduction r in dB, gain 10^(−r/20), applied by the core's ideal gain cell. The
  VCA's own distortion (the 2180's, the Bus+'s 4K mode) is Character's job, never the model's
  (`PLAN.md`).
- **Auto Gain** (core): M = r(L_ref), L_ref = −18 dB on the detector scale, with T_eff in the
  curve, so the coupling is included. For a sine at −18 dBFS peak both detectors read −18 dB, so
  Auto Gain is the same for Peak and RMS on a sine; on music the Peak detector reads higher (the
  crest factor) and compresses more at the same Threshold, by design of the two types.
- **Learn** solves exactly (feed-forward) on the captured envelope. The two detectors need
  different envelopes: the per-millisecond **peak** for Peak and the per-millisecond **mean
  square** for RMS (*Proposed changes*, 5).

## Gates

The core's rows apply (identity, static, ballistics with the core's definition for Peak, Range,
stereo, clicks, real-time). VCA's own rows:

| Gate | VCA check |
|---|---|
| Identity | below T_eff − W/2 (not T): bit-identical, both detectors; Ratio 1 bit-identical at every Threshold (the 6 dB shift must not leak) |
| Static curve | the core's grid with T_eff = T − 6/R: 1 kHz sine and the carrier (the carrier's level on each detector's own reading: +3.01 dB on RMS, core *Conventions*), ±0.25 dB of `min(x − G(x; T_eff), Range)`, both detectors. **Coupling:** hard knee, Ratio 2, 4, 10, 20, ∞, carrier on a 0.01 dB level sweep: the knee, found by extrapolating the reduction above it linearly to 0 dB, at T − 3, −1.5, −0.6, −0.3, 0 dB ±0.05 (the first level with reduction > 0.05 dB lies 0.05/(1 − 1/R) above the knee, 0.10 dB at 2:1, so it is not the measure). RMS also on pink noise band-limited to 20 Hz … 20 kHz, ≥ 8 s, mean reduction in dB ±0.25 (measured, simulation: 6.004 dB for a 6.000 dB curve at the mark, 6.017 at Attack 100 ms). Pink noise with content below 20 Hz is not a static test for a 35 ms RMS (simulation: −0.18 dB, the level itself wanders). Peak is not gated on noise (its reading depends on the release; core). Reported: Peak at 50 Hz reads −0.14 dB at Attack 10 ms, Release 100 ms and −0.28 dB at 10/50 ms (in-cycle droop; measured, simulation) |
| Ballistics, Peak | the core's definition and grid: ±10 % |
| Ballistics, Peak Auto | Attack 1 ms, Release 100 ms, carrier, 10 dB, held 20 ms / 1 s / 5 s: the reductions in the table under *Peak: Auto* at +0.1, +1, +10 s within ±0.3 dB, and the release to 36.8 % of the reduction applied when the signal stops within ±10 % (0.112, 7.6, 12.3 s); Attack 10 ms, Release 150 ms likewise (0.192, 7.9, 18.2 s). Release 50 ms likewise against Auto 2's row. The double-precision reference of these equations is part of the test |
| Ballistics, RMS attack | carrier settled at Threshold on the RMS reading (amplitude T − 3.01 dB, reduction 0), step to T + 10, + 20, + 30 dB on the same reading at Ratio ∞, Knee 0: time to 63.2 % of the settled reduction = 0.45442, 0.19299, 0.08105 × Attack/0.45442 ±10 %, at Attack 1, 15.79, 100 ms. At the mark: 15.79, 6.71, 2.82 ms ±10 %. Reported next to the 160A's 15/5/3 ms |
| Ballistics, RMS release | carrier at T + 6, + 12, + 24 dB on the RMS reading, settled, then silence, Ratio ∞: recovery slope between 90 % and 10 % of the depth = 10 dB per Release ±10 % at every depth (the constant rate), Release 20 ms … 5 s at Attack 1 ms; and at Release below the floor, the floor ±10 % |
| Clicks | the detector switch at 6 dB of reduction (both directions) at Attack 10 and 300 ms, Auto on/off at 6 dB with Peak: no step artefact above −60 dB, and no swell (the reduction stays within 0.5 dB of the blend of the two settled reductions during the fade, on a 1 kHz sine: the carrier reads 3.01 dB apart on the two detectors, so their settled reductions differ and the incoming Peak path takes its Release to reach its own, *Measured in M2*) |
| Distortion | 1 kHz, 6 dB reduction, Ratio 4. **Peak:** ≤ −80 dBc at Attack ≥ 10 ms and Release ≥ 2 × Attack (measured, simulation, eq. 17 with h(ρ): −87.3 dBc at 10/20 ms, −100.0 at 10/50, −107.2 at 10/100, −111.1 at 10/150, −114.0 at 30/100; Auto 10/100: −126.9 measured in M2, M0's −97.8 not reproduced, *Peak: Auto*). Shorter releases reported, as the core's static rule **RMS:** a one-pole RMS ripples at 2f by construction; the third harmonic is ≈ 20·log10[(1 − 1/R) / (4·√(1 + (4πfτ)²))] dBc (derived here; simulation matched it within 0.1 dB). At the mark that is **−67.3 dBc** (R 4; −64.8 at ∞), as the 160A's own < 0.2 % allows. Gate: ≤ −80 dBc at Attack ≥ 100 ms (simulation: −83.4 dBc at R 4, −80.9 at ∞), and within ±1 dB of the formula at Attack 15.79 and 50 ms; below, reported |
| Auto Gain | the core's, with T_eff |
| Learn | the core's; the RMS run solves on the mean-square capture |

## Implementation (M2)

Where this file left a choice open, or the engine needed one (**Basalt's design**, M2):

- **One class, the detector fixed per instance:** `VcaModel (Detector::peak)` and
  `VcaModel (Detector::rms)` are the chain's two VCA engines (`../../chain/Engines.h`: "the VCA's
  two detectors as two"). An instance reads neither `vcaRms` nor, for RMS, `autoRelease`. Rejected:
  one instance switching on `vcaRms`. The lane hands both engines the same snapshot during a
  switch, so the outgoing one would change detector at the fade's first sample.
- **Auto release glides** (the core's 20 ms `SmoothGlide`, as Clean's factor): the first stage is
  the blend w·(network update) + (1 − w)·(eq. 17 update), w gliding 0 ↔ 1; C₂ is advanced while
  w > 0, starts empty when Auto comes on and is dropped at w = 0. *Switching Auto* above
  ("keeps v1") alone stepped the reduction's slope. Measured: Auto switched 50 ms into a release
  from 6 dB moves the reduction by at most 0.00037 dB a sample, as before the move.
- **The network's step** is e^(A/fs) in closed form, e^p·(cosh q·I + sinh q / q·(A/fs − p·I)),
  recomputed when Attack or Release move (per sample while they glide). The test's
  transcription takes it by eigendecomposition instead; the two traces agree within 10⁻⁶ dB.
- **The power-sum link windows the sum:** the look-ahead window runs on (m_L + m_R)/2 itself, so
  the linked level is the window's largest power sum, not the sum of each channel's largest. Each
  side of r_c = (1 − λ)·r(L_c) + λ·r(L_Σ) is computed only where its weight is not 0: Link 100 %
  takes one log a sample for both channels, Link 0 % is Unlinked to the bit.
- **The warm-up:** `settleMs` is 5·τ_RMS for RMS (as Clean's 5·τ_RMS; 174 ms at the mark, 3.3 s at
  Attack 300 ms) and 0 for Peak, whose only key-only state is the look-ahead window. A switch to
  RMS at slow Attack is heard that much later. Rejected: 3·τ_RMS, which leaves 5 % of an older,
  louder passage's power in the reading.
- **The model takes any key:** a NaN reads as silence and ±∞ as the key's limit 10⁶ before the
  detectors, as `saneKey` does in the chain (Settled 59), at one compare a sample.
- **Cost measures as Clean's:** Peak caches the reduction of a held window maximum and skips the
  log below the knee's foot, T_eff − W/2; RMS skips it below the foot's mean square.

## Measured in M2

**Measured (M2)**, `BasaltTests_vca` at 48 kHz (`-s` prints every figure), model level: the
reductions `VcaModel` returns, no chain.

| What | Result | Test |
|---|---|---|
| Static curve, the core's grid (T −60/−30/−10, R 1.5 … ∞, W 0/6/24, Range 40/10, T − 20 … T + 30), Attack 10 ms, Release 1 s | Peak: worst 0.0015 dB on 1 kHz sines, 0.0000 on the carrier; RMS: 0.0073 and 0.0063 dB (carrier on its own reading) | *the static curve with the moving threshold* |
| Coupling, hard knee, carrier, 0.01 dB sweep | knee at T − 3.000, −1.500, −0.600, −0.300, +0.000 dB (Peak) and −2.999, −1.499, −0.599, −0.299, +0.001 (RMS) for 2, 4, 10, 20, ∞; slopes 1 − 1/R to 10⁻⁴ | *the knee moves with the ratio* |
| RMS on pink noise, 20 Hz … 20 kHz (4th-order Butterworth), 9 s, 6.000 dB curve, Release 5.07 × Attack | 5.982 dB at the mark, 5.994 at Attack 100 ms (M0: 6.004, 6.017; another realisation and filter). With Release 1 s: 6.140 and 5.994, the rate limiter holding the level's dips (reported) | *RMS on band-limited pink noise* |
| Exact zeros | bit-identical 0 just under T_eff − W/2, at Ratio 1 for every Threshold −60 … 0, at Range 0, both detectors, Auto on and off | *the reduction is exactly 0* |
| Peak ballistics, Attack 0.02 … 300 ms × Release 5 ms … 5 s × R 2/4/20/∞ | attack within 0.34 % from 0.1 ms, release within 0.21 % of the knob or of 1.01 × Attack; times independent of depth (3, 10, 30 dB) | *attack and release mean the knobs* |
| Peak Auto, the nine rows of *Peak: Auto* | the engine equals the transcription within 10⁻⁶ dB everywhere, and M0's table to its printed digits (0.1122, 7.601, 12.30 s; 0.1919, 7.92, 18.23 s; 0.05837, 5.463, 6.174 s) | *the two-capacitor release* |
| Peak Auto over a held floor | 2 s at 6 dB, a 50 ms burst at 12 dB: back halfway in 61 ms, 6.000 dB at +1 s | *peaks above a held floor* |
| RMS attack from Threshold, Ratio ∞ | 15.790, 6.706, 2.816 ms at the mark for 10, 20, 30 dB (the sheet's 15, 5, 3); Attack 1 and 100 ms on the formula within 0.6 % | *the attack is program-dependent* |
| RMS release | 10 dB per Release within 0.01 % at depths 6, 12, 24 dB, Release 20 ms … 5 s at Attack 1 ms; below the floor the floor itself (80.01 ms at the mark, 50.67 at Attack 10 ms, 506.7 at 100 ms, 1520 at 300 ms) | *the release is a constant rate* |
| Look-ahead 3 and 10 ms | attack side the LA 0 trace shifted, release side identical, both within 10⁻⁶ dB, both detectors (Auto off: Auto's C₂ charges LA longer) | *look-ahead starts the attack early* |
| Stereo | Link 100 % bit-identical over 10 s of noise and Link 0 % = Unlinked to the bit, both detectors; Peak with one channel silent equals the mono key to the bit; RMS one-sided reads 3.01 dB low, centred its own reading, Link 50 % the mean of the two, each within 10⁻⁶ dB | *Link 100 %*, *the louder channel wins*, *linked stereo sums power* |
| Detector switch (warm-up, seed, 30 ms raised cosine, as the lane), 6 dB at R 4 | 1 kHz sine: within 0.0085 dB (Attack 10 ms) and 0.024 dB (300 ms) of the blend, steps no larger than the detectors' own. Carrier, RMS → Peak: 1.30 and 2.05 dB from the blend of settled reductions 2.26 dB apart, no step larger than 0.0025 dB a sample (reported) | *a detector switch* |
| Seed | at Attack 300 ms both stay at the seeded 10 dB within 10⁻⁶ dB; Peak Auto from a seed releases in 108 ms at Release 100 ms (C₂ empty) | *a seed starts the ballistics* |
| Block size | the same bits at blocks 1, 7, 64, 448, 4096, settings still and changing on given samples | *does not depend on the block size* |
| Knob jumps at Attack 0.02 and 10 ms | largest step 0.073 dB a sample; second difference ≤ 1.9·10⁻⁴ dB (Peak, Auto on or off); RMS up to 1.39·10⁻³ dB, its rate limiter meeting its target (10 dB per Release a sample, here 150 ms) | *a knob jump* |
| NaN and ±∞ in the key | the reduction always finite, 0 … Range; 5 s later equal to the clean key's (Peak Auto within 2.2·10⁻⁶ dB) | *a key with NaNs* |
| Distortion, 1 kHz, 6 dB at R 4, hard knee | Peak THD+N −87.3, −100.0, −107.2, −111.1, −114.0 dBc at 10/20, 10/50, 10/100, 10/150, 30/100 ms (M0's THD to 0.1 dB); Auto 10/100 −126.9; reported 1/50 −82.0, 3/100 −97.5, 10/10 −59.8. RMS H3 −67.3, −77.3 dBc at Attack 15.79, 50 ms (formula −67.3, −77.4), THD+N −83.4 and −92.9 at 100 and 300 ms; ∞:1 −64.8 … −90.4 on the formula | *distortion* |
| Real time | nothing allocated once prepared (random settings, layouts, follow and seed); nothing subnormal; exactly 0 again 11 s after a release at Attack 300 ms, Release 5 s (RMS), and after **355 s** with Peak Auto, its 20 s slow stage falling to 10⁻⁶ dB | *nothing allocates*, *nothing goes denormal* |

- **Peak at low frequencies** (reported; R 4, 8.6 dB at T + 10 dB, Attack 10 ms): 50 Hz reads
  −0.160 dB in mean reduction (−0.173 dB on the fundamental) at Release 100 ms and −0.331
  (−0.357) at 50 ms; 100 Hz −0.085 and −0.182. M0's −0.14 and −0.28 used a set-up not recorded
  here, so the two do not compare one to one. RMS at 50 Hz reads +0.187 dB at Release 1 s: above
  the detector's floor the rate limiter holds the ripple's crests.
- **RMS above its floor:** a Release longer than 5.07 × Attack/(1 − 1/R) lets the limiter catch
  the falling half of the 2f ripple, so the reduction sits towards the ripple's crests and its
  harmonic is no longer the single pole's: −77.3 dBc at the mark with Release 1 s, where the
  formula gives −67.3. The gates run at or below the floor, where the model is the 2252's one
  time constant.
- **Corners the specification makes** (not steps): RMS's attack is the detector alone, so the
  hard knee and Range reach the reduction as slope corners, and its release ends at its target
  with a corner of 10 dB per Release a sample; Peak Auto's rectifier catches the falling small
  capacitor at the target, a corner of (v1 − v2)/τ1. Through the processor the click gate
  decides whether either shows; at model level the largest is 1.39·10⁻³ dB.
- **Cost** (`[.perf]`, the model with the gain conversion and the matrix, as Clean's; 20 s of pink
  noise, 512-sample blocks, look-ahead 5 ms, the fastest of five passes, load average 3): Peak
  0.132–0.134 % of a core (Attack 10 ms Auto on or off, 0.5 ms, unlinked); RMS 0.163 % linked,
  0.170 % at Attack 0.5 ms, 0.154 % unlinked, 0.205 % at Link 50 % (three logs a sample). Clean
  measured beside it in one binary with the same flags: 0.274 % at Attack 10 ms with Auto, 0.210 %
  at 0.5 ms. Both detectors stay under Clean's cost, so the chain stays under Clean's 0.284 %
  (budget 0.3 %); the chain figure itself waits for the integration.
- **Not done in M2:** S voiced by ear between 3 and 9 dB (no listening here: S stays 6 dB), the
  A/B of the core's feedback loop on the Peak detector, and every row through the processor
  (`BasaltMeasure`), which needs the chain's two VCA engines.

## Through the processor (M2)

`BasaltMeasure all --model vca` (48 kHz, Normal and Live, both detectors, 2026-11-17), against
`tests/models/vca/Reference.h`:

- **Identity:** 452 rows bit-identical (both detectors, under T_eff − W/2 at T 0, Knee 24, ∞:1).
- **Static:** 446 pass, worst 0.012 dB (RMS, 1 kHz, ∞:1, hard knee at Threshold); the coupling
  knees at T − 3, −1.5, −0.6, −0.3, 0 dB within 0.0002 dB on both detectors; RMS on band-limited
  pink noise 5.999 dB (mark) and 6.013 dB (Attack 100 ms) for the 6 dB curve. 50 Hz reported:
  0.26 dB.
- **Ballistics:** Peak's core grid within 0.9979–1.0175 × the knob (or 1.01 × Attack); every Peak
  Auto row of the table above equal to the transcription to 10⁻⁴ dB and 0.01 ms (0.112, 7.60,
  12.30 s at Attack 1 ms, Release 100 ms); RMS attack 15.790, 6.706, 2.816 ms at the mark and the
  formula within 0.2 % at Attack 1 and 100 ms; RMS release 10 dB per Release (or the floor) at
  6, 12, 24 dB deep within 0.01 %; Auto release changes nothing on RMS, bit for bit.
- **Clicks:** the detector switch both ways at Attack 0.02, 10 and 300 ms and every model switch
  pass (worst −75.5 dB), no swell on the 1 kHz sine or the carrier. Peak passes every knob row.
  **RMS fails 16 rows**, the detector being the attack: Look-ahead 0 ↔ 10 ms at Attack 0.02 ms
  reads −11.7 dB (the 50 ms tap glide reads the Nyquist-rate key through the interpolator; the
  0.044 ms mean square follows the dips and the limiter lets the reduction fall about 2 dB),
  −58.8 dB at 10 ms; the key EQ rows at Attack 10 ms −30.3 (Bell Freq), −33.3 (Bell Gain), −55.0
  (Bell Q), −59.95 dB (HPF), a key jump reaching the gain through the single pole's program
  dependent attack; Release 5 ↔ 5000 ms in the releases −42 to −49 dB (the limiter's landing on
  its target is a corner of 10 dB per Release per sample, 0.04 dB at 5 ms); Key Internal ↔
  External at 0.02 ms −54.4 dB. These are the specified detector's behaviour, not wiring: the
  options are a window on the key's square before the pole (look-ahead) and a rounded landing.
- **Distortion:** Peak and RMS within the gate where gated (worst −92.9 dBc for RMS at Attack
  300 ms); the RMS ripple's third harmonic −67.32 and −77.33 dBc at Attack 15.79 and 50 ms
  against the formula's −67.34 and −77.35.
- **Cost:** Peak 0.193 %, RMS 0.168 % of a core at the defaults through the chain; 0.243 and
  0.255 % with the whole key path; Peak ↔ RMS every 100 ms 0.228 %.

## Proposed changes to PLAN.md

1. ***The reference behaviour*, VCA row:** Release: "0.1, 0.3, 0.6, 1.2 s and **Auto: two stages,
   100 ms and 12 s** (SSL *THE BUS+ User Guide* rev. 1.1, 2022, which calls it the Auto of every
   SSL bus compressor; Auto 2 50 ms / 6 s). Basalt specifies a two-capacitor auto release with
   Release as the fast stage and 120 × Release as the slow one." Detector · side chain: "Peak
   ('true peak full wave'), stereo dominant · the side chain is a feedback loop fed from a
   feed-forward position (Bus+ guide); Basalt builds it feed-forward (the loop's ratio-scaled
   time constant is calibrated away by measured knobs)".
2. ***VCA detector*, RMS:** "Attack scales the RMS time constant, with the 160A's timings at a
   point M0's NOTES marks" → "τ_RMS = Attack/0.4544, so Attack is the 63 % time of a 10 dB step
   from Threshold; the 160A point is Attack 15.8 ms with Release 80 ms (τ = 34.7 ms, one time
   constant). Release is a recovery-rate limit of 10 dB per Release, floored by the detector's own
   fall (10 dB in 5.07 × Attack at ∞:1). Linked stereo sums power (the 160A's strapping)."
3. ***Measurement and quality gates*, Ballistics, VCA RMS:** "attack ≈ 15, 5, 3 ms for 10, 20,
   30 dB steps" → "attack 15.8, 6.7, 2.8 ms for 10, 20, 30 dB steps from Threshold at the marked
   Attack (the exact single pole; no single pole meets the 160A's 15/5/3 within 17 %)".
4. ***Measurement and quality gates*, Distortion:** "Gated for Clean and VCA at Attack ≥ 10 ms" →
   "Clean and VCA Peak at Attack ≥ 10 ms (VCA Peak with Release ≥ 2 × Attack); VCA RMS at Attack ≥
   100 ms, and within ±1 dB of the single-pole ripple formula below that (−67 dBc at the 160A
   point is the reference's own behaviour)".
5. ***Learn*, Capture:** "a per-millisecond envelope" → "per millisecond, the peak magnitude and
   the mean square of the key after the key EQ, per channel": the VCA's two detectors (and the
   peak/RMS blend of Clean) need different envelopes from the same capture.
6. ***The reference behaviour*, the paragraph after the table:** "the threshold moving with ratio
   on the VCA … families, are kept": for the VCA it is kept, as T_eff = T − 6 dB/R (S voiced
   3 … 9 dB in M2).
7. ***References*, Reference units: VCA:** add SSL *THE BUS+ User Guide*, rev. 1.1, April 2022, on
   solidstatelogic.com (read: the bus-compressor sections), and THAT Corporation, *THAT4305
   Analog Engine Dynamics Processor*, doc. 600067 rev. 02, 2015 (read: topology and detector
   sections); mark the 4301 "skimmed". Note that the planning library's other dbx files are
   failed downloads.

## Not verified

- How much the console's effective threshold moves with ratio, and its knee shape.
- Whether SSL's attack, release and Auto figures are time constants or measured times.
- The console's side chain beyond the Bus+ guide's one-paragraph description (no schematic read).
- How the 160A's attack figures are defined (from what level, to what fraction).
- Whether the 160A's threshold moves with ratio.
- Every number marked "measured (simulation)" is a simulation of these equations, not of an
  engine or of hardware. M2 repeated them on the engine at model level (*Measured in M2*);
  `BasaltMeasure` repeats them through the processor once the chain runs the VCA.
