# Passive: tube program EQ with a mid dip

A passive LCR equaliser in front of a two-stage push-pull valve amplifier that makes up its
insertion loss. Behaviour reference: the Pulse Techniques **Pultec EQP-1A** program equaliser for
the low shelf boost/attenuate, the resonant high boost and the high shelf attenuator, plus the
**Pultec MEQ-5** midrange equaliser for the dip (Low-Mid slot). Hardware names appear only here and
in `PLAN.md`.

Status: M0 literature notes. Nothing below is implemented or measured yet. Every value is marked
with where it comes from; **unsourced: plan value** means no primary source was found and the
number is `PLAN.md`'s.

## Sources

What was actually read, and how far each can be trusted:

| Source | What it gives | Trust |
|---|---|---|
| Pulse Techniques, *Pultec Program Equalizer Model EQP-1A* (8-page scan: spec sheet, EQP-1A3 sheet, response curves, operating instructions, pot list, EQP-1R network drawing, EQP-1A amplifier drawing DWG E-72,658-2, EQP-1R amplifier DWG E-112,353-7) | step tables, ranges, pot values and tapers, the amplifier schematic, the curve families | primary; the network drawing in this scan is the **EQP-1R**, not the EQP-1A |
| J. Erland (Gyraf Audio), *Do-A-Pultec* page: `pultech.gif` (PE-1A filter and amp, Nov 2001), `meq5.gif` (MEQ-5 filter sections, Feb 2002) | an EQP-1A-type network with per-position capacitors and inductances; the MEQ-5 dip network | reverse-engineered; the author notes the transformer data are guesswork |
| A. Barrera Herrero, *Pultec EQP-1A Modeling with Wave Digital Filters*, MSc thesis, Universitat Pompeu Fabra 2023 (Zenodo 8382764) | a published digital model and its errors; insertion loss; how they sourced the circuit | secondary |

Not obtained: the original EQP-1A equaliser-network drawing (the scan carries the EQP-1R network
instead), the MEQ-5 manual. The SMC 2024 paper by the same group (Barrera, Lizarraga-Seijas,
Font, *Modeling the Pultec EQP-1A with Wave Digital Filters*) was found in a search but could not
be downloaded (host unreachable); it is not cited for any value here.

## Step tables

From the EQP-1A spec sheet and operating instructions unless marked.

| Control | Positions | Range | Part (manual p. 5) |
|---|---|---|---|
| Low Frequency (switch, shared by boost and atten) | 20, 30, 60, 100 Hz | | |
| Low Boost (shelf) | continuous, panel 0–10 | 0 to 13.5 dB | 10 kΩ, audio (log) taper |
| Low Atten (shelf) | continuous, panel 0–10 | 0 to 17.5 dB | 100 kΩ, audio (log) taper |
| High Frequency (boost, switch) | 3, 4, 5, 8, 10, 12, 16 kHz | | |
| High Boost (peak) | continuous, panel 0–10 | 0 to 18 dB | 10 kΩ, linear |
| Bandwidth (high boost) | continuous, sharp … broad | | 2.5 kΩ, linear |
| Atten Sel (switch) | 5, 10, 20 kHz | | |
| High Atten (shelf) | continuous, panel 0–10 | 0 to 16 dB | 1 kΩ, linear |
| Dip frequency (MEQ-5) | 0.2, 0.3, 0.5, 0.7, 1, 1.5, 2, 3, 4, 5, 7 kHz | | (Gyraf MEQ-5 drawing legend) |
| Dip depth (MEQ-5) | continuous | 0 to 10 dB: **secondary** (retailer listings only) | 2.5 kΩ, counter-clockwise log taper (manual p. 5, MEQ-5 entry) |

The manual's own wording on the controls, which the model must keep:

- The low boost and atten work on the same Low Frequency curve. The manual says not to boost and
  attenuate together on the low frequencies; doing it anyway is the well-known "trick" (below).
- High boost and high atten have separate frequency switches and may be used together.
- The IN-OUT key "away from IN" is equivalent to returning every boost and attenuate control to
  zero; **the amplifier remains in the circuit**. This is exactly Viridian's EQ In key.
- "The boost, attenuate and bandwidth controls are continuously variable": the manual's curve
  families are typical settings, not steps.

Filter slopes: the shelves are first-order RC/RL sections (6 dB/oct asymptotes); the high boost is
a second-order series resonance. No HPF or LPF (the HPF and LPF slots dim, as `PLAN.md` says).

## Circuit

### Network: EQP-1A type, as drawn by Gyraf

The equaliser sits between the input transformer's terminated secondary and the interstage
transformer. Read from `pultech.gif` at its resolution (node-level connectivity to be re-traced
and confirmed in ngspice against the manual's curves in M3):

```
 IN (600:600 secondary, 570 Ω termination)
  ├─ Boost Hi 10 kΩ (lin), IN → A; wiper ─[C_hb]─[L_hb tap]─[Bandwidth 2.2 kΩ rheostat]─► OUT
  A ─[R_cut 100 kΩ (log) rheostat ∥ C_cut]─ C ─[1 kΩ]─ OUT
  A ─[1 MΩ]─ OUT
  A ─[Cut Hi 1 kΩ]─ F ;  Cut Hi wiper ─[75 Ω]─[C_hc]─ F
  OUT ─[10 kΩ]─ E ;  E = F ;  E ─[C_lb ∥ Boost Lo 10 kΩ (log) rheostat]─ ground
 OUT ─► interstage transformer 10k:10k+10k (primary DCR 637 Ω, secondary 2 × 513 Ω)
```

Per-position parts on the Gyraf drawing:

| Low Frequency | C_lb (boost leg) | C_cut (atten leg) |
|---|---|---|
| 20 Hz | 2.2 µF | 100 nF |
| 30 Hz | 1 µF | 47 nF |
| 60 Hz | 470 nF | 22 nF |
| 100 Hz | 330 nF | 15 nF |

| High boost | C_hb | L_hb | √(LC) resonance (computed here) |
|---|---|---|---|
| 3 kHz | 15 nF | 175 mH | 3.1 kHz |
| 4 kHz | 15 nF | 100 mH | 4.1 kHz |
| 5 kHz | 10 nF | 90 mH | 5.3 kHz |
| 6 kHz on the drawing (8 kHz on the hardware) | 10 nF | 65 mH | 6.2 kHz |
| 10 kHz | 6.8 nF | 35 mH | 10.3 kHz |
| 12 kHz | 6.8 nF | 23 mH | 12.7 kHz |
| 16 kHz | 3.3 nF | 19 mH | 20.1 kHz |

The drawing lists a second column of 175 / 100 / 50 / 22 mH beside the inductances; it reads as the
winding's tap values but is not labelled. Two entries disagree with the hardware: the 6 kHz
position (the EQP-1A panel and spec sheet say 8 kHz) and the 16 kHz values, whose bare LC
resonance is 20 kHz. The source and load impedances pull the peak, so the netlist decides; if the
16 kHz peak still lands high in ngspice, the 16 kHz parts are **unsourced** and need a better
source before M3 closes.

High atten: C_hc 47 nF, 47 nF, 150 nF for 20, 10, 5 kHz. Whether the switch selects them singly or
in parallel is not readable at the drawing's resolution: resolve it in ngspice against the manual's
5/10/20 kHz attenuate curves.

Indicative check of the "trick" geometry (computed here from the table, not a source): at full
setting the boost leg's corner 1/(2π · 10 kΩ · C_lb) is 7.2 / 15.9 / 33.9 / 48.2 Hz and the cut
leg's 1/(2π · 100 kΩ · C_cut) is 15.9 / 33.9 / 72.3 / 106 Hz. The cut sits about an octave above the
boost on every position, consistent with the bump-below-a-dip that boost plus cut gives.

### Network: EQP-1R, as drawn in the manual

The manual scan's only network drawing is the EQP-1R, a sibling with fewer frequencies. Same
family: 620 Ω input shunt, 10 kΩ linear high boost, 2.5 kΩ linear bandwidth with a 1 MΩ, a
tapped 50 mH + 100 mH inductor, 1 kΩ linear high atten with an 82 Ω and a 0.13 µF capacitor
(legibility uncertain), 100 kΩ log low atten with capacitors read as 0.0362 / 0.02 / 0.01 µF,
a 12 kΩ, and the low boost pot with capacitors read as 1 / 0.5 / 0.28 µF on 30 / 60 / 100 Hz.
Several values are only partly legible in the scan. Barrera (2023) built his LTspice circuit from
this drawing plus Gyraf's and then adjusted values to a measured reissue; Viridian does not copy
his adjusted values (`PLAN.md`: no curve fitting to measurements).

### Netlist plan (M3)

- Nodes: IN, A, C, E/F, OUT, the high-boost branch (wiper, C_hb–L_hb junction, L_hb–bandwidth
  junction), the high-atten branch, and the dip section (below) between the passive EQP network
  and the interstage transformer.
- Pots as two resistors around a wiper node, with the manual's tapers (audio taper for the low
  pots, linear for the high pots, CCW log for the dip).
- Source: the input transformer referred to its secondary (600 Ω source, 570–620 Ω termination).
  Load: the interstage primary with its DCR. Transformer magnetising inductances and leakage are
  in the amp stages (below), not in the linear network.
- Inductors as ideal L plus DCR (DCR unsourced: plan value until a source or a measurement is
  found).

### Dip section (MEQ-5)

From `meq5.gif`: a series 510 Ω in the line, then the Dip pot (2.5 kΩ, CCW log) from the line to
ground; its wiper feeds an optional 270 Ω "Q" resistor, a tapped inductor (taps 34, 61, 108, 145,
277, 420 mH) and a capacitor bank (1.5 µF + 15 nF, 1 µF, 680 nF, 330 nF, 220 nF, 170 nF, 68 nF,
30 nF, 16 nF, 10 nF) to ground: a series-resonant notch whose depth the pot sets.
The pairing of tap and capacitor per position is not readable at the drawing's resolution. One
pair checks exactly (computed here): 420 mH with 1.515 µF resonates at 200 Hz. The rest are
resolved in M3 by matching each position's LC resonance to its legend frequency.

The MEQ-5's own low and high peak sections (2.5 kΩ / 10 kΩ pots, 120–520 mH and 477/625 mH
inductors on the drawing) are not modelled in v1 (`PLAN.md`, M6).

Where the dip goes in the chain is a decision for M3: in the hardware the MEQ-5 is a separate box
with its own amplifier. Viridian puts the dip network in series with the EQP network inside one
make-up amp, so its insertion loss adds to the make-up gain the amp needs.

## Amplifier and transformers

From DWG E-72,658-2 (EQP-1A, original drawing):

```
 in ─ Triad HS-56 (600 Ω, strappable 250/150/60) ─ 620 Ω ─ passive EQ ─ Triad HS-29 interstage
   ─ (secondary: 270 pF ∥ 43 kΩ) ─ ECC83/12AX7 push-pull, common cathode 820 Ω (+1.3 V),
     plate loads 200 kΩ 1 W from +250 V, 20 pF across the plates
   ─ 0.25 µF ─ ECC82/12AU7 push-pull, grids 1 MΩ to a 62 kΩ / 470 kΩ bias divider (+31 V),
     cathodes 4.7 kΩ 1 W (selected alike) bypassed by 50 µF
   ─ Peerless S-217-D output transformer, B+ +290 V at the centre tap,
     a 620 Ω and a 3 nF across the secondary windings, feeds 600 Ω (strappable 300/150/75)
   feedback: a tertiary winding with a 1 kΩ to ground drives both 12AX7 cathodes through 360 Ω
 supply: 6X4 rectifier, 470 Ω, 1 kΩ and 2.2 kΩ RC filtering, B+ 345 / 325 V
```

"Overall gain with input and output transformers connected for same impedance is 0 dB."
Spec sheet: amplifier flat 20 Hz–20 kHz +0/−1 dB, distortion 0.15 % at +10 dBm into 600 Ω,
maximum peak output +21 dBm, noise 92 dB below +10 dBm. Barrera (2023) gives the passive
network's insertion loss as 16 dB, which the amplifier restores.

Gyraf's PE-1A amplifier is the same push-pull topology (ECC83 then ECC82, tertiary feedback)
with its own values; the original drawing above is the authority.

Mapping to Umber's circuits:

| Stage | Umber circuit | Fit |
|---|---|---|
| 12AX7 and 12AU7 triodes | Valve's Koren triode and nodal DK solver | the device model and solver carry over; the **topology does not**: Umber's Valve is one single-ended common-cathode 12AX7 without feedback, the EQP-1A is two push-pull stages in a global feedback loop. A new netlist on the same solver |
| 12AU7 parameters | none in Umber | Koren publishes a 12AU7 set; take it from his paper (not checked in M0) |
| Input, interstage and output transformers | Desk's Jiles–Atherton transformer stage | carries over; core and winding values are **unsourced** (the drawing names the parts, Triad HS-56, HS-29, Peerless S-217-D, but gives no inductances) |

Push-pull cancels even harmonics, so the colour is odd-order from the output stage and the iron,
plus whatever imbalance Tolerance introduces. The feedback loop through the output transformer
makes its LF saturation part of the loop: keep the loop inside one DK system, not as separate
stages.

## Published digital models

- **Barrera Herrero (2023).** WDF model of the passive section only (no amplifier), built with
  pywdf/chowdsp_wdf from the EQP-1R drawing plus Gyraf's, values adjusted to the 2019 reissue's
  published measurements. Input and output resistors were removed to avoid an R-type adaptor.
  RMS magnitude error against LTspice: low boost 3.65e-2 / 1.86e-2 / 9.37e-3, low cut 2.07e-3 /
  5.43e-4 / 1.39e-4, **high boost 1.40 / 0.644 / 0.220**, high cut 3.60e-2 / 1.28e-2 / 3.96e-3 at
  48 / 96 / 192 kHz. The high-boost error at 48 kHz is the bilinear transform cramping the
  resonance, which is the reason `PLAN.md` maps each section with a magnitude-matched design
  instead of a plain WDF at the host rate.
- He cites a Faust WDF library paper (Roosenburg, Stine, Michon, Chowdhury, SMC 2021) with an
  EQP-1A example using R-type adaptors; not read here.

No published model of the EQP-1A amplifier or the MEQ-5 dip was found.

## What the gates must reproduce

At small signal, against ngspice of the netlist (`PLAN.md`'s 0.1 dB gate). Qualitative targets from
the manual's curves, to check the netlist before any gate:

1. **The boost/cut interaction.** Low Boost and Low Atten together on one frequency: a shelf
   boost below a dip, the dip above the boost corner (about an octave higher, see the RC check).
   It must come out of the shared network, never a curve.
2. **Ranges at the manual's settings.** Max low boost ≈ 13.5 dB, max low atten ≈ 17.5 dB, max high
   boost ≈ 18 dB (sharp), max high atten ≈ 16 dB. These are the spec sheet's numbers; the netlist
   is right when ngspice lands near them, not when we force them.
3. **Bandwidth.** Sharp and broad families at each high-boost frequency, as on the manual's curve
   page (its sharp charts are scaled to 20 dB, its broad charts to 12 dB).
4. **EQ In.** EQ out equals every control at zero, amplifier still in.
5. **Make-up.** Flat at all-zero settings within the amp's own response (+0/−1 dB 20 Hz–20 kHz);
   the ≈ 16 dB insertion loss restored by the amplifier, then removed again by Viridian's
   inverse small-signal gain.
6. **Distortion.** THD vs level reported against the 0.15 % at +10 dBm spec (the dBm ↔ dBFS
   mapping is a chain decision for M3).

## Differences from `PLAN.md`'s slot mapping

The slot-mapping rows below are applied in M0: `PLAN.md`'s table and `src/dsp/ModelInfo.cpp` carry
the sourced values. The amplifier row is for M3.

| Slot | PLAN | Sourced | Action for the scaffold |
|---|---|---|---|
| Low: Freq | 20/30/60/100 Hz | same | none |
| Low: Gain (Boost) | 0–10 | panel 0–10 = 0 to 13.5 dB | keep 0–10 legend; dB range is the network's |
| Low: Q (Atten) | 0–10 | panel 0–10 = 0 to 17.5 dB | as above |
| Low-Mid (Dip) | 0.2–7 kHz, Dip 0–10 | 11 positions 0.2, 0.3, 0.5, 0.7, 1, 1.5, 2, 3, 4, 5, 7 kHz; depth 0–10 dB (secondary) | use the 11 positions |
| High-Mid | 3–16 kHz, Boost 0–10, Bandwidth | 7 positions 3, 4, 5, 8, 10, 12, 16 kHz; 0 to 18 dB; Bandwidth sharp … broad | use the 7 positions |
| High: Freq | 5/10/20 kHz | same | none |
| High: Gain | Atten 0–10 | panel 0–10 = 0 to 16 dB | none |
| Amp stages | two-stage valve make-up amp from Umber's Valve | push-pull 12AX7 → push-pull 12AU7 with global feedback from an output-transformer tertiary | new netlist on Umber's solver and triode model; not Umber's Valve circuit as-is |

## Unsourced

- The EQP-1A network's own drawing (Gyraf's reverse-engineering stands in for it).
- Inductor DCR and self-capacitance for L_hb and the dip inductor.
- Transformer magnetising inductance, leakage, winding capacitance and core material for the
  HS-56, HS-29 and S-217-D.
- The dip's tap/capacitor pairing per position (resolved by resonance in M3) and the dip depth's
  primary source.
- The high-atten capacitor switching (resolved against the manual's curves in M3).

## References

- Pulse Techniques, *Pultec Program Equalizer Model EQP-1A*: specifications, installation and
  operating instructions, "Tubes & potentiometers in Pultec equalizers", EQP-1R equaliser drawing,
  DWG E-72,658-2 (EQP-1A amplifier), DWG E-112,353-7 (EQP-1R amplifier). Scan:
  thehistoryofrecording.com/Manuals/Pultec/Pultec_EQP-1A_Manual.pdf.
- J. Erland, Gyraf Audio, *Do-A-Pultec* (gyraf.dk/gy_pd/pultec/pultec.htm): `pultech.gif`
  (PE-1A program equaliser, November 2001) and `meq5.gif` (MEQ-5 filter sections, rev. 1,
  15-02-2002).
- A. Barrera Herrero, *Pultec EQP-1A Modeling with Wave Digital Filters*, MSc thesis, Universitat
  Pompeu Fabra, 2023. Zenodo record 8382764.
- N. Koren, *Improved vacuum tube models for SPICE simulations*, Glass Audio 8(5), 1996 (via
  Umber's Valve NOTES; the 12AU7 set is still to be read).
- Umber `src/dsp/models/valve/NOTES.md` and `desk/NOTES.md`: the triode solver and the
  transformer core reused here.
