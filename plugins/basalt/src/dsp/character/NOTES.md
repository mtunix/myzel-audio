# Character: Warm, Iron, Edge

Character is the one place Basalt adds colour (`PLAN.md`, *Character*, Settled 2, 13, 21): three
smooth shapes, not circuit models, run oversampled with antiderivative antialiasing, off by
default. This file turns `PLAN.md`'s *Character* section into Basalt's specification: each curve
and its antiderivatives in closed form with a tabulation plan, Warm's envelope bias, Iron's flux
path, Edge's knee and tilt, Drive and its compensation, Placement and Link, the oversampler per
host rate, the droop compensation, the dry path, latency, and the gates with numbers. Product and
unit names appear only here and in `PLAN.md`.

Status: M0 specification. Nothing is implemented. Marks, as in the model NOTES:

- **source**: read in the cited paper or file;
- **measured (simulation)**: computed in M0 with Python (NumPy 1.26, SciPy 1.11, mpmath 1.2) in
  double precision, by the method given next to the numbers (*Measured in M0*). The filters are
  ports of `common/dsp/Oversampler2x.h`'s designs. The scripts lived in the M0 session's
  scratchpad and are not kept; M3 repeats every gated number through the real chain in
  `BasaltMeasure`;
- **measured (C++ sketch)**: a stand-alone benchmark compiled outside the repository (g++ -O3, the
  repository's default x86-64 target), not the product code;
- **Basalt's design**: a choice made here where no source decides it;
- **inference**: reasoning from sources, not measured;
- **not verified**: looked for and not read.

## Sources

| Source | What Character takes | Read |
|---|---|---|
| J. D. Parker, V. Zavalishin, E. Le Bivic, *Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution*, Proc. DAFx-16, 137–144, 2016 | first-order ADAA (eq. 9); the ill-conditioning at x[n] ≈ x[n−1], F0(0) = 0 against cancellation, the midpoint substitute with O(Δ²) error (§2.3, eq. 10); the linear response (x[n] + x[n−1])/2, half a sample of delay (§4, eq. 17); Table 1: 4× with ADAA ≈ 12× without for a hard clipper | read in full (the DAFx-16 copy), M0 |
| S. Bilbao, F. Esqueda, J. D. Parker, V. Välimäki, *Antiderivative Antialiasing for Memoryless Nonlinearities*, IEEE SPL 24(7), 1049–1053, 2017 | second-order ADAA with the second antiderivative | abstract only; the open copy (CORE) served a web page, not the PDF. The formula used below is the form usually quoted for this letter; its linear response and accuracy were **derived and checked here** |
| J. Kahles, F. Esqueda, V. Välimäki, *Oversampling for Nonlinear Waveshaping: Choosing the Right Filters*, JAES 67(6), 440–449, 2019 | the ANMR procedure (§3): Chebyshev 120 dB window, harmonics resynthesised from magnitude and phase at k·f0 and subtracted, residual energy by Parseval, NMR against a simplified masking threshold, A-weighting, audibility above −10 dB | read in full (Aalto's CC BY copy), M0 |
| H.-M. Lehtonen, J. Pekonen, V. Välimäki, *Audibility of Aliasing Distortion in Sawtooth Signals and Its Implications for Oscillator Algorithm Design*, JASA 132(4), 2721–2733, 2012, doi:10.1121/1.4748964 | the listening tests behind the ANMR (tones at **60 dB SPL**, 44.1 kHz); proposes the A-weighted NMR | abstract only (PubMed); the masking model itself **not verified** |
| K. Brandenburg, *Evaluation of Quality for Audio Encoding at Low Bit Rates*, AES 82nd Conv., preprint 2433, 1987 | the NMR | record only (Kahles et al.'s reference list) |
| D. T. Yeh, J. S. Abel, J. O. Smith, DAFx-07; D. T. Yeh, PhD thesis, 2009 | a symmetric clipper gives odd harmonics only (Edge) | per `PLAN.md` (read in planning); not re-read in M0 |
| J. Pakarinen, D. T. Yeh, CMJ 33(2), 2009 | asymmetry gives even harmonics; bias shifts with level (Warm) | per `PLAN.md` (read in planning); not re-read in M0 |
| M. van der Veen, AES 130th Conv., paper 8360, 2011; M. Holters, U. Zölzer, DAFx-16; J. Mačák, PhD thesis, 2012 | B ∝ V/f; saturation growing as frequency falls; output-transformer distortion below ≈ 120 Hz (Iron) | per `PLAN.md` (read in planning); not re-read in M0 |
| FabFilter, *Pro-C 3 Help: Style and Character* | Drive "reverse-linked with the Threshold knob" to "exchange compression for saturation in a single movement"; Post saturates "transients that remain"; the direction of the link is not stated | read (fetched again in M0) |
| `common/dsp/Oversampler2x.h`, `common/dsp/OpAmpClip.h` (`FoldedHalfBand`), `plugins/viridian/src/dsp/chain/EqChain.h`, `plugins/mulberry/src/dsp/machines/clean/CleanMachine.h`, `plugins/inkcap/src/dsp/modules/crush/NOTES.md` | the shared oversamplers, their latency conventions, how the siblings use them | read (the repository) |
| `../dynamics/NOTES.md` | the key taken before Character; the audio delay D | read |

The psychoacoustic pieces of the ANMR below (Bark scale, spreading function, threshold in
quiet, A-weighting) are written from the formulas as commonly reproduced; their original papers
and IEC 61672-1 were **not read** in M0 (*Not verified*).

## Where Character sits

```
Post (default):  in ─ gain cell (× g[n]) ─ Character ─ Gain ─ Mix ─ Output
Pre:             in ─ Character ─ gain cell (× g[n]) ─ Gain ─ Mix ─ Output
key (Internal):  in ─ key EQ ─ detector …           (before Character in both placements)
```

Inside Character, per channel:

```
x ─► ↑M ─► u = x·d ─► shape (ADAA, M·fs) ─► C5 (droop) ─► ÷ d·s0 ─► ↓M ─► [Edge: tilt] ─► out
       (Normal: linear-phase half-bands; Live: Oversampler2x cascade; M by host rate)
```

- **Key before Character** (**Basalt's design**, as `../dynamics/NOTES.md` *Key path* states).
  Learn, Auto Gain, the transfer curve and Link all refer to the input's level, and the detector
  stays at the host rate, off the oversampler. With Pre, the key delay line takes Character's
  integer delay too (key delay = D − LA, with D = 10 ms + Character's delay), so look-ahead stays
  exact. In Live, Pre delays the audio by Character's unreported 4.9 samples (*Latency*), which
  the detector sees as that much extra look-ahead: harmless, and stated. **No model's detector
  hears Character**, feedback models included: their loop never reads the gain cell's output but
  estimates it from the key, y = L_w(key) − s (`../dynamics/NOTES.md`, *The loop*), and the key is
  taken before Character. A hardware feedback tap would hear a Pre stage; Basalt deliberately does
  not. Rejected: the key after Character in Pre. It would let
  Drive, through Link, move the detector's level, so Learn's solve and Link would feed each
  other.
- **Off is bit-exact** (`PLAN.md`): no oversampler runs, only the integer delay of *Latency*.
- **Mix** blends the dry tap after Gain (`PLAN.md`, *Signal flow*); the dry tap's own path is in
  *The dry path*.

## The shared frame

### Drive and its compensation

- **Input scaling** (**Basalt's design**): u = d·x with d = 10^((D_eff − 6)/20). At Drive 0 a
  full-scale sine reaches u = 0.5. Drive 24 reaches u = 7.94. Every curve below has its knee near
  |u| = 1, so the default Drive 6 puts full scale at the knee and a −18 dBFS RMS programme 15 dB
  under it. The offset (−6 dB) sets how hard the gate's full-scale sines drive the curves. The
  aliasing figures depend on it (*Measured in M0*, the +30 row is the same curves with 0 dB
  offset), so M3 re-measures if voicing moves it.
- **Small-signal compensation** (`PLAN.md`): the output is divided by d·s0, s0 the curve's slope
  at rest (Warm sech²(b0), Iron and Edge 1 by construction). A small signal passes at unity at
  every Drive, so the small-signal level never depends on Drive. Loud signals come out quieter
  than they go in, by the saturation itself (measured, a 1 kHz sine, after compensation):

  | Input | Drive | Warm: level, H2, H3 | Edge: level, H3 |
  |---|---|---|---|
  | −18 dBFS RMS | 0 | −0.11 dB, −38.6, −65.6 dBc | −0.00 dB, −78.8 dBc |
  | −18 dBFS RMS | 6 | −0.25 dB, −32.0, −54.0 dBc | −0.01 dB, −66.5 dBc |
  | −18 dBFS RMS | 12 | −0.61 dB, −25.2, −42.9 dBc | −0.05 dB, −53.3 dBc |
  | −18 dBFS RMS | 24 | −3.47 dB, −13.2, −24.7 dBc | −2.15 dB, −19.1 dBc |
  | 0 dBFS peak | 6 | −2.31 dB, −15.5, −28.7 dBc | −0.82 dB, −27.6 dBc |
  | 0 dBFS peak | 24 | −14.85 dB, −19.0, −10.5 dBc | −15.27 dB, −9.8 dBc |

  So "Drive changes colour, not loudness" holds at small signals and to within 0.6 dB on a
  −18 dBFS RMS programme up to Drive 12, not at Drive 24 on loud material. A level-matched
  compensation (a per-mode table over Drive at the −18 dBFS RMS reference) is the alternative,
  rejected for now: it makes quiet passages louder as Drive rises, and the plan names
  small-signal compensation. M3's listening pass decides (*Proposed changes*, 9).
- **Smoothing:** d is smoothed per host sample (one-pole, 20 ms) and interpolated linearly across
  the M oversampled samples; the compensation uses the same d at each sample, so u/d = x and a
  Drive sweep never moves the small-signal level (**Basalt's design**). ADAA accepts any input,
  so a moving d needs nothing more.

### Link to Threshold

- **Direction:** the reference "reverse-links" Drive and Threshold to "exchange compression for
  saturation in a single movement" (FabFilter help, **source**); it does not give the direction.
  Basalt: raising Threshold (less compression) raises Drive (more saturation), so one movement
  trades one for the other (**inference** from "exchange"; **Basalt's design**).
- **Formula** (**Basalt's design**, the plan's default slope):
  D_eff = clamp(Drive + 0.5·(Threshold + 18 dB), 0, 24) dB with Link on, D_eff = Drive with it
  off. Stateless: the anchor is Threshold's default (−18 dBFS), so presets and automation
  reproduce exactly and nothing depends on when Link was switched on. The UI shows D_eff as the
  Drive control's live value (M4). Rejected: a link relative to the values at the moment Link
  is engaged (state outside the parameters, and automation could not reproduce it).
- **Level:** `PLAN.md` says the link holds the output level. It does not, in general: less
  compression raises the level by up to (1 − 1/R)·ΔT for material above Threshold, while Drive's
  saturation lowers a −18 dBFS RMS programme by 0.1–3.5 dB (the table above). The two only
  partly offset; level is Auto Gain's job (*Proposed changes*, 5).

### The linear part and the droop compensation

Second-order ADAA (*Antialiasing*) responds to small signals as
L(u)[n] = (u[n] + u[n−1] + u[n−2])/3: one oversampled sample of delay and a droop of −1.54 dB at
20 kHz at 176.4 kHz (derived from the formula; checked numerically). Iron runs its input through
the same L so that all three modes share one linear response (mode crossfades without combing,
one dry path).

**C5** (**Basalt's design**), a symmetric 5-tap FIR at the oversampled rate after the shape:

```
C5 = [ 0.141436, −0.894272, 2.505672, −0.894272, 0.141436 ]      (sum 1, delay 2 samples)
```

fitted (minimax) so that L·C5 is flat to 20 kHz at 176.4 kHz: **±0.0018 dB**, and the same at
192 kHz (measured). It raises in-band aliases by the droop it undoes (≈ 1.2 dB at worst,
*Measured in M0*). C5 alone boosts up to +13.2 dB towards the oversampled Nyquist; L·C5 rolls off
above 20 kHz and comes back up there (computed, at 176.4 kHz: −0.20 dB at 30 kHz, −1.41 dB at
40 kHz, a null at fs/3 = 58.8 kHz, then inverted, +3.67 dB at 88.2 kHz; at 192 kHz −0.82 dB at
40 kHz, the null at 64 kHz). At 4× and 2× the decimators' stop bands remove what lies above the
host band. **At 1× (176.4/192 kHz) there is no decimator**, so the host band carries L·C5 as it is:
the roll-off from 30 kHz and the +3.7 dB lift near Nyquist reach the output, and at 2×
(88.2/96 kHz) the host band to 40 kHz carries the roll-off (−1.4 dB at 40 kHz). **Accepted**
(**Basalt's design**): all of it lies above 20 kHz, where nothing is gated or audible, and Character
is a colour effect. Rejected: a low-pass or a longer compensator for the 1× case (more delay and
cost for ultrasonics). A 3-tap fit gives ±0.045 dB; C5 costs two multiplies more per
oversampled sample and makes Character, at small signals, a pure integer delay (*The dry path*).
A host-rate shelf (`PLAN.md`'s wording) was rejected: its target depends on the factor and it
cannot make the delay an integer.

## Warm

Even and odd harmonics, soft; the second harmonic leads and grows with level (`PLAN.md`).

```
v[n]   = u[n] + b[n]                                     the bias rides on the input
w[n]   = ADAA2_tanh(v)[n] − tanh(b[n−1])                 zero at rest
r[n]   = cosh²(b0)·w[n] − L(u)[n]                        the distortion residual
y[n]   = L(u)[n] + HP20(r)[n]                            then C5, ÷ d
```

- **Curve:** f(v) = tanh v. Antiderivatives with F(0) = 0 (**source** for F1: Parker §5.1;
  F2 derived, checked by differentiation to 1.5·10⁻⁹ with a ±10⁻⁵ central difference):
  - F1(v) = log cosh v = |v| + log1p(e^(−2|v|)) − ln 2
  - F2(v) = sgn v · [ v²/2 − |v|·ln 2 + ½·Li₂(−e^(−2|v|)) + π²/24 ]   (Li₂ the dilogarithm)
- **Bias** (**Basalt's design**, on Pakarinen & Yeh's bias shift with level):
  b = min(b0 + κ·e, b_max), b0 = 0.25, κ = 0.35, b_max = 0.9, where e is the RMS envelope of u:
  e² follows (d·x)² at the host rate through a one-pole with 10 ms attack and 200 ms release (a
  grid that charges fast and leaks slowly). b and tanh(b) are interpolated linearly across the
  oversampled samples. Because the bias is part of the input v, ADAA sees one static curve and
  needs nothing for the moving bias. Squaring at the host rate aliases only near Nyquist, and the
  1 Hz-scale smoothing removes it (**inference**).
- **What the bias does** (measured, 1 kHz, steady state): with b fixed at 0.25, H3 overtakes H2
  from u ≈ 1.5; with the envelope bias, H2 leads up to u ≈ 4 and rises from −42 to −12 dBc as u
  goes from 0.06 to 2. Near b = 0.66 the cubic term of tanh(u + b) vanishes (tanh²b = 1/3), which
  is why the rising bias keeps H3 down (**inference**, from the Taylor series).
- **DC:** an asymmetric curve makes DC that moves with level (−0.14·A at u = A = 1). A 20 Hz
  first-order high-pass on the **residual** r only (at the oversampled rate) removes it; the
  linear part is untouched, so small signals and the dry path need no blocker (**Basalt's
  design**). Measured on a 1 kHz burst (200 ms, −6 dBFS, Drive 12, 48 kHz): after the burst the
  sub-20 Hz tail stays above −60 dB re the burst's peak for **40 ms**, against **266 ms** for a
  3 Hz blocker on the whole output; the first sample of the tail is −16.5 dB in both. Rejected:
  a blocker on the whole output (the thump lasts six times longer, and the dry path would need it
  too); an envelope-predicted DC (it releases with the envelope and leaves a 200 ms tail).

## Iron

Low-end weight, saturating bass first; a transformer whose flux B ∝ V/f (van der Veen; `PLAN.md`).
Integrate, saturate, differentiate (**Basalt's design** on that law):

```
p[n]   = L(u)[n]                                          the shared linear part
φ[n]   = a·φ[n−1] + β·p[n]                                leaky flux, a = e^(−2π·fL/fs'), β = 2π·fref/fs'
S(φ)   = (1 − λ)·φ + λ·tanh φ                             a core that saturates to slope 1 − λ
y[n]   = (S(φ[n]) − a·S(φ[n−1]))/β
       = (1 − λ)·p[n] + λ·(tanh φ[n] − a·tanh φ[n−1])/β    then C5, ÷ d
```

fs' is the oversampled rate. Values: **fref = 120 Hz** (the flux amplitude equals the input
amplitude at 120 Hz, Mačák's ≈ 120 Hz onset), **fL = 10 Hz**, **λ = 0.5** (**Basalt's design**,
voiced in M3 within λ 0.4 … 0.7).

- **Exact at small signals:** with tanh φ ≈ φ, y = (φ[n] − aφ[n−1])/β = p[n]. Iron's linear
  response is L, as the others'.
- **DC-safe:** the integrator leaks below fL, so a DC input of c holds φ at c·fref/fL (12·c) and
  never runs away; the differentiator is its exact inverse, so DC comes back out, compressed by
  the core where it saturates (0.5 DC with a 1 kHz tone of 0.5: φ peaks at 6.1, the output's DC
  is 0.29, |y| ≤ |u|; measured). The curve is odd, so a DC-free input makes no DC.
- **Bounded:** y is p times S′ at a point within the flux's step (1 − λ ≤ S′ ≤ 1) plus a leak term
  of at most about 1.5·λ·fL/fref = 0.06, so |y| stays within |p| + 0.06 (**inference**, mean-value
  theorem). Heavy bass loses at most about 6 dB (λ = 0.5), never collapses to pulses
  (λ = 0.9 lost 17 dB at 30 Hz, Drive 24, and was rejected).
- **Antialiasing by construction:** (S(φ[n]) − a·S(φ[n−1]))/β is the difference of an
  antiderivative over one sample: S' averaged over the flux's step, the same structure as
  first-order ADAA with S as F1 (**inference**, Parker eq. 9). No division by a small difference
  occurs, so there is no ill-conditioning; β is small (0.0043 at 176.4 kHz), and in double the
  difference loses about 10⁻¹⁴ (**inference**). Measured: every alias ≤ −150 dBc at the gate's
  fundamentals up to Drive 24 (≤ −142 dBc at +30), and ≤ −239 dBc at 30–700 Hz fundamentals where
  Iron saturates hardest.
- **Frequency law** (measured, λ 0.5, full chain at 192 kHz, THD of a sine):

  | Input, Drive | 30 Hz | 60 Hz | 120 Hz | 240 Hz | 1 kHz | 5 kHz |
  |---|---|---|---|---|---|---|
  | −18 dBFS RMS, 6 | −26.4 dB (−0.46 dB level) | −36.4 | −48.2 | −60.1 | −84.9 | −112.9 |
  | −18 dBFS RMS, 24 | −12.2 (−4.23) | −12.6 (−2.97) | −17.0 (−1.42) | −25.6 (−0.48) | −49.0 | −76.9 |
  | 0 dBFS, 6 | −12.2 (−3.62) | −14.2 (−2.19) | −20.9 (−0.85) | −30.9 (−0.25) | −54.9 | −82.9 |
  | 0 dBFS, 24 | −17.4 (−5.67) | −14.1 (−5.36) | −12.2 (−4.73) | −11.8 (−3.66) | −21.4 (−0.80) | −47.0 |

  Below saturation, THD falls 12 dB per octave (H3 ∝ φ² ∝ 1/f²): 23.7 dB from 60 to 240 Hz at
  Drive 6. Odd harmonics only (H2 below −318 dBc, the numerical floor).
- Rejected: y = d/dt tanh(∫u) literally (λ = 1): its output collapses to pulses at the input's
  peaks once the core saturates; and a feed-forward source-resistance model y = u − ρ·N(φ),
  whose drop is unbounded at low frequencies.

## Edge

Odd harmonics, brighter; a transistor or diode stage: symmetric, harder but smooth knee, a gentle
presence tilt after it (`PLAN.md`).

- **Curve** (**Basalt's design**): a hard clip at ±1 with its corners rounded, normalised to slope
  1 at rest, k = 2.5 the knee's hardness, s = tanh k = 0.986614:

  ```
  E(u) = [ log cosh(k(u/s + 1)) − log cosh(k(u/s − 1)) ] / (2k)
  ```

  C^∞ (analytic on the real line), odd, |E| < 1. Antiderivatives with F(0) = 0, G = tanh's F2
  above, H = its F3 (derived here; checked by central differences: F1 to 5·10⁻⁹, F2 to 2·10⁻⁹,
  H to 4·10⁻⁹):
  - F1(u) = s/(2k²) · [ G(k(u/s + 1)) − G(k(u/s − 1)) − 2·G(k) ]
  - F2(u) = s²/(2k³) · [ H(k(u/s + 1)) − H(k(u/s − 1)) ] − s·G(k)/k² · u
  - H(x) = |x|³/6 − x²·ln 2/2 + π²|x|/24 − ¼·Li₃(−e^(−2|x|)) − 3ζ(3)/16   (even, H(0) = 0)
- **Why this curve** (measured, harmonics of a sine of amplitude A at the curve):

  | A | tanh: H3, H5, H7, H9 (dBc) | Edge k = 2.5: H3, H5, H7, H9 |
  |---|---|---|
  | 0.25 | −45.8, −90.0, −134.1, −178.2 | −60.2, −95.3, −139.9, −184.7 |
  | 0.5 | −34.1, −66.6, −99.0, −131.3 | −45.9, −70.2, −106.0, −131.4 |
  | 1 | −23.5, −45.1, −66.5, −88.0 | −27.6, −46.6, −67.2, −78.9 |
  | 2 | −15.5, −28.3, −40.9, −53.4 | −14.0, −29.0, −48.0, −43.9 |

  Cleaner than tanh below the knee, brighter above it: that is the "harder knee". Rejected
  (measured): erf (an entire function; its high harmonics fall *faster* than tanh's, so it is
  darker, not brighter); the static antiparallel-diode curve u = y + α·sinh y (output grows
  logarithmically, so heavy drive is duller than tanh); k = 3 (brighter, but 2.4 dB less margin
  in the aliasing gate at Drive 24) and k = 2 (duller).
- **Presence tilt** (**Basalt's design**), at the host rate after the decimator, Edge only:
  y + (10^(t/20) − 1)·HP1(y), HP1 a first-order high-pass at 4 kHz (bilinear, prewarped),
  t = D_eff/8 dB: flat at Drive 0, +1.5 dB at 12, +3 dB at 24. The gain is a multiplier on one
  branch, smoothed with d, so moving Drive never clicks. Scaling with Drive keeps the Mix gate
  (Drive 0) exact. The tilt is linear and acts at small signals too (+0.05 dB at 1 kHz at the
  default Drive 6, +0.25 dB at Drive 24; computed), so Edge's small-signal and Level gates compare
  against Character off followed by this shelf (*Gates*). It is not in the alias tables of
  *Measured in M0*; an independent re-run with it read Edge at 44.1 kHz, Drive 24, −89.4 dBc at
  4.5 kHz and −94.8 dBc at 5 kHz (−90.5 and −95.7 without): about 1 dB worse, still inside the
  gate (measured, simulation, M0 review).

## Antialiasing

**Second-order ADAA** for Warm and Edge, Iron antialiased by construction, inside the oversampler
of *Oversampling* (**Basalt's design**, the plan's named next step; `PLAN.md` asked for first
order). With first order the gate fails: Edge reaches **−71.7 dBc** at 4.8 kHz, 44.1 kHz,
Drive 24 (−75.5 with the softer k = 2), and Warm keeps only 5.9 dB of margin (*Measured in M0*).
Second order passes with ≥ 10 dB everywhere for no measurable extra cost (the CPU is in the
filters, *CPU*).

```
D(a, b)   = (F2(a) − F2(b))/(a − b)                  (|a − b| < ε:  F1((a + b)/2))
y[n]      = 2·(D(x[n], x[n−1]) − D(x[n−1], x[n−2]))/(x[n] − x[n−2])
if |x[n] − x[n−2]| < ε:   x̄ = (x[n] + x[n−2])/2,  δ = x̄ − x[n−1]
          y[n] = (2/δ)·(F1(x̄) + (F2(x[n−1]) − F2(x̄))/δ)      (|δ| < ε:  f((x̄ + x[n−1])/2))
```

- **Linear response** (f = x): (x[n] + x[n−1] + x[n−2])/3, one sample of delay (derived; checked
  to 8·10⁻¹⁶). C5 undoes its droop. The substitute for x[n] ≈ x[n−2] converges to the general
  form as O(Δ²) (checked on tanh: 1.5·10⁻⁶ apart at Δ = 10⁻², 1.5·10⁻¹⁰ at 10⁻⁴).
- **ε = 10⁻⁴, in double** (**Basalt's design**, measured): the cancellation in D grows as
  ε_mach·|F2|/Δ², so ε trades rounding against the substitutes' O(Δ²) error. The numerical floor
  (worst non-harmonic bin, 20 and 100 Hz full-scale sines, Drive 0 and 24) was −122 dBc at
  ε = 10⁻⁶, −162 at 10⁻⁵, **−169 at 10⁻⁴**, −148 at 10⁻³. D[n−1] is the previous sample's D[n],
  so each sample evaluates F2 once. In float the window would have to be far wider; Character
  runs in double, as the siblings' oversamplers do.
- **Tabulation plan** (**Basalt's design**, validated): F2 needs Li₂ (Warm) and Li₃ (Edge), which
  have no fast closed form in C++. Each curve is tabulated as a **quintic Hermite spline of f**
  (f, f′, f″ exact at the knots) on |x| ≤ X with knot spacing **h = 1/16**, and F1, F2 are the
  spline's **exact** antiderivatives (per cell a polynomial of degree 7, plus cumulative values
  at the knots), as `common/dsp/OpAmpClip.h` does for its clip. ADAA is then exact for the spline,
  which is C² and within 1·10⁻⁹ of E (Warm: 7·10⁻¹¹ of tanh). Beyond X the curve is its
  asymptote to double precision and the antiderivatives continue as exact polynomials (tanh:
  X = 20; Edge: X = 12). Odd symmetry halves the tables (Warm 320 cells, Edge 192). Measured:
  the spline and the closed forms give the same aliasing to 0.1 dB at the worst cases, and the
  same −169 dBc floor. The closed forms above are the tests' reference.
- **Rest:** after 4096 samples of exact zeros in and the output below −140 dB, Character outputs
  exact zeros and stops computing until sound returns (Inkcap Crush's rule). Warm's bias and
  Iron's flux decay towards rest, so neither reaches exact zero on its own.

## Oversampling

Factor by host rate (`PLAN.md`, Settled 21): **4× at 44.1/48 kHz, 2× at 88.2/96, 1× at
176.4/192**, so the shape runs at 176.4–192 kHz. The measurements show the aliasing comes from
the shape's own folding at that rate, not from the filters: 176.4 kHz at 1× and 44.1 kHz at 4×
give the same figures to 0.3 dB (*Measured in M0*).

**Normal: linear-phase half-bands. They exist**: `myzel::dsp::LinearPhaseHalfBand<K>` in
`common/dsp/Oversampler2x.h` (Kaiser half-band, polyphase, constant delay; Mulberry's Clean
machine cascades three by hand). Nothing must be added. `FoldedHalfBand<K, N>` in
`common/dsp/OpAmpClip.h` is the same filter with its taps folded (half the multiplies, N channels
in one loop) and is what Character should use; M3 moves it into `Oversampler2x.h` (a pure move;
Viridian's goldens must stay byte-identical, *Proposed changes*, 11).

| Stage | Filter | Pass band | Stop band | Delay (host samples, measured on the ported structure) |
|---|---|---|---|---|
| host ↔ 2× | `LinearPhaseHalfBand<34>(100 dB)`, 139 taps | ±0.0001 dB to 0.4535·fs (20 kHz at 44.1) | ≤ −99.6 dB from 0.5465·fs | 68.5 (up and down) |
| 2× ↔ 4× | `LinearPhaseHalfBand<6>(100 dB)`, 27 taps | −0.0001 dB to 24.1 kHz at 176.4 kHz | ≤ −93.6 dB from 64.1 kHz (what folds under 24.1 kHz) | 6.25 |

- The host-rate stage is the same at every rate (one set of coefficients); at 88.2/96 kHz it is
  the only stage and passes to 0.4535·fs. Rejected: a shorter 96 kHz stage (pass 20 kHz only; it
  would image ultrasonic content of 96 kHz recordings into the shape).
- Shorter host-rate stages: K = 32 at 100 dB reaches only −80 dB; K = 30 or 28 at 90 dB changed
  no gated figure (the shape dominates) but leave 10 dB less in reserve for 8 and 4 samples less
  delay. K = 34 at 100 dB matches `Oversampler2x`'s stop band. The planning simulation's 255 taps
  would have cost about 127 samples.
- **Live: `OversamplerCascade<4>`** at 44.1/48 kHz (`Oversampler2x`, stop ≤ −99.3 dB from
  0.54·fs, then `HalfBandPair<4>(0.22)`, ≤ −106.6 dB from 64.1 kHz at 176.4 kHz) and
  `Oversampler2x` alone at 88.2/96. Minimum phase, a few samples of group delay, not reported
  (`PLAN.md`; Inkcap and Viridian use the same class). Nothing must be added.

## The dry path

Mix blends the dry tap with the wet path; Character's linear response must be matched, or a 50 %
Mix combs (`PLAN.md`).

- **Normal: the dry path is the integer delay.** L (1 sample), C5 (2) and two oversampled
  samples of padding at 4× make Character's small-signal path a pure delay of **76** host samples
  at 44.1/48 kHz (68.5 + 6.25 + 5/4), **70** at 88.2/96 (68.5 + 3/2), **3** at 176.4/192:
  measured on the ported structures, within ±0.0022 dB and 0.0000° of a pure delay **to 20 kHz**.
  Above 20 kHz, at 88.2 kHz and up, it is not (*The linear part and the droop compensation*): a
  50 % Mix there combs mildly in the ultrasonic band (−1.4 dB of wet at 40 kHz against the dry
  delay), accepted with the rest. So
  the dry tap is the same integer delay that replaces Character when it is off. No second
  oversampler, and Character on and off differ at small signals by ±0.002 dB, not by the quarter
  sample `PLAN.md` expected (**Basalt's design**; *Proposed changes*, 2).
- **Live: the dry tap runs the same chain**: `OversamplerCascade<4>` (or `Oversampler2x`), L and
  C5, without the shape. The cascade is not linear-phase, so only an identical chain matches. It
  runs only while it is heard (Mix below 100 % or moving, Listen › Delta) and starts from rest:
  its settling is weighted by the Mix smoother's near-zero dry gain (**inference**).
- **Measured (simulation, 44.1 kHz Normal, full chain, Drive 0, |Mix 50 % − Mix 100 %| over
  20 Hz–20 kHz):** at a −40 dBFS sine ≤ 0.0027 dB (Warm), 0.0007 (Iron, Edge); at −20 dBFS
  0.029 (Warm), **0.069** (Iron, whose core already saturates a little at 20 Hz), 0.0005 (Edge);
  at −6 dBFS 0.20, 1.37, 0.013. The gate must use a small signal: what it tests is the dry path,
  not the curves (*Gates*).

## Latency

Character's share of the latency, added to Normal's 10 ms of look-ahead (`PLAN.md`, *Latency*);
the same with Character on or off (off: the integer delay).

| Host rate | Factor | Normal: samples, ms | Live: group delay (not reported) |
|---|---|---|---|
| 44.1 kHz | 4× | 76, 1.723 ms | 4.9 samples, 0.11 ms |
| 48 kHz | 4× | 76, 1.583 ms | 4.9 samples, 0.10 ms |
| 88.2 kHz | 2× | 70, 0.794 ms | 4.6 samples, 0.05 ms |
| 96 kHz | 2× | 70, 0.729 ms | 4.6 samples, 0.05 ms |
| 176.4 kHz | 1× | 3, 0.017 ms | 3 samples |
| 192 kHz | 1× | 3, 0.016 ms | 3 samples |

Live's figures were measured on a port of the classes' processing (the method of
`HalfBandPair::latencySamples()`): `Oversampler2x`'s pair 3.07 samples, as Inkcap's NOTES give.

## Switching

- **Character on/off:** equal-gain crossfade over 30 ms between Character and its integer delay
  (Normal: identical at small signals). Switching on starts the chain from rest; the fade starts
  once the chain's latency has passed, so the fade never hears the filters fill.
- **Mode (Warm, Iron, Edge):** both shapes run on the shared up-sampled signal for 30 ms and are
  crossfaded before C5; the linear parts are identical, so the fade cannot comb.
- **Placement Pre ↔ Post:** two Character chains for 30 ms (CPU: *CPU*), crossfaded.
- **Latency setting:** `PLAN.md`'s fade out, switch, fade in.

## Measured in M0

Method (**measured (simulation)**): full-scale sines (peak 1.0) at coherent frequencies (an odd
bin of a 2¹⁵-sample period, so every alias lands on its own bin), periodic steady state, the
oversampling filters applied as their exact responses on the FFT grid, the shape in the time
domain at the oversampled rate with second-order ADAA, C5 included, Warm's bias at its
steady-state value. Worst alias = the largest bin at or below 20 kHz that is neither a harmonic
nor DC, re the fundamental. Drive 0, 12, 24, and **+30**, which is the curves at a 0 dB input
offset instead of −6 (reported, not gated).

### Worst alias ≤ 20 kHz, fundamentals swept 200 Hz … 5 kHz in 100 Hz steps (the gate)

| Rate, factor | Mode | Drive 0 | Drive 12 | Drive 24 | +30 (reported) |
|---|---|---|---|---|---|
| 44.1 kHz 4×, Normal | Warm | −140.1 @ 0.5k | −134.9 @ 4.3k | −106.2 @ 4.9k | −78.6 @ 4.9k |
| 44.1 kHz 4×, Normal | Iron | −248.7 @ 4.5k | −233.1 @ 4.9k | −185.2 @ 4.9k | −161.7 @ 4.9k |
| 44.1 kHz 4×, Normal | Edge | −139.5 @ 0.5k | −129.0 @ 4.9k | −90.5 @ 4.5k | −78.7 @ 4.8k |
| 48 kHz 4×, Normal | Warm | −137.0 @ 0.6k | −136.1 @ 4.7k | −111.1 @ 4.8k | −85.2 @ 4.8k |
| 48 kHz 4×, Normal | Iron | −248.1 @ 4.6k | −248.1 @ 4.6k | −224.1 @ 4.1k | −189.0 @ 4.1k |
| 48 kHz 4×, Normal | Edge | −136.4 @ 0.6k | −146.8 @ 3.4k | −94.9 @ 4.7k | −84.0 @ 5.0k |
| 88.2 kHz 2×, Normal | Warm | −139.8 @ 0.5k | −181.4 @ 0.3k | −106.2 @ 4.8k | −78.7 @ 4.9k |
| 88.2 kHz 2×, Normal | Iron | −253.7 @ 4.9k | −253.7 @ 4.9k | −253.7 @ 4.9k | −249.7 @ 4.0k |
| 88.2 kHz 2×, Normal | Edge | −139.2 @ 0.5k | −171.1 @ 4.8k | −90.5 @ 4.5k | −78.7 @ 4.8k |
| 96 kHz 2×, Normal | Warm | −137.0 @ 0.6k | −164.5 @ 0.3k | −111.2 @ 4.8k | −85.2 @ 4.8k |
| 96 kHz 2×, Normal | Iron | −254.5 @ 3.9k | −254.5 @ 3.9k | −254.5 @ 3.9k | −254.5 @ 3.9k |
| 96 kHz 2×, Normal | Edge | −136.4 @ 0.6k | −178.3 @ 5.0k | −94.9 @ 4.7k | −84.0 @ 5.0k |
| 176.4 kHz 1× | Warm | −140.0 @ 0.5k | −178.8 @ 0.3k | −106.3 @ 4.9k | −78.8 @ 4.9k |
| 176.4 kHz 1× | Iron | −260.0 @ 4.3k | −260.0 @ 4.3k | −260.0 @ 4.3k | −260.0 @ 4.3k |
| 176.4 kHz 1× | Edge | −139.5 @ 0.5k | −171.4 @ 4.8k | −90.6 @ 4.5k | −79.0 @ 4.8k |
| 192 kHz 1× | Warm | −136.8 @ 0.6k | −164.8 @ 0.3k | −111.0 @ 4.8k | −85.1 @ 4.8k |
| 192 kHz 1× | Iron | −261.8 @ 3.9k | −261.8 @ 3.9k | −261.8 @ 3.9k | −261.8 @ 3.9k |
| 192 kHz 1× | Edge | −136.2 @ 0.6k | −178.2 @ 5.0k | −94.8 @ 5.0k | −83.9 @ 5.0k |
| 44.1 kHz 4×, Live | Warm | −140.0 @ 0.5k | −126.2 @ 4.1k | −106.2 @ 4.9k | −78.6 @ 4.9k |
| 44.1 kHz 4×, Live | Iron | −248.7 @ 4.5k | −231.0 @ 4.9k | −183.2 @ 4.9k | −159.6 @ 4.9k |
| 44.1 kHz 4×, Live | Edge | −139.5 @ 0.5k | −122.2 @ 4.9k | −90.5 @ 4.5k | −78.7 @ 4.8k |
| 48 kHz 4×, Live | Warm | −137.0 @ 0.6k | −126.6 @ 4.8k | −111.1 @ 4.8k | −85.2 @ 4.8k |
| 48 kHz 4×, Live | Iron | −248.1 @ 4.6k | −248.1 @ 4.6k | −219.9 @ 4.1k | −184.9 @ 4.1k |
| 48 kHz 4×, Live | Edge | −136.4 @ 0.6k | −132.6 @ 3.2k | −94.9 @ 4.7k | −84.0 @ 5.0k |
| 96 kHz 2×, Live | Warm | −137.0 @ 0.6k | −164.5 @ 0.3k | −111.2 @ 4.8k | −85.2 @ 4.8k |
| 96 kHz 2×, Live | Iron | −254.5 @ 3.9k | −254.5 @ 3.9k | −254.5 @ 3.9k | −254.5 @ 3.9k |
| 96 kHz 2×, Live | Edge | −136.4 @ 0.6k | −171.3 @ 4.6k | −94.9 @ 4.7k | −84.0 @ 5.0k |

Entries: worst dBc @ the fundamental (kHz) where it occurs. The Drive 0 entries are second-order
ADAA's numerical floor (ε = 10⁻⁴), not the curves' aliasing; first order reads −170 dBc and lower
there.

The named fundamentals miss the worst spots: Edge (k = 2) with first-order ADAA read −79.8 dBc at
5 kHz but −75.5 at 4.8 kHz (44.1 kHz, Drive 24). The gate sweeps (*Gates*).

### Worst alias ≤ 20 kHz at the named fundamentals (dBc; 1, 3, 5, 8, 12 kHz)

| Rate, factor | Mode | Drive | 1 kHz | 3 kHz | 5 kHz | 8 kHz | 12 kHz |
|---|---|---|---|---|---|---|---|
| 44.1 kHz 4×, Normal | Warm | 0 | −160.1 | −187.6 | −190.1 | −158.7 | −156.7 |
| 44.1 kHz 4×, Normal | Warm | +12 | −204.2 | −156.1 | −142.8 | −133.0 | −124.4 |
| 44.1 kHz 4×, Normal | Warm | +24 | −160.6 | −131.5 | −110.4 | −93.9 | −75.6 |
| 44.1 kHz 4×, Normal | Warm | +30 | −146.4 | −98.2 | −83.0 | −76.2 | −72.7 |
| 44.1 kHz 4×, Normal | Iron | 0 | −265.6 | −256.0 | −251.6 | −251.6 | −214.6 |
| 44.1 kHz 4×, Normal | Iron | +12 | −265.6 | −256.0 | −251.2 | −251.6 | −190.6 |
| 44.1 kHz 4×, Normal | Iron | +24 | −265.6 | −253.3 | −203.3 | −217.3 | −166.6 |
| 44.1 kHz 4×, Normal | Iron | +30 | −249.7 | −207.4 | −179.8 | −193.5 | −154.6 |
| 44.1 kHz 4×, Normal | Edge | 0 | −159.6 | −187.0 | −188.6 | −174.9 | −162.7 |
| 44.1 kHz 4×, Normal | Edge | +12 | −207.2 | −146.8 | −147.7 | −130.9 | −121.0 |
| 44.1 kHz 4×, Normal | Edge | +24 | −160.8 | −115.9 | −95.7 | −98.4 | −88.8 |
| 44.1 kHz 4×, Normal | Edge | +30 | −139.8 | −98.3 | −85.5 | −91.3 | −77.6 |
| 48 kHz 4×, Normal | Warm | 0 | −157.9 | −185.4 | −197.6 | −170.9 | −181.1 |
| 48 kHz 4×, Normal | Warm | +12 | −201.9 | −176.2 | −150.7 | −141.8 | −152.1 |
| 48 kHz 4×, Normal | Warm | +24 | −168.7 | −132.8 | −115.0 | −103.7 | −87.6 |
| 48 kHz 4×, Normal | Warm | +30 | −140.9 | −108.6 | −92.9 | −80.0 | −78.4 |
| 48 kHz 4×, Normal | Iron | 0 | −270.1 | −259.6 | −255.5 | −249.6 | −242.0 |
| 48 kHz 4×, Normal | Iron | +12 | −270.3 | −259.6 | −255.5 | −249.6 | −238.7 |
| 48 kHz 4×, Normal | Iron | +24 | −272.5 | −259.9 | −241.0 | −245.7 | −216.2 |
| 48 kHz 4×, Normal | Iron | +30 | −276.0 | −246.5 | −205.6 | −224.9 | −204.4 |
| 48 kHz 4×, Normal | Edge | 0 | −157.4 | −184.8 | −197.0 | −210.1 | −188.4 |
| 48 kHz 4×, Normal | Edge | +12 | −218.7 | −160.7 | −151.2 | −143.6 | −111.9 |
| 48 kHz 4×, Normal | Edge | +24 | −162.2 | −118.1 | −94.9 | −104.2 | −81.1 |
| 48 kHz 4×, Normal | Edge | +30 | −139.5 | −102.0 | −84.0 | −93.2 | −77.7 |
| 96 kHz 2×, Normal | Warm | 0 | −158.5 | −185.4 | −197.6 | −210.8 | −219.0 |
| 96 kHz 2×, Normal | Warm | +12 | −202.0 | −231.8 | −229.1 | −203.3 | −150.9 |
| 96 kHz 2×, Normal | Warm | +24 | −224.9 | −154.3 | −115.1 | −103.8 | −87.6 |
| 96 kHz 2×, Normal | Warm | +30 | −188.8 | −108.8 | −92.9 | −80.0 | −78.4 |
| 96 kHz 2×, Normal | Iron | 0 | −266.3 | −262.0 | −270.1 | −258.1 | −253.5 |
| 96 kHz 2×, Normal | Iron | +12 | −266.3 | −262.0 | −270.1 | −258.1 | −253.5 |
| 96 kHz 2×, Normal | Iron | +24 | −266.3 | −262.0 | −269.9 | −258.1 | −253.5 |
| 96 kHz 2×, Normal | Iron | +30 | −264.6 | −259.2 | −258.2 | −258.2 | −246.6 |
| 96 kHz 2×, Normal | Edge | 0 | −157.9 | −184.8 | −197.0 | −210.2 | −211.7 |
| 96 kHz 2×, Normal | Edge | +12 | −224.4 | −216.1 | −178.3 | −146.6 | −111.9 |
| 96 kHz 2×, Normal | Edge | +24 | −197.8 | −118.2 | −95.0 | −104.1 | −81.1 |
| 96 kHz 2×, Normal | Edge | +30 | −139.3 | −102.2 | −84.0 | −93.1 | −77.6 |
| 192 kHz 1× | Warm | 0 | −157.9 | −184.2 | −199.7 | −210.8 | −218.2 |
| 192 kHz 1× | Warm | +12 | −202.0 | −227.5 | −237.9 | −203.1 | −152.0 |
| 192 kHz 1× | Warm | +24 | −222.7 | −154.6 | −115.0 | −103.6 | −87.6 |
| 192 kHz 1× | Warm | +30 | −201.3 | −109.0 | −92.8 | −79.9 | −78.4 |
| 192 kHz 1× | Iron | 0 | −282.1 | −277.9 | −263.3 | −261.1 | −263.0 |
| 192 kHz 1× | Iron | +12 | −282.1 | −277.9 | −263.3 | −261.1 | −263.0 |
| 192 kHz 1× | Iron | +24 | −282.2 | −277.9 | −263.3 | −261.1 | −263.0 |
| 192 kHz 1× | Iron | +30 | −280.5 | −277.9 | −263.3 | −261.1 | −263.0 |
| 192 kHz 1× | Edge | 0 | −157.3 | −183.6 | −199.1 | −210.3 | −217.6 |
| 192 kHz 1× | Edge | +12 | −220.0 | −242.2 | −178.2 | −146.7 | −111.9 |
| 192 kHz 1× | Edge | +24 | −205.0 | −118.5 | −94.8 | −104.2 | −81.0 |
| 192 kHz 1× | Edge | +30 | −139.8 | −102.4 | −83.9 | −93.3 | −77.5 |
| 44.1 kHz 4×, Live | Warm | 0 | −160.0 | −186.9 | −181.2 | −146.4 | −135.5 |
| 44.1 kHz 4×, Live | Warm | +12 | −203.8 | −142.1 | −136.8 | −119.9 | −114.5 |
| 44.1 kHz 4×, Live | Warm | +24 | −136.4 | −115.3 | −110.4 | −93.9 | −75.6 |
| 44.1 kHz 4×, Live | Warm | +30 | −122.2 | −98.2 | −83.0 | −76.2 | −72.7 |
| 44.1 kHz 4×, Live | Iron | 0 | −265.6 | −256.0 | −251.6 | −251.6 | −202.1 |
| 44.1 kHz 4×, Live | Iron | +12 | −265.6 | −256.0 | −236.4 | −248.2 | −178.1 |
| 44.1 kHz 4×, Live | Iron | +24 | −265.6 | −241.7 | −188.5 | −200.3 | −154.1 |
| 44.1 kHz 4×, Live | Iron | +30 | −234.8 | −195.8 | −164.9 | −176.4 | −142.2 |
| 44.1 kHz 4×, Live | Edge | 0 | −159.5 | −186.3 | −172.2 | −158.9 | −142.7 |
| 44.1 kHz 4×, Live | Edge | +12 | −190.3 | −132.7 | −131.5 | −118.2 | −114.0 |
| 44.1 kHz 4×, Live | Edge | +24 | −135.4 | −111.8 | −95.7 | −98.4 | −88.8 |
| 44.1 kHz 4×, Live | Edge | +30 | −118.2 | −98.3 | −85.5 | −91.3 | −77.6 |

### First order, for the record (worst over 200 Hz … 5 kHz, Normal)

| Rate | Mode | Drive 0 | Drive 12 | Drive 24 | +30 |
|---|---|---|---|---|---|
| 44.1 kHz 4×, Normal | Warm | −180.2 @ 4.4k | −133.9 @ 4.4k | −85.9 @ 4.8k | −60.4 @ 4.9k |
| 48 kHz 4×, Normal | Warm | −180.6 @ 4.8k | −134.4 @ 4.8k | −91.6 @ 4.8k | −65.6 @ 4.8k |
| 44.1 kHz 4×, Normal | Edge | −171.8 @ 4.9k | −130.7 @ 4.9k | −71.7 @ 4.8k | −59.3 @ 4.8k |
| 48 kHz 4×, Normal | Edge | −204.1 @ 4.8k | −144.7 @ 4.8k | −73.9 @ 5.0k | −62.6 @ 5.0k |

First order without droop compensation, Edge at k = 2.5.

### 5 … 12 kHz: aliases and ANMR (fundamentals in 250 Hz steps)

| Rate, factor | Mode | Drive | Worst alias (dBc) | ANMR at 60 dB SPL (worst band) | at 75 | at 90 |
|---|---|---|---|---|---|---|
| 44.1 kHz 4×, Normal | Warm | +12 | −120.5 | −78.5 (band −65.0) | −63.8 (band −50.0) | −48.8 (band −35.0) |
| 44.1 kHz 4×, Normal | Warm | +24 | −75.6 | −38.1 (band −24.2) | −23.1 (band −9.2) | −8.1 (band 5.8) |
| 44.1 kHz 4×, Normal | Warm | +30 | −70.4 | −31.1 (band −17.1) | −16.1 (band −2.1) | −1.1 (band 12.9) |
| 44.1 kHz 4×, Normal | Edge | +12 | −105.8 | −65.3 (band −51.4) | −50.3 (band −36.4) | −35.3 (band −21.4) |
| 44.1 kHz 4×, Normal | Edge | +24 | −71.5 | −32.3 (band −18.3) | −17.3 (band −3.3) | −2.3 (band 11.7) |
| 44.1 kHz 4×, Normal | Edge | +30 | −65.4 | −25.9 (band −12.0) | −10.9 (band 3.0) | 4.1 (band 18.0) |
| 48 kHz 4×, Normal | Warm | +12 | −124.5 | −84.3 (band −70.5) | −69.3 (band −55.5) | −54.3 (band −40.5) |
| 48 kHz 4×, Normal | Warm | +24 | −77.4 | −38.2 (band −24.2) | −23.2 (band −9.2) | −8.2 (band 5.8) |
| 48 kHz 4×, Normal | Warm | +30 | −70.8 | −31.0 (band −17.1) | −16.1 (band −2.1) | −1.1 (band 12.9) |
| 48 kHz 4×, Normal | Edge | +12 | −111.9 | −73.4 (band −59.4) | −58.4 (band −44.4) | −43.4 (band −29.4) |
| 48 kHz 4×, Normal | Edge | +24 | −78.9 | −41.3 (band −27.4) | −26.3 (band −12.4) | −11.3 (band 2.6) |
| 48 kHz 4×, Normal | Edge | +30 | −71.1 | −31.8 (band −17.8) | −16.8 (band −2.8) | −1.8 (band 12.2) |
| 96 kHz 2×, Normal | Warm | +12 | −148.2 | −112.8 (band −99.5) | −98.3 (band −84.5) | −83.4 (band −69.5) |
| 96 kHz 2×, Normal | Warm | +24 | −77.4 | −38.2 (band −24.2) | −23.2 (band −9.2) | −8.2 (band 5.8) |
| 96 kHz 2×, Normal | Warm | +30 | −70.8 | −31.0 (band −17.1) | −16.0 (band −2.1) | −1.0 (band 12.9) |
| 96 kHz 2×, Normal | Edge | +12 | −111.9 | −73.4 (band −59.5) | −58.4 (band −44.5) | −43.4 (band −29.5) |
| 96 kHz 2×, Normal | Edge | +24 | −78.9 | −41.3 (band −27.4) | −26.3 (band −12.4) | −11.3 (band 2.6) |
| 96 kHz 2×, Normal | Edge | +30 | −71.0 | −31.7 (band −17.8) | −16.7 (band −2.8) | −1.7 (band 12.2) |
| 192 kHz 1× | Warm | +12 | −148.3 | −115.0 (band −102.8) | −100.0 (band −87.8) | −85.2 (band −73.1) |
| 192 kHz 1× | Warm | +24 | −77.4 | −38.2 (band −24.2) | −23.2 (band −9.2) | −8.2 (band 5.8) |
| 192 kHz 1× | Warm | +30 | −70.8 | −31.1 (band −17.1) | −16.1 (band −2.1) | −1.1 (band 12.9) |
| 192 kHz 1× | Edge | +12 | −111.9 | −73.4 (band −59.4) | −58.4 (band −44.4) | −43.4 (band −29.4) |
| 192 kHz 1× | Edge | +24 | −78.9 | −41.4 (band −27.4) | −26.4 (band −12.4) | −11.4 (band 2.6) |
| 192 kHz 1× | Edge | +30 | −71.1 | −31.8 (band −17.9) | −16.8 (band −2.9) | −1.8 (band 12.1) |
| 44.1 kHz 4×, Live | Warm | +12 | −111.5 | −65.4 (band −51.5) | −50.5 (band −36.5) | −35.5 (band −21.5) |
| 44.1 kHz 4×, Live | Warm | +24 | −75.6 | −38.1 (band −24.2) | −23.1 (band −9.2) | −8.1 (band 5.8) |
| 44.1 kHz 4×, Live | Warm | +30 | −70.4 | −31.1 (band −17.1) | −16.1 (band −2.1) | −1.1 (band 12.9) |
| 44.1 kHz 4×, Live | Edge | +12 | −105.8 | −65.3 (band −51.4) | −50.3 (band −36.4) | −35.3 (band −21.4) |
| 44.1 kHz 4×, Live | Edge | +24 | −71.5 | −32.3 (band −18.3) | −17.3 (band −3.3) | −2.3 (band 11.7) |
| 44.1 kHz 4×, Live | Edge | +30 | −65.4 | −25.9 (band −12.0) | −10.9 (band 3.0) | 4.1 (band 18.0) |

Iron, every configuration: ANMR at 60 dB SPL ≤ −106.4 dB.

Worst over the fundamentals 5 … 12 kHz, each figure maximised separately; ANMR in dB. Drive 0
was not part of the final run; an earlier run (Edge at k = 2.5, without C5) read ANMR ≤ −65 dB at
90 dB SPL there.

## The ANMR procedure (for M3)

`PLAN.md` gates 5–12 kHz perceptually: A-weighted noise-to-mask ratio ≤ −20 dB, twice Kahles et
al.'s −10 dB audibility margin. Kahles §3 (**source**) defines the residual; the masking threshold
is "simplified" after Lehtonen et al. 2012, whose model was not read, so Basalt specifies its own
(**Basalt's design**, *Not verified*). For one fundamental f0, mode, Drive and rate:

1. **Render** a full-scale sine through Character at the host rate, steady state. In
   `BasaltMeasure`: f0 on an odd bin of N = 2¹⁶ samples (coherent sampling, rectangular window)
   after 0.5 s of settling. Kahles window 1 s with a 120 dB Chebyshev window instead; with
   coherent sampling the bins are exact and no window is needed.
2. **Split** the spectrum: the harmonic bins k·k0 (k·k0 < N/2) are the masker, every other bin
   except DC is the residual (aliases and noise). Kahles resynthesise and subtract; on a coherent
   grid that is the same split.
3. **Calibrate:** a full-scale sine = **60 dB SPL** (the level of Lehtonen et al.'s listening
   tests, the origin of the −10 dB criterion). Power per bin P = (|Y|²/2)·10^(60/10), in units of
   (20 µPa)².
4. **A-weight** masker and residual:

   ```
   A(f) = 20·log10[ 12194²·f⁴ / ((f² + 20.6²)·√((f² + 107.7²)(f² + 737.9²))·(f² + 12194²)) ] + 2.00 dB
   ```

5. **Bands:** Bark z(f) = 13·atan(0.00076·f) + 3.5·atan((f/7500)²); bands one Bark wide from
   z = 0 to z(20 kHz) (the last one partial), each covering the bins whose z falls in it.
6. **Mask** in band j (centre z_j): M_j = Σ_i P_i·10^((SF(z_j − z_i) − O_j)/10) + T_q(f_j), over
   the masker's harmonics i, with the spreading function
   SF(Δz) = 15.81 + 7.5·(Δz + 0.474) − 17.5·√(1 + (Δz + 0.474)²) dB, the tonal-masker offset
   O_j = 14.5 + z_j dB, and the threshold in quiet
   T_q(f) = 3.64·(f/kHz)^−0.8 − 6.5·e^(−0.6·(f/kHz − 3.3)²) + 10^−3·(f/kHz)⁴ dB SPL at the band's
   mean bin frequency (f clamped to ≥ 20 Hz), unweighted.
7. **Noise** in band j: N_j = Σ of the residual's A-weighted P over the band's bins.
8. **ANMR** = 10·log10(mean over bands of N_j/M_j) (Brandenburg's band-averaged NMR). Report also
   the worst band, 10·log10(max N_j/M_j).

Sanity check (measured, 44.1 kHz): a 12 kHz full-scale tone with one synthetic alias reads, at
60 dB SPL, ANMR −31.3 dB for an alias at 3 kHz and −80 dBc, −51.3 dB at −100 dBc, and −55.2 dB
for an alias at 11 kHz and −80 dBc; at 90 dB SPL −1.3, −21.3 and −52.5 dB. Near the masker the
calibration hardly matters (masking); far from it the alias is judged against the threshold in
quiet, and the figure moves with the calibration dB for dB.

The calibration decides the outcome (*Measured in M0*): at 60 dB SPL every mode passes with
≥ 12.3 dB of margin at Drive 24 (≥ 5.9 dB at +30); at 75 dB SPL Edge at 44.1/88.2/176.4 kHz
misses by 2.7 dB at Drive 24; at 90 dB SPL Warm and Edge above ≈ 10 kHz at Drive 24 are predicted
audible in silence. Basalt gates at 60 dB SPL, the calibration the criterion was validated at,
and reports 75 and 90. This calibration was chosen after the results were known, so it is a
**provisional decision** (`PLAN.md`, Settled): the alternative is 8× oversampling with
second-order ADAA, which passes even at 90 dB SPL (below) at 1.0–1.6 % of a core. M3 reads
Lehtonen et al.'s mask model; if it differs, M3 replaces steps 5–6 with it, re-measures, and
revisits the choice.
What would pass at 90 dB SPL: 8× with second-order ADAA (worst −33.6 dB, measured without C5),
at 1.0 % of a core, 1.6 % with a dry chain (*CPU*).

## CPU

**Measured (C++ sketch)**, 48 kHz stereo, 20 s, this machine, one core: the folded half-bands
(K = 34, 6), a tabulated degree-7 F2 per oversampled sample, C5, both channels.

| Path | % of a core |
|---|---|
| 4×, first-order ADAA, wet only | 0.65 |
| **4×, second-order ADAA, wet only (Normal, Basalt)** | **0.69** |
| 4×, second-order ADAA, wet and a second (dry) chain | 1.12 |
| 8×, second-order ADAA, wet only | 1.00 |
| 8×, second-order ADAA, wet and dry chain | 1.61 |
| Live: `OversamplerCascade<4>` (the repository's class), wet with `std::tanh` and dry chain | 0.64 |

The filters dominate; second order adds 0.04 %. Normal needs no dry chain (*The dry path*), so
Character in Normal is about 0.7 % against `PLAN.md`'s 1.5 %; a Placement switch (two chains for
30 ms) about 1.4 %. M1's `[.perf]` test measures the real code.

## Gates

`PLAN.md`'s gates, as they apply to Character. Every rate means 44.1, 48, 88.2, 96, 176.4 and
192 kHz; every mode means Warm, Iron, Edge; both latencies unless stated.

| Gate | Character's test | Pass | M0 simulation |
|---|---|---|---|
| **Identity** | Character Off, gain exactly 1 | bit-identical to the latency-aligned input (the core's gate) | by construction (Off is a delay) |
| **Aliasing ≤ 5 kHz** | full-scale sine, coherent, fundamentals 200 Hz … 5 kHz in 100 Hz steps, Drive 0, 6, 12, 18, 24 (and 0 … 24 in 3 dB steps in `BasaltMeasure`), every mode, rate and latency; worst bin ≤ 20 kHz that is not harmonic or DC | ≤ −80 dBc | worst **−90.5 dBc** (Edge, 4.5 kHz, Drive 24, 44.1/88.2/176.4 kHz) |
| **Aliasing 5 … 12 kHz** | the same in 250 Hz steps; ANMR as specified above, 60 dB SPL | ANMR ≤ −20 dB; the dBc figure and 75/90 dB SPL reported | worst ANMR −32.3 dB (Edge, 44.1 kHz, Drive 24) |
| **Mix with Character** | Drive 0, −40 dBFS sine sweep 20 Hz … 20 kHz, Mix 50 % against 100 %, every mode | ±0.1 dB | ≤ 0.003 dB |
| **Small-signal transparency** | Character on, every Drive, −60 dBFS sine sweep, against Character off (Normal); for Edge, against Character off followed by the specified tilt at that Drive (the *Edge tilt* row tests the tilt itself) | ±0.01 dB, ±0.5° to 20 kHz | ±0.0022 dB, 0.0000° (the linear chain) |
| **Latency** | an impulse at −60 dBFS through Character on and off, Normal | the same integer delay as reported (76/70/3) | as specified |
| **Clicks** | Character on/off, mode switch, Placement, Link, Drive 0 ↔ 24 | no step artefact above −60 dB (Halite's method) | not simulated |
| **Warm** | 1 kHz sine, Drive 6 and 12, −30 … −6 dBFS peak | H2 > H3 at every level | H2 leads to u ≈ 4 (−6 dBFS at Drive 24) |
| **Warm DC** | 200 ms 1 kHz burst, −6 dBFS, Drive 12 | sub-20 Hz tail above −60 dB re peak ≤ 60 ms after the burst | 40 ms |
| **Iron law** | Drive 6, −18 dBFS RMS: THD at 60 Hz minus THD at 240 Hz | 24 ± 2 dB | 23.7 dB |
| **Iron DC** | 0.5 DC plus a 0.5 sine at 1 kHz, Drive 6, 2 s | bounded (‖y‖∞ ≤ ‖x‖∞), no drift, DC out ≤ DC in | 0.29 out for 0.5 in |
| **Edge** | 1 kHz sine, every Drive | even harmonics ≤ −120 dBc (symmetry) | ≤ −320 dBc |
| **Edge tilt** | Drive 0, 12, 24, −60 dBFS sine sweep | within ±0.05 dB of the specified shelf (t = 0, 1.5, 3 dB); exactly flat at Drive 0 | as specified |
| **Level** | 1 kHz, −60 dBFS, every Drive and mode | within ±0.01 dB of Character off; Edge of Character off followed by its tilt (+0.05 dB at Drive 6, +0.25 dB at 24) | as specified (compensation exact at small signals) |
| **Real-time** | `AllocationTests` with Character on, every mode | no allocation, lock or I/O | — |
| **CPU** | `[.perf]`, 48 kHz stereo, Character on | ≤ 1.5 % (`PLAN.md`) | 0.69 % Normal, 0.64 % Live (sketches) |
| **Goldens** | one render per mode at Drive 6 and 24, Pre and Post | re-recorded only for an intended change | — |

## Proposed changes to PLAN.md

1. *Character*, Antialiasing, and Settled 21: **second-order ADAA** (Bilbao et al. 2017) for Warm
   and Edge, not first order; Iron antialiased by its own flux difference. First order fails the
   gate (Edge −71.7 dBc at 4.8 kHz, 44.1 kHz, Drive 24) or keeps 5.9 dB of margin (Warm); second
   order passes with ≥ 10 dB and costs 0.04 % more.
2. *Character*, Dry path: in **Normal the dry tap is the integer delay** (Character's linear path
   is a pure delay of 76/70/3 samples within ±0.002 dB); only **Live** runs the dry tap through
   the same chain. Strike "a 50/50 Mix loses about 0.6 dB at 20 kHz" and "differs from Character
   off by a quarter of a sample".
3. *Character*, Half-sample delay and droop: the droop is undone by **a 5-tap symmetric FIR at the
   oversampled rate** (C5), not a high shelf after the downsampler; with it the delay is an
   integer.
4. *Latency* / *Signal flow*: Character's share is **76 samples at 44.1/48 kHz, 70 at 88.2/96,
   3 at 176.4/192** (Normal); Live's unreported group delay 4.9 / 4.6 / 3 samples.
5. *Character*, Link to Threshold: **raising Threshold raises Drive**, D_eff = clamp(Drive +
   0.5·(Threshold + 18), 0, 24), stateless; replace "so the output level holds" with "so one
   movement trades compression for saturation; Auto Gain keeps the level". *Controls*, Link to
   Threshold: the same.
6. *Measurement and quality gates*, Aliasing: sweep the fundamentals (200 Hz … 5 kHz in 100 Hz
   steps, 5 … 12 kHz in 250 Hz steps); the ANMR is calibrated at **60 dB SPL** with Basalt's mask
   model (this file), 75 and 90 dB SPL reported. *Mix with Character*: at **−40 dBFS**.
7. *Measured in planning*: add a line "*Measured in M0* (`src/dsp/character/NOTES.md`): with the
   real filters and second-order ADAA every configuration passes ≤ −80 dBc to 5 kHz (worst
   −90.5 dBc, Edge, 4.5 kHz, Drive 24); first order fails Edge (−71.7 dBc). The planning sim's
   drive scale is not recorded; Basalt's −6 dB input offset is in the NOTES."
8. *Character* table: Warm, Iron and Edge as specified here (tanh with an envelope bias; leaky
   flux with a core of residual slope 1 − λ; the rounded hard clip, k = 2.5, with a Drive-scaled
   presence tilt).
9. *Character*, Drive: keep small-signal compensation; record that a −18 dBFS RMS programme loses
   0.1–3.5 dB between Drive 0 and 24 (Warm) and that M3's listening decides whether a
   reference-level table replaces it.
10. *Performance budget*: Character measured as a sketch at 0.69 % (Normal) and 0.64 % (Live,
    with the dry chain); a Placement switch about 1.4 %.
11. *Shared infrastructure*, `common/dsp/`: **`FoldedHalfBand` moves from `OpAmpClip.h` to
    `Oversampler2x.h`** in M3 (a pure move, Viridian's goldens byte-identical); Character uses
    `LinearPhaseHalfBand`/`FoldedHalfBand` (Normal) and `OversamplerCascade<4>` (Live), all of
    which exist.
12. *Signal flow*: the internal key is the input **before Character** in both placements (as
    `../dynamics/NOTES.md`); with Pre the key delay includes Character's delay.
13. *References*: add Lehtonen et al. 2012 (abstract only) and Brandenburg 1987 (record only);
    mark Parker et al. 2016 and Kahles et al. 2019 as re-read in M0; note that Bilbao et al. 2017's
    full text was not reachable in M0.

## Not verified

- Bilbao et al. 2017's text: the second-order formula is the commonly quoted form; it was checked
  here (linear response, accuracy against the closed forms), not against the letter.
- Lehtonen et al. 2012's masking model, hence whether Basalt's ANMR equals Kahles's. The −10 dB
  criterion and the 60 dB SPL level are from the abstracts and Kahles §3.
- The psychoacoustic formulas (Bark after Zwicker and Terhardt, the spreading function after
  Schroeder, Atal and Hall, the threshold in quiet after Terhardt, the tonal offset after
  Johnston, A-weighting after IEC 61672-1) as written from memory of their common forms; M3
  checks A-weighting against the standard's table before gating.
- How the planning simulation scaled Drive (its +24 dB and this file's are not known to match).
- The CPU figures beyond this machine and the sketch; Warm's and Edge's sound, which no number
  here decides.
