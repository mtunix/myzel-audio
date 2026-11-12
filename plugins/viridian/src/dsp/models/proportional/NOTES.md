# Proportional: American discrete console EQ

A stepped, reciprocal console equaliser built around discrete op-amps, whose bandwidth narrows as
the boost or cut rises (proportional Q), with a transformer-coupled output. Behaviour reference:
the **API 550A** (three bands, band-pass filter) and the **API 550B** (four bands, the frequency
sets used here); the **API 560** graphic is a family member, not modelled. Hardware names appear
only here and in `PLAN.md`.

Status: M0 literature notes. Nothing below is implemented or measured yet. **No schematic of the
550A or 550B was obtained**; the circuit section is therefore built from the manufacturer's own
descriptions and is the weakest of the five models. **unsourced: plan value** marks numbers with
no primary source.

## Sources

| Source | What it gives | Trust |
|---|---|---|
| API, *550A Discrete 3 Band EQ* brochure (apiaudio.com/docs/brochures/br550a.pdf) | features, op-amp, proportional Q, reciprocity, band-pass | manufacturer |
| API, 550A product page specifications (apiaudio.com) | current 550A step tables, gain steps, band-pass range, clip level, impedances, THD | manufacturer (current production) |
| API, *550b Four Band Switchable Equalizer* brochure (hosted at barryrudolph.com) | 550B step tables, filter slope, clip level and rails | manufacturer |
| API, *5500 Dual Equalizer Operator's Manual*, rev. 2024-01-17 | the circuit family's description: twin-T plus a proportional-Q section, reciprocity by reorganising the same parts, 2520 op-amps, 2503 output transformer | manufacturer (a 550B-derived product) |
| UA, *API 500 Series EQ Collection* manual and the Waves *API 550* manual (search-result text only, pages not opened) | the original 550A's five frequencies per band | secondary |
| ms-tas.com, "API 550 Equalizers" | version table (550, 550A, 550A-1, 550B), proportional vs constant Q in numbers | secondary; links a Wolff/Klett versions document that could not be retrieved |

Not obtained: any schematic, the 2520 op-amp's circuit, the output transformer's data, the
"Versions of API 550 Equalizer" document (technicalaudio.com, offline; the archive copy was
refused).

## Step tables

550B (four bands, API brochure). This is the set recommended for Viridian's four band slots:

| Band | Positions | Shape |
|---|---|---|
| Low | 30, 40, 50, 100, 200, 300, 400 Hz | peak / shelf |
| Low-Mid | 75, 150, 180, 240, 500, 700 Hz, 1 kHz | peak |
| Mid-High | 800 Hz, 1.5, 3, 5, 8, 10, 12.5 kHz | peak |
| High | 2.5, 5, 7, 10, 12.5, 15, 20 kHz | peak / shelf |

Gain, every band (API 550A spec and 5500 manual): **±2, 4, 6, 9, 12 dB**, plus 0: eleven detents.
550B brochure: "Filter Slope: 12dB per Octave", without saying which section it describes; the
550B has no high- or low-pass filter (see *Differences* below). Reading it as the order of the
band sections is ours, not the brochure's, and it sets no slope for the 550A's band-pass, which
stays unsourced.

550A, current production (API spec): Low 30, 40, 50, 100, 200, 300, 400 Hz; Mid 200, 400, 600,
800 Hz, 1.5, 3, 5 kHz; High 2.5, 5, 7, 10, 12.5, 15, 20 kHz; low and high peak/shelf; band-pass
filter **50 Hz to 15 kHz**, switched in by a toggle, slope not stated.

550A, original five-point version (secondary): Low 50, 100, 200, 300, 400 Hz; Mid 0.4, 0.8, 1.5,
3, 5 kHz; High 5, 7, 10, 12.5, 15 kHz. The brochure's "21 points in 5 steps of boost … fifteen
equalization points" mixes the two versions.

## Circuit

What the manufacturer states:

- "The Twin 'T' topology is found in many equalizer circuits, but … Saul Walker included a novel
  Proportional Q section." Proportional Q "spread[s] the equalizer's action over a broad bandwidth
  at low degrees of boost or cut … as the amplitude is advanced … the bandwidth narrows" (5500
  manual).
- **Reciprocal**: "When switching an API EQ from boost to cut, the exact same components are used,
  reorganized around the amplifier for the appropriate function. This guarantees 100 %
  reciprocity" (5500 manual); "reciprocal and repeatable filtering" (550A/550B brochures).
- Gain from the **2520** discrete op-amp; the 550A's output is transformer isolated (API spec);
  the 5500 pairs the 2520 with the API **2503** output transformer.
- Proportional Q in numbers (ms-tas, secondary): a 2 dB boost at 200 Hz spans roughly 20 Hz to
  2 kHz (0.1× to 10× the centre); more boost keeps the span and narrows the curve. A constant-Q
  design (the 550A-1) spans roughly 0.25× to 4× at 2 dB.

Reading of the above (ours, to verify against a schematic before M2): a reciprocal feedback
equaliser, the band network switched between the op-amp's input side (boost) and feedback side
(cut), with stepped resistor ladders as the gain switch. In such a circuit the curve's bandwidth
follows from how far the ladder moves the network's connection point, so Q rises with gain
without any extra control: proportional Q is the circuit's response, never a drawn rule. Whether
each band's resonance is an LC tank, a twin-T RC network or a gyrator is **not sourced**.

### Netlist plan (M2)

Blocked on a schematic. Two ways forward, for the user to choose (see the report):

1. Obtain the 550A/550B schematic (the TB550A clone threads on GroupDIY, or API service
   documentation) and netlist it, as the other models.
2. Design our own reciprocal stepped-ladder network that meets the manufacturer's published
   behaviour (the step tables, ±2/4/6/9/12 dB, proportional Q, 12 dB/oct sections, exact
   reciprocity) and document it here as our circuit, not as a copy of the hardware.

## Amplifier and transformers

| Item | Value | Source |
|---|---|---|
| Op-amp | 2520 discrete | API |
| Clip level | +30 dBu at ±16 V rails (550B); +30 dBm at 3 % THD (550A) | API |
| Input impedance | 66 kΩ high level, 17 kΩ low level (550A); 18.5 kΩ +4 in, 7.5 kΩ −2 in (550B) | API |
| Output impedance | < 85 Ω, transformer isolated (550A); < 75 Ω (550B) | API |
| THD | 0.05 % max at +4 dBm, 0.07 % max at +18 dBm, 30 Hz–20 kHz (550A) | API |
| Response, EQ flat | ±0.5 dB 30 Hz–20 kHz (550A); +0/−0.3 dB 30 Hz–20 kHz (550B) | API |
| Output transformer | 2503 (in the 5500) | API; core data unsourced |
| Op-amp slew rate, open-loop gain | | unsourced: plan value ("high slew") |

Mapping to Umber's circuits: Desk's line amp (a discrete op-amp model with a soft class-AB output
into asymmetric rails, a slew limit and a tabulated loop solution) for the 2520 with its rails set
to ±16 V and a harder knee (`PLAN.md`: "hard-ish rail clip at ±16 V"); Desk's JA transformer stage
for the output transformer, voiced lighter (`PLAN.md`). Both voicings are unsourced beyond the
clip level and THD figures above.

## Published digital models

None in the literature searched (DAFx, AES, JAES, IEEE). Commercial models exist (UA, Waves); their
manuals were seen only as search-result text and are cited for the original 550A frequencies alone.

## What the gates must reproduce

1. **Proportional Q.** At a fixed frequency the −3 dB bandwidth of the deviation shrinks as the
   step goes from 2 to 12 dB (the 550B brochure plots "the 'Proportional Q' characteristics … from
   0 to +12 dB of boost").
2. **Reciprocity.** Boost +x then cut −x at the same frequency and step sums to flat: the cut
   response is the exact inverse of the boost response (mirror image in dB). Test it at every
   frequency and step, within the gate tolerance.
3. **Steps.** Exactly ±2, 4, 6, 9, 12 dB at the centre frequency (peak) or on the plateau (shelf).
4. **Shelf/peak** on the low and high bands.
5. **Band-pass** (Alt): 50 Hz to 15 kHz, switched in independently of the bands.
6. **Headroom.** THD vs level against 0.05 % at +4 dBm and 0.07 % at +18 dBm; clip at +30 dBu.

## Differences from `PLAN.md`'s slot mapping and model text

The slot-mapping rows below are applied in M0: `PLAN.md`'s table and `src/dsp/ModelInfo.cpp` carry
the sourced values (the HPF and LPF knobs dim; Alt switches the fixed band-pass in). The boost
vs cut row is open for M2: `PLAN.md`'s model text still says "not mirror images".

| Item | PLAN | Sourced | Action |
|---|---|---|---|
| Low | 5 steps, 30–400 Hz | 7 steps 30, 40, 50, 100, 200, 300, 400 Hz (550B and current 550A); the original 5-step 550A is 50–400 Hz | 550B's 7 steps |
| Low-Mid | 5 steps, 200 Hz–1.5 kHz | no API band matches; 550B Low-Mid is 7 steps 75 Hz–1 kHz; the 550A Mid is 200 Hz–5 kHz (7) or 0.4–5 kHz (5) | 550B's 7 steps |
| High-Mid | 5 steps, 0.8–8 kHz | 550B Mid-High 7 steps 800 Hz–12.5 kHz | 550B's 7 steps |
| High | 5 steps, 2.5–20 kHz | 7 steps 2.5–20 kHz (550B, current 550A); original 550A 5–15 kHz (5) | 550B's 7 steps |
| Gain | ±12 dB stepped | ±2, 4, 6, 9, 12 dB | eleven detents |
| HPF / LPF | "stepped (filters, with Alt)" | the 550A has one fixed band-pass, 50 Hz–15 kHz, on a toggle; the 550B has none | HPF slot 50 Hz, LPF slot 15 kHz, fixed, both on Alt |
| Boost vs cut | "cut and boost are not mirror images" | API: "100 % reciprocity", the same components reorganised | model reciprocal; the PLAN sentence contradicts the manufacturer |

## Unsourced

- The schematic of every band, the gain ladder and the band-pass filter (including its slope).
- 2520 open-loop gain, slew rate, output stage; 2503 core data.

## References

- API (Automated Processes Inc.), *550A Discrete 3 Band EQ*, brochure br550a.pdf, and the 550A
  specifications page.
- API, *550b Four Band Switchable Equalizer*, brochure.
- API, *5500 Dual Equalizer Operator's Manual*, revised 2024-01-17, sections 1.2–1.4.
- ms-tas.com, "API 550 Equalizers" (version table, proportional vs constant Q).
- V. Välimäki, J. D. Reiss, "All About Audio Equalization: Solutions and Frontiers", Applied
  Sciences 6(5), 2016 (listed in `PLAN.md`; not read for these notes).
- Umber `src/dsp/models/desk/NOTES.md`: the line amp and transformer reused here.
