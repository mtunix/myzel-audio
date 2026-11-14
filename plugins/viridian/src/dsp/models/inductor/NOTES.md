# Inductor: British class-A console module

A transformer-coupled discrete class-A channel amplifier with an active shelving EQ, an
inductor-tuned presence (mid) band and a passive third-order inductor high-pass. Behaviour
reference: the **Neve 1073** channel amplifier (EQ, filters, amplifiers, transformers) and the
**Neve 1084** for the low-pass filter, the switchable high shelf and the High-Q mid. Hardware
names appear only here and in `PLAN.md`.

Status: M0 literature notes. Nothing below is implemented or measured yet. **unsourced: plan
value** marks numbers with no primary source.

## Sources

| Source | What it gives | Trust |
|---|---|---|
| Rupert Neve & Co., *1073 Channel Amp*, drawing EH10023 (1973, issues to 17-5-74); Neve, B182 C & D high-pass filter D/10,019 (1970/71); B205 circuit diagram D/10,042 (1971); B211 D/10,048 (1970); BA283 AM & AV EX/10,283 (1971/72) with the BA283AV parts list, as collected in the service pack "Neve 1073 channel amplifier documentation" v0.1a | module wiring, step tables, every EQ and filter part value, the amplifier boards | primary; the pack lacks the switch drawings EK20065 (LF), EK20066 (HPF), EK20105 (presence) and the module parts list |
| AMS Neve, *1073 & 1084 Channel Amplifiers User Guide*, issue 5.2 | the published control specs of both modules, the HPF termination note, module specs | manufacturer (current reissues) |
| AMS Neve 1084 product page (ams-neve.com/1084-2) | 1084 specs | manufacturer; disagrees with the user guide on the filters |
| D. Berners, "How is the Neve 1073 different from the Pultec or from other EQs UA has modeled?", *UA WebZine* 4(4), May 2006 (Ask the Doctors) | what a published model of this EQ found | secondary, by the modeller |
| Retailer listings for replacement inductors (don-audio.com): T1280 taps 200/160/100/80/50 mH, T1530 taps 2 H / 1.1 H / 450 mH | inductor taps | secondary; used only as a consistency check below |

Not obtained: a 1084 schematic (the High-Q and low-pass circuits), the presence switch drawing
EK20105, inductor self-capacitance and core data.

## Step tables

1073, from EH10023's switch table and the AMS Neve user guide (they agree):

| Control | Positions | Range |
|---|---|---|
| High pass (S4, B182) | Off, 50, 80, 160, 300 Hz | 18 dB/oct |
| Low frequency (S3, RV2 50 kΩ, B205) | Off, 35, 60, 110, 220 Hz | ±16 dB shelf |
| High frequency (RV1 10 kΩ, B205) | fixed 12 kHz | ±16 dB shelf |
| Presence (S5, RV3 10 kΩ, B211) | Off, 0.36, 0.7, 1.6, 3.2, 4.8, 7.2 kHz | ±18 dB peak, fixed Q |

1084, from the AMS Neve user guide issue 5.2:

| Control | Positions | Range |
|---|---|---|
| High pass | 45, 70, 160, 360 Hz | 18 dB/oct |
| Low pass | 6, 8, 10, 14, 18 kHz | 18 dB/oct |
| Low frequency | 35, 60, 110, 220 Hz | ±16 dB shelf |
| Mid frequency | 0.36, 0.7, 1.6, 3.2, 4.8, 7.2 kHz | ±12 dB or ±18 dB peak, switchable High Q |
| High frequency | 10, 12, 16 kHz (the guide prints "0kHz, 12kHz and 16kHz") | ±16 dB shelf |

The AMS Neve 1084 web page gives the high pass as 50, 80, 160, 300 Hz and the low pass as 6, 8,
10, 18 kHz instead. Without a 1084 schematic the user guide is taken, as the longer document.

## Circuit

### Where the EQ sits

UA's Berners (2006), consistent with the drawings: the high and low shelves are **active**,
"incorporated in feedback around a single amplification stage"; the presence band is feedback
around an active stage with **an inductor and capacitor** setting the resonance; the high-pass is
**passive**, "uses magnetics to generate a third-order low-cut response", and is sensitive to its
load. The AMS Neve guide says the same of the HPF and gives its termination: a 5k1 from the fader
send to 0 V (R70 on EH10023), replaced by a 4k7–5k fader when one is fitted; without it the
levels and the frequency response are wrong.

**The low shelf has no inductor.** B205 is resistors and capacitors only. The inductors in the
module are the presence coils T1280 and T1530 (B211) and the HPF coil T1295 (B182).

Module chain on EH10023: line input transformer T2 (31267) or mic transformer T1 (10468) →
BA284 (gain, with the sensitivity switch EK20033) → BA283AV pre-amp section with the B205 shelves
and the B211 presence band around it → B182 high-pass (passive, into R70 5k1) → BA283 output stage
→ output transformer T3 (LO1166, 1k5 + 10 nF across the secondary) → phase switch → output. The
pin-level wiring of the EQ boards into the amplifiers (R37 1k2, R39 39 kΩ, R45 12 kΩ, R71 39 kΩ,
R72 39 kΩ, R58 2k7 …) is traced from EH10023 in M3.

### High-pass (B182 version C, the 1073's)

A switched C–L–C T-section: series capacitor, shunt tap of T1295 to ground, series capacitor.

| Position | Series capacitors (each) | T1295 tap | √(LC) (computed here) |
|---|---|---|---|
| 50 Hz | 1.0 µF | 10 H | 50.3 Hz |
| 80 Hz | 0.47 µF | 7 H | 87.7 Hz |
| 160 Hz | 0.22 µF | 3 H | 196 Hz |
| 300 Hz | 0.1 µF ∥ 0.022 µF | 1.3 H | 400 Hz |

A 10 kΩ is drawn between the 10 H and 7 H tap nodes. Off routes around the network. The
position order follows the switch letters on the drawing (T/B, D/C, U/A, V/S); the switch drawing
EK20066 is missing, but the √(LC) column confirms the order.
Load: 5k1 (R70). Source: the BA283AV stage's output impedance (unsourced until traced).

### Shelves (B205, RV1, RV2)

| Low frequency | Boost-side capacitor | Cut-side capacitor |
|---|---|---|
| 35 Hz | 100 nF ∥ 100 nF | 100 nF ∥ 100 nF |
| 60 Hz | 100 nF | 100 nF |
| 110 Hz | 47 nF | 47 nF |
| 220 Hz | 15 nF | 15 nF |

The frequency assignment follows the capacitance order (the switch drawing EK20065 is missing).
The two banks are mirror images on S3a/S3b (pins B–E and R–U), each with a 6k8 (pins A and V),
around RV2 50 kΩ: the same parts reorganised for boost and cut. High shelf: 620 Ω + 22 nF on
each side (F–P and N–H) to RV1 10 kΩ, and a 15 nF to a 12 kΩ ∥ 10 nF (J–L). 1/(2π · 620 Ω · 22 nF)
= 11.7 kHz (computed here), the fixed 12 kHz.

### Presence (B211, RV3)

Around the amplifier: IN ─ 6k8 ─ X ─ 6k8 ─ OUT; X ─ 8k2 ─ the amplifier input E; E ─ 2k7 ─ the
inductor tops. A series LC (inductor tap and switched capacitor) runs from the inductors to the
wiper of RV3 (10 kΩ), whose ends sit on IN and OUT: boost on one side, cut on the other, the same
LC either way.

Capacitors on the drawing: T1530 with 22 nF, 47 nF, 100 nF (pins J, M, H); T1280 with 10 nF + 2n2
(S), 4n7 (V), 2n2 + 220 pF (T), 1n2 (U). The switch EK20105 that assigns them is missing from the
pack, and the inductor taps are not on the drawing. A consistency check with the retailer tap
values (computed here, not a source):

| Legend | Pairing | √(LC) |
|---|---|---|
| 0.36 kHz | T1530 2 H, 100 nF | 356 Hz |
| 0.7 kHz | T1530 1.1 H, 47 nF | 700 Hz |
| 1.6 kHz | T1530 0.45 H, 22 nF | 1600 Hz |
| 3.2 kHz | T1280 200 mH, 12.2 nF | 3222 Hz |
| 4.8 kHz | T1280 200 mH, 4.7 nF | 5191 Hz (8 % high: tap or pairing uncertain) |
| 7.2 kHz | T1280 200 mH, 2.42 nF | 7234 Hz |

The 1n2 position does not fit a 1073 legend frequency; it may serve another module that shares the
board. The 4.8 kHz pairing and every inductance stay **unsourced** until EK20105 or a coil
datasheet is found; the netlist starts from this table.

### Netlist plan (M3)

- HPF: three-element T-section per position, T1295 tap with DCR (unsourced), loaded by 5k1.
- Shelves and presence: the two networks with an amplifier model around them. For the linear
  `tools/netlist` analysis the amplifier is an ideal op-amp first; the BA283's finite open-loop
  gain enters only if ngspice with the discrete stage shows a difference above the 0.1 dB gate.
- Inductors: ideal L + DCR + a parallel self-capacitance. Berners: "in the midband peaking
  filters, the responses are drawn towards unity gain at high frequencies by parasitic
  capacitances in the inductor", and "parasitic effects in the low-cut filter from the inductor
  slightly reduce its resonances". Values **unsourced**; without them the curves are "what might be
  calculated from the schematic alone", which Berners says differ measurably.
- Nonlinear: the saturating cores belong to T1280/T1530 (presence) and T1295 (HPF), solved as
  `NonlinearInductor` WDFs at 2× (`PLAN.md`). Their small-signal inductance must equal the linear
  network's (gate).

## Amplifier and transformers

| Stage | Drawing | Umber circuit |
|---|---|---|
| Line input transformer T2 (31267), 10 kΩ bridging (user guide) | EH10023 | Desk's JA transformer stage; core values unsourced |
| BA284 gain amplifier | EH10023 (board drawing not in the pack) | Class A |
| BA283AV pre-amp section: TR4, TR5, TR6 (BC184C) | EX/10,283 | Class A |
| BA283 output stage: TR1, TR2 (BC184C), TR3 (2N3055 on the drawing; BDY61 in the parts list), C6/C7 80 µF to T3 | EX/10,283 | Class A: Umber's stage is the same school (degenerated common-emitter into a class-A emitter follower, cap-coupled to the output transformer); BA283's has a third transistor |
| Output transformer T3 (LO1166), 1k5 + 10 nF across the secondary | EH10023, EX/10,283 | Desk's JA transformer stage; core values unsourced |

Module specs (AMS Neve user guide): output > +26 dBu into 600 Ω, output impedance 75 Ω at 1 kHz;
distortion not more than 0.07 % from 50 Hz to 10 kHz at +20 dBu into 600 Ω (80 kHz bandwidth);
frequency response ±0.5 dB 20 Hz–20 kHz, −3 dB at 40 kHz, EQ out. Berners: the discrete
amplifiers "are highly linear"; "most of the subtlety probably comes from the magnetics".

## Published digital models

- **Berners (2006)** describes the linear behaviour of a commercial model of this EQ:
  - the low shelves "have varying degrees of resonance as different capacitors are switched in";
  - the high shelf has "a very gradual slope for both boost and cut, because its poles and zeros
    are interpolated";
  - both shelves "feature a small 'divot', a region of inverse gain slightly before the shelving
    frequency";
  - in the presence band "at higher center frequencies, the Q goes up", and the bandwidth depends
    on gain;
  - the HPF "is tuned to have a square roll-off without having excessive resonance";
  - the inductors' self-resonances "change the filter responses measurably from what might be
    calculated from the schematic alone".
- **Moro, McPherson (DAFx-15)** model a saturating-core inductor (Fröhlich–Kennelly, no
  hysteresis) as a time-variant linear filter whose inductance is updated every sample, applied to
  a wah pedal's inductor. A cheaper alternative to the WDF `PLAN.md` chose; the reference here for
  how the band moves as the core saturates.

No academic paper modelling the 1073 or 1084 EQ was found.

## What the gates must reproduce

1. **Low shelf divot and resonance.** The small inverse-gain region before the corner and the
   per-position resonance, from the RC network (not an inductor).
2. **High shelf.** Gentle slope, the divot, fixed 12 kHz.
3. **Presence.** ±18 dB, Q rising with centre frequency, bandwidth narrowing with gain, response
   drawn back towards unity above the peak (inductor self-capacitance).
4. **HPF.** Third-order, square knee without excessive resonance, correct only with the 5k1 load.
5. **Iron.** At level, the presence and HPF inductors soften and shift the band; at −40 dBFS the
   nonlinear sections equal the linear ones within 0.01 dB (`PLAN.md`).
6. **Amplifier.** THD vs level against the 0.07 % at +20 dBu spec, LF of the transformers
   saturating first.

## Differences from `PLAN.md`'s slot mapping and model text

The slot-mapping rows below are applied in M0: `PLAN.md`'s table and `src/dsp/ModelInfo.cpp` carry
the sourced values. The nonlinear-inductor row is for M3; `PLAN.md`'s model text still says
"low and mid band".

| Item | PLAN | Sourced | Action |
|---|---|---|---|
| HPF | 50/80/160/300 Hz, 18 dB/oct | same (1073); 1084: 45/70/160/360 Hz | keep the 1073 set |
| Low | 35/60/110/220 Hz, ±16 dB | same | none |
| Low-Mid | 0.36–7.2 kHz, ±18 dB, Hi-Q on Alt | six positions 0.36, 0.7, 1.6, 3.2, 4.8, 7.2 kHz, ±18 dB; the 1084's High Q comes with a ±12 dB range option, its circuit unsourced | six positions; Hi-Q circuit unsourced |
| High | 12 kHz fixed, ±16 dB | same (1073); 1084: 10/12/16 kHz | none |
| LPF | 18/10/6.5 kHz (1084), 18 dB/oct | 6/8/10/14/18 kHz, 18 dB/oct (user guide); 6/8/10/18 kHz (web page) | use 6/8/10/14/18 kHz |
| Nonlinear inductors | "Inductor model's low and mid band"; "low shelf … LC networks with real inductors"; "the low shelf has its characteristic resonant bump" | the low shelf is an active RC network; the inductors are in the presence band and the HPF | saturating cores on the presence and HPF inductors; the low shelf's bump is the RC network's resonance and divot |

## Unsourced

- The 1084 schematic: High-Q mid, low-pass filter, switched high shelf.
- Presence switch EK20105 (capacitor-to-tap assignment) and the coils' inductances (retailer taps
  used as a starting point).
- Inductor DCR, self-capacitance and core material for T1280, T1530, T1295.
- Transformer data for T2 (31267) and T3 (LO1166).
- The BA284 board drawing.

## References

- Rupert Neve & Co., *1073 Channel Amp*, EH10023, 1973; Neve Group, *B182 C & D*, D/10,019;
  *B205 circuit diagram*, D/10,042; *B211*, D/10,048; *BA283 AM & AV*, EX/10,283 and parts list.
  Service pack "Neve 1073 channel amplifier documentation", v0.1a.
- AMS Neve, *1073 & 1084 Channel Amplifiers User Guide*, issue 5.2.
- D. Berners, "Ask the Doctors: How is the Neve 1073 different from the Pultec …", *Universal
  Audio WebZine* 4(4), May 2006.
- G. Moro, A. P. McPherson, "Approximating non-linear inductors using time-variant linear
  filters", DAFx-15, Trondheim 2015.
- Umber `src/dsp/models/classa/NOTES.md` and `desk/NOTES.md`: the class-A stage and the
  transformer core reused here.
