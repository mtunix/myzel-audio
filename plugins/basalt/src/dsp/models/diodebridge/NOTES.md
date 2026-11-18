# Diode bridge (1.1): the progressive feedback compressor

A specification only: Diode bridge is built in 1.1 (`PLAN.md`, *Milestones*, M6), in the frozen
place it already holds in the `model` list. A balanced diode bridge, forward-biased by a control
voltage, is the gain cell; both side chains listen after it; the compressor's curve is soft and
progressive, the limiter's nearly flat, and two automatic releases are fast after transients and
slow under sustained level. Behaviour reference: the **33609** limiter/compressor (AMS Neve:
*33609/N User Manual*, 527-409 issue 1.0, 2015; *33609/J Technical Handbook*, 527-149 issue 3,
2002). Product names appear only here and in `PLAN.md` (root `NOTICE`).

Diode bridge runs on the shared core's feedback loop (`../../dynamics/NOTES.md`, *Feedback*) with
its own knee, a ratio cap at the core's 50, and a two-tier Auto release.

Status: M0 specification for 1.1. Nothing is implemented or simulated for this model. Marks as in
the core's NOTES: **source**, **Basalt's design**, **inference**, **not verified**.

## Sources

| Source | What Diode bridge takes | Read |
|---|---|---|
| AMS Neve, *33609/N Dual Compressor / Limiter User Manual*, 527-409 issue 1.0, © 2015 (`n33609n.pdf`) | front panel (pp. 5–6): limit attack fast ~2 ms, slow ~4 ms; compress attack fast ~3 ms, slow ~6 ms, the slow one less sensitive below 100 Hz; a1 and a2 recovery self-adjusting, fast after transient peaks and slower under persistent high levels; the compress ratio "soft and progressive", the full ratio reached only more than 5 dB above the threshold; stereo links the two side chains. Specifications (p. 9): limit threshold +4 … +15 dBu, limit ratio under 0.1 dB of output change for a 10 dB step (10 to 20 dBu), limit attack 2 ms ±1 / 4 ms ±1, limit recovery 50–800 ms, a1 100 ms / 2000 ms, a2 50 ms / 5000 ms; compress ratio 1.5:1 to 6:1; compress attack 3 ms ±1 / 6 ms ±1, the slow one with a first-order 100 Hz high-pass; compress recovery 100–1500 ms, the same a1 and a2 | read in full |
| AMS Neve, *33609/J Limiter/Compressor Technical Handbook*, 527-149 issue 3, © 2002 (third-party copy; `n33609j.pdf`) | specification (pp. 2–3): limit ratio 0.1 dB ±0.1 dB of change for a 10 dB step from +10 dBu; attack defined as the time for the output to come back within 1 dB of its earlier value after a 10 dB rise in input; limit recovery 50, 100, 200, 800 ms, AUTO 1 1500 ms, AUTO 2 3000 ms (all ±50 %), defined as the time for the control voltage to reach its new value after a +20 dBu input falls by 10 dB; the compress-ratio test table; compress recovery 100, 400, 800, 1500 ms, AUTO 1 800 ms, AUTO 2 1500 ms. Circuit description of board 11475 (pp. 5–6) | read in full |
| C. V. Pines, *Real-Time Virtual Analog Modelling of Diode-Based VCAs*, Proc. DAFx25, 2025 (`dafx25p44.pdf`) | the diode-resistor VCA's distortion, dependent on signal level and control; cites the 2254/E (schematic B185/A, 1974) and the 33609/J | skimmed; for Character, not for this model's dynamics |
| `../../dynamics/NOTES.md` | the loop, the cap, Range on the state, the measurement definition | Basalt |

The planning library's `g4m1.pdf` is not a diode-bridge document: it is Universal Audio's 2-1176
manual (P/N 65-00047), used in `../fet/NOTES.md`.

## Topology: feedback, both side chains

**Source** (/J, pp. 5–6): the audio passes the input transformer, a balanced pad, the diode bridge
(the active attenuator), a high-impedance buffer, the gain preset RV1 and the line amplifier.

- **Limiter side chain:** fed from the output of the power amplifier board (10640), through the
  limit-in switch and the threshold control, into a phase-splitting amplifier pair, rectified by
  two diodes, a threshold detector, then an emitter follower charging a storage capacitor through
  the attack resistor (p. 5). After the bridge, and after the line amplifier: **feedback**, and it
  sees the make-up gain, which acts in that amplifier's feedback (p. 6).
- **Compressor side chain:** taken from the wiper of the gain preset RV1 (p. 6), which sits after
  the bridge and its buffer and before the line amplifier. Also **feedback**; it does not see the
  make-up gain.
- **The two combine** as the larger of the two side-chain signals, and a "law correction" network
  shapes that voltage for the diode bridge (pp. 5–6).
- **Stereo:** the two channels' control voltages are linked by a relay (/J p. 8; /N: "the two side
  chains are linked together internally").

`PLAN.md`'s "both side chains after the bridge (feedback)" is confirmed.

## 1. Detector

The core's feedback loop on the **peak** level (both side chains rectify the audio and charge a
capacitor through an attack resistor: peak type, **inference** from the description), the core's
pre-hold `h = min(Release/10, 5 ms)`, Range on the state, Link as the core (the hardware links
control voltages, as the core does).

- **The compressor's slow attack adds a 100 Hz first-order high-pass to its side chain** (/N,
  **source**). Basalt does not couple a filter to the Attack knob; the key EQ's high-pass does the
  job (**Basalt's design**: a hidden filter that switches with a time setting would surprise).
- **Two sections, one threshold:** the hardware runs compressor and limiter with separate
  thresholds and takes the larger demand. Basalt's single curve (*Gain computer*) carries the
  compressor's shape below the ∞ step and the limiter's at it (**Basalt's design**; a two-threshold
  mode would need a second Threshold the frozen list does not have).

## 2. Gain computer: soft and progressive

**Source:** "the control characteristic soft and progressive, so the true ratio is only attained
> 5 dB above the threshold" (/N p. 6). The /J's ratio test (threshold −20 dBu, input 0 dBu, then
+10 dB) asks for output changes of 6.5, 5.0, 3.5, 2.5, 1.5 dB (tolerance ±1, ±1, ±1, ±0.5,
±0.5 dB) at the 1.5, 2, 3, 4, 5:1 positions: measured ratios 1.54, 2.0, 2.86, 4.0 and 6.7:1, the
nominal values well above the threshold (the 5:1 position's 1.5 dB ±0.5 spans 5:1 to 10:1).

**Basalt's curve:** eq. 4 with its knee moved up so that it starts at the threshold, a one-sided
knee:

```
r_s(x) = min( x − G(x; T + W/2, R, W), Range )         eq. 4 centred at T + W/2
```

- The curve leaves unity exactly at **Threshold** and reaches the full **Ratio** at Threshold +
  **Knee**. At the shared default Knee of 6 dB this is the "> 5 dB above the threshold" of the /N;
  Knee 0 is a hard knee at T.
- **Ratio** 1 … 20 continuous: the compressor's 1.5–6:1 is marked, higher values continue the same
  curve. **The ∞ step is the limiter:** the core's cap, **50:1**, which changes the output by
  0.2 dB for a 10 dB step, the /J's "0.1 dB, ±0.1 dB" at its upper edge (**Basalt's design** on
  the core's cap; a higher cap chatters in the loop, core *The ratio cap*).
- Feedback form: the core's `G⁻¹(y) − y` with T + W/2 in place of T.
- **The bridge's law:** the hardware's "law correction" network linearises the bridge's
  attenuation against the side chain's voltage (/J p. 6, **source**); Basalt applies an ideal dB
  gain (**inference** that the correction aims at that). The bridge's distortion, which depends on
  level and control (Pines 2025), belongs to Character.

## 3. Ballistics

**Attack** and **Release** are the core's measured times, 1:1 over the knob ranges.

- **Attack marks, converted:** the /J defines attack as the time to come back "within 1 dB of its
  original value" after a 10 dB step, i.e. 90 % of the reduction. For an exponential in dB that is
  ln 10 = 2.30 time constants, so the published 2, 3, 4, 6 ms are **0.87, 1.30, 1.74, 2.61 ms** on
  Basalt's 63 % definition (**inference**: exponential in dB assumed). Those are the marks.
- **Release marks:** limit 50, 100, 200, 800 ms; compress 100, 400, 800, 1500 ms (/J), 50–1500 ms
  overall (/N). The /J's definition (the control voltage reaching "a new value") does not state a
  fraction, so the figures are marked as published, not converted (**not verified**).
- **Auto release** (On, the default), two tiers (**Basalt's design** on /N's a1 and a2): the
  Vari-mu's follower structure (`../varimu/NOTES.md`, *Auto release*) with one memory:

  ```
  r = max(s, m)
  m ← charges towards s over 0.3 s while the side chain called for reduction within 25 ms,
      else releases with min(20 × Release, 25 s)
  ```

  After a transient the release is Release; under sustained level it is 20 × Release. The ratio 20
  is a1's (100 ms / 2000 ms); a2's is 100 (50 ms / 5000 ms). One ratio, because Basalt has one
  Auto switch; at Release 100 ms it reproduces a1, and at Release 250 ms a2's 5 s slow tier.
  The /J's single Auto figures (1500 and 3000 ms, 800 and 1500 ms) are older variants' and differ
  from the /N's; Basalt follows the /N, the current manual.
- **Auto release off** is a single exponential release.

## 4. Make-up and gain law

- Ideal dB gain after the loop (*Gain computer*); make-up after the dynamics, outside the loop, as
  the hardware's make-up sits after the compressor's tap (/J p. 6).
- **Deviation, the ∞ (limiter) step:** the /J's limiter is fed from the power amplifier's output,
  where make-up acts, so its limiter hears make-up gain. Basalt keeps Gain outside the loop for
  the limiter step too (**Basalt's design**): one gain law for every Ratio, Gain never moving the
  threshold, and Auto Gain and Learn solving the same curve at every Ratio.
- **Auto Gain** inverts the static curve at the reference level: `M = r_s(L_ref)` with the knee at
  T + W/2.

## 5. Gate table (for 1.1)

| Gate | Diode bridge |
|---|---|
| Identity | Ratio 1; Range 0; input below T (the one-sided knee): bit-identical as the core |
| Static curve | the core's grid with the one-sided knee, R 1.5, 2, 3, 4, 6, 20 and ∞ (= 50): ±0.25 dB of r_s. **The /J test:** Knee 6, T −20, input from T + 20 to T + 30 dB: output change 6.67, 5.0, 3.33, 2.5, 2.0 dB at 1.5, 2, 3, 4, 5:1 ±0.25 (the full ratios; the hardware's tolerances are wider); at ∞ (= 50): 0.20 ±0.02 dB (10/50; the /J allows 0.1 ±0.1) |
| Progressive knee | carrier, Knee 6: local ratio 2R/(R + 1) ±2 % at T + 3 dB (the knee's midpoint) and R ±2 % from T + 6 dB |
| Ballistics | the core's definition, ±10 %, Attack 0.87 … 2.61 ms marks included; Auto on: after a 50 ms burst at 10 dB = Release ±10 %, after 2 s sustained = min(20 × Release, 25 s) ±10 % |
| Distortion, Clicks, Stereo, Learn, Auto Gain | as the core and the other feedback models |

## Proposed changes to PLAN.md

1. ***The reference behaviour*, Diode bridge row:** cite the /N as "527-409 issue 1.0, 2015";
   "Limit: < 0.1 dB of change for a 10 dB step" → "(/N; /J: 0.1 ±0.1 dB)"; add the /J's attack
   definition (to within 1 dB after a 10 dB step) and that 2/3/4/6 ms correspond to 0.87–2.61 ms
   time constants; add "the compressor's slow attack adds a 100 Hz first-order high-pass (/N), not
   kept in Basalt".
2. **Diode bridge row, Basalt's reading:** "one-sided knee (eq. 4 centred at Threshold + Knee/2),
   ∞ = the core's cap 50:1 as the limiter; Auto = 20 × Release under sustained level (/N a1)".
3. ***References*:** `g4m1.pdf` in the planning library is the 2-1176 manual, not a 33609 document.

## Not verified

- The 33609's schematic (not read) and the law-correction network's actual curve.
- The /J's recovery definition's fraction; whether a2 differs from a1 only in its times.
- The progressive knee's exact shape: only "> 5 dB above the threshold" and the /J's test points.
- No simulation was run for this model; 1.1 runs the core's tests on it before building.
