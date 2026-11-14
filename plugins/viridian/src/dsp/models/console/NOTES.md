# Console: British console channel EQ, two curve families

A transformerless four-band channel equaliser with high- and low-pass filters, built from IC
op-amp active filters; Alt switches between two generations of the same EQ. Behaviour reference:
the **SSL SL 4000 E** channel EQ in its two documented versions, the **'02' ("Brown Knob", card
82E02)** and the **'242' ("Black Knob", card 82E242)**. Hardware names appear only here and in
`PLAN.md`; the netlists call the two cards *early* (Alt off) and *later* (Alt on).

Status: M1 built and measured. Both cards are netlisted from SSL's own drawings (`netlist/`), every
trimmer calibrated as SSL's set-up note prescribes, and `netlist/check.py` sweeps them in ngspice
against the manufacturer's figures. Four of the later card's figures miss the fax; the parts behind
them were read again and stand, so the drawings rule (PLAN.md Settled 20, *Against the published
figures*). The model runs both cards' networks generated from those netlists, the control laws
below and the op-amp stages (*Implementation*); `tools/measure` checks it through the real chain
against ngspice (*Measured*). Every gate is met; the phase is reported, not gated (PLAN.md
Settled 13; *Measured*).

| File | Contents |
|---|---|
| `netlist/early_front.cir`, `netlist/later_front.cir` | high-pass, low-pass, HF and HMF bands into the first summing amp |
| `netlist/early_back.cir`, `netlist/later_back.cir` | LMF and LF bands into the second summing amp, the card's output |
| `netlist/opamp.lib` | the ideal op-amp the netlists use (`*@ nullor` for tools/netlist), and a rail-clip model for transients |
| `netlist/check.py` | ngspice sweeps, a nodal solver for poles and cross-checks, calibration, every table below |
| `netlist/networks/*.cir` | the tools/netlist view of each netlist: the netlist unchanged, the source and the directives |
| `netlist/laws.py` | the gain and Q laws from the nodal solver → `ConsoleLaws.h` |
| `generated/*.h` | the ten generated networks (*Implementation* lists them; tools/netlist, never edited by hand) |
| `ConsoleControls` | the control laws: knob values to pot fractions |
| `ConsoleCard`, `Console{Early,Later}Card.cpp` | one card for one channel: front network into back |
| `ConsoleModel` | the stages: input amp, network (switch crossfades, M/S, Tolerance, Noise), summing amp |

## Sources

| Source | What it gives | Trust |
|---|---|---|
| Solid State Logic, *Equaliser + Filters*, drawing T82002·71, issue 5 (redrawn 3.84), card 82E02, 2 sheets | the early card: every part value of the EQ and filters, op-amp type, rails | primary |
| Solid State Logic, *Equaliser Filters*, drawing T82242·70, revisions 0–4, assembly rev. 5, card 82E242, 2 sheets. Rev. 1 (Mar 1985) and 2 (Apr 1985, filter part values, which filters unstated) precede the fax; rev. 3 (Nov 1985) sets R6, R7, R44, R45, R69, R71, R81, R87, RV1 and RV4; rev. 4 (Jan 1986) adds a capacitor beside R3, which we read as the undesignated 220p across the HF frequency gang a (`CHFp` in the netlist; the early card's C2) | the later card, same | primary |
| Solid State Logic, fax *The 82E242 Rev. 4*, 15 Nov 1985, page 1 of 2 | the later card's published figures with tolerances, the rev. 3 part values, the set-up procedure; the 02's HF bell Q | primary |
| Solid State Logic, *X-Rack E Series EQ User's Guide* (82S6XR0K0A, module XR425) | both curve families' ranges and Q values, the calibration frequencies | manufacturer; describes SSL's 2000s re-implementation, not the cards |
| Solid State Logic, *E Series EQ module for 500 format, User Guide* | the Brown/Black history in words | manufacturer |
| Waves, *SSL E-Channel & G-Channel User Manual* | filter ranges and slopes | secondary |
| GroupDIY forum, thread "SSL 4000 EQ" | pot types (dual 50k anti-log frequency pots, centre-tapped 25k boost/cut, dual log Q pots); a post's account that the 242 followed complaints that switching the 02 in changed the sound even with every gain flat | secondary, forum posts |
| Texas Instruments, *NE5534 … Low-Noise Operational Amplifiers*, SLOS070D (rev. Nov 2014), *Electrical Characteristics* and *Operating Characteristics* | output swing, open-loop gain (DC and at 10 kHz), unity-gain bandwidth, slew rate | manufacturer |
| onsemi, *J111, J112 JFET chopper transistors* (J111/D) | rDS(on) ≤ 50 Ω (J112) | manufacturer (a mirrored copy) |

The drawings are 100 dpi scans, the only resolution found. Every value was read magnified 3–5×;
the parts behind the four figures the later card misses (*Against the published figures*) were
read again on 2026-11-12, at 3× (Lanczos), with the same values. The fax's second page was not
obtained.

## Published figures

The original 242 card (SSL's fax, Rev. 4; tolerances as given):

| Band | Amplitude (±1 dB) | Frequency (±10 %) | Q |
|---|---|---|---|
| HF | ±15 dB shelf, ±18 dB bell | 1.5–20 kHz | bell 1.0 (±10 %) |
| HMF | ±15 dB at lowest Q, ±18 dB at highest Q | 600 Hz–10 kHz | 0.5–4.5 (±15 %) |
| LMF | ±15 dB at lowest Q, ±18 dB at highest Q | 160 Hz–2.2 kHz | 0.5–4.5 (±15 %) |
| LF | ±15 dB shelf, ±18 dB bell | 40–450 Hz | bell 1.7 (±10 %) |

The fax adds that the HF bell's Q went down from about 1.7 to 1.0 on Rev. 4 and that the 02's is
about 0.6. Its set-up: each band at full boost with its frequency pot in the middle of its
travel, the mids at highest Q and the HF and LF on bell, its Q trimmer set for +18 dB.

SSL's re-implementation (X-Rack guide), both families, all controls continuous:

| Band | Frequency | Gain, 02 / 242 | Q, 02 / 242 |
|---|---|---|---|
| HF | 1.5–16 kHz, shelf or bell | ±15 / ±18 dB | bell 0.8 / 1.3 |
| HMF | 600 Hz–7 kHz | ±15 / ±18 dB | 0.5–2.5 / 0.5–4 |
| LMF | 200 Hz–2.5 kHz | ±15 / ±18 dB | 0.5–2.5 / 0.5–4 |
| LF | 30–450 Hz, shelf or bell | ±15 / ±18 dB | bell 0.8 / 1.3 |

X-Rack calibration: maximum gain trimmed to +15 dB (Brown) and +18 dB (Black) at HF 12 kHz bell,
HMF 3 kHz, LMF 1 kHz, LF 80 Hz bell. Filters (Waves, secondary): high-pass 16–350 Hz, low-pass
3–22 kHz, 12 dB/oct; SSL's guide credits the 242 with a steeper, 18 dB/oct high-pass.

The X-Rack figures describe the re-implementation and disagree with the fax on the original
242 (HF top, LMF range, bell and mid Q); the fax and the drawings rule for the cards.

## Circuit

Both cards share one architecture (drawings; NE5534 throughout, ±18 V rails):

```
 in ─► HPF ─► LPF ─► EQ in ─┬─[R10 4K75]─┬─► T3 (inverting, R12 || C8) ─┬─[R38 4K75]─► T8 (inverting) ─► EQ out
                            │            ├─[R14]◄─ HF band ◄─ wiper ┐   │              ├─[R37]◄─ LF band
                            │            └─[R13]◄─ HMF band ◄─ wiper│   │              └─[R36]◄─ LMF band
                            └── each band's boost/cut pot from T3's input to its output ┘   (LMF, LF: T8's)
```

- **Two inverting summing stages**, T3 (HF, HMF) and T8 (LMF, LF), unity gain at flat; the card
  is non-inverting overall.
- **Boost/cut:** a 25k linear pot bridged from the stage's input (through 1K0) to its output
  (through 1K0), its centre tap to ground through 4K7 on the motherboard, the wiper loaded by
  1K8. The wiper drives the band filter, whose output returns into the stage's virtual earth
  through 4K75: wiper at the input end boosts, at the output end cuts (the band sits in the
  feedback path). Cut mirrors boost: bells and mids within 0.13 dB, shelves within 0.35 dB.
- **Band filter:** an inverting RC band-pass (series R + C in, parallel R ‖ C feedback; both R
  include one gang of the dual 50k anti-log frequency pot, as rheostats) whose non-inverting input
  takes a fraction of its own output: positive feedback that sets Q. A post amp (inverting) sets
  the band's gain.
- **Mid-band Q:** a dual 10k log pot. Gang a taps the positive-feedback divider (trimmer 500R,
  910, [1K8 ‖ gang a], 470 to ground); gang b is a rheostat in the post amp's feedback, so its
  gain falls as Q rises. The gangs pair as b = 1 − a: at the early HMF's calibration point the
  other pairing gives +2.9 dB at lowest Q and +31.8 dB at highest, this one +16.0 and +15.0 dB,
  SSL's constant-gain mid band.
- **Cross-feed:** each stage's two bands feed each other's wiper through 12K (early R63–R66,
  later R94–R97).
- **Bell/Shelf:** early card: a 2-pole switch. HF: bell takes C5 and the Q divider to T1's output
  (through C7); shelf hangs them on C6 330p instead. LF: bell feeds the divider through C20; shelf
  shorts C16 ‖ C17 (switch a: the input becomes resistive, a first-order low shelf) and feeds the
  divider through R46 2K2. Later card: J112 switches do the same (driven from the panel switch
  on the motherboard), with LF switch a still mechanical; on shelf the divider reaches the
  band-pass output through 100K (R79, R85) only, and the post amp's feedback loses its R81 / R87
  branch (bell: post gain 6.2 HF, 2.1 LF; shelf: 10.8, 10.2).
- **Filters:** early: R58/R59 pad (0.63) into a Sallen-Key high-pass with gain 1 + 3K/5K1 (1.59,
  so 1.00 overall), 12 dB/oct; unity-gain Sallen-Key low-pass, 12 dB/oct. Later: RV5/R56 pad, a
  first-order section (C34, R55 + gang c) and a Sallen-Key with gain 1 + 7K5/(RV6 + 5K6): with
  equal R and C, gain 2 makes the cascade a third-order Butterworth, 18 dB/oct; the low-pass as
  the early card's. Each later filter is switched out at its pot's end.

### Values

Signal-path parts per band, early / later (designators as drawn; * = 0.5 %):

| Part | HF | HMF | LMF | LF |
|---|---|---|---|---|
| Input R (+ gang a, 50k) | 4K3 / 3K0 | 4K3 / 3K0 | 4K3 / 3K0 | 4K3 |
| Input C | C3 2n2 (gang a ‖ 220p) | C9 4n7 | C12 15n / 22n | C16 33n ‖ C17 47n |
| Feedback R (+ gang b) | 5K1 / 3K6 | 5K1 / 3K6 | 5K1 / 3K6 | 5K1 / 4K7 |
| Feedback C | C4 220p, + C5 2n2 on bell | C10 4n7 | C13 15n / 22n | C18 33n ‖ C19 47n |
| Q divider | R6 1K0 / 5K1, R7 1K8 / 2K4, link / RV1 | trim 500R, 910, 1K8 ‖ 10k, 470; 100K | as HMF | R44 1K0 / 5K1, R45 1K0 / 2K4, link / RV4 |
| Post amp in / feedback | 750 / 7K5; 3K0 / 43K ‖ (33K + 2µ2) | 750 / 750 + gang b | 750 / 750 + gang b | 750 / 3K3; 3K0 / 43K ‖ (7K5 + 100µ) |
| Coupling | C7 100µ | C11 100µ | C14 100µ | C21 100µ, C20 100µ |

Summing stages: R10, R12–R14, R35–R38 4K75*; C8 330p / 220p, C15 150p / 220p. Boost/cut ends
1K0*, wiper loads 1K8. Inputs: early C1 100µ (R1 100K); later C22, C1 220µ (R1 100K, R50 1M).
Outputs: early C29 100µ, R60 100K; later C23 220µ ‖ R51 100K, R52 1M, C24 220µ, R53 10K.

### Readings and choices

- **C43, the later low-pass capacitor, reads 68n**; **6n8** is used, as on the early card
  (C24). With 68n the low-pass would span 1.0–16 kHz at Q 2.3, against every published range
  (3–22 kHz) and the drawing's own 12 dB/oct legend; with 6n8 it spans 3.19–50 kHz at Q 0.72.
- **RV4 reads 5K** on the drawing; the fax says RV1 and RV4 became 10K. Moot: the calibrated
  value (811 Ω) lies inside both.
- **Duplicate designators** on the drawings: R38/R39 (page 2 of both) and R79 (later). The
  netlists use R38b, R39b and R79b for the second ones.
- **The later LF post amp's 43K** reads R48 or R49: the value is legible, the designator's last
  digit is not. The rev. 1 change list (R7, R9, R45, R48, pairing HF and LF) and the early card's
  R48 in that place point to R48, and `later_back.cir` names it R48. A designator only: the
  regenerated `ConsoleLaterBack.h`, its test vectors and `ConsoleLaws.h` differ from the ones
  before only in their stamps.
- **Links:** where the later card has its HF and LF Q trimmers (RV1, RV4), the early card has
  wire links, as drawn. The 02's HF bell Q then comes out at 0.60–0.63, the fax's figure of
  about 0.6.
- **J112 switches** are ideal: closed 0 Ω, open infinite. A J112 at its 50 Ω maximum moves the
  later LF bell by up to 0.41 dB at the same trim (HF 0.13 dB); recalibrating RV4 (765 Ω instead
  of 811 Ω) absorbs most of it.
- **Op-amp compensation** capacitors (22p, later 10p on T2, T3, T8, T10) belong to the NE5534's
  frequency compensation (pins 5–8), not to the network, and are left out with the ideal
  op-amps. Supply decoupling and the J112 gate drive (R80, R83, R84, R86, R89–R93) are left out.
- **Rails are AC ground:** bias resistors to ±18 V are in the netlists, to ground.
- **Which gang end is which** (boost direction, Q pairing) is not legible from the drawings;
  both were settled by simulation against the fax and the 02's documented behaviour (above).
- The J112, gang-pairing, coupling-capacitor and cascade figures in these notes come from one-off
  runs of `check.py`'s solver on variant netlists (a part changed or removed), not from the
  report.

## Calibration

Each trimmer is set the way SSL's fax prescribes for the 242: full boost, mids at highest Q, HF
and LF on bell, the level trimmed to +18 dB; the early card's mid trimmers to +15 dB (X-Rack's
Brown target). The frequency is X-Rack's calibration frequency (the fax puts the frequency pot in
the middle of its travel and defines that no further). The level is the band's own deviation:
the response divided by the same setting with the gain at its centre tap.

| Card | Trimmer | Point | Value | Stable above |
|---|---|---|---|---|
| early | HMF Q trim (500R) | HMF 3 kHz, highest Q | 273.23 Ω | 66.2 Ω |
| early | LMF Q trim (500R) | LMF 1 kHz, highest Q | 260.24 Ω | 63.6 Ω |
| later | RV1, HF bell Q (10K) | HF 12 kHz, bell | 2110.87 Ω | all |
| later | RV2, HMF Q (500R) | HMF 3 kHz, highest Q | 192.47 Ω | 58.3 Ω |
| later | RV3, LMF Q (500R) | LMF 1 kHz, highest Q | 181.47 Ω | 55.1 Ω |
| later | RV4, LF bell Q (10K) | LF 80 Hz, bell | 810.91 Ω | 69.9 Ω |
| later | RV5, the high-pass's level (500R) | no level step at 1 kHz when the high-pass switches in at its lowest corner | 294.56 Ω | |
| later | RV6, the high-pass's Sallen-Key gain (5K) | gain 2 (Butterworth) | 1.9 kΩ | |

- Below the "stable above" value the band-pass oscillates at highest Q: an AC sweep there shows
  a plausible but meaningless peak, so the calibration searches the stable branch only (poles
  from the nodal solver).
- Calibrating at the fax's middle position instead (frequency pot at 10 % of its resistance) moves
  the trimmers by up to 30 Ω (RV1; the 500R trimmers by 1.7 Ω) and the level at the X-Rack points
  by at most 0.13 dB, inside the fax's ±1 dB.
- RV5 and RV6 are our reading: the drawing gives no set-up. The switch for the high-pass sits at
  the pot's lowest end, so level continuity there is what a unity-gain trim is for; gain 2 is the
  condition that makes the equal-component cascade Butterworth, and the RV5/R56 pad (0.5)
  complements exactly that gain.

## Netlists

The convention `tools/netlist` reads; `check.py`'s nodal solver reads the same subset.

- **Files:** `<card>_<part>.cir`, card `early` (Alt off) or `later` (Alt on), part `front` (HPF,
  LPF, HF, HMF; out = T3's output) or `back` (LMF, LF; out = the card's output). The channel is
  H = H_front · H_back, exactly: the back is driven by an op-amp output and loads nothing behind
  it (`check.py` compares against one netlist of the whole card: under 10⁻¹⁰ dB). The filters
  stay in the front because their output drives the EQ input, whose impedance moves with the EQ
  (down to 815 Ω early and 690 Ω later at full boost), and the later high-pass takes its feedback
  from that output: cascading them as ideal sections errs by up to 0.09 dB (early) and 0.28 dB
  (later).
- **Signal:** input node `in`, driven by an ideal source the caller adds (`Vin in 0 AC 1`);
  output node `out`, unloaded; ground `0`.
- **Elements:** R, C, V (0 V only: switch contacts, closed J112s, bypasses) and op-amps as
  `X<name> <non-inverting> <inverting> <output> opamp`. Include `opamp.lib` first. `opamp` is a
  nullor for `tools/netlist` (ngspice runs it as a 1e9 VCVS); `opamp_rail` is the rail clip for
  transients. No L, E, G or B elements in the netlists.
- **Values:** numbers with SPICE suffixes, or `{expression}` of parameters, each affine in one
  control (`{25k*(1-hf_gain)}`).
- **Parameters:** `.param` lines give the defaults (flat, filters out). Controls are pot
  fractions 0 … 1 or switches 0/1 (table below); trimmers are calibrated constants in ohms. In
  ngspice a `.param` after the `.include` overrides the file's default.
- **Topology:** `.if (condition)` / `.else` / `.endif`, never nested; conditions are switch tests
  (`hf_bell == 1`), the boost/cut half (`hf_gain >= 0.5`, which side of the centre tap the wiper
  is on) and `hpf_in + lpf_in >= 1`. Every combination is its own network: front 2⁵ = 32, back
  2³ = 8. A filter section that cannot reach the output is left out when bypassed.
- **Pot ends:** a segment reaches 0 Ω at the end of its travel; treat it as a short (ngspice
  substitutes 1 mΩ; `check.py` stamps a 0 V branch).
- **Order:** poles per network: early front 9–15 (filters out, bell: 9), early back 7–9; later
  front 12–24 (filters out, bell: 12), later back 10–11. The electrolytic coupling
  capacitors (corners 0.004–4 Hz) are part of it: inside the Q loops they move a highest-Q mid
  by up to 2.0 dB at its bottom frequency (later LMF at 121 Hz; early 0.9 dB), so they stay.

Controls (`front`: hpf_*, lpf_*, hf_*, hmf_*; `back`: lmf_*, lf_*):

| Parameter | Range | Meaning |
|---|---|---|
| `*_gain` | 0 … 1 | boost/cut wiper: 0 full cut, 0.5 centre tap (flat), 1 full boost |
| `hf_freq`, `hmf_freq`, `lmf_freq`, `lf_freq` | 0 … 1 | both frequency gangs, fraction of 50k in circuit: 0 top frequency, 1 bottom |
| `hmf_q`, `lmf_q` | 0 … 1 | Q gang a wiper: 0 lowest Q, 1 highest; gang b is 1 − q |
| `hf_bell`, `lf_bell` | 0, 1 | 0 shelf, 1 bell |
| `hpf_freq` | 0 … 1 | high-pass gangs (early 50k ×2, later 47k ×3): 0 top corner, 1 bottom |
| `lpf_freq` | 0 … 1 | low-pass gangs (10k lin ×2): 0 top corner, 1 bottom |
| `hpf_in`, `lpf_in` | 0, 1 | 1 in, 0 bypassed (the panel's Off) |

## Control laws

ModelInfo's knob positions map to pot fractions so that each read-out is the circuit's own value
(at the calibration points where the value depends on other controls). `t` is ModelInfo's
normalised position inside the control's range.

- **Band frequency:** ModelInfo's log mapping gives f = f_lo · (f_hi / f_lo)^t; the gangs take
  x = r / 50k with r the positive root of (Ri + r)(Rf + r) = 1 / ((2πf)² Ci Cf), the nominal
  frequency of the band-pass's time constants. f_lo = f(x = 1), f_hi = f(x = 0):

  | Card | HF | HMF | LMF | LF |
  |---|---|---|---|---|
  | Ri, Ci, Rf, Cf | 4K3, 2n2, 5K1, 2n42 | 4K3, 4n7, 5K1, 4n7 | 4K3, 15n, 5K1, 15n | 4K3, 80n, 5K1, 80n |
  | early range | 1261.03–14729.29 Hz | 619.08–7231.08 Hz | 193.98–2265.74 Hz | 36.37–424.83 Hz |
  | Ri, Ci, Rf, Cf | 3K0, 2n2, 3K6, 2n42 | 3K0, 4n7, 3K6, 4n7 | 3K0, 22n, 3K6, 22n | 4K3, 80n, 4K7, 80n |
  | later range | 1294.14–20988.88 Hz | 635.33–10304.11 Hz | 135.73–2201.33 Hz | 36.50–442.53 Hz |

  (HF: Cf is C4 + C5, the bell's.) The bell's actual peak moves with gain and Q: within
  −12 … +5 % of the nominal frequency, furthest at the top of the travel (later HMF: nominal
  10.3 kHz, peaks 9.04–9.41 kHz).
- **Filter frequency:** f = A / (R0 + x · Rpot), so x = (A / f − R0) / Rpot, with A fitted to the
  −3 dB corners (log least squares over 21 positions): early HPF A = 556 122 Ω·Hz (R0 1K8, 50k;
  within 1.46 %), early LPF 3.40573×10⁷ (470, 10k; within 0.01 %), later HPF 737 359 (1K8, 47k;
  within 2.94 %), later LPF 3.40573×10⁷ (680, 10k; within 0.01 %).
- **Gain:** ModelInfo's linear dB value G = range · (2t − 1); the wiper g is the inverse of the
  band's deviation at its calibration point, G_cal(g), tabulated below and monotonic.
  `tools/netlist` tabulates it exactly from H(s); the table is the reference. Away from the
  calibration point the circuit gives what it gives (the 242's mids: ±15.5 dB at lowest Q, ±18
  at highest, as the fax says).
- **Q:** ModelInfo's log value Q = Q_min · (Q_max / Q_min)^t; gang a's wiper is the inverse of
  Q_cal(q), the −3 dB-from-peak Q at full boost at the calibration point (table below).
- **Shape:** `hf_bell`, `lf_bell` = the toggle's index (Shelf 0, Bell 1).
- **Off:** `hpf_in` = 0 in the high-pass knob's Off zone, `lpf_in` = 0 in the low-pass's.

## ngspice results

All from `python3 netlist/check.py` (ngspice 42; the nodal solver where marked), last run
2026-11-12 after the later card's re-read (3.5 min): every figure below as printed. ngspice and the
nodal solver agree to 2.0×10⁻⁵ dB and 5.8×10⁻⁵ ° over 72 random settings, 10 Hz–40 kHz; front ×
back equals one netlist of the whole card to 6.9×10⁻¹¹ dB over 80; `check.py --calibrate` solves
every trimmer to the value its netlist holds (RV1 2110.88 Ω against 2110.87 Ω; at the held value
ngspice puts the calibration level on +18.000 dB).

**Stability.** The least-damped pole over a grid of every control, filters included (front 23 746
settings, back 6 250): damping ratio 0.142 (early front), 0.143 (early back), 0.089 (later
front), 0.086 (later back), all at highest Q with the neighbouring band boosted. Every setting is
stable.

**Mid bands** (full boost / full cut; Q at −3 dB from the peak):

| Card | Band | Freq pot | Nominal Hz | Q pot | Peak Hz | Boost | Q | Cut | Q |
|---|---|---|---|---|---|---|---|---|---|
| early | HMF | 0 | 7231 | 0 | 6478 | +15.86 | 0.51 | −15.93 | 0.50 |
| early | HMF | 0 | 7231 | 1 | 6768 | +15.30 | 2.92 | −15.41 | 2.93 |
| early | HMF | 0.5 | 1140 | 0 | 1180 | +15.81 | 0.51 | −15.80 | 0.51 |
| early | HMF | 0.5 | 1140 | 1 | 1138 | +14.68 | 2.69 | −14.67 | 2.69 |
| early | HMF | 1 | 619 | 0 | 644 | +15.61 | 0.49 | −15.60 | 0.49 |
| early | HMF | 1 | 619 | 1 | 623 | +14.44 | 2.65 | −14.43 | 2.65 |
| early | LMF | 0 | 2266 | 0 | 2118 | +15.57 | 0.49 | −15.57 | 0.49 |
| early | LMF | 0 | 2266 | 1 | 2130 | +15.49 | 3.06 | −15.50 | 3.07 |
| early | LMF | 0.5 | 357 | 0 | 348 | +15.49 | 0.48 | −15.49 | 0.48 |
| early | LMF | 0.5 | 357 | 1 | 359 | +14.77 | 2.83 | −14.77 | 2.83 |
| early | LMF | 1 | 194 | 0 | 186 | +15.57 | 0.49 | −15.58 | 0.49 |
| early | LMF | 1 | 194 | 1 | 198 | +14.76 | 2.84 | −14.76 | 2.84 |
| later | HMF | 0 | 10304 | 0 | 9039 | +15.66 | 0.50 | −15.73 | 0.49 |
| later | HMF | 0 | 10304 | 1 | 9412 | +17.64 | 3.99 | −17.77 | 4.01 |
| later | HMF | 0.5 | 1197 | 0 | 1263 | +15.82 | 0.51 | −15.81 | 0.51 |
| later | HMF | 0.5 | 1197 | 1 | 1194 | +17.75 | 4.00 | −17.75 | 4.00 |
| later | HMF | 1 | 635 | 0 | 658 | +15.59 | 0.48 | −15.59 | 0.48 |
| later | HMF | 1 | 635 | 1 | 640 | +17.50 | 3.96 | −17.49 | 3.96 |
| later | LMF | 0 | 2201 | 0 | 2006 | +15.47 | 0.49 | −15.48 | 0.49 |
| later | LMF | 0 | 2201 | 1 | 2019 | +18.05 | 4.28 | −18.07 | 4.29 |
| later | LMF | 0.5 | 256 | 0 | 250 | +15.50 | 0.48 | −15.50 | 0.48 |
| later | LMF | 0.5 | 256 | 1 | 259 | +17.95 | 4.30 | −17.95 | 4.30 |
| later | LMF | 1 | 136 | 0 | 128 | +15.61 | 0.50 | −15.61 | 0.50 |
| later | LMF | 1 | 136 | 1 | 141 | +17.94 | 4.35 | −17.94 | 4.35 |

`check.py` also prints the Q-pot midpoint rows.

**HF and LF** (bell: full boost / cut and Q; shelf: level at 20 kHz (HF) or 20 Hz (LF), the
extreme inside 20 Hz–20 kHz, the steepest slope):

| Card | Band | Freq pot | Nominal Hz | Bell Hz | Bell | Q | Shelf at the edge | Shelf extreme | Slope dB/oct |
|---|---|---|---|---|---|---|---|---|---|
| early | HF | 0 | 14729 | 13387 | +16.19 / −16.28 | 0.63 | +15.36 / −15.23 | +15.36 / −15.23 | 4.4 |
| early | HF | 0.5 | 2323 | 2199 | +16.13 / −16.14 | 0.60 | +15.50 / −15.82 | +16.23 / −16.35 | 4.6 |
| early | HF | 1 | 1261 | 1232 | +16.19 / −16.19 | 0.62 | +14.22 / −14.57 | +16.07 / −16.16 | 4.5 |
| early | LF | 0 | 425 | 398 | +15.86 / −15.86 | 0.96 | +15.16 / −15.16 | +15.16 / −15.16 | 4.5 |
| early | LF | 0.5 | 67 | 68 | +15.22 / −15.22 | 0.91 | +14.22 / −14.22 | +14.22 / −14.22 | 4.1 |
| early | LF | 1 | 36 | 37 | +15.12 / −15.12 | 0.92 | +13.03 / −13.03 | +13.03 / −13.03 | 4.1 |
| later | HF | 0 | 20989 | 18898 | +18.06 / −18.17 | 0.96 | +14.24 / −14.13 | +14.24 / −14.13 | 4.4 |
| later | HF | 0.5 | 2437 | 2374 | +17.97 / −17.98 | 0.90 | +14.76 / −14.84 | +15.78 / −15.79 | 4.7 |
| later | HF | 1 | 1294 | 1289 | +18.05 / −18.05 | 0.91 | +14.46 / −14.53 | +15.99 / −16.01 | 4.7 |
| later | LF | 0 | 443 | 414 | +17.74 / −17.74 | 2.05 | +16.49 / −16.49 | +16.58 / −16.58 | 4.6 |
| later | LF | 0.5 | 67 | 68 | +17.97 / −17.97 | 2.12 | +16.41 / −16.41 | +16.41 / −16.41 | 4.6 |
| later | LF | 1 | 37 | 37 | +17.89 / −17.89 | 2.13 | +15.75 / −15.75 | +15.75 / −15.75 | 4.4 |

The shelves are first order (4.1–4.7 dB/oct at most over a 15 dB step). Neither card overshoots:
on the far side of the corner a boost dips, or a cut rises, by 0.05 dB at most (`check.py`'s
"opposite excursion"). At the bottom of its travel the HF shelf peaks at +16.1 dB near 10 kHz and
falls back to +14.2 dB at 20 kHz, the card's own HF roll-off.

**Gain law** at the calibration points (nodal solver; the band's deviation in dB against the
wiper):

| Card | Band | 0 | 0.05 | 0.1 | 0.2 | 0.3 | 0.4 | 0.45 | 0.55 | 0.6 | 0.7 | 0.8 | 0.9 | 0.95 | 1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| early | HF | −16.24 | −12.57 | −10.19 | −7.04 | −4.75 | −2.64 | −1.45 | +1.46 | +2.65 | +4.77 | +7.05 | +10.18 | +12.54 | +16.16 |
| early | HMF | −15.02 | −11.34 | −9.05 | −6.13 | −4.11 | −2.28 | −1.26 | +1.26 | +2.28 | +4.11 | +6.14 | +9.05 | +11.33 | +15.00 |
| early | LMF | −15.00 | −11.23 | −8.93 | −6.02 | −4.03 | −2.23 | −1.23 | +1.23 | +2.24 | +4.03 | +6.02 | +8.93 | +11.23 | +15.00 |
| early | LF | −15.26 | −11.85 | −9.61 | −6.63 | −4.48 | −2.48 | −1.36 | +1.36 | +2.48 | +4.48 | +6.63 | +9.61 | +11.85 | +15.26 |
| later | HF | −18.06 | −14.05 | −11.46 | −7.98 | −5.42 | −3.02 | −1.66 | +1.67 | +3.04 | +5.44 | +8.00 | +11.45 | +14.03 | +18.00 |
| later | HMF | −18.02 | −13.75 | −11.10 | −7.65 | −5.18 | −2.89 | −1.60 | +1.60 | +2.90 | +5.19 | +7.66 | +11.10 | +13.75 | +18.00 |
| later | LMF | −18.01 | −13.30 | −10.56 | −7.15 | −4.81 | −2.69 | −1.49 | +1.49 | +2.69 | +4.81 | +7.15 | +10.56 | +13.30 | +18.00 |
| later | LF | −18.00 | −14.14 | −11.60 | −8.13 | −5.54 | −3.09 | −1.69 | +1.69 | +3.09 | +5.54 | +8.13 | +11.60 | +14.14 | +18.00 |

**Q law** at the calibration points (−3 dB-from-peak Q at full boost against gang a's wiper):

| Card | Band | 0 | 0.1 | 0.2 | 0.3 | 0.4 | 0.5 | 0.6 | 0.7 | 0.8 | 0.9 | 1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| early | HMF | 0.53 | 0.54 | 0.57 | 0.60 | 0.63 | 0.68 | 0.75 | 0.86 | 1.03 | 1.41 | 2.78 |
| early | LMF | 0.49 | 0.51 | 0.53 | 0.56 | 0.60 | 0.65 | 0.72 | 0.82 | 1.01 | 1.40 | 2.89 |
| later | HMF | 0.54 | 0.56 | 0.58 | 0.61 | 0.65 | 0.71 | 0.78 | 0.90 | 1.11 | 1.60 | 4.07 |
| later | LMF | 0.49 | 0.51 | 0.54 | 0.57 | 0.60 | 0.66 | 0.73 | 0.85 | 1.06 | 1.57 | 4.27 |

**Q against gain** (bandwidth at half the peak in dB, so it stays defined at small boosts): within
10 % of its small-boost value up to about 7 dB of boost, 34–37 % lower at full boost, on both
cards and at every Q. Neither card has proportional Q; on the 242 it is
the gain that follows Q (±15.5 dB at lowest, ±18 at highest), as the fax documents.

**Filters** (re the front with both filters out, EQ flat; slope over fc/8–fc/4 or 4fc–8fc):

| Card | Filter | Pot | −3 dB Hz | Law Hz | Passband dB | Slope dB/oct | Peak over passband dB |
|---|---|---|---|---|---|---|---|
| early | HPF | 0 | 313.5 | 309.0 | −0.366 | 12.0 | +0.010 |
| early | HPF | 0.5 | 20.7 | 20.8 | −0.025 | 12.1 | +0.066 |
| early | HPF | 1 | 10.7 | 10.7 | −0.013 | 12.1 | +0.069 |
| early | LPF | 0 | 72463 | 72462 | 0.000 | 12.0 | +0.004 |
| early | LPF | 0.5 | 6226 | 6226 | 0.000 | 12.0 | +0.004 |
| early | LPF | 1 | 3253 | 3253 | 0.000 | 12.0 | +0.004 |
| later | HPF | 0 | 397.6 | 409.6 | −0.684 | 18.5 | +0.039 |
| later | HPF | 0.5 | 29.2 | 29.1 | −0.025 | 18.8 | +0.030 |
| later | HPF | 1 | 15.4 | 15.1 | 0.000 | 19.3 | +0.080 |
| later | LPF | 0 | 50085 | 50084 | −0.033 | 12.0 | +0.004 |
| later | LPF | 0.5 | 5996 | 5996 | −0.033 | 12.0 | +0.004 |
| later | LPF | 1 | 3189 | 3189 | −0.033 | 12.0 | +0.004 |

The passband loss at the top of the high-pass travel is the input pad loaded by the filter (its
first resistor falls to 1K8); the later low-pass's −0.033 dB is R75 (10 Ω) into the EQ input. The
later high-pass at its top position has a −109 dB notch near 13 Hz (leakage through its output
coupling network), immaterial.

**Flat** (filters out, every gain at its centre tap; whole card):

| Card | 20 Hz | 100 Hz | 1 kHz | 5 kHz | 10 kHz | 15 kHz | 20 kHz |
|---|---|---|---|---|---|---|---|
| early | −0.004 | −0.001 | −0.014 | +0.156 | +0.195 | +0.161 | +0.093 |
| later | −0.003 | −0.001 | −0.007 | +0.121 | +0.136 | +0.102 | +0.047 |

With the other controls anywhere (gains at centre): early front up to +0.613 dB at 20 kHz (HF
shelf at its bottom, HMF at its top, lowest Q), early back +0.078 dB, later front +0.380 dB, later
back +0.102 dB. The cause is C8 (330p / 220p across T3's feedback): T3 rolls off, so its output
is not quite the inverse of its input, the centre-tapped wipers sit slightly off 0 V and the
bands (HF's post gain is 10) add a lift; with C8 removed the stage is flat to 0.0000 dB, and the
12K cross-feed doubles the effect. This is the drawn circuit, and it fits the forum account that
switching the 02 in changed the sound even with every gain flat.

**Headroom** (largest op-amp output re its part's input, 20 Hz–20 kHz, nodal solver): the summing
amps carry the most, T3 +20.8 dB (early) / +22.7 dB (later) with HF and HMF boosted together, T8
+15.9 / +18.3 dB; the band-pass and post amps stay at or below their band's boost (later LF
band-pass +10.1 dB, early HF band-pass −5.1 dB); the filter amps at unity (later T11 −6 dB after
its pad). `check.py` prints the per-amp table.

### Against the published figures

The later card against the fax (tolerances as given):

| Item | Published | ngspice | Verdict |
|---|---|---|---|
| HF bell | ±18 dB ±1 | +17.97 … +18.06 / −17.98 … −18.17 | meets |
| HF shelf | ±15 dB ±1 | in-band extremes ±14.13 … ±16.01 | meets, 0.01 dB over at the bottom of the travel (cut) |
| HF frequency | 1.5–20 kHz ±10 % | bell peaks 1.29–18.9 kHz | top meets; **bottom 1.29 kHz, 4.5 % below the tolerance**: drawn so |
| HF bell Q | 1.0 ±10 % | 0.90–0.96 | meets |
| HMF amplitude | ±15 lowest Q, ±18 highest Q, ±1 | ±15.59 … ±15.82; ±17.49 … ±17.77 | meets |
| HMF frequency | 600 Hz–10 kHz ±10 % | 640–9412 Hz (top 9039 Hz at lowest Q) | meets |
| HMF Q | 0.5–4.5 ±15 % | 0.48–4.01 | meets |
| LMF amplitude | as HMF | ±15.47 … ±15.61; ±17.94 … ±18.07 | meets |
| LMF frequency | 160 Hz–2.2 kHz ±10 % | 128–2019 Hz | top meets; **bottom 128–141 Hz, 2–11 % below the tolerance**: drawn so |
| LMF Q | 0.5–4.5 ±15 % | 0.48–4.35 | meets |
| LF bell | ±18 dB ±1 | ±17.74 … ±17.97 | meets |
| LF shelf | ±15 dB ±1 | ±15.75 … ±16.58 | **0.58 dB over at the top of the travel**: drawn so |
| LF frequency | 40–450 Hz ±10 % | bell peaks 37–414 Hz | meets |
| LF bell Q | 1.7 ±10 % | 2.05–2.13 | **21–25 % high**: drawn so |
| High-pass | 18 dB/oct (SSL) | 18.5–19.3 dB/oct over fc/8–fc/4, corners 15.4–398 Hz | meets |

PLAN.md Settled 20 rules: where the drawing and the fax disagree the drawing is read again, and a
reading that stands wins. Every part behind the four misses was read again (*Sources*) and
reads as the netlists have it; no value changed. Per figure:

- **HF bottom end.** Set by R4 3K0, R5 3K6, C3 2n2, C4 220p, C5 2n2 and the 50K anti-log gangs;
  all legible. Rev. 4's 220p across gang a postdates the fax but does not move the bottom: without
  it (RV1 recalibrated to 2154 Ω) the bell peaks at 1283 Hz instead of 1289 Hz.
- **LMF bottom end.** Set by R28 3K0, R29 3K6, C12 22n, C13 22n and the 50K gangs; all legible.
  The top, set by the fixed parts alone, meets the fax (nominal 2201 Hz against 2.2 kHz).
- **Why both bottoms miss.** HF, HMF and LMF share one topology, the same fixed resistors and the
  same 50K gang, so each travel spans √((3K0 + 50k)(3K6 + 50k) / (3K0 · 3K6)) = 16.2. The fax's
  HMF range (16.7) fits; its HF (13.3) and LMF (13.75) ranges do not. A misread fixed part would
  move the top as well; only the gang moves the bottom alone, and a gang of 47.8k (HF) or 46.9k
  (LMF) puts the nominal bottom on the tolerance's edge: 4–6 % under 50K, inside a carbon pot's
  ±20 %. The drawing gives 50K for all three gangs.
- **LF bell Q.** Set by R42 4K3, R43 4K7, C16 33n ‖ C17 47n, C18 33n ‖ C19 47n, the divider R44
  5K1, R45 2K4, RV4, and the post amp R47 3K0, R48 43K (*Readings*), R87 7K5; the values are
  legible, and R44, R45 and R87 are also the fax's own rev. 3 values. Not a matter of definition:
  the band-pass's pole Q equals the −3 dB Q (2.07–2.16 LF, 0.92–0.96 HF), so the drawn LF's Q is
  2.2–2.3 times the HF's where the fax's is 1.7 times. R87 is what sets it at the +18 dB
  calibration: 10K would give 1.70–1.74 (RV4 1013 Ω), 33K (the HF's R81) 0.91–0.93. RV4 reads
  5K, the fax says 10K: the calibrated 811 Ω lies inside both.
- **LF shelf level.** The same parts, with R85 and R88 100K on shelf; all legible. R87 does not
  bring it down (10K: +16.63 dB at the top of the travel).

The variant figures come from one-off runs of `check.py`'s solver on copies of the netlists
(a part changed or removed, the trimmer recalibrated as *Calibration* describes).

The early card against the fax's HF bell Q of about 0.6: 0.60–0.63, met. Against X-Rack's Brown
figures (the re-implementation): HMF 623 Hz–6.8 kHz (0.6–7 kHz), LMF 186 Hz–2.1 kHz (0.2–2.5
kHz), HF 1.2–13.4 kHz (1.5–16 kHz), LF 37–398 Hz (30–450 Hz); gain ±14.4 … ±16.3 dB (±15); mid Q
0.48–3.07 (0.5–2.5); bell Q HF 0.6, LF 0.9 (0.8). The 02 high-pass is 12 dB/oct (the drawing
settles what SSL's guide implied).

## Amplifier

The drawings specify 5534-type op-amps throughout, on ±18 V rails (early through 10 Ω, later
22 Ω decoupling), compensated with 22 pF (the later card 10 pF on T2, T3, T8 and T10). The model
has two op-amp stages (*Implementation*), each `common/dsp/OpAmpClip.h`: LineAmp's loop (an
output stage S (u) = u / √(1 + u²) inside a loop of gain L) at the chip's figures, so it clips
like the op-amp (PLAN.md Settled 18). From the datasheet (SLOS070D; VCC ±15 V unless noted):

| Figure | Datasheet | Model |
|---|---|---|
| Output swing | VO(PP) 32 V typical (30 V minimum) at ±18 V, RL ≥ 600 Ω | rails ±16 V, symmetric (the class-AB output) |
| Open-loop gain | AVD 100 V/mV typical (DC, VO = ±10 V, RL ≥ 600 Ω); Avd 2.2 V/mV at 10 kHz with CC = 22 pF; unity-gain bandwidth 10 MHz (CC = 22 pF) | loop gain 2.2 V/mV times the feedback fraction: the input amp, a follower, 2200; T8, inverting with four 4K75 at its virtual earth (R35 from its output, R36, R37, R38 from op-amp outputs), a quarter of it, 550 |
| Slew rate | 6 V/µs with CC = 22 pF (13 V/µs with none) | 6 V/µs |

- **Which open-loop gain.** The gain falls 20 dB per decade from about 200 Hz with 22 pF, and the
  model's loop is memoryless, so it takes one value: the datasheet's audio-band figure for the
  cards' compensation, at 10 kHz, the top of the band where an op-amp's distortion is highest. At
  1 kHz the chip's loop gain is about ten times this, so below the rails the model's THD is a
  ceiling there (about 20 dB above the chip's), and right at 10 kHz. PLAN.md *Provisional* 2.
- **The later card's 10 pF** on T8: the datasheet gives figures for 0 and 22 pF only; both cards
  use the 22 pF ones.
- **The closed loop** in units of the swing is the rails' hard clip plus a small correction d
  (`OpAmpLoop`, tabulated in the output's own units, within 7.7e-9 of the swing of the exact loop;
  LineAmp's table, uniform in the loop's input, could not resolve a knee at these loop gains, which
  is why the M1 model ran at loop gain 100). Below the rails d is the loop's residue (-1.3e-4 of
  the swing at -1 dBFS in T8); at the rail it is the knee, the output 0.44 % (input amp) and 1.06 %
  (T8) below the swing; beyond it the output approaches the rail as (1 / (1 + L))² / (2 (q - 1)²).
  Umber's `LineAmp` is unchanged (its golden renders byte-identical).
- **The slew** is a hard limit of 6 V/µs on the clipped output, solved in continuous time with
  the clip (*Antialiasing*): the chip's output moves at its rate until it meets the signal again,
  its corners far sharper than an 8x sample, so the kernel, not a knee, band-limits them. Within
  the band an unclipped signal never reaches it (16 V at 20 kHz moves 2.0 V/µs); a clipped one
  does, at its edges.

**Antialiasing** (PLAN.md Settled 16 and *Provisional* 4: -80 dBc at every Drive and every EQ
setting). Each stage runs in the chain's 2x run as a main path at 2x, the linear output delayed
plus the loop's smooth residue d_in (d below 0.65 of the swing, faded out by 90 % of it), and a
residual at 8x through linear-phase half-band FIRs (Kaiser, 95 dB, as Mulberry's Clean, their
symmetric taps folded; K = 8 from 2x to 4x, K = 3 from 4x to 8x; 22 samples at 2x in all): the
rails' clip and the slew, plus the knee's part of d (d_k), less what the main path holds.

- *The input as a cubic spline.* The 8x samples are taken as an interpolating uniform cubic
  B-spline (a 7-tap prefilter gives its coefficients: the inverse of (1 4 1) / 6 to the third
  power in 1 - cos w), whose images lie sinc⁴ down where a linear interpolation's lie sinc² down.
- *Clip and slew, exactly.* On each segment of the spline (a cubic in t) the clip and the slew are
  one continuous-time problem: the segment splits where its slope is 0 or ±6 V/µs and where it
  crosses a rail (Newton with bisection on each monotone stretch), so that on each piece the
  clipped input is one rail or the cubic, its slope on one side of the limit; a hard rate limiter
  follows it piece by piece, and the output, piecewise polynomial, is integrated exactly under a
  cubic B-spline kernel, less the spline's own part (antiderivative antialiasing of the third
  order: Bilbao, Esqueda, Parker, Välimäki 2017, after Parker, Zavalishin, Le Bihan 2016; aliases
  attenuated by sinc⁴ of their frequency over the 8x rate). Against a 20 000-step simulation of
  the clamp and the limiter, 4000 random segments per rate, the moments agree within 2.8e-5 of
  the swing at 88.2 kHz (the simulation's own step is 5.3e-5) and 1.2e-5 at 192 kHz; a segment
  that neither clips nor slews takes closed-form shares, equal to the exact ones within 1e-13
  (`ConsoleAmpTests.cpp`).
- *The knee*, d_k, smooth, under a triangle through its exact antiderivatives (each table cell a
  cubic and its own integrals) on lines through the segment's knots, and across or on a rail
  through its crossings and the pieces' midpoints, so that d_k's kink falls where the clip's does.
  Where d_k is nearly linear over a segment the triangle runs on the line through its values at the
  knots instead (M1c review round 2): a segment on one side that stays below 0.95 of the swing or
  beyond 1.05, or whose span is within a quarter of its distance from the rail. Near the rails the
  two agree within 1.3e-5 of the swing (T8; 5.4e-6 in the input amp) on lines that rule samples
  (`ConsoleAmpTests.cpp`); further from them the sampled knee is where the integral's antialiasing
  is not needed, which the aliasing grid measures (*Measured*). It was the knee's cost near the
  rails: with a sine just under them a stage costs about 28 % less (1 and 15 kHz), the chain at
  Drive +24 dB 6 ... 7 % less (*Measured*, CPU).
- *The main path near the rails.* While every sample its kernel reaches stays below 0.6 of the
  swing d_in is sampled: there it is c q³ (c = -1 / (2 (1 + L))), which puts nothing above three
  times the input's band, plus a remainder below 7e-7 of the swing. Above, c q³ is sampled and the
  remainder runs under a triangle through its antiderivatives, centred on the delayed sample (no
  added delay), its droop undone, for 32 samples after the last such sample. Sampled there, d_in
  held one stage at -80.4 ... -86.1 dBc; under the triangle -89.3 ... -93.1 dBc (M1c's design
  step). Its table is finer than d_k's (2048 cells per unit of the swing against 1024): within
  2.1e-13 of the swing of the exact loop up to the window.
- *Droop.* Two 7-tap FIRs undo the kernels' droop on the spline: within 5e-6 dB to 20 kHz at
  44.1 kHz.
- *Only near the rails.* The residual is exactly zero while the input stays below the window's
  start over the FIRs' and the prefilter's largest overshoot (0.315 of the swing: -4.75 dBFS at
  Drive 0; at 192 kHz the slew's bound, 0.25 of the swing, -6.7 dBFS) and slower than the slew
  rate. It is computed only then, its FIRs restarted from the input's history, and while it runs
  every segment that stays inside both, and every FIR whose history is all zeros, is skipped: the
  output is the same to the bit as computing it on every sample (`ConsoleAmpTests.cpp`). Stereo
  runs as two lanes in one loop, the residual for both while either needs it, each lane the same
  to the bit as a stage of its own.
- *Band-limits.* The chain's 2x pairs are half-bands with half Oversampler2x's transition (PLAN.md
  *Deviations* 10): with Oversampler2x the image of the clipped tone's third harmonic (21 kHz at
  44.1 kHz) entered T8 43 dB down and T8's clip mixed it into the band (-72.7 dBc at Drive +9).
  Their transition band, 0.48 ... 0.52 fs, still passes half of what lies near Nyquist: a clipped
  harmonic just above it folds at the decimation, one just below comes back from T8's interpolator
  barely attenuated and T8's clip mixes it into the band (tones from 2 to 15 kHz at Init, Drive +12
  and +24 dB: 12 of 131 over -80 dBc, the worst -26.2 dBc at 7.4 kHz, in M1c's design step), and input near Nyquist
  reaches the input amp's clip the same way (-68.9 dB re a 21.4 kHz tone at Drive +12 dB). An
  elliptic low-pass of order 10 at the host rate (`ChainBandLimit` in `EqChain.h`; scipy
  `ellip (10, 0.001, 100, 0.907)`: flat to 0.4535 fs within 0.001 dB, below -100 dB from
  0.4702 fs, one set of coefficients for every rate) in front of the first pair and behind each
  takes it out. M1c's design had order 8 with 0.01 dB of ripple: it ripples ten times more and
  attenuates less between 0.455 and 0.48 fs (-11 against -16 dB at 0.46 fs, -51 against -95 dB at
  0.47 fs) for one section less. Each band-limit delays the band's top: group delay 0.43 samples
  at DC, 1.9 at 15 kHz and 6.7 at 20 kHz (44.1 kHz). The pass band now ends at 20 kHz at
  44.1 kHz (0.4535 fs), where the pairs alone passed to 21.2 kHz.

How the combination was chosen (one stage, then the chain; from the M1 close-out's first review
on, the strongest alias read exactly at the folded harmonics; from M1c every setting, tone and
Drive of the gate, *Measured*):

| Combination | Worst alias | |
|---|---|---|
| 2x, the clip as drawn in M1 (loop gain 100, no antialiasing) | -24.8 dBc | M1; 7 kHz at Init |
| 2x, second-order ADAA | -58 dBc | estimated: the clipped tone's harmonics times sinc² at 88.2 kHz |
| 4x, second-order ADAA, the clamp alone | -81 … -83 dBc | the kernel's linear interpolation of the input, coarse at 4x |
| 8x, second-order ADAA, the clamp alone | -102 … -104 dBc | |
| 8x, as above with the knee not antialiased | -86 dBc (L 2200), -76 dBc (L 550) | so d_k is antialiased too |
| 8x, the slew after the droop's compensation | -64 dBc at +24 | the compensation steepens the edges into the slew |
| 8x, the slew on the kernel's output, LineAmp's knee / its smooth onset | -74 / -86 dBc at +24 | at Init; with the HF shelf boosted -48 dBc, with the HMF boosted at the tone -76 dBc: the slew was not antialiased |
| 8x, clip and slew in continuous time on the input's linear interpolation, cubic B-spline (M1 close-out) | -88.9 dBc at Init, -63.5 dBc with a band boosted | the HF bell near the top of its travel boosting the clipped tone's 21 kHz harmonic into T8: the linear interpolation's images, sinc² down, mixed into the band |
| 4x, the input as a cubic spline | -70 … -78 dBc (stages, a band boosted) | the spline's own images at 4x (M1c's design step) |
| 16x, linear interpolation | -74 dBc | the boosted case (M1 close-out) |
| 8x, cubic spline, d_in sampled, window from 0.5 / 0.65 of the swing | -78.8 / -81.0 dBc over every setting | d_in's sampling at 2x left (M1c's design step) |
| 8x, cubic spline, d_in antialiased near the rails, window from 0.5 / 0.65 / 0.7 | -93.2 / -85.5 / -82.9 dBc | at -1 dBFS 3.1 / 2.3 / 2.2 % (48 kHz stereo; M1c's design step); from 0.65 as built |
| the chain, both stages, Oversampler2x around them | -72.7 dBc at +9 | the image above |
| the chain as built | -85.1 dBc, every setting, tone and Drive | *Measured* (-85.5 before the knee was sampled where nearly linear) |

The worst left is a 15 kHz tone at -85.1 ... -85.8 dBc on many settings and Drives alike, from
Drive -12 dB up (-88.8 dBc at Init); every other tone stays below -92 dBc.

Cost: below the window's start each stage is a delay and one table lookup; the residual costs more
the more often the signal comes near the rails (*Measured*, CPU). The stages at 8x on every sample
(IIR cascades, both stages, both channels) cost 1.6 % at 48 kHz alone.

Where it clips: in the circuit, at the summing amps' outputs first (*Headroom* above), T3 before
T8 when the front's bands boost and the back's cut. The model has rails at two places only, the
channel's input amp and one amp at the card's output (*Implementation*); T3, T8 and the bands'
op-amps are ideal inside the generated networks. A front boost that the back cuts again
therefore clips nowhere in the model, where the card would clip at T3: a known limit, reached
only with a band boosted by more than the 5.3 dB of headroom above 0 dBFS (*Level*) and cut
again behind it.

## Implementation

The model is three stages (`ConsoleModel`, PLAN.md *Open for a channel strip*):

| Stage | Rate | What |
|---|---|---|
| Input amp | 2x, its clip 8x | the channel's line amp, an NE5534 follower into its rails (`OpAmpClip`, loop gain 2200; *Amplifier*) |
| Network | host | the card: front network into back network, per channel |
| Summing amp | 2x, its clip 8x | T8's rail clip at the card's output: an NE5534 at unity gain into its rails (`OpAmpClip`, loop gain 550), the only clip inside the card (T3 and the bands' op-amps have none: they are ideal inside the network, *Amplifier*) |

The chain runs each 2x stage inside a 2x pair and band-limits in front of the first pair and
behind each (`ChainBandLimit`, *Amplifier*): input band-limit, input amp, band-limit, network,
summing amp, band-limit.

**Networks.** `netlist/networks/<card>_<part>.cir` include the card netlists unchanged and add the
source and tools/netlist's directives; `tools/netlist` generates `generated/*.h` from them. The
tool cuts each network at op-amp outputs that alone join the input to the output (exact: an ideal
source is not loaded by what follows), so a buffered filter is a block of its own:

| Network | `networks/` | States | Blocks (largest degree) | `generated/` | Analysis |
|---|---|---|---|---|---|
| early front | `early_front.cir` | 32: filters in/out, shape, two boost/cut halves each | high-pass, low-pass (cut at their op-amps), bands 11 | `ConsoleEarlyFront.h`, 4.6 MB | 54 min |
| early back | `early_back.cir` | 8 | bands 8, output coupling 1 | `ConsoleEarlyBack.h`, 0.9 MB | 5 min |
| later filters | `later_filters.cir` (out at the low-pass's op-amp) | 2 | first-order high-pass 1, the rest of the filters 8 | `ConsoleLaterFilters.h`, 7 kB | 1 s |
| later output coupling | `later_coupling.cir`, `later_coupling_z.cir` | 1 | 2 | `ConsoleLaterCoupling.h`, `ConsoleLaterCouplingZ.h`, 2 kB each | 1 s |
| later high-pass, open circuit | `later_source.cir`, `later_source_z.cir` | 1 | 1, 8 | `ConsoleLaterSource.h`, `ConsoleLaterSourceZ.h`, 7 kB each | 1 s |
| later bands | `later_front_direct.cir` (both filters out) | 8 | 13 | `ConsoleLaterFrontDirect.h`, 1.5 MB | 29 min |
| later EQ input impedance | `later_core_z.cir` | 8 | 13 | `ConsoleLaterCoreZ.h`, 1.7 MB | 60 min |
| later back | `later_back.cir` | 8 | bands 9, output coupling 2 | `ConsoleLaterBack.h`, 1.1 MB | 23 min |

(Analysis: wall time of `netlist.py generate` with a process per state, 24 cores.) Each header
evaluates a state in a function of its own, in parts of at most 32 statements or 16 kB
(tools/netlist/README.md, *How it works*): the early front's translation unit builds in 16.6 s
and 0.53 GB with GCC (-O3), against 38.8 s and 2.1 GB as one evaluate(); the headers were
re-emitted from the one-function ones, statement for statement, and give the same polynomials to
the bit (200 000 settings per network, every state). The later
card's front is one of three, after its filter switches: both out, the bands alone; the
low-pass in, the filters to its op-amp (an exact cut) and then the output coupling joined to the
bands; the high-pass in and the low-pass out, the high-pass joined to the bands. The coupling
(C46, R72, R74, C47, R76, R75) is copied into its own two small netlists because the EQ is always
attached to it in `later_front.cir`; the ngspice comparison runs on `later_front.cir` itself.

With the high-pass in and the low-pass out, the later card's high-pass drives the EQ input
through R64 (10 ohm) and takes its own feedback there, so the two interact and no op-amp output
separates them; solved as one network the analysis ran 50 CPU minutes per state without
finishing. Each side is generated alone instead and `PortComposition` joins them exactly at
control rate (`ConsoleLaterCard.cpp`): the high-pass's open-circuit output h1 = n1 / d1 (the
low-pass's input network loads it in both cases; with the low-pass in, what follows hangs behind
its op-amp and drops out), its output impedance and the EQ input's impedance through a 1k sense
resistor, h2 = n2 / d2 = Zs / (Zs + R) and h3 = n3 / d3 = Ze / (Ze + R), and the bands from the
EQ input, hc = nc / dc:

    H = h1 Ze / (Ze + Zs) hc = h1 n3 (d2 - n2) / (n2 (d3 - n3) + n3 (d2 - n2)) hc

n3 is dc times a constant and d1 is (d2 - n2) times what the source buffers off (the high-pass's
first-order section; a constant for the coupling), each pair the same determinant, so both cancel
exactly and the block is kappa n1 nc / (q (n2 (d3 - n3) + n3 (d2 - n2))): degree 22 with the
high-pass, 15 with the coupling. The low-pass-in case joins the coupling the same way.

Blocks above second order (the bands around one summing amp) run as a `RootCascade`
(tools/netlist/README.md, *Interacting blocks*): roots tracked from design to design, paired
once (a band's two poles together, coupling capacitors' roots together), one stage per pair with
coefficients that follow a quartic B-spline through the designs (roots of different stages meet in
fast glides; *Measured*, *Automation*), roots beyond a thousand times the sample rate at infinity;
the zeros fill the stages in
order of size, so each stage keeps its zeros on its poles' scale. Every pot sweeps without a
click (*Measured*). Order of size matters for the later card's high-pass joined to its bands
(degree 22, the high-pass's three zeros at DC): placed by nearness, its DC zeros land with the
200 kHz poles, the stages' gains at 0.01 Hz span -242 ... +78 dB and the rounding between them
comes out as a sub-audio swell (at 192 kHz 0.4 of an impulse after 1 s, +108 dB over
`response()` at 20 Hz; at 44.1 kHz +44 dB). In order of size the stages span -65 ... +24 dB and
the tail settles like the circuit's (`ConsoleTests.cpp`).

**Controls.** `ConsoleControls` maps ModelInfo's physical values to the netlists' pots with the
laws in *Control laws*: the band frequency in closed form, the filters by their fitted constant,
gain and Q through `ConsoleLaws.h` (generated by `netlist/laws.py` from check.py's nodal solver:
G_cal and Q_cal sampled at 241 positions, denser at the ends, inverted with a monotone cubic and
stored as 129 points per half, uniform in dB and in ln Q; 0 dB is the centre tap exactly). The
gain ranges in ModelInfo sit inside the tables' ends (+-15.0001 ... +-18.0000 dB); where a table
ends 0.0001 dB short of the knob's range (later HMF, LMF boost; later LF cut) the wiper stops at
its end.

**Switches.** Shelf/bell, a filter in or out (its knob's Off zone), the card (Alt), EQ In, Stereo
and Tolerance off/on change the networks' topology. There are two sets of networks. A change
builds the idle set at the new switches, designed from scratch and from rest, one bounded step
per 32 host samples (`ConsoleModel::stepSamples`; `Network::resetStep()`: an evaluation, the
sections, or about 40 µs of a cascade's cold roots and maps), runs it unheard on the input for
10 ms, then crossfades to it over 10 ms along a raised cosine. The new networks start from rest,
and starting is a transient (the bands ringing up, a boosted high band the most): run unheard
first, it has died away before they are heard, and the fade itself has no step. Without the
10 ms, every knob swept together (the low-pass passing through Off at the top) clicked at
-61.9 dB (early card, 44.1 kHz) and -60.7 dB (later, 48 kHz), the 3rd difference at the switch
-71 / -69 dB under its envelope against the clean sine's -80 / -82 dB; with it the switch is at
the clean sine's floor. A change is heard 10 ... 35 ms after it at 48 kHz (the build, 0 ... 25 ms,
then 10 ms unheard; Tolerance switched on with the later card's low-pass in is the longest) and
complete 10 ms later. The set being built and warmed keeps the pots of its own design steps and
follows the knobs only once it fades in, gliding on from them: following them throughout, its
designs doubled the active set's while every knob was automated (0.52 ms in the 0.33 ms period at
96 kHz in blocks of 32, Tolerance 50 %, a switch every 50 ms). Only the networks the switches
use are designed: the later card's other fronts wait for a change that selects them. The set
fading out keeps its own switches, its pots and its Tolerance spread (a filter switched Off fades
out at its last frequency; after Alt the current pots follow the other card's laws, which would
click; Tolerance switched off would glide the outgoing pair's pots to the unspread ones while the
incoming set builds, two networks' designs per control block). A change that arrives during a
build or a fade waits for it, or restarts the build if it changes the switches again. The
switches are read on the same 32-sample grid, so the output does not depend on the host's block
sizes. With Tolerance at 0 both channels run through one set of networks; above 0 the two
channels' networks run side by side (`processPair()`, the same output as one by one). A model
change builds the Console's networks the same way while the chain warms it up unheard
(`EqChain`).

**Prepare and reset** (PLAN.md Settled 19). `prepareToPlay` designs the active model for the
settings it is given, at once; a host's `reset()` clears every state and keeps the designs (a
running crossfade completes, a change being built starts again with the next block), so neither
the first block after `prepareToPlay` nor the first after `reset()` designs anything. A setting
changed in between arrives as automation does (PLAN.md, *Provisional* 1).

**Pots** glide inside the networks (tools/netlist/README.md, *Network*): two one-pole stages of
20 ms in the log ratio of each pot's two segments, so a wiper near the end of its travel, where a
segment is a few ohms, moves the circuit by a bounded factor per control block, and the path has
no corner when it starts. A jump is within 10 % of its way at 80 ms and within 1 % at 0.13 s.
The control block is 64 samples up to 72 kHz and 128 above (tools/netlist/README.md,
*Network*; 32 up to 72 kHz before M1c's last step, *Measured*), and above 72 kHz each network
designs in a quarter of it of its own on the model's
sample clock: the first channel's front, then the second's, then the first's back, then the
second's (the later card's low-pass filters with the back), so at 96 kHz in blocks of 32 a
callback holds one network's design. With every knob automated a callback held all four
(0.45 ms of a 0.33 ms period).
Each block is designed normalised, its scale in the network's gain, so the generated code's
split between a block and the gain (the early high-pass block grows against its gain as the
corner moves; the joined later front scales by its largest coefficient) never clicks; the gain
follows the same spline through the designs as the cascades' coefficients, so a cascade's scale
moves with its roots when several of its pots move at once (tools/netlist/README.md, *Network*).

**Stereo.** Mid or Side: the network runs on (L + R) / 2 or (L - R) / 2 alone (channel 0's
networks); the amps stay L/R. Mono runs channel 0.

**Tolerance.** A fixed spread per channel and pot (uniform -1 ... 1, from a fixed seed), times
Tolerance times +-20 % (carbon pots): frequency, Q and filter pots scale their resistance, the
boost/cut wipers scale their distance from the centre tap (a centred knob stays flat). At 0 both
channels get the same pots and are bit-identical.

**Noise.** Hiss and hum added in front of the network (the EQ shapes them): hiss -90 dBu over
20 Hz ... 20 kHz, hum -100 dBu at 50 Hz and -106 dBu at 150 Hz, at Noise 100 %. Our figures for
a quiet NE5534 channel (not sourced: the card drawings and the fax give none); seeded per channel,
reset with the model.

**Level.** A 0 dBFS sine is +18 dBu (EBU R68): sample 1.0 is 8.70 V. The rails' +-16 V sit 5.3 dB
above 0 dBFS at unity, so the summing amp clips at -12.7 dBFS with a band at full boost, and
Drive (up to +24 dB) reaches the input amp's rails. Both amps are `OpAmpClip` with gain 1, rails
+-16 V, 6 V/us slew and loop gains 2200 and 550 (*Amplifier*). The chain divides by Drive times
the stages' small-signal gain (exactly 1 here): Drive changes the colour, not the level.

## Measured

2026-11-12. `tools/measure` (`ViridianMeasure`, `compare.py`; the shared measurements in
`tests/support/ChainGates.h`) renders through the real chain; `ViridianTests` (`[gates]`,
`[chain]`, `[golden]`) and `ViridianTests_console` hold the gates as tests.

**Curves against ngspice, through the chain** (`compare.py`). 650 settings, 325 per card: Init;
each band's frequency at eleven positions at full boost and full cut (mids at highest and lowest Q,
HF and LF as bell and shelf); each gain and Q at eleven positions; each filter from Off over its
travel; the four shape combinations with every band at work; interactions (the high-pass in with
the low-pass out and every band at work, every band at full boost or cut at once, the mids cut over
Q, overlapping bands, every frequency at one end of its travel, the filters just past their Off
zones); flat settings away from Init; 24 seeded random settings. Each is the chain's response with
EQ In over the same chain with EQ In off (an impulse at -60 dBFS, 4 s, DFT at 121 points from 20 Hz
to 20 kHz: the amps, oversamplers and DC block divide out), against ngspice AC on
`<card>_front.cir` times `<card>_back.cir` for the pots the chain ran. Stop bands below -80 dB of
the setting's peak are compared in absolute terms (what 0.1 dB is at -80 dB, 1.2e-6 of the peak):

| Rate | Magnitude to 20 kHz (gate 0.1 dB) | Worst setting | Stop bands | Display = audio (gate 0.05 dB) |
|---|---|---|---|---|
| 44.1 kHz | 0.0031 dB | later HMF cut, highest Q, knob 80 % (6.3 kHz) | 2.7e-7 | 0.0023 dB |
| 48 kHz | 0.0005 dB | later HMF boost, highest Q, knob 80 % | 2.5e-7 | 0.0009 dB |
| 96 kHz | 0.0005 dB | later LF bell cut at its bottom, at 36 Hz | 1.4e-7 | 0.0009 dB |
| 192 kHz | 0.0022 dB | later high-pass at 100 %, at 21 Hz | 9.1e-8 | 0.0033 dB |

(Measured 2026-11-13, M1c, with the band-limits in the path; review round 2 had 0.0012 dB at
192 kHz and display = audio 0.0005 ... 0.0012 dB: the curve window draws the network, and the three
band-limits' ripple, 0.001 dB each, is the rest.)

At 96 and 192 kHz these are the measurement's floor, not the networks' (the card-level figures
below are under 0.0001 dB there): the bottom of the band, where the 4 s window cuts the coupling
capacitors' tails (2 s left 0.004 dB), and the deepest levels of the float output. Display =
audio: the curve window's `response()` against the measured response, the same grid.
The card-level comparison (`tests/models/console/reference/`, 56 settings with pots set directly,
the realised filter's own `response()`) gives 0.0026 dB at 44.1 kHz, 0.0005 at 48 and below
0.0001 at 96 and 192; the generated H(s) is within 2e-5 dB of ngspice (its op-amps' 1e9 gain) and
reproduces the netlists' exact responses to 3e-13 (`ConsoleGeneratedTests.cpp`).

**Phase** (PLAN.md Settled 13: reported, not gated; `compare.py` prints it per rate). The bands'
phase is the setting's response over its flat twin (the same knobs, every gain centred and the
filters Off), against ngspice's, to 10 kHz; the flat twin's own phase is the network's (C8 and C2's
poles at 100 ... 220 kHz). Worst cases over the grid, not bounds (a one-off run with 144 random
settings per card found 64.9 / 63.1 / 31.2 / 17.0 deg in the first column), measured 2026-11-13:

| Rate | Bands' phase, every band below fs/8 | A band above fs/8 | Highest band below 250 Hz / 0.5-1 kHz / 1-2 kHz / 4-8 kHz / above 8 kHz (low-pass out) | Low-pass in | The network at flat | Whole response |
|---|---|---|---|---|---|---|
| 44.1 kHz | 57.1 deg | 69.2 deg | 2.2 / 6.9 / 11.6 / 16.9 / 15.6 deg | 57.1 ... 69.2 deg | 7.0 deg | 75.5 deg |
| 48 kHz | 53.1 deg | 63.3 deg | 1.8 / 5.9 / 10.4 / 15.2 / 14.7 deg | 52.5 ... 63.3 deg | 6.9 deg | 69.6 deg |
| 96 kHz | 30.1 deg | 28.7 deg | 0.5 / 1.6 / 6.6 / 7.9 / 8.6 deg | 26.5 ... 30.1 deg | 6.5 deg | 36.2 deg |
| 192 kHz | 15.5 deg | (none) | 0.1 / 0.4 / 3.8 / 4.7 / 5.2 deg | 14.2 ... 15.5 deg | 5.7 deg | 21.1 deg |

The matched sections are minimum phase over 0 ... fs/2; the analog bands carry their magnitude
above Nyquist (the low-pass's 12 dB/oct, the HF band's shelf up to the C8 roll-off) into their
phase below it. The error grows with the band's frequency, not at a threshold: at 44.1 kHz a band
below 250 Hz already shows 2.2 deg at 10 kHz. At 192 kHz the bands above 1 kHz miss by 4 ... 5
deg because the frequency map is fitted to 32 kHz and the magnitude above it departs by design
(tools/netlist/README.md). An all-pass excess-phase correction per network, or at 96 and 192 kHz a
map fitted closer to Nyquist, would reduce it; neither is built (Settled 13).

**Flat** (PLAN.md Settled 12: gated against the netlists' own flat). At Init the chain equals the
netlists' flat within 0.0001 dB at every rate; the circuit itself lifts by 0.107 dB at 20 kHz
(early) and dips by 0.024 dB at 2 kHz (later). Over the grid's flat settings (every gain
centred, filters Off, 18 per card) the cards lift by up to 0.592 dB (early: HF shelf at its
bottom, HMF at its top, lowest Q) and 0.205 dB (later), and the chain follows ngspice within the
magnitude gate above; the one-off run with 144 random settings per card found 0.665 dB.

**Drive compensation.** The level of a 1 kHz tone at -60 dBFS over Drive -12 ... +24 dB in 3 dB
steps, every band and both filters at work: within 5.2e-6 dB of Drive 0 at 44.1 and 192 kHz,
under 0.0001 dB at 96 (gate +-0.1 dB).

**THD** (H2 ... H9, dB re the fundamental, 96 kHz, Init, 2026-11-13, M1c; both cards alike within
0.4 dB):

| Drive, level | 50 Hz | 1 kHz | 10 kHz |
|---|---|---|---|
| 0 dB, -40 dBFS | -161.3 | -166.3 | -157.7 |
| 0 dB, -20 dBFS | -121.5 | -121.5 | -123.7 |
| 0 dB, -12 dBFS | -105.4 | -105.4 | -107.5 |
| 0 dB, -6 dBFS | -92.9 | -92.9 | -95.1 |
| 0 dB, -1 dBFS | -81.4 | -81.4 | -83.6 |
| +12 dB, -40 dBFS | -137.5 | -138.1 | -139.4 |
| +12 dB, -20 dBFS | -97.1 | -97.2 | -99.3 |
| +12 dB, -12 dBFS | -78.8 | -78.8 | -80.9 |
| +24 dB, -40 dBFS | -113.5 | -113.5 | -115.6 |
| +24 dB, -20 dBFS | -63.7 | -63.8 | -66.0 |
| +24 dB, -12 dBFS | -12.0 | -12.0 | -15.1 |

Below the rails this is the loop's residue, T8's (loop gain 550) above the input amp's (2200): odd
harmonics, 12 dB more level giving about 24 dB more THD, -81.4 dB at -1 dBFS and Drive 0 (it was
-62.6 dB with LineAmp at loop gain 100). The same at every frequency, because the loop is
memoryless; the chip's rises with frequency (*Amplifier*). At 10 kHz the figures read about 1 dB
lower than before M1c: the band-limit between the two amps is dispersive near the top of the band,
so the input amp's third harmonic (30 kHz, inside the band at 96 kHz) reaches T8 at another phase
against T8's own (M1c's design step: without the band-limits the same within 0.04 dB). At Drive +24 dB and -12 dBFS the tone is 15.7 dB over the rails.

**Aliasing** (PLAN.md Settled 16 and *Provisional* 4 and 11: a tone at -1 dBFS, 44.1 kHz, the
strongest non-harmonic component, read at each harmonic folded about fs against the tone at the
stage that clips it, -80 dBc at every Drive and every setting), 2026-11-14, M1c review round 2 (the
knee sampled where nearly linear; round 1's figures where they differ in brackets).
`ViridianMeasure` runs the full grid on every core (`aliasGrid()`: 1, 3, 7, 11 and 15 kHz; Init,
every band at work, each band at full boost and at full cut at 0, 50, 75, 90 and 100 % of its
frequency's travel with both shapes, or the mids' lowest and highest Q, every band boosted and
every band cut as shelves and as bells, the high-pass at its top, the low-pass at its bottom and
both, and 20 random settings per card; both cards; Drive -12 ... +24 dB in 3 dB steps: 14 170
renders), a
tone scan from 2 to 15 kHz in 100 Hz steps at Init (Drive +12 and +24 dB, both cards, 524) and
input from 18 to 21.8 kHz against the input tone's level (Drive 0 ... +24 dB in 6 dB steps, 390;
above 20.3 kHz the band-limit removes the tone itself). `ViridianTests "[aliasing]"` gates a subset
in ctest: per card and tone Init, every band at work and three of the worst settings at every
Drive, the known case below, at 15 kHz the later card's random 4, the scan, and the input from
19 kHz.

| Tone | Worst, early card | Worst, later card | Init / every band at work, worst of both cards |
|---|---|---|---|
| 1 kHz | -104.0 dBc (HF shelf at 50 %, Drive +24) | -101.1 dBc (HF bell at 90 %, +24) | -123.8 / -122.0 dBc |
| 3 kHz | -93.5 dBc (HF shelf at 90 %, +24) | -94.6 dBc (HF shelf at 75 %, +24) | -107.8 / -110.4 dBc |
| 7 kHz | -97.5 dBc (HMF at its highest Q at 100 %, -9; round 1 -102.5, every band boosted as shelves, +24) | -100.7 dBc (HMF at its highest Q at 100 %, +24) | -102.8 / -110.6 dBc |
| 11 kHz | -92.4 dBc (LF bell at 100 %, +6) | -92.2 dBc (random 19, +6) | -94.1 / -110.4 dBc |
| 15 kHz | -85.1 dBc (HMF at its lowest Q at 90 %, -6; round 1 -85.6, HMF at its highest Q at 90 %, +3) | -85.5 dBc (LMF at its lowest Q at 100 %, 0) | -88.8 / -107.3 dBc |

Every band at work cuts the top three tones; against the output's tone it reads -121.8, -110.4,
-107.8, -105.7 and -100.1 dBc at the five. No render of the 14 170 over -80 dBc; the cut and random
settings come no closer than the boosts (the later high-pass at its top -85.6 dBc at 15 kHz, Drive
+6 dB). Against the output's tone instead (*Provisional* 11), 8 of their 390 renders at 15 kHz and
Drive 0, +12 and +24 dB read over -80 dBc, the worst -71.2 dBc (the later card's random 4, +24 dB;
-96.6 dBc against the tone the input amp clipped, which the network then cuts) and the later
low-pass at its bottom -79.8 dBc (-107.0; round 1). On settings no step had used (review round 2:
each band at 0, 0.8 and full gain at five frequencies with both shapes or three Q, the filters at
their ends, every band cut or at work, 20 random settings per card from other seeds; 1 ... 15 kHz,
Drive -12 ... +24 dB, 23 140 renders) none reads over -80 dBc against the stage that clips it, the
worst -83.6 dBc (the early card's random 18, Drive +24 dB, 15 kHz), and 14 do against the output's
tone, all at 15 kHz, the worst -64.2 dBc (the later card's random 0, Drive +24 dB). The tone scan -94.3 dBc at worst (later card, 15 kHz, +12);
input near Nyquist -82.0 dB re the input at worst (20 kHz, Drive +6 dB; at 21.4 kHz and Drive +12
dB it was -68.9 dB before the input band-limit). The case that had aliased most, the later card's
HF bell at full boost at 90 % of its travel with 7 kHz, -103.1 dBc at worst over Drive (+24),
-106.7 dBc at +12 dB, where it measured -63.6 dBc before M1c. On the code before M1c (M1c's
design step) 710 of the 5720 renders were over -80 dBc, and the tone scan found 12 of 131 tones
over -80 dBc (-26.2 dBc at 7.4 kHz). At Init and with every band at work the 7 kHz tone of
Settled 16 measures -146 dBc up to Drive 0, -130 at +3, -102.8 at +6 and -106.8 ... -108.3 dBc from +9 to +24
(M1c, against the output's tone).

**Automation** (330 Hz sine, 3rd-difference crest, gate -60 dB; a clean sine alone measures
-76.6 dB at 44.1 kHz). Through the chain in 32-sample blocks, every band and both filters at
work: each of the twelve knobs swept end to end and back in 2 s at 44.1 kHz, -72.4 dB (early)
and -70.2 dB (later); the filters and band frequencies at 48 and 192 kHz too. Each knob jumped
in one block between both ends of its travel and the middle (a filter's ends just inside its Off
zone; -40 dBFS, the amps linear):

| Card | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz | Peak in 30 ms over the steady one |
|---|---|---|---|---|---|
| early | -73.7 dB | -74.7 dB | -85.5 dB | -96.2 dB | +0.43 dB (the high-pass, top to bottom) |
| later | -73.4 dB | -72.2 dB | -88.5 dB | -93.9 dB | +1.5 dB (the same: it moves off the tone) |

The jumps rely on each block being designed normalised (*Implementation*, *Pots*); since review
round 1 they are measured at the tone's worst phase (below) and every one stays within -60 dB. Drive
jumping -12 ↔ +24 dB every 50 ms: -76.3 dB at 44.1 kHz, -96.2 dB at 96 kHz; Output jumping
-24 ↔ +24 dB every 50 ms: -68.6 dB and -89.1 dB. Shape keys, a filter to Off, EQ In and Stereo,
-76.0 / -75.8 dB; Alt to the other card, -76.5 dB or lower both ways, at 44.1 and 48 kHz in
blocks of 32 and 256; a model change to and from each other model, -76.2 dB. The cards alone,
each pot swept with the others at work (bells and shelves, the later card's low-pass in and
out): -68.0 dB at 44.1 kHz, -70.3 at 48, -88.3 at 96 and -106.4 at 192 (shelves at 44.1 and
96 kHz), the worst the early LMF gain swept from full cut to full boost at every rate.

Knobs moved together (`ViridianTests "[groups]"`, -40 dBFS so that bands boosting together leave
the amps linear): each band's frequency and gain, the mids' frequency, gain and Q, and every
knob, each group also with its gain (every other knob) moving against the rest, swept end to end
and back in 2 s and jumped in one block between each pair of travel points; and every knob
jumped between 30 seeded pairs of random settings. Worst crest per rate, from the first base alone
(the close-out's first step; the review rounds' bases and figures follow):

| Card | | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| early | swept together | -67.4 dB | -69.6 dB | -87.6 dB | -93.1 dB |
| | jumped together | -66.8 dB | -66.6 dB | -78.1 dB | -77.3 dB |
| | every knob, random settings | -68.1 dB | -71.5 dB | -80.7 dB | -83.5 dB |
| later | swept together | -67.2 dB | -69.4 dB | -87.1 dB | -89.2 dB |
| | jumped together | -62.2 dB | -62.2 dB | -67.4 dB | -76.5 dB |
| | every knob, random settings | -63.1 dB | -63.8 dB | -62.1 dB | -64.6 dB |

Three causes, all fixed: the cascades' root tracking compared angles across the negative real
axis unwrapped, so a pair turning real in the same design as two real roots met went to the
wrong labels and the stages re-grouped (every knob of the early card jumped together, -50.3 dB);
the network's gain ramped linearly while the cascades follow a spline about a block behind it,
so a cascade's scale moved ahead of its roots (every knob between random settings, -56.6 dB at
44.1 kHz; tools/netlist/README.md, *Network*); and a switch's new networks entered with the
transient of starting from rest (*Switches*). The later card's worst, about -62 dB, is two real
poles in different stages meeting and turning complex in a fast glide (a square-root corner in
each, which the spline rounds but cannot remove); a fresh search over 400 more random pairs per
rate found nothing above -62.5 dB.

Review round 1 widened the gate: the groups and the random jumps start from four bases (LF shelf
with HF bell and both filters in, as above; LF bell with HF shelf and both filters in; both shelves
with the high-pass in and the low-pass Off, the later card's run-time join; both bells with both
filters Off, the bands alone; a filter held Off stays out of the groups and the random settings),
and the jumps are measured at the tone's worst phase (a sine into the left channel and a cosine
into the right, one network carrying both at Tolerance 0: the crest at any phase is bounded by the
two channels' 3rd differences and levels, so a fixed jump instant no longer decides where the
click falls on the tone). Review round 2 added the fourth front state (LF bell, HF shelf, the
high-pass Off and the low-pass in) and 100 random pairs per base instead of 30, which had missed
the worst cases. Worst crest per rate over the five bases, `ViridianMeasure`, 2026-11-13:

| Card | | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| early | swept together | -65.3 dB | -67.5 dB | -85.6 dB | -103.5 dB |
| | jumped together | -52.7 dB | -54.9 dB | -77.2 dB | -74.4 dB |
| | every knob, random settings | -49.4 dB | -38.8 dB | -68.5 dB | -59.7 dB |
| later | swept together | -66.0 dB | -68.2 dB | -86.2 dB | -98.8 dB |
| | jumped together | -58.5 dB | -59.3 dB | -80.2 dB | -80.6 dB |
| | every knob, random settings | -47.7 dB | -48.7 dB | -55.5 dB | -37.7 dB |

Eight of the 30 test cases fail (`ViridianTests "[groups]"`):

| Card, base | Case | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| early, both bells, both filters Off | every knob, random pair 45 | -49.4 dB | -38.8 dB | | -59.7 dB |
| early, both bells, both filters Off | HMF frequency, gain and Q jumped (and against) | -52.7 / -59.7 dB | -54.9 / -58.7 dB | | |
| later, both bells, both filters Off | every knob, random pair 11 | -47.7 dB | -48.7 dB | -55.5 dB | -57.9 dB |
| later, both bells, both filters Off | HMF frequency and gain jumped | -58.5 dB | -59.3 dB | | |
| later, both shelves, high-pass in, low-pass Off | every knob, random pair 44 | -53.0 dB | -50.9 dB | -56.7 dB | -37.7 dB |
| later, LF shelf, HF bell, filters in | every knob, random pairs 11 and 58 | -59.1 dB | -58.7 dB | | |
| later, LF bell, HF shelf, filters in | HMF frequency and gain jumped | -59.2 dB | | | |
| later, LF bell, HF shelf, high-pass Off, low-pass in | HMF frequency and gain jumped | -59.1 dB | | | |

(Blank: within -60 dB.) The two worst, -37.7 and -38.8 dB, measure the same on the sources before
round 2. Round 2's root-finder fix (a pair that had met within a design bounced on the real axis
from real warm starts; tools/netlist/README.md, *Interacting blocks*) took the early card's pair 45
at 44.1 kHz from -42.6 to -49.4 dB. The cause of the rest is the one named above, not the bases:
on the later card's bands alone (`ConsoleLaterFrontDirect`, the pots of random pair 11, the 330 Hz
tone in quadrature) the crest is -57.8 dB, its peak 43.5 ms after the jump, where two real poles
held in different stages (-21.3 and -17.4, sigma) meet and turn complex (-18.6 ± 4.7i), each stage
taking one of them and the square-root corner of its path. Logged on the cards alone with the
cascades' roots at 44.1 kHz, the three traced (early pair 45, later pairs 11 and 44) each peak at
such a meeting, between stages 0 and 2, 1 and 5, and 1 and 10 of the front's cascade. The back
network alone stays at the clean sine's floor. A re-pairing at run time was built in round 2 and
dropped (PLAN.md *Open* 14).

M1c found two mechanisms and fixed both (tools/netlist/README.md, *Interacting blocks*). Most
cases are the meetings above, but the corner is carried into the audio by the coefficients'
spline: a quadratic B-spline is only C1, so its second derivative jumps at each design near the
corner (designing more often made it worse, a higher order better). The coefficients, the
matched sections and each network's gain now follow a quartic B-spline (C3) through the last
five designs. The worst case, the later card's pair 44 at 192 kHz, is a root the root finder
dropped: the high-pass front's leading coefficient falls below 1e-14 of the largest in the glide,
a finite real root at -292.6 kHz went to infinity and a pair moved from -141.8 ± 17.1i to -69.3 ±
63.2i kHz; the degree is now decided by the roots' size against a thousand times the sample rate.
The three traced jumps as the cards' pots, the cards alone (`ViridianTests_console "Console: every
pot jumped at once*"`, gate -60 dB), before and after:

| Jump | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| early, both filters Off, pair 45 | -49.1 → -76.2 dB | -38.0 → -78.4 | -68.1 → -96.5 | -59.4 → -111.8 |
| later, both filters Off, pair 11 | -45.6 → -70.5 | -47.8 → -72.3 | -54.6 → -90.9 | -58.3 → -107.4 |
| later, high-pass in, low-pass Off, pair 44 | -52.7 → -73.4 | -50.7 → -75.5 | -55.8 → -93.7 | -36.7 → -111.3 |

The gate grew with the fix (`ViridianTests "[groups]"`, 77 test cases): every knob jumped between
200 seeded random settings per base (the first 100 are round 2's), one test case per card, base
and rate; a preset change, every parameter at once (the knobs, both shape keys, each filter Off a
quarter of the time, the card, EQ In, Drive -12 ... +6 dB, Output -24 ... +24 dB) between 200
seeded pairs per card and rate; and A/B, 100 such pairs toggled every 50 and every 200 ms, often
before a glide or a switch change has finished. A whole setting's change moves the tone's level
by tens of dB, and each control glides at its own pace (Output in two stages of 10 ms, the pots
in two of 20 ms in their own coordinate): with Output raised while the bands cut the tone, the
level overshoots both settings in between, by up to 26 dB in 1000 pairs, and the crest against
the whole render's level reads the tone's own curvature in that swell (the clean sine's -76.6 dB
plus the swell over the mean: every one of 2000 pairs at 44.1 kHz over -63 dB swells by 12 ...
26 dB, every pair swelling under 1 dB measures -70 dB or below). Preset changes and A/B are
therefore measured against the tone's level within ±5 ms of each sample (`worstPhaseLocalCrestDb()`:
a clean tone reads the same at any level, a click where the tone is quiet reads larger; on the
old code it reads round 2's clicks at -33.3, -40.7 and -53.4 dB) and the swell is reported
(PLAN.md *Provisional* 8). Worst crest per rate, 2026-11-13 (200 pairs per base, or per card for
presets; before → after the fix):

| Card | | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| early | swept together | -65.3 → -65.3 dB | -67.5 → -67.5 | -85.6 → -85.5 | -103.5 → -103.5 |
| | jumped together | -52.7 → -69.6 | -54.9 → -71.8 | -77.2 → -89.8 | -74.4 → -105.4 |
| | every knob, random settings | -49.4 → -67.1 | -38.8 → -69.6 | -68.5 → -87.3 | -59.7 → -104.0 |
| | preset changes | -67.3 → -72.5 | -68.5 → -74.2 | -87.4 → -87.5 | -94.7 → -99.2 |
| | A/B every 50 and 200 ms | -62.7 → -72.7 | -63.6 → -74.7 | -83.4 → -89.7 | -87.7 → -103.0 |
| later | swept together | -66.0 → -66.0 | -68.2 → -68.2 | -86.2 → -86.2 | -98.8 → -104.1 |
| | jumped together | -58.5 → -68.1 | -59.3 → -70.3 | -80.2 → -88.2 | -80.6 → -105.2 |
| | every knob, random settings | -47.7 → -64.8 | -48.7 → -67.0 | -55.5 → -84.9 | -57.9 → -101.5 |
| | preset changes | -59.7 → -72.9 | -64.8 → -74.8 | -85.5 → -87.4 | -92.1 → -92.8 |
| | A/B every 50 and 200 ms | -56.9 → -67.9 | -58.9 → -70.1 | -80.9 → -80.9 | -87.6 → -92.7 |

On the old code 26 of the 77 test cases fail, on the new none. The cards' pot sweeps
(`ViridianTests_console`, 210 sweeps) moved by 0.001 dB at most; renders whose settings do not
change are the same to the byte (44 settings, both cards, Stereo, Mid, Side, Tolerance and Noise,
four rates, 16.7 million samples, built against the code before and after); after a jump has
settled the outputs differ by -124 dB, the states' different paths decaying. `ViridianMeasure`
runs 1000 random pairs per base, 1000 preset changes and 500 A/B pairs per card and rate, all
within the gate: every knob between random settings -64.5 / -66.4 / -85.3 / -94.4 dB (early, 44.1
... 192 kHz) and -62.5 / -64.7 / -82.7 / -91.6 dB (later; pair 292, the LMF at full boost and Q
about 4.2 gliding past the tone, the same with any spline order or control block; first read as
the LF bell); preset changes -71.0 /
-66.3 dB and A/B -71.0 / -67.9 dB at 44.1 kHz (early / later); the swell after a preset change up
to 26.4 dB. Re-run after M1c's antialiasing (the band-limits in the path, the latency 30): every knob
between random settings the same to 0.1 dB (-92.1 dB at 192 kHz on the later card, was -91.6),
preset changes -71.0 / -67.5 dB and A/B -71.0 / -68.0 dB at 44.1 kHz, the swell up to 26.4 dB.

M1c's control-block step. The quartic spline smooths a meeting of roots in different stages; it
does not remove it. Settings the gate had not sampled found a third case: the early card's back,
both filters Off, pair 719 of `ViridianMeasure`'s seeds (`measureSeeds`), the cards' pots of a
jump that clicked at -51.2 dB at 44.1 kHz and -53.4 dB at 48 (1 of the card's 1000 knob draws, at
one of their five filter and shape states). Its real poles at -578 Hz (stage 1) and -825 Hz (stage 3) meet and turn complex
(-737 ± 80i Hz), the two stages' coefficient steps rise to 2.7e-2 and 4.0e-2 in one design
(1.1e-2 ... 2.5e-2 in the designs either side), and the crest grows with the design rate as pair 45's did: 16-sample control
blocks -31.3 dB, 32 -51.2, 64 -70.4, 128 -73.1 (the card alone, 44.1 kHz). The networks design
every 64 samples up to 72 kHz since (*Pots*; tools/netlist/README.md, *Network*). The card jumps,
before → after (`ViridianTests_console "Console: every pot jumped at once*"`; at 96 and 192 kHz
the control block was already 128):

| Jump | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| early, both filters Off, pair 45 | -76.2 → -76.2 dB | -78.4 → -78.4 | -96.5 | -111.8 |
| later, both filters Off, pair 11 | -70.5 → -70.8 | -72.3 → -73.0 | -90.9 | -107.4 |
| later, high-pass in, low-pass Off, pair 44 | -73.4 → -73.4 | -75.5 → -75.6 | -93.7 | -111.3 |
| early, both filters Off, pair 719 (`measureSeeds`) | -51.2 → -70.4 | -53.4 → -73.5 | -91.6 | -88.9 |

Through the chain, worst crest per rate (44.1 / 48 / 96 / 192 kHz). `ViridianTests "[groups]"`
(77 test cases, all pass): every knob between 200 random settings per base -67.4 / -69.3 / -87.3 /
-104.0 dB (early) and -64.7 / -66.9 / -84.9 / -101.7 dB (later), within 0.3 dB of before; groups
swept together -65.3 / -67.5 / -85.5 / -103.5 (early) and -65.9 / -68.2 / -86.2 / -104.1 (later),
jumped together -69.6 / -71.8 / -89.8 / -105.7 and -68.1 / -70.3 / -88.3 / -105.4; preset changes
-72.5 / -72.8 dB, A/B -72.5 / -68.0 dB at 44.1 kHz (early / later). `ViridianMeasure` (its long
runs on `measureSeeds` now, settings the ctest's do not sample, though not held out: the control
block was chosen on them; they had been the ctest's 200 pairs and 800 more): every knob between random settings -62.6 / -64.8 / -82.8 / -88.5 dB (early)
and -62.9 / -65.1 / -83.0 / -97.6 dB (later), preset changes -69.4 / -68.4 dB and A/B -72.3 /
-70.4 dB at 44.1 kHz, the swell after a preset change up to 25.5 dB. The worst random pairs (early
842 and later 557 at 44.1 kHz; the ctest's early 112 and later 111) are the LMF boosted gliding
past the tone, which swells 8 ... 13 dB over the render's level: the same at control blocks of 32
and 64 within 0.3 dB, and read against the tone's level within ±5 ms -73.7 ... -76.1 dB. So is the earlier report's pair 292
(-62.5 dB, the LMF at full boost and Q about 4.2; the LMF alone -64.0 dB, every band but it
-73.6 dB). A third seed set (offset 104729, later card) has one such pair over the gate, -59.4 ...
-59.7 dB at 44.1 kHz, the same at control blocks of 1 to 64 samples, -73.0 dB against the local
level (PLAN.md *Provisional* 9). Two sets M1c had not used (offsets 31337 and 999983, both cards,
44.1 and 48 kHz, each card's 1000 knob draws at the five bases' filter and shape states; M1c review
round 1) read -62.75 and -61.56 dB at worst, none of their 20 000 renders each over -60 dB. A fourth
(offset 271828, review round 2) reads -57.1 dB at 44.1 kHz and -59.3 dB at 48 (the later card's
pair 113, the LMF at full boost gliding across the tone; -71.8 dB against the local level), 4 of
its 20 000 renders over -60 dB, the early card's worst -64.2 dB.

Unchanged by the step: renders whose settings do not change, and the golden renders, are the same
to the byte (the steady set above, against a build of the code before); after a jump has settled
the outputs differ by -126.8 dB at worst; the cards' pot sweeps; every figure of the curves,
display = audio, flat, Drive, aliasing and single-knob tables above.

**Real time.** Nothing allocates in `process()` while every parameter is automated at random,
model, Alt, filters into and out of Off and Tolerance on and off included (44.1 and 192 kHz,
`AllocationTests.cpp`). The longest callback, stereo, best of five runs, after each of 48
changes (every switch each way on both cards, Stereo's three modes, Alt, the model to and from
each other model, each also at Drive +24 dB, where the amps' 8x path runs on every sample), and
with every knob moving every block while a switch changes every 50 ms (Alt, either shape, either
filter to Off; Tolerance 0 and 50 %, Drive 0 and +24 dB; `ViridianTests "Gates: no callback*"`,
gate three quarters of the period at 96 kHz in blocks of 32 and 48 kHz in blocks of 64, and at
192 kHz in blocks of 64), 2026-11-13, M1c:

| | 48 kHz, blocks of 64 (1.33 ms) | 96 kHz, blocks of 32 (0.33 ms) | 192 kHz, blocks of 64 (0.33 ms) |
|---|---|---|---|
| worst change: the later card's low-pass Off, Drive 0 / +24 dB | 0.125 / 0.247 ms (19 %) | 0.068 / 0.131 ms (39 %) | 0.072 / 0.209 ms (63 %) |
| the high-pass to Off, later card, Drive 0 / +24 dB | 0.052 / 0.155 ms | 0.031 / 0.082 ms | 0.036 / 0.136 ms |
| Tolerance 0 -> 50 %, later card, Drive 0 / +24 dB | 0.045 / 0.159 ms | 0.032 / 0.081 ms | 0.040 / 0.141 ms |
| a placeholder -> Console (later card), Drive 0 / +24 dB | 0.051 / 0.173 ms | 0.039 / 0.100 ms | 0.042 / 0.186 ms; 0.217 ms (65 %) from Inductor, the worst |
| first callback after prepare / reset, later card, Tolerance 50 % | 0.022 / 0.096 ms | 0.012 / 0.037 ms | 0.018 / 0.092 ms |
| every knob moving, switches changing: worst (later card, Tolerance 50 %, Drive +24 dB) | 0.447 ms (34 %) | 0.184 ms (55 %) | |
| the same at Drive 0 / Tolerance 0 and Drive +24 dB | 0.311 / 0.310 ms | 0.130 / 0.182 ms | |

The two "every knob moving" rows are from M1c's control-block step (64-sample control blocks up to
72 kHz, *Pots*): at 48 kHz in blocks of 64 such a callback held two designs of each network and
holds one now (0.647 ms, 49 %, and 0.501 / 0.405 ms before; interleaved runs of both builds 0.65
... 0.67 against 0.44 ... 0.46 ms); re-measured then, the single changes are as in the table
within 0.04 ms (the worst 0.129 / 0.208 / 0.216 ms). M1c's close-out (the same output to the bit,
fewer instructions into the rails), at a load of 3 ... 5: the worst single change 0.213 / 0.150 /
0.221 ms (16 / 45 / 66 %), every knob moving 0.455 / 0.215 ms (34 / 65 %; 0.189 ms in another
run). Callbacks this short move by 0.02 ... 0.03 ms between runs on a quiet machine; under other
sessions' load (averages of 30 ... 59 on 24 threads) they read up to 0.238 ms (71 %) at 96 kHz.

At Drive +24 dB the amps' residual runs on every sample and costs about twice what it did before
M1c (*CPU*): review round 2 measured 0.173 / 0.093 / 0.128 ms for the worst change at Drive +24 dB
and 0.517 / 0.138 ms with every knob moving; the 192 kHz column is the closest to the gate.

Each step designs about 40 µs of a network (*Switches*); a callback at 48 kHz in blocks of 64
takes two steps. Before the steps a change designed a whole network per callback (the later
card's high-pass joined to its bands, 0.37 ms) and `prepare()` and `reset()` left the designs to
the first callback (0.88 ms with Tolerance): 161 % and 242 % of the 96 kHz period. With every knob
automated a switch change measured 0.54 ms (162 %) before review round 1: the set being built
followed the knobs, every network designed in the same callback, and the root finder spent most
of its time in the library's complex division and hypot (*Switches*, *Pots*). Round 1 left
0.270 ms (81 %), one warm tracking of the later card's coupled front in one callback (175 µs):
its matching recomputed every distance per round, and a pair that had met within a design bounced
on the real axis from real warm starts until the cold restart. Both fixed in round 2
(tools/netlist/README.md, *Interacting blocks*).

**Denormals.** The chain relies on FTZ/DAZ, which the processor and the tools set: with it, a 1 s
noise burst then silence costs 0.73 % from 5 to 10 s (early card), as at rest; without it the
amps' and oversamplers' states (`common/dsp`) decay through subnormals and the same costs
14.9 % (measured in M1, with the 2x clip; an idle 8x residual holds no state, and restarts from
zero). The networks flush their own states every control block.

**Tolerance, Noise, determinism.** Tolerance 0 keeps L and R bit-identical (Drive +18 dB, every
band at work); Noise 0 gives exact silence for silence at Drive +24 dB; Noise 100 % is -109.7 dB
RMS against sample 1.0 (48 kHz). Block sizes 1 ... 4096 and variable give the same output, model
changes included, and renders are bit-identical across reset and prepare (`ViridianTests`,
`[chain]`). Latency: 30 samples, the same for every model and rate: two 2x pairs of 3.32 samples,
three band-limits of 0.43 and each amp's linear-phase path to its 8x clip, 11 samples (29.92,
rounded; a model not built yet runs through the same two pairs and band-limits and a 22-sample
pad, so a model crossfade stays in phase; the dry path for Bypass is a plain 30-sample delay,
PLAN.md *Provisional* 5).

**Laws.** At the calibration points the gain read-out is the band's deviation within
0.0004 dB (every band, both cards, -range ... +range in seven steps) and the Q read-out the
band's Q within 0.66 %; the frequency law inverts its nominal frequency to 1e-9. The gain tables
end exactly on the travel's ends (1.0, 0.0): a knob at its end puts the wiper on the end, where
the segment is a short. A wiper 1e-16 short of it would be a 3e-12 ohm segment, which ngspice
solves 0.4 dB wrong (later HMF at full boost), so `laws.py` pins the ends.

**CPU** (48 kHz stereo, one core, the chain with both amps, both oversampler pairs and the three
band-limits; PLAN.md *Performance budget*, Settled 22 and *Provisional* 10: 3 % worst case with the
controls at rest up to the amps' rails at Drive 0, Noise and Tolerance included; driven into the
rails, by Drive, the input's level or a boost, the provisional 10 % ceiling; medians of seven 3 s
runs of `consoleCpuCases()`, `ViridianTests "Gates: CPU*"` and `ViridianMeasure`, which fail over
them), 2026-11-14, M1c review round 2 (the knee sampled where nearly linear, *Amplifier*), two
`[.perf]` runs at a load of 3 ... 11, the first shown (the second within 0.08 points); round 1's
figures in brackets:

| Up to the rails, Drive 0 (budget 3 %), early / later card | Noise and Tolerance 0 | 100 % |
|---|---|---|
| Init, noise peaking at -12 dBFS | 0.68 / 0.75 % | 0.76 / 0.81 % |
| Init, noise peaking at -1 dBFS (sparse peaks: 1.07 of the swing in the input amp, 1.15 in T8) | 2.24 / 2.31 % | 2.31 / 2.36 % |
| Init, 1 kHz sine peaking at +5 dBFS (0.97 of the swing in both) | **3.19 / 3.23 %** (4.11 / 4.22) | **3.24 / 3.29 %** (4.22 / 4.34) |
| Init, 15 kHz sine peaking at +5 dBFS (0.97 of the swing in both) | **4.01 / 3.95 %** (5.04 / 5.05) | **4.00 / 4.10 %** |
| every band at work, noise peaking at -12 dBFS | 0.77 / 0.86 % | 0.83 / 0.93 % |
| every band at work, noise peaking at -1 dBFS | 1.97 / 2.09 % | 2.04 / 2.19 % |
| every band at work, 1 kHz sine peaking at +4 dBFS (T8 0.88 / 0.96 of the swing) | **3.09 / 3.34 %** (3.51 / 4.05) | **3.21 / 3.33 %** (3.74 / 3.93) |
| HF bell at full boost at 100 %, noise up to T8's rails (-13 / -14 dBFS) | | 1.51 / 1.55 % |
| HF shelf at full boost at 90 %, noise up to T8's rails (-12 / -11 dBFS) | | 1.49 / 1.57 % |
| every band boosted as shelves, noise up to T8's rails (-17 dBFS) | | 1.44 / 1.48 % |

| Into the rails (ceiling 10 %), Noise and Tolerance 100 %, early / later card | Drive +24 dB, noise peaking at -12 dBFS | -6 dBFS | -1 dBFS | Drive 0, noise peaking at -1 dBFS | +4 dBFS |
|---|---|---|---|---|---|
| Init | 8.51 / 8.60 % | 9.74 / 9.74 % (10.29 / 10.42) | 9.86 / 9.95 % (10.37 / 10.49) | | 4.47 / 4.53 % |
| every band at work | 7.33 / 7.55 % | 8.35 / 8.55 % | 8.45 / 8.69 % | | 3.76 / 4.03 % |
| HF bell at full boost at 100 % | **10.53 / 10.95 %** (11.16 / 11.53) | **10.88 / 11.20 %** (11.52 / 11.92) | **10.80 / 11.02 %** (11.46 / 11.62) | 5.92 / 6.23 % | 7.89 / 8.15 % |
| HF shelf at full boost at 90 % | **10.70 / 10.69 %** (11.43 / 11.43) | **11.04 / 11.07 %** (11.74 / 11.77) | **10.93 / 11.02 %** (11.56 / 11.64) | 5.74 / 5.52 % | 7.85 / 7.84 % |
| every band boosted as shelves | 9.95 / 9.88 % (10.58 / 10.61) | **10.22 / 10.34 %** (10.84 / 11.09) | **10.03 / 10.21 %** (10.65 / 10.76) | 5.96 / 5.96 % | 7.45 / 7.37 % |

Bold: over the limit (PLAN.md *Open* 17): 12 of the 34 cases up to the rails and 16 of the 50
into them, the same 28 in both runs. Before M1c's review round 1 the gates timed only the noise
rows and Drive +24 dB at -12 dBFS on Init and every band at work. Noise spends little time near
its peaks (1.1 % of the input amp's samples above the knee's 0.65 at -1 dBFS); a sine 54 %, and
the residual runs on each of those segments. A 1 kHz sine at Init cost (round 1) 1.49 / 1.56 %
peaking at -1 dBFS, 1.83 / 1.90 % at +1, 2.70 / 2.77 % at +2, 3.17 / 3.24 % at +3, 3.37 / 3.48 %
at +4 and 4.10 / 4.21 % at +5 dBFS. A 15 kHz sine costs more: near its peak the segments are too
fast for the knee to be sampled (*Amplifier*), and a stage costs 1.60 % of a core with it at 0.97 of
the swing, 1.28 % with a 1 kHz one (one stereo stage at 96 kHz, scratch harness; 2.25 and 1.78 %
in round 1). Where the rest goes, the 1 kHz sine at +5 dBFS timed with parts of the stage left out
(one stage, scratch builds): of its 1.24 % the residual's segment steps take 0.58 points (the
knee's lookups and pieces, the compensation FIRs 0.14 of them), the FIRs to and from 8x and the
prefilter about 0.4, the main path's triangle 0.17; without the knee at all the stage costs
0.91 %, so the knee is no longer what the budget needs, the residual's machinery near the rails
is. At Drive +24 dB (the later HF bell at full boost at 90 %, -6 dBFS) round 1 timed 2.0 % without
the residual's segment steps, 7.4 % without the knee, 8.0 % without the exact clip and slew, 9.4 %
without the knee's splits, of 11.6 %; the exact clip and slew and the residual's per-segment work
(about seven mispredicted branches per segment in cachegrind, decisions on the signal) are most of
what is left. M1c's close-out had measured Drive +24 dB at 7.87 ... 9.22 % on the quiet rows
alone.

Reported: every band at work with Noise and Tolerance at 100 %, noise at -6 dBFS 1.57 / 1.68 %,
Drive +6 dB 1.57 / 1.68 %, the four band frequencies moving 5.2 / 7.5 % (2.9 / 4.1 % without
Noise and Tolerance; 8.2 / 12.4 % and 4.5 / 6.3 % with 32-sample control blocks, before M1c's
control-block step); while a model change to a placeholder and back runs both models 1.30 ...
1.42 % against the two budgets' sum (6 ... 8 %). The three band-limits cost 0.10 % together. Before
M1c (review round 2) the figures were 0.65 / 0.75 % at rest, 2.11 ... 2.62 % at -1 dBFS and 4.53
... 4.77 % at Drive +24 dB: the residual now runs on the cubic spline and solves each segment with
its slope's and its rail crossings' roots, about twice the work per clipped sample, and the main
path's triangle runs near the rails. Where it goes at Drive +24 dB (callgrind instruction counts,
early card, Init, the designs at prepare left out): the residual's per-segment step 41 %, the exact
segment solver with its pieces and limiter 18 %, the knee's pieces 10 %, the band-limits 2 %. M1c's design step timed the alternatives (*Amplifier*): the
window from 0.5 of the swing costs 3.1 % at -1 dBFS, from 0.7 2.2 %; the residual always on 1.5
... 1.7 % at rest. At rest every cascade stage is real and runs a block at a time, two channels in
one loop (or, with Tolerance, the two channels' networks side by side): the later card at 100 %
Tolerance and Noise measured 1.04 % run one channel after the other.

**Golden files.** `tests/golden/console.f32` and `console-alt.f32`: 1 s at 48 kHz through the
chain on each card (a sweep, low bursts into the rails at Drive +9 dB, noise; every band and both
filters at work, Tolerance 30 %, Noise 20 %), re-recorded 2026-11-13 in M1c (PLAN.md Settled 16,
*Provisional* 4: an intended change of sound). The previous files through the three band-limits
match the new ones to -98 ... -114 dB on the sweep, where the amps are linear; the bursts into the
rails differ by -71 ... -80 dB and the noise by -86 ... -102 dB (the amps' antialiasing near the
rails). `[golden]` fails a change above -100 dB. The knee sampled where nearly linear (M1c review
round 2) moves the renders by -134.4 / -132.8 dB against the files (early / later): not re-recorded.

## Published digital models

None in the literature searched. Commercial models (Waves E/G-Channel, UA 4000 E) document ranges
but not circuits.

## What the gates must reproduce

1. **The netlists**, every control and switch combination, within PLAN's 0.1 dB / 2°: H_front ·
   H_back with the laws above.
2. **The calibration points:** +15 dB (early mids), +18 dB (every later band) at the X-Rack
   frequencies; the gain and Q tables above as the inverse laws' references.
3. **The 242's amplitude following Q** (±15.5 → ±18 dB) and the 02's constant gain over Q, from
   the circuit.
4. **First-order shelves without overshoot; 12 dB/oct high-pass (early), 18 dB/oct (later);
   12 dB/oct low-pass.**
5. **Flat is the circuit's flat** (PLAN.md Settled 12): 0.107 dB (early) and 0.024 dB (later) at
   the Init positions, up to +0.61 dB (early) and +0.38 dB (later) at 20 kHz with the frequency
   knobs elsewhere and every gain centred.
6. **The least colour:** only the rail clips, at the input amp and at T8, the card's output
   (*Amplifier*), as the op-amp does: the loop gain and swing from its datasheet, THD below the
   rails the loop's residue (-81.4 dB at -1 dBFS), aliasing at -80 dBc or below at every Drive.

## Slot mapping

ModelInfo's Console mapping (`src/dsp/ModelInfo.cpp`, `console (bool later)`; `PLAN.md`'s slot
table carries the same values, `tests/ModelInfoTests.cpp` checks them) so the read-outs are the
circuit's; each range lies inside the circuit's travel (min rounded up, max down), so the laws
never leave it:

| Slot | Early (Alt off) | Later (Alt on) |
|---|---|---|
| HPF | 10.74–308.96 Hz, Off at start | 15.11–409.64 Hz, Off at start |
| LF freq | 36.37–424.83 Hz | 36.50–442.53 Hz |
| LF gain | ±15.2 dB | ±18 dB |
| LF Q (fixed) | 0.91 | 2.12 |
| LMF freq | 193.98–2265.74 Hz | 135.73–2201.33 Hz |
| LMF gain | ±15 dB | ±18 dB |
| LMF Q | 0.49–2.89 | 0.50–4.26 |
| HMF freq | 619.08–7231.08 Hz | 635.33–10304.11 Hz |
| HMF gain | ±15 dB | ±18 dB |
| HMF Q | 0.53–2.78 | 0.54–4.06 |
| HF freq | 1261.03–14729.29 Hz | 1294.14–20988.88 Hz |
| HF gain | ±16.1 dB | ±18 dB |
| HF Q (fixed) | 0.62 | 0.94 |
| LPF | 3252.85 Hz–22 kHz, Off at end | 3188.89 Hz–22 kHz, Off at end |

- The low-pass stops at 22 kHz, the published top: the pot goes on to 72 kHz (early) and 50 kHz
  (later), on the hardware's linear taper in its last 10 % of rotation; on a log knob that would
  be the last 39 % of the travel, all above the audio band. Our decision; Off still follows.
- The fixed bell Q needs its own value per band (the circuit gives HF and LF different Q), as
  does the mid Q range and the gain range. Alt's meaning text stays true (wider gain, higher Q,
  steeper high-pass).
- The re-implementation's published ranges (*Published figures*) are not the drawn cards'
  travel; the cards against the fax: *Against the published figures* (`PLAN.md` Settled 20).
- **Flat is flat** (PLAN's gate: "every model at flat settings within 0.1 dB of its amps-only
  response"): the drawn network departs by itself, 0.107 dB at the Init positions (early) and up
  to +0.61 dB (early) and +0.38 dB (later) at 20 kHz with the gains centred elsewhere (ngspice
  results). PLAN.md Settled 12 decides it: the Console is modelled as drawn and gated against its
  netlists' own flat (*Measured*); MANUAL.md is to say so when it is written (M5).

## Unsourced

- The orientation of the pot gangs (settled by simulation, see Readings).
- RV5 and RV6's set-up (our reading, see Calibration).
- C43's value (read as 6n8, see Readings).
- J112 on-resistance in circuit (ideal; datasheet maximum 50 Ω).

## References

- Solid State Logic, drawings T82002·71 issue 5 (82E02) and T82242·70 revision 4, assembly rev. 5
  (82E242); fax "The 82E242 Rev. 4", 15 Nov 1985.
- Solid State Logic, *X-Rack E Series EQ User's Guide*, 82S6XR0K0A (XR425), sections K.2–K.4.
- Solid State Logic, *E Series EQ for 500 format, User Guide*.
- Waves, *SSL E-Channel & G-Channel User Manual*.
- Texas Instruments, NE5534 datasheet, SLOS070D; onsemi, J111/J112 datasheet, J111/D.
- GroupDIY forum, thread "SSL 4000 EQ".
