# PWM: broadcast compressor-limiter (1.1, specification only)

A program compressor with a peak limiter riding on top, whose releases are rates (seconds per
10 dB) and whose automatic releases follow the program: the compressor's the crest factor, the
limiter's how often peaks crowd the threshold. Behaviour reference: the **EMT 156** PDM compressor,
which varies its gain by pulse-duration modulation. Product names appear only here and in
`PLAN.md` (root `NOTICE`).

Status: M0 specification for 1.1 (`PLAN.md`, Settled 5, *M6*). Nothing is built in v1; the model's
place in the frozen `model` list holds it (Settled 9). Marks as in the core
(`../../dynamics/NOTES.md`): **source**, **measured (simulation)**, **Basalt's design**,
**inference**, **not verified**.

## Sources

| Source | What it gives | Read |
|---|---|---|
| EMT, *PDM-Compressor EMT 156 / 156 TV* manual, March 1978 (archive copy; scanned, poor OCR; the release-scale figures on the panel drawings are illegible) | operating modes, static curves, attack and release figures, the automatic release rules, the specifications | read in full (text); the panel drawings not legible |
| `../vca/NOTES.md` | the RMS detector with τ from Attack (0.45442) and the constant-rate release limiter | Basalt |
| `../clean/NOTES.md` | the crest factor (Giannoulis 2013, eq. 7) and Clean's crest law | Basalt |
| `../../dynamics/NOTES.md` | detector scale, peak level, look-ahead window, Range, Link, snap | Basalt |

## What the reference does (source: EMT 156 manual)

Three sections, switched in combinations (limiter; compressor; limiter + compressor; compressor +
expander; all three):

| | Limiter | Compressor | Expander |
|---|---|---|---|
| Static | gain unity until the **output** would exceed the threshold, then any input increase is met by an equal gain decrease (peak output held) | gain falls dB-linearly with level above a knee; the curve is set by a maximum gain (0 … +18 dB), a ratio (1.5:1 … 4:1) and a rotation point on the unity-gain line (−6 … −1.5 dB re the reference level) around which the ratio turns | below its rotation point (−35 … −55 dB) the gain returns to unity at 1:1.5 or 1:2.5, so the compressor's gain never lifts noise |
| Threshold | −2 … +7.5 dB re the reference level | (the rotation point) | (the rotation point) |
| Detects | the peak | the average program level, "approximating loudness" | |
| Attack | 80 µs (operating text); max. 100 µs (specification) | factory 2 ms; internally 1 … 4 ms (specification; the operating text says 2 … 5 ms) | program-dependent, tied to the compressor's release |
| Release, manual | 0.25 … 2.5 s per 10 dB | 0.5 … 3.5 s per 10 dB of gain change | 1.5 … 7.5 s per 10 dB |
| Release, automatic | from how often the limiter acts and how dense the peaks within 2 dB below the threshold are: rare → very short; frequent → approaching infinity | from the program's density, the ratio of average to peak: dense, small dynamic range → long; peaks far above the average → short | follows the compressor's release for loud program (no gain-modulated noise when the program stops); proportional to level for quiet program; ≈ 4.5 s |

- **Release is a rate** (source): the knob is time per 10 dB; a 5 dB recovery at 2 s/10 dB takes
  1 s. For the limiter, the recovery equals the level drop, so the time follows directly.
- **Auto release** has a short and a long part in all three sections; the long part extends the
  short-term release (source). How the two parts combine is not given (**not verified**).
- **Metering:** the gain, and a second scale counting limiter operations per second (full scale at
  10/s, which the manual calls too frequent for normal programme) (source).
- **Other figures:** overall range 40 dB (compressor + limiter); THD < 0.6 % in the compressor's
  range and < 1 % in the limiter's, automatic release (source).
- **Topology:** the manual does not say where either side chain is taken (**not verified**). A
  monitor output carries the PDM control voltage (proportional to the compressor's gain) and the
  auto-release control voltage (source).
- **The gain element:** pulse-duration modulation, a 200 kHz switching multiplier (oscilloscope
  captions in the service section) (source). Its residue lies around the carrier, far above audio;
  Basalt's gain cell is the ideal multiplier, and any colour is Character's (`PLAN.md`).

### The rotation point is Auto Gain

The 156's compressor curve, gain = min(G_max, (1 − 1/R)·(P − x)) with P the rotation point, is a
downward compressor whose knee sits at x_k = P − G_max·R/(R − 1), plus a fixed make-up of G_max
(inference, algebra on the source's description). The rotation point is the input level that comes
out unchanged. Basalt's Auto Gain does exactly that at −18 dB (core, *Make-up and Auto Gain*):
Threshold is the knee, Ratio the ratio, and Auto Gain puts the rotation point at the reference
level. So the 156's three compressor controls map onto Threshold, Ratio and Auto Gain without a
new parameter.

## 1. Detector

Feed-forward for both stages (**Basalt's design**; the manual is silent).

- **Compressor:** the VCA RMS detector (`../vca/NOTES.md`): `L_C = 10·log10(2m)`, one pole on the
  square with **τ_C = Attack / 0.45442**, so Attack is the 63 % time of a 10 dB step from
  Threshold. "Average program level approximating loudness" (source) is read as mean power; at the
  factory 2 ms attack τ_C = 4.4 ms.
- **Limiter:** the core's peak level through the look-ahead window, minus the compressor's current
  reduction: `L_L[n] = L_P[n] − y_C[n]`, the compressor's output peak as estimated from the key. No
  loop: y_C[n] is this sample's compressor reduction.
- **Linked:** the core's Link for each stage (louder wins). The 156 is a stereo unit; how its
  channels are coupled is not stated (**not verified**). Everything that steers the releases, the
  crest factor C² and the limiter's event rate ν (*Auto release*), is computed once from the linked
  pair in Linked mode (max(k_L², k_R²) for C², the linked L_L for ν), so the gains stay
  bit-identical at Link 100 % (core, *Stereo*).

## 2. Gain computer

- **Compressor:** the core's eq. 4 at Threshold T, Ratio R, Knee W: `r_C = x − G(x)` on L_C. No
  threshold–ratio coupling (the rotation point already holds level, above).
- **Limiter:** threshold **T_L = T + 6 dB**, ratio ∞, hard knee, on L_L: `r_L = max(0, L_L − T_L)`
  (**Basalt's design**: a fixed offset above the knee, so the limiter takes the peaks and leaves the
  body to the compressor. The 156 sets its limiter threshold with a control of its own, −2 … +7.5 dB
  re its reference level, source; Basalt has no knob for it, and M6 voices the offset by ear
  between 3 and 9 dB).
- **Ratio** range 1 … 20, ∞ as everywhere (feed-forward); marks at 1.5, 2, 3.5, 4 (the 156's range,
  and its suggested 2:1 for classical and 3.5:1 for popular music, source).
- **Range** caps the total: `r_C ≤ Range`, `r_L ≤ max(0, Range − y_C)`, both on the targets before
  the ballistics (the core's rule), so the sum never exceeds Range. Range's 40 dB maximum is the
  156's overall range (source).
- **Static curve:** `out(x) = min(G(x), T_L)`, reduction `x − out(x)`, capped by Range, with each
  stage's level on its own detector's reading. A sine reads alike on both stages; the
  constant-magnitude carrier reads 3.01 dB higher on the compressor's RMS than on the limiter's
  peak (core, *Conventions*), so on the carrier the compressor's x is the peak level + 3.01 dB.

## 3. Ballistics

```
compressor:  y_C = r_C,t                              if r_C,t ≥ y_C   (attack: the detector)
             y_C = max(r_C,t, y_C − 10 dB/(Rel_C·fs)) otherwise         (constant rate)
limiter:     y_L = α_L·y_L + (1 − α_L)·r_L,t          if r_L,t > y_L   (τ = 80 µs)
             y_L = max(r_L,t, y_L − 10 dB/(Rel_L·fs)) otherwise
reduction    r = y_C + y_L
```

- **Rel_C = Release** (time per 10 dB), Auto off. Marks at 0.5 and 3.5 s (the 156's range).
- **Rel_L = Release / 2** (**Basalt's design**: the 156's limiter range, 0.25 … 2.5 s, against the
  compressor's 0.5 … 3.5 s, about half). The limiter's attack is fixed at **80 µs** (the operating
  text; the specification's 100 µs is a maximum), not the Attack knob.
- **Attack** marks at 1, 2, 4 ms (the specification's internal range, factory 2 ms).
- The compressor's floor: it cannot recover faster than its detector falls, 10 dB in
  5.067 × Attack/(1 − 1/R) (`../vca/NOTES.md`); at 2 ms and 4:1, 13.5 ms, far below the 156's range.

### Auto release (Basalt's design on the source's rules)

- **Compressor: density.** Clean's crest factor C² (Giannoulis 2013, eq. 7: a peak and an RMS
  detector of the key, both 200 ms; `../clean/NOTES.md`) and Clean's law on the rate:
  `Rel_C = Release · clamp(√(2/C²), 1/8, √2)`. Peaks far above the average (large C²) shorten the
  release; dense, square-like material (C² → 1) lengthens it to √2 × Release, as the 156 does.
  Reusing Clean's law keeps one crest detector in the engine.
- **Limiter: peak density.** Events are counted on a **held** level, never on the raw sample peak:
  L_L is instantaneous and falls below any threshold at every zero crossing, so a tone near the
  threshold would count two events per cycle and pin d at 0.95. The counter reads
  `L_H` = the running maximum of L_L over 25 ms (one period of 40 Hz, the Vari-mu's rule). An event
  is counted when L_H rises to T_L − 2 dB or above from below T_L − 3 dB (1 dB of hysteresis), and
  the counter re-arms no sooner than 50 ms after an event (at most 20 events per second).
  (**Basalt's design**.) The event rate ν (events per second) is a leaky count,
  `ν[n] = α_ν·ν[n−1] + (1 − α_ν)·fs·e[n]`, τ_ν = 1 s, e[n] = 1 on an event. With
  `d = min(ν / 10, 0.95)` (10 per second: the 156's meter full scale),
  `Rel_L = (Release / 8) / (1 − d)`: rare peaks release at Release/8 per 10 dB ("very short"),
  7.5 per second at Release/2, 9.5 per second and more at 2.5 × Release (the source's "approaching
  infinity", bounded). The UI may show ν as the 156's operations-per-second scale (M4 of 1.1).
- **The two parts** (short and long, source) are these two laws plus the compressor's own slow
  rate; no third stage is added.

## 4. Make-up and gain law

- Gain 10^(−r/20), the core's ideal cell; PDM's switching is not modelled (above).
- **Auto Gain** (core): M = r_static(−18 dB) with the static curve of *2. Gain computer*; this is
  the 156's rotation point at −18 dB (above). The 156's "compressor gain" (maximum lift of quiet
  material) appears as that make-up.
- **Left out:** the expander. Without it, Auto Gain's make-up lifts quiet passages and noise as the
  156 does with its expander off. Floor (`PLAN.md`, Upward only) could serve as the expander's
  rotation point in 1.1 (*Open*).

## Gates (for 1.1)

The core's rows apply. PWM's own:

| Gate | PWM check |
|---|---|
| Static curve | 1 kHz sine and the carrier, the core's grid: reduction within ±0.25 dB of `x − min(G(x), T + 6)`, Range applied, levels on each stage's own reading (the carrier +3.01 dB on the compressor) |
| Limiter ceiling | 1 kHz bursts 20 dB over T_L, Attack 2 ms: output peak more than 0.5 dB over T_L only during the first 0.5 ms of a burst |
| Ballistics, compressor | attack as VCA RMS (10 dB from Threshold, 63 %): Attack ±10 % for Attack ≥ 1 ms; release slope 90 → 10 % of 6, 12, 24 dB: 10 dB per Release ±10 % (Auto off) |
| Ballistics, limiter | step from 10 dB below to 10 dB above T_L, Ratio 1 (compressor idle): 63 % of the reduction in 0.08 ms ±20 % (4 samples at 48 kHz); release 10 dB per Release/2 ±10 % (Auto off) |
| Auto, compressor | after 2 s of a sine (C² ≈ 2): release rate 10 dB per Release ±10 %; after 2 s of band-limited pink noise: per 0.42 × Release ±15 % (Clean's measured C² median) |
| Auto, limiter | 1 kHz bursts reaching T_L (10 ms long) at 0 (one burst), 5 and 8 per second for 5 s, then silence: each burst counts exactly one event (the 25 ms hold keeps L_H up through the burst, and 90 ms of gap at 8 per second exceed the re-arm time); ν at the last burst 5 ± 0.6 and 8 ± 0.6 per second (the leaky count ripples by about ±0.5 around the rate); the limiter's release slope over its first 2 dB within ±15 % of 10 dB per Rel_L computed from the ν read at the step (expected about Release/8, Release/4, Release/1.6). A 1 kHz tone held 1 dB under T_L for 2 s counts one event, at its onset |
| Distortion | reported only: a 4.4 ms RMS ripples (the VCA RMS formula: ≈ −49 dBc at 1 kHz, Ratio 4, 6 dB reduction, Attack 2 ms; inference), inside the 156's own < 0.6 % (−44 dBc) |

## Proposed changes to PLAN.md

1. ***The reference behaviour*, PWM row:** Detector "Not stated (M0)" → "not stated in the manual;
   Basalt: compressor on mean power (the manual's 'average program level'), limiter on the peak of
   the compressor's output, both feed-forward". Ratio "A ratio control" → "compressor 1.5 … 4:1
   around a rotation point (= Auto Gain at the reference); peak limiter at ∞ (Basalt: 6 dB above
   Threshold)". Attack "Compressor ≈ 2 ms (2–5 ms trim)" → "compressor 2 ms (1–4 ms in the
   specification, 2–5 ms in the operating text); limiter 80 µs (≤ 100 µs specified)". Release add
   "limiter 0.25–2.5 s, compressor 0.5–3.5 s per 10 dB".
2. ***Open*:** add "PWM's expander: Floor as its rotation point in 1.1, or left out".

## Open

- The expander (above).
- Whether the limiter should look at the compressor's output (as here) or at the key (simpler,
  identical when the compressor is idle). Decided here for the output, as the manual describes the
  limiter by the output level.

## Not verified

- The panel's release-scale figures (illegible in the copy read), the attack definition, how the
  short and long parts of the automatic release combine, where the side chains are taken, and how
  the stereo channels are coupled.
- The distortion figure in the table is a formula estimate, not a simulation of this model.
