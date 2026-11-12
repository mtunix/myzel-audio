# Console: British console channel EQ, two curve families

A transformerless four-band channel equaliser with high- and low-pass filters, built from IC
op-amp active filters; Alt switches between two generations of the same EQ. Behaviour reference:
the **SSL SL 4000 E** channel EQ in its two documented versions, the **'02' ("Brown Knob")** and
the **'242' ("Black Knob")**. Hardware names appear only here and in `PLAN.md`.

Status: M0 literature notes. Nothing below is implemented or measured yet. **No schematic of the
02 or 242 card was obtained.** **unsourced: plan value** marks numbers with no primary source.

## Sources

| Source | What it gives | Trust |
|---|---|---|
| Solid State Logic, *X-Rack E Series EQ User's Guide* (82S6XR0K0A, module XR425; archive.org copy) | both curve families' ranges and Q values, the history of the 02 and 242, calibration targets | manufacturer; describes SSL's own re-implementation of both circuits |
| Solid State Logic, *E Series EQ module for 500 format, User Guide* | the same history and the Brown/Black differences in words | manufacturer |
| Waves, *SSL E-Channel & G-Channel User Manual* (waves.com) | filter ranges and slopes; which EQ each plug-in models | secondary (an SSL-licensed plug-in) |
| GroupDIY forum threads on SSL 4000 EQ clones (search-result text only) | the 02 card's part number (CF82E02, rev. B, 1977) | secondary, unverified |

Not obtained: the 82E02 and 82E242 card schematics, the 4000 E console's channel-filter schematic,
the op-amp types.

## Naming: E, G, brown, black

SSL's own guides: the **02** ("Brown Knob") "was standard on all early production E Series
consoles"; the **242** ("Black Knob") was developed "in 1983 … in conjunction with … George Martin
for the first SSL console to be installed in AIR studios" (the 500-format guide says "mid-eighties").
Both are **E Series** EQs. A separate **G Series** EQ exists: Waves' G-Channel models "the 383
G EQ"; other sources call the G EQ the 292. Waves' E-Channel models the 242.

`PLAN.md` labels the Alt pair "E (brown)" and "G (black)". The behaviour it asks for under those
labels (constant-ish Q and gentle shelves, against tighter control and shelves with more shape)
is SSL's documented 02 ↔ 242 pair. These notes source the Alt pair as **02 ↔ 242**.

## Step tables

All controls continuous. From the X-Rack E Series EQ guide (SSL):

| Band | Frequency | Gain, 02 (Brown) | Gain, 242 (Black) | Q, 02 | Q, 242 |
|---|---|---|---|---|---|
| HF | 1.5–16 kHz, shelf or Bell | ±15 dB | ±18 dB | 0.8 on Bell | 1.3 on Bell |
| HMF | 600 Hz–7 kHz | ±15 dB | ±18 dB | 0.5–2.5 | 0.5–4 |
| LMF | 200 Hz–2.5 kHz | ±15 dB | ±18 dB | 0.5–2.5 | 0.5–4 |
| LF | 30–450 Hz, shelf or Bell | ±15 dB | ±18 dB | 0.8 on Bell | 1.3 on Bell |

Panel legends on the guide's drawing: HF 1.5 … 16 kHz; HMF 0.6, 0.8, 1.5, 4.5, 6, 7 kHz; LMF 0.2,
0.3, 0.8, 1.5, 2.0, 2.5 kHz; LF 30, 50, 300, 450 Hz.

Calibration targets (same guide): maximum gain trimmed to +15 dB (Brown) and +18 dB (Black) with
a 0 dBu input, measured at HF 12 kHz Bell, HMF 3 kHz, LMF 1 kHz, LF 80 Hz Bell.

Filters (Waves manual, secondary): high-pass 16–350 Hz, 18 dB/oct; low-pass 3–22 kHz, 12 dB/oct;
both −3 dB points; fully left bypasses. SSL's guide adds that the 242 brought "a steeper 18dB/octave
high pass filter", so the 02's high-pass was shallower; its slope (12 dB/oct by implication) is
**unsourced**.

## Circuit

What SSL states:

- **02 (Brown):** "the two parametric mid-band sections feature SSL's classic logarithmically
  symmetric design that ensures that the ±3 dB up/down points retain the same musical interval
  from the centre frequency regardless of frequency and amplitude settings" (constant Q with
  frequency and gain). "The two shelving sections are traditional 6 dB/octave designs with an
  option for a fixed Q parametric response (Bell)."
- **242 (Black):** "enhanced cut and boost ranges (±18 dB instead of ±15 dB) together with a
  different control law and a steeper 18 dB/octave high pass filter for tighter control of low
  frequencies."

"Q tied to gain" and "shelves with overshoot and undershoot" for the 242 (`PLAN.md`'s wording) are
**not stated** by SSL. Waves' E-Channel (the 242) documents a gain range that varies with Q: "±18 dB
when Q is set at 3.5, to ±15 dB when Q is set at 0.1" on both mid bands (secondary, and its Q
range 0.1–3.5 differs from SSL's 0.5–4). Treat both behaviours as properties the netlist must show
or not show by itself, once a schematic is in hand.

Topology: IC op-amp active filters, transformerless (`PLAN.md`; no schematic seen). Whether the
mid bands are state-variable, Wien or gyrator sections is **unsourced**.

### Netlist plan (M1)

Console is M1's end-to-end model, so the schematic is on the critical path:

1. Obtain the 82E02 and 82E242 card schematics and the channel-filter schematic (GroupDIY clone
   documentation, SSL service documentation) and netlist them.
2. If neither is available, design our own constant-Q (02) and gain-dependent (242) active
   sections that meet SSL's published numbers above, and document them as our circuit.

Either way, op-amps are ideal in the linear netlist; the rail clip is the only nonlinearity
(`PLAN.md`).

## Amplifier

IC op-amps, no transformers (`PLAN.md`). Op-amp type and rail voltage: **unsourced: plan value**.
SSL's re-implementation (XR425) specifies THD+N < 0.003 % at +20 dBu (1 kHz and 10 kHz), response
±0.5 dB 20 Hz–20 kHz, −6 dB at 100 kHz, output headroom > +26 dBu at 1 % THD; these describe the
2000s module, not the 1980s card.

Mapping to Umber: Desk's line amp loop solution is the template for the op-amp rail clip, with an
IC op-amp's open-loop gain and rails (unsourced) instead of the discrete stage's; no transformer
stages.

## Published digital models

None in the literature searched. Commercial models (Waves E/G-Channel, UA 4000 E) document ranges
but not circuits.

## What the gates must reproduce

1. **02 constant Q.** On both mid bands the ±3 dB points keep the same interval from the centre
   at every frequency and gain.
2. **02 shelves.** First-order (6 dB/oct) shelves; Bell gives Q 0.8 on LF and HF.
3. **242.** ±18 dB, Q 0.5–4 on the mids, Bell Q 1.3, 18 dB/oct high-pass.
4. **Calibration points.** +15 dB / +18 dB maximum at HF 12 kHz Bell, HMF 3 kHz, LMF 1 kHz,
   LF 80 Hz Bell, exactly as SSL trims its module.
5. **Filters.** High-pass 16–350 Hz and low-pass 3–22 kHz at the −3 dB point, 18 and 12 dB/oct.
6. **The least colour.** Only the rail clip colours, and only when pushed.

## Differences from `PLAN.md`'s slot mapping and model text

The slot-mapping rows below are applied in M0: `PLAN.md`'s table and `src/dsp/ModelInfo.cpp` carry
the sourced values, with Alt as the 02 ↔ 242 pair (legend TIGHT, provisional).

| Item | PLAN | Sourced | Action |
|---|---|---|---|
| Alt | E (brown) ↔ G (black) | brown = 02 and black = 242, both E Series; the G EQ is a third circuit (383 or 292) | Alt = 02 ↔ 242 (question to the user) |
| HPF | 16–350 Hz, 18 dB/oct | same for the 242 (secondary); the 02's is shallower | 18 dB/oct on 242, 12 dB/oct on 02 (unsourced slope) |
| LPF | 3–22 kHz, 12 dB/oct | same (secondary) | none |
| Low | 30–450 Hz, ±15 dB | ±15 dB (02), ±18 dB (242) | per-curve range |
| Low-Mid | 0.2–2 kHz, Q 0.5–2.5 | 200 Hz–2.5 kHz; Q 0.5–2.5 (02), 0.5–4 (242) | 2.5 kHz top; per-curve Q |
| High-Mid | 0.6–7 kHz, Q 0.5–2.5 | same frequencies; Q 0.5–2.5 (02), 0.5–4 (242) | per-curve Q |
| High | 1.5–16 kHz, ±15 dB | ±15 dB (02), ±18 dB (242) | per-curve range |
| Bell Q (Low, High) | fixed | 0.8 (02), 1.3 (242) | none |

## Unsourced

- Every schematic: the 02 and 242 cards, the channel filters.
- Op-amp types and rails.
- The 02 high-pass slope.

## References

- Solid State Logic, *X-Rack E Series EQ User's Guide*, 82S6XR0K0A (XR425), sections K.2–K.4.
- Solid State Logic, *E Series EQ for 500 format, User Guide*.
- Waves, *SSL E-Channel & G-Channel User Manual*.
