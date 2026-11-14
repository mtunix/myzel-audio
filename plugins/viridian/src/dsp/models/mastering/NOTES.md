# Mastering: clean discrete parametric

A transformerless, all-discrete, fully parametric equaliser with continuous controls and symmetric
boost and cut: the reference for transparency. Behaviour reference: the **GML Model 8200**
parametric equaliser (George Massenburg Labs); the **Sontec MES-432** is the same class but was not
sourced. Hardware names appear only here and in `PLAN.md`.

Status: M0 literature notes. Nothing below is implemented or measured yet. **No schematic was
obtained**; the 8200's is not public. **unsourced: plan value** marks numbers with no primary
source.

## Sources

| Source | What it gives | Trust |
|---|---|---|
| George Massenburg Labs, *Model 8200 Parametric Equalizer Series II* owner's manual | features, controls, band table, specifications with test notes, gain/Q/phase plots | manufacturer |
| G. Massenburg, "Parametric Equalization", AES 42nd Convention, 1972, paper K-2 | the parametric EQ's original description (all-active RC, no inductors) | seen only as an AES E-Library listing in search results; full text not read |
| GroupDIY thread "Massenburg EQ schematic?" (search-result text only) | a builder's remark that the Massenburg and Sontec EQs share a topology | secondary, unverified |

Not obtained: any 8200 or MES-432 schematic, the MES-432 manual.

## Step tables

All controls continuous (8200 manual):

| Band | Frequency | Q | Gain |
|---|---|---|---|
| 1 (Low) | 15 Hz–800 Hz | 0.4–4.0, or shelf (counter-clockwise detent) | ±15 dB |
| 2 | 15 Hz–800 Hz | 0.4–4.0 | ±15 dB |
| 3 | 120 Hz–8 kHz | 0.4–4.0 | ±15 dB |
| 4 | 400 Hz–26 kHz | 0.4–4.0 | ±15 dB |
| 5 (High) | 400 Hz–26 kHz | 0.4–4.0, or shelf (counter-clockwise detent) | ±15 dB |

- The manual defines Q as the centre frequency of the alteration over its bandwidth. Which
  bandwidth (−3 dB of the deviation, or of the half-gain points) is not stated: a decision for
  M2, recorded here when made.
- Gain: continuous from −15 to +15 dB, with an accurate 0 dB position and no detent. The
  manual's gain-step and Q-step plots show symmetric boost and cut families.
- The frequency legends are approximate and vary slightly between units (manual).
- No high- or low-pass filter on the 8200.

## Circuit

What the manual states: an all-discrete class-A design with no integrated circuits and no
interstage or coupling capacitors; transformerless, with an electronically balanced input buffer
and a direct-coupled output held by a DC servo; the GML 9202 discrete op-amp (low noise, wide
bandwidth); carbon-film pots; no tantalum, ceramic or electrolytic capacitors in the signal path;
±28 V supply with board-level regulation to ±18 V. The pots sit where their noise is attenuated,
and noise rises in proportion to gain, frequency and Q.

The band topology is **unsourced**. Massenburg's 1972 paper describes an all-active RC design
(per its listing); the forum remark ties the 8200 and the MES-432 to one topology but names none.

### Netlist plan (M2)

Our own circuit, documented here as ours: four continuous parametric sections (the slot mapping
below picks four of the 8200's five bands) around ideal op-amps whose analog H(s) is a symmetric boost/cut bell (cut the exact
inverse of boost) with independent frequency, gain and Q, and a shelf mode on the outer bands.
The circuit is chosen so that the 0.05 dB prototype gate (`PLAN.md`) is met by construction:
`tools/netlist` and ngspice must agree on it like any other model.

## Amplifier

| Item | Value (8200 manual) |
|---|---|
| Op-amp | GML 9202 discrete, 30 V/µs slew |
| Clipping | +26.8 dBu, input and output (THD+N above 0.1 %) |
| THD | < 0.001 % EQ out (1 kHz at +20 dBu, includes the analyser's 0.0007 % residual); 0.0015 % EQ in; 20 Hz–20 kHz |
| IMD | 0.0015 % SMPTE at +20 dBu, EQ in or out: equal to the analyser's stated residual |
| Response | ±0.01 dB 20 Hz–20 kHz; ±0.1 dB 10 Hz–80 kHz; −3 dB at 260 kHz and well below 10 Hz |
| Noise | EQ out −118 dBu spot, −98 dBu broadband; EQ in −100 dBu spot, −80 dBu broadband |
| Input | 20 kΩ balanced bridging, 52–60 dB CMRR |

0.0015 % is −96.5 dB. The manual states the test level (+20 dBu, 6.8 dB under clipping) for the
EQ-out figure only. `PLAN.md`'s gate (THD below −100 dB at Drive 0) is ours and is measured at the
plug-in's nominal level, which M2 maps to dBu.

Mapping to Umber: Desk's line-amp loop solution for a discrete op-amp, with high open-loop gain,
a soft class-AB output and rails at the ±18 V regulated supply (unsourced beyond the clip level);
no transformer stages.

## Published digital models

None in the literature searched.

## What the gates must reproduce

1. **Prototype accuracy.** Curves match the analog prototype within 0.05 dB (`PLAN.md`).
2. **Symmetry.** Cut is the exact inverse of boost at every frequency, gain and Q.
3. **Ranges.** ±15 dB, Q 0.4–4, the band frequency limits above, shelf on the outer bands.
4. **Flat is flat.** At 0 dB gain the band is exactly transparent, whatever its frequency and Q.
5. **Transparency.** THD below −100 dB at Drive 0 (`PLAN.md`), against the 0.0015 % spec.

## Differences from `PLAN.md`'s slot mapping

The slot-mapping rows below are applied in M0: `PLAN.md`'s table and `src/dsp/ModelInfo.cpp` carry
them.

The 8200 has five bands and no filters; Viridian has four band slots plus HPF and LPF.

| Slot | PLAN | Sourced | Action |
|---|---|---|---|
| HPF | 10–500 Hz, 12 dB/oct | none on the 8200 | **unsourced: plan value** |
| Low | 10 Hz–1 kHz, Q 0.4–4, shelf/bell | band 1: 15–800 Hz, Q 0.4–4 or shelf | 15–800 Hz |
| Low-Mid | 30 Hz–3 kHz, Q 0.4–4 | band 2: 15–800 Hz, or band 3: 120 Hz–8 kHz | band 3 (120 Hz–8 kHz) covers the most ground between Low and High; band 2 would duplicate Low |
| High-Mid | 300 Hz–20 kHz, Q 0.4–4 | band 4: 400 Hz–26 kHz | 400 Hz–26 kHz |
| High | 1–25 kHz, shelf/bell | band 5: 400 Hz–26 kHz, Q 0.4–4 or shelf | 400 Hz–26 kHz |
| LPF | 2–40 kHz, 12 dB/oct | none on the 8200 | **unsourced: plan value** |
| Gain, Q | ±15 dB, 0.4–4 | same | none |

At 44.1 and 48 kHz the 26 kHz top of bands 4 and 5 lies above Nyquist; how the frequency range
behaves there is a decision for M2, recorded here when made.

## Unsourced

- Every schematic; the band topology is ours.
- The Q definition's bandwidth convention.
- Rails and open-loop gain of the 9202.
- HPF and LPF (plan values).
- The MES-432 entirely.

## References

- George Massenburg Labs, *Model 8200 Parametric Equalizer Series II*, owner's manual: features,
  operation, specifications and notes, Appendix A plots.
- G. Massenburg, "Parametric Equalization", 42nd AES Convention, Los Angeles, 1972, paper K-2
  (listing only).
