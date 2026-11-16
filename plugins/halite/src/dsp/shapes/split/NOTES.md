# Split: the band filter

**Steep (M2b, 2026-11-14)**, Settled 38: a cut of two steep shelves that holds the full depth across
the band and next to none a third of an octave past it (*The edges*), for the Ess band and the Harsh
band. It replaces M2's magnitude-matched 2nd-order bell (*History*). The design comes from an offline study (Python
reference, scripts not committed: `split2_ref.py`, `vectors2.py`, `realised.py`), ported and checked
against its vectors; its modulation is not the study's, which clicked in the chain (*Modulation*).
Numbers marked *measured* come from `HaliteTests_reduction "[split]"`, `"[accuracy]"`, `"[clicks]"`,
`"[.report]"` and `"[.perf]"`, at 48 kHz unless stated.

## The interface

`SplitFilter` is everything the chain knows about Split; the Harsh bell is the same class on the
Harsh prototype.

- `SplitFilter (splitDesign::ess())` or `(splitDesign::harsh())`: the prototype, built for the top
  of its band's Range (24 and 12 dB, `Shapes.h`; the processor asserts they equal `Parameters.h`).
  A deeper depth is held there.
- `setBand (kind, low, high)`: a **bell**, the full depth over [low, high] and back to next to none a
  third of an octave past either edge (*The edges*), or a **high shelf** from low, the band the detector measures
  (`Detector::bandsFor`). `SplitFilter::kindFor` is the chain's rule: a shelf once the upper Detect
  handle reaches 0.4 · fs; a change between bell and shelf is a lane change (crossfaded).
- `process (x, removed, channels, depths, paths, n, phase, limit)`: **what the band removes**,
  `x − filtered (x)`, for one or two signals, with one depth for both (Linked: one design) or one
  each (Unlinked). Depth = 1 − the linear gain of the cut, a value every sample, with
  `lookahead (phase, n, limit)` more of them after the last sample. Within a segment the depth moves
  as the chain's does, continuous in value and slope (ReductionTrack's quadratic B-spline of hop
  values): the segment's end slope is read from its last two samples. A depth that steps there is
  taken as a step, not a slope (*A step is not a slope*), so any depth on any sample stays stable;
  only the chain's spline gets the click-free path. Depth 0 returns exactly 0 (a branch, not
  arithmetic), so the identity gate does not depend on the filter's state.
- `reset()`: state to zero, the band set last without a glide, the design at depth 0.

## The design (`SplitDesign.{h,cpp}`)

### The prototype

A **shelf** is `|H|² = (B − (1 − G) A) / (B − (1 − 1/G) A)` in `t = Ω² = (fc / f)²`, G the linear
gain: `A (t)` and `B (t)` are the squared numerator and denominator of an elliptic analog low-pass
(passband edge Ω = 1, stopband edge a third of an octave on, 2^(1/3)), i.e. `(1 + G ρ) / (1 + ρ / G)`
with `ρ = |H_LP|² / (1 − |H_LP|²)`. A **bell** over [low, high] is the cut shelf at `low` times a
boost shelf (1/G) at `high · 2^(1/3)`: the cut shelf holds its full depth from `low` up, the boost
shelf gives it back from a third of an octave past `high`, and each stays within 0.1 dB of none a
third of an octave on the other side (the analog prototype; the digital cut: *The edges*).

| | order | passband ripple | stopband | sections (bell) |
|---|---|---|---|---|
| Ess | 6 | 0.0026270336 dB | 28.3164228963 dB: 0.1 dB a third of an octave out at 24 dB | 6 SVFs |
| Harsh | 5 | 0.0100176110 dB | 22.0649609154 dB: the same at 12 dB | 4 SVFs, 2 one-poles |

The poles, zeros and gain are scipy's `signal.ellipap (order, rp, rs)` (1.11.4), embedded; the
polynomials are built from them. A bell is exact at DC (the two shelves' levels cancel); a shelf alone
keeps the even-order prototype's floor there, −0.1 dB at 24 dB (within the collateral gate).

**The edges** (the study, *Options* 2 below for the user): the full depth from Detect Low to Detect
High (in the band at 24 dB −23.86 … −24.00 dB; *measured* at 8 dB −7.98 … −7.99 dB). A third of an
octave past either edge the **analog** cut is within 0.1 dB of none at every depth, and exactly 0.1 dB
at the deepest (24 dB; Harsh 12): the stopband was chosen for it, so it has no margin there. The
**digital** cut, *measured* on the near-Nyquist grids (every band, every FFT bin from a third of an
octave past the edges to 0.45 · fs): within 0.066 dB up to 16 dB of cut at every rate; at 24 dB, 0.100
from 88.2 kHz, but **−0.146 dB at 44.1 kHz** (19052 Hz, Detect 5345 … 15119 Hz) and **−0.190 dB at
48 kHz** (21384 Hz, 6000 … 16971 Hz), where the boost shelf's first-order base no longer follows the
analog shelf near 0.45 · fs (as in the near-Nyquist gate's worst case). The Harsh bell sits on its
analog 0.100 dB at 12 dB, every rate. So the collateral gate holds to 16 dB of Range everywhere, and
fails at the deepest cuts for a band in the top octave at 44.1 and 48 kHz, by up to 0.09 dB near
0.45 · fs (`"[split]"`: held to 0.2 dB, the written gate under `[!mayfail]`). The transition lies in the
third of an octave past each edge, where neither gate measures. M2 put the half-depth points at the
edges instead.

### The base

Per edge, a first-order base in `x = sin² (π f / fs)`: `t = α / γ` with `α = (1 − x) + a1 x`,
`γ = c1 x`; `a1 = p (u)`, `c1 = q (u) / (π u)²`, `u = fc / fs`. p and q come from a minimax table of 69
nodes (u = i / 128, node 0 the u → 0 limit; the study's Nelder–Mead fit of the composed cut at the
deepest depth, per prototype), read by Catmull-Rom and clamped to its ends (u ≤ 0.53). A bell's
boost corner goes past that: Detect High stays under 0.4 fs, but `Detector::bandsFor` widens a narrow
band to a third of an octave, up to 0.45 fs, so the corner reaches 0.45 fs × 2^(1/3) = 0.567 fs (equal
handles of 19049 Hz at 48 kHz: 0.561 fs). Past u = 0.53, a1 and q are held at the last node while c1
still scales as 1 / u²; the near-Nyquist gate's grids hold such bands and pass. p and q depend on u
only: no fit per band, nothing to fit on the audio thread.

### The roots

The roots `t_j` of `B − (1 − G) A` at −24 … +24 dB (Harsh ±12) in 1 dB rows, a negative depth giving
the denominator's: one per conjugate pair (above the real axis), then the real root of an odd order.
They are followed from the depth-0 roots `−p²` by Newton continuation in 1/32 dB steps, once, when a
prototype is first used (not on the audio thread: the filter holds a reference), so each section
pairs a numerator root with the denominator root it starts from and is the identity at depth 0. At a
depth the rows are interpolated linearly (the study: within 0.0005 dB). *Measured*: every row is a
root to 10⁻¹³ of the polynomial's scale, and five rows per prototype match the study's within 10⁻¹⁰.

### Sections

Each root gives `|1 − w x|²` with `w = (1 − a1) + t c1`: a conjugate pair is one TPT SVF with M2's
formula from `P = (|1 − w_n|², −|w_n|² / 4)` and `Q` likewise from the denominator's root (its square
roots are non-negative for any w: |1 − w|, |w| and 1 form a triangle); a real root is a TPT one-pole,
`g = (1 − w_d)^(−1/2)`, `r = 1 − √((1 − w_n) / (1 − w_d))`. The cut shelf runs first, so the
intermediate signal never exceeds the input; the boost shelf swaps numerator and denominator roots.
A shelf alone carries the prototype's level, `√(lead (G) / lead (1/G))`; a bell 1.

## Modulation

### The design grid: one design pair per hop, a C1 path between

- **The design runs once per segment of 32 samples**, for the segment's last sample, and once more
  for the sample before it: their difference is the **slope**, the design's change per sample (of the
  depth and of a band on its glide). The coefficients (a1, a2, a3, r0, r1 per section, and the gain)
  follow the **cubic Hermite** from where they are, with the slope they had, to the new design with its
  slope, as forward differences (three additions per coefficient and sample); the segment's last sample
  runs its design exactly. The path is continuous in value and slope, so a fast depth does not click.
- **Segments lie on the hop grid of the reductions each sample reads**, so a segment never crosses a
  hop: the chain knows every depth of a segment once it knows its first (`../NOTES.md`, *Timing*:
  Normal has hop h's value when the hop's first sample leaves the delay, Live reads the newest complete
  hop), renders the depths to the segment's end (`SplitFilter::lookahead`), and the grid adds no lag.
  After a transport restart the audio in flight keeps the previous analysis's grid and its last segment
  ends at the restart (`DesignGrid`; a segment never reads a depth the new analysis has not decided):
  the frame in flight is the uninterrupted render bit for bit up to its last look-ahead, as M2's.
- **Why not the study's linear ramps** (a design every 16 samples, the coefficients in a straight
  line to it): the path's slope steps every segment, which the click gate's 3rd difference sees on the
  330 Hz sines, where a steep cut's phase below its band is not small (the removed part of the sine is
  29 dB under it at 8 dB). *Measured* in the chain: Live, Split, Detect automated every block −49.1 dB
  and Harsh on and off −58.2 dB (Split and Wide; gate −60). On the filter alone, a 330 Hz sine, crest
  in dB (a probe, scratch, not committed):

  | Coefficient path | Live jumps 0 ↔ 8 dB | Live jumps 0 ↔ 24 dB | a random depth every hop | a detector-like ess |
  |---|---|---|---|---|
  | a design every sample | −75.1 | −57.4 | −61.5 | −78.8 |
  | linear ramps every 4 / 8 / 16 samples | −62.0 / −57.4 / −54.8 | −34.5 / −31.8 / −27.9 | −38.5 / −33.5 / −28.7 | −75.2 / −73.3 / −70.9 |
  | quadratic B-spline through the designs, every 16 (lags 8 samples) | −46.5 | −42.6 | −68.5 | −78.8 |
  | Hermite, slopes from the designs before, every 16 | −67.7 | −52.7 | −54.1 | −77.5 |
  | **Hermite, the design's change over the last sample, every 32 (built)** | **−75.4** | **−59.6** | **−63.8** | **−78.8** |

  The built path clicks no more than a design every sample. (A depth that jumps 24 dB within a hop
  clicks even then: the cut itself moves that fast; the chain's Range 8 click scenarios read −76 dB.)
- **Deviation from a design every sample** (`"Split: ringing and modulation, report"`, the removed
  signal on the tests' ess, the chain's spline of hop depths, RMS / peak in dB): a detector-like ess
  (12 dB, 40 ms release) −81.1 / −64.5; Range moving every 512-sample block −75.3 / −61.2; a random
  depth every hop (0 … 24 dB) −40.9 / −33.3; a 24 dB step −55.4 / −30.8. (The study's linear ramps
  every 16: −60, −63, −33, −58 dB RMS.)
- **Cost:** two designs per hop while the depth or the band moves (none while both hold still), about
  75 ns each; the Hermite's three additions per coefficient and sample. One design serves both channels
  in Linked; Unlinked has a path per channel. The kernel runs a segment section by section, so a
  section's coefficients and state stay in registers, and the two signals run in the two lanes of one
  SIMD register (`juce::dsp::SIMDRegister<double>`: SSE2, NEON; a plain pair where JUCE has none), each
  coefficient holding its path's value per lane, so the two cubics step together too. One signal runs
  the same arithmetic in one lane, so the result is the sample-by-sample cascade's bit for bit whatever
  the number of signals; it is latency-bound (*CPU*).

### A step is not a slope

The end slope is the design's change over a segment's last sample, times the segment's length in the
cubic. On the chain's spline that change is at most 63/64 of the deepest depth over 32 samples; a depth
that steps between those two samples (or is noise within a segment) would read as a slope 32 times the
step, and the cubic overshot into unstable coefficients: a step to 24 dB on a segment's last sample
removed 604 times the input's peak, a random depth every sample 10⁹ times (the M2b review's probes,
reproduced). So a change over the last sample larger than the deepest depth over a segment's length
(`SplitFilter::steepestStep`) counts as a step: the segment ends level in depth (the band's glide keeps
its slope). With that, a step on a segment's last sample removes at most 0.70 of the input's peak, a
random depth every sample 0.86, every section's frozen spectral radius under 0.981 (*measured*,
`"[split]"`). On the chain's depths it never acts: the output is the same bit for bit (the golden renders
and a scratch run of the spline of random and 0 ↔ 24 dB hops against the filter without it), and the
cubic test runs on the chain's steepest spline.

### A new band

The edges (log2 Hz of the cut shelf's corner and the boost shelf's) glide in a straight line over
5 ms from where they are; a band that moves again sets off from where the edges are then (M2's fit cost
deferred changes to the end of each glide; a table lookup costs nothing). The design at a segment's end
and the slope see the glide; a glide's start or end is a corner in the band's path, as in M2.

### Ringing

Steep edges ring (the study; no gate measures it; *Options* 3). *Measured*, the removed impulse's energy
60 dB down: the default band 2.94 / 2.98 / 3.10 / 3.56 ms at 4 / 8 / 12 / 24 dB (M2's bell 0.23 ms
at 8 dB); 1.5 … 3 kHz 9.02 … 10.54 ms (M2 0.92 … 1.19); the shelf from 8 kHz 1.71 … 2.10 ms; the Harsh
band (3.2 kHz, 1 octave) 5.06 … 5.46 ms, (1.5 kHz, 0.3 octave) 9.44 … 9.90 ms.

## Measured (M2b, 2026-11-14)

### Split near Nyquist: pass

The gate: the bell and the shelf within 0.5 dB of their analog responses up to 0.45 · fs at
44.1 kHz. Measured on the real filter as in M2 (impulse responses at frozen depths, now 16384 samples
at 44.1/48 kHz and doubled per octave of rate, for the ringing of the narrowest Harsh band; the last eighth under 10⁻¹²; every FFT bin from 20 Hz to
0.45 · fs and 0.45 · fs itself) against the analog prototype computed from the low-pass's poles and
zeros (not the design's polynomials or roots), over every pair of Detect handles on a
sixth-of-an-octave grid and every Harsh band, 0.25 … 24 dB (Harsh 12):

| Rate | Bell | Shelf | Harsh |
|---|---|---|---|
| 44.1 kHz | **−0.425 dB** (19845 Hz; 13470 … 16971 Hz band, 24 dB) | −0.067 dB (13986 Hz; 15751 Hz corner, 24 dB) | +0.025 dB (13375 Hz; 3000 … 12000 Hz, 12 dB) |
| 48 kHz | −0.460 dB (21600 Hz; 14271 … 17980 Hz, 24 dB) | −0.067 dB (15223 Hz; 17144 Hz, 24 dB) | +0.016 dB |
| 88.2 kHz | −0.041 dB | (none: 0.4 fs is past Detect) | +0.005 dB |
| 96 kHz | +0.033 dB | | −0.005 dB |
| 176.4 kHz | −0.012 dB | | −0.004 dB |
| 192 kHz | −0.009 dB | | −0.004 dB |

The worst case is always at the deepest cut, at 0.45 · fs, for a band whose boost corner lies near or
past Nyquist (the base's first order cannot follow the analog shelf there). M2's bell: +0.283 dB at
44.1 kHz. The default band at 44.1 kHz, measured / analog: at 8 dB −7.994 / −7.993 dB at 4.5 kHz,
−7.981 at 7348 Hz, −7.988 / −7.986 at 12 kHz, −0.013 at 3572 Hz, −0.008 / −0.005 at 15119 Hz; at 24 dB
−23.95 … −23.86 dB in the band, −0.097 / −0.100 at 3572 Hz, +0.082 at 1 kHz.

The study predicted −0.463 dB at worst (a fine scan of Detect High at 44.1 and 48 kHz) and −0.425 dB
on M2's grid at 44.1 kHz: 0.04 dB of margin to the gate. The test gates 44.1 kHz on sixths, 48 and
96 kHz on thirds.

**Test vectors:** the study's 23 cases (Ess and Harsh, 44.1–192 kHz, 0–30 dB, swapped and widened
handles, shelves, a depth past the deepest) pass: the band, both bases (10⁻¹²), every section's
coefficients (10⁻⁹), the shelf's level (10⁻⁶: the study interpolated it), the analog prototype at nine
frequencies (10⁻⁹ dB), the real filter's response against the study's design (10⁻⁴ dB) and within the
gate, and the default band's removed impulse (10⁻⁶: the depth reaches the filter as a float).

### Accuracy (Settled 38–40)

Settled 39 reads Spectral's accuracy on the band's own power; **M2b reads Split's the same way**, for
the user to confirm (*Options* 1): the power in the band's own bins (Detect Low … High, each weighted
by the detector's band filter, as Spectral weighs them) loses the reduction within ±0.5 dB, against
what it loses under the detector's reduction applied exactly (Wide's render of the same input, the same
window and averaging; read against the mean of the detector's hop reductions in dB, Wide itself sits
0.10 dB off). The landing on B is reported beside it, and checked against the written gate under
`[!mayfail]`. *Measured* (the tests' ess, 4.5 … 12 kHz noise with 8th-order edges over a vowel; dB):

| Over / Range | detector | own band: lost − asked | B lost | landing (out − expected) | M2 bell: landing |
|---|---|---|---|---|---|
| 3 / 8 | 3.55 | −0.00 | 3.49 | −0.53 | +0.19 |
| 6 / 8 | 6.55 | −0.01 | 6.45 | −0.51 | +0.93 |
| 12 / 8 | 8.00 | −0.01 | 7.92 | +0.08 | +5.82 (gate +4: +1.82) |
| 6 / 24 | 6.55 | −0.01 | 6.45 | −0.51 | +0.93 |
| 12 / 24 | 12.56 | −0.02 | 12.33 | −0.39 | +2.63 |
| 18 / 24 | 18.55 | −0.04 | 18.02 | −0.07 | +4.76 |
| 24 / 24 | 23.98 | −0.08 | 22.78 | +1.22 | |

The study predicted −0.00 … −0.08 own and −0.53, −0.50, +0.07, −0.50, −0.38, −0.07 landing: as
measured. Where the landing misses (3 and 6 dB over) it is Speed Auto's overshoot, as Wide's −0.56
(`../NOTES.md`): Split joins Wide's M3 item under the written reading. B loses less than the band at
depth because the test ess's skirts past the edges (3.55 % of B) are not cut.

**Across rates** (6 dB over, Range 8): the own band loses the ask within −0.01 dB at 44.1, 48, 88.2,
96, 176.4 and 192 kHz. Split's landing on B moves with the rate (−0.51 at 44.1 and 48 kHz, −0.43 at
96, −0.35 at 176.4, −0.29 at 192): the test ess's skirts, whose share of B changes with the rate, are
past the band; M2's consistency check (within 0.15 dB of 48 kHz's) misses at 176.4 (0.151) and 192 kHz
(0.218) and is kept under `[!mayfail]` with the written reading. Every shape's B lost − asked stays
within 0.1 dB of 48 kHz's.

**Harsh** (Settled 40: the band's power through the detector's Harsh band filter against the
detector's mean reduction, H reported), the bell in Split and Wide alike, 3 / 6 / 9 / 15 dB over (Range
4, 4, 12, 12): power −0.04, −0.03, −0.08, −0.06 dB (the M2 bell −0.31 … −1.48); H lands −0.14, +0.02,
−0.12, +0.05 dB (M2 +0.13, +0.37, +0.77, +1.36).

### Collateral

At 8 dB of Ess reduction, outside the band (a third of an octave and more from either edge), dB:

| Hz | 250 | 500 | 1000 | 1500 | 2250 | 3000 | 3572 | 15119 | 19000 |
|---|---|---|---|---|---|---|---|---|---|
| M2b | +0.001 | +0.005 | +0.012 | +0.007 | −0.007 | −0.003 | −0.012 | −0.006 | −0.000 |
| M2 bell | −0.012 | −0.047 | −0.190 | −0.432 | −0.985 | −1.777 | −2.541 | −2.380 | −1.478 |

The Harsh bell at 4 dB (3.2 kHz, 1 octave): −0.006 dB at 250 Hz, −0.015 at 500, +0.016 at 1131,
+0.003 at 1600, +0.002 at 1796, −0.000 at 5701, +0.006 at 9051, −0.006 at 16 kHz (M2 −1.03 at
1796 Hz). The chain's cut equals the design's response within 0.05 dB (500 Hz … 20 kHz, 8 dB). All
gated now (`"[collateral]"`). **At the deepest cuts** the reading fails near Nyquist: −0.146 / −0.190 dB
a third of an octave above a top-octave band at 44.1 / 48 kHz and 24 dB (*The edges*; within 0.066 dB
to 16 dB of cut at every rate).

### Real material (`HaliteMeasure --reduction`, m1 tune split, `../NOTES.md`)

Every ess the detector finds, through the chain; median [p5, p95], share of events within the gate:

| | VCTK, 2409 events | VocalSet, 18 |
|---|---|---|
| own band: lost − asked | −0.00 [−0.01, +0.02], 100 % (pass) | +0.00 [−0.00, +0.02], 100 % (pass) |
| B: lost − asked | −0.34 [−2.56, −0.05], 64.1 % (M2 −0.93, 26.5 %) | −0.34, 61.1 % |
| landing on B | +0.01 [−0.66, +2.74], 65.5 % (M2 +0.64, 42.6 %) | +0.11, 77.8 % |
| below / above the band (the third octave that moved most) | +0.006 [−0.032, +0.041] / −0.000; 99.3 / 100 % (M2 −0.45 / −0.64, 5.7 / 5.2 %) | 100 % |

Harsh episodes (VCTK 669, VocalSet 666): own band −0.00 / −0.01 dB median, 100 % within (against the
reduction applied hop by hop; Wide's Harsh path is a bell too); below the band 98.7 / 94.3 % within
0.1 dB, above 100 %. Read against the collateral gate (every event within 0.1 dB), **Split fails** on
VCTK (99.3 % of esses; within its reach 99.8 %) and **the Harsh bell fails** on both corpora (98.7 /
94.3 %); `HaliteMeasure` prints **fail** in its gate column. **The collateral outliers** (worst +0.35 dB below the Ess band, −0.64 dB below the
Harsh band on a sung tone) lie in third octaves 18–79 dB under the event's power. The mechanism, on a
synthetic sung tone through the Harsh bell (a probe): at a fixed depth a third octave below the band
moves by at most 0.06 dB (4 dB of cut) per 46 ms frame, with episodes of a moving depth up to 1.9 dB
(6.2 dB with vibrato) in the quietest third octaves, while over the whole tone none moves by more than
0.05 dB: a steep cut that moves rotates the phase below its band and rings, which puts a little energy
where there was almost none. No longer a magnitude change of the band's skirts (M2's −0.45 dB median),
but not zero on every event either (*Options* 4).

### Clicks

The click gate (`"[clicks]"`, 330 Hz sines, 3rd-difference crest): every Split case under −60 dB in
Normal and Live, worst −71.1 dB (Output automated every block, as in M2; the mechanisms' cases read as
M2's within 1.2 dB); Detect automated every block in Live −76.6 dB and Harsh on and off −77.1 (with the
study's straight lines −49.1 and −58.2). In the band (the 6 kHz envelope, not the plan's gate): nothing
changing −46.7 dB (M2 −47.5: the detector's own ripple), Detect swept every block −42.5 (M2 −42.4),
Stereo −44.8, Harsh on and off −55.2; `../NOTES.md`, *M2b*.

### CPU

`taskset -c 5 HaliteTests_reduction "[.perf]"` (after the review; core 5, as the before-run), % of one
core, stereo, 512-sample blocks, detector included, fastest of five passes over 10 s; load average
3.1 … 4.8. **All 198 readings within budget.** The review found an ess that never stops in Unlinked
missing from the table, and over budget there; the before-column is the code as M2b first shipped, on
the same core in the same session (the review's extra rows, 48 kHz):

| 48 kHz (96 kHz) | after the review | M2b as shipped | M2 | budget |
|---|---|---|---|---|
| Split, Linked | 0.368 (0.683) | 0.399 | 0.343 | 0.5 (1.0) |
| Split, Unlinked | 0.401 (0.754) | 0.469 | 0.346 | 0.5 |
| Split, Mid | 0.284 (0.545) | 0.270 (M2b's table) | 0.197 | 0.5 |
| Worst: Unlinked, an ess all the time | 0.432 (0.787) | **0.501 over** | | 0.5 |
| Worst: Unlinked, Detect swept every block, an ess all the time | **0.438** (0.812) | **0.507 over** | | 0.5 |
| Worst: Unlinked, an ess all the time, Harsh on | 0.696 (1.262) | 0.779 | | 0.8 |
| Worst: Unlinked, both bands cutting | 0.684 (1.247) | 0.770 | | 0.8 |
| Worst: Unlinked, Detect swept, an ess all the time, Harsh on | 0.700 (1.284) | 0.785 | | 0.8 |
| Harsh's share: Split / Wide, Linked | 0.252 / 0.256 (0.453 / 0.460) | 0.271 / 0.276 | 0.201 / 0.205 | 0.3 (0.6) |
| Split, Linked, Harsh on | 0.619 (1.136) | 0.670 | 0.544 | 0.8 |
| Worst: Linked, an ess all the time, Harsh on | 0.653 (1.173) | 0.699 | 0.574 | 0.8 |
| Worst: Linked, both bands cutting | 0.643 (1.159) | 0.689 | 0.564 | 0.8 |
| Worst: Linked, Detect swept every block, an ess all the time | 0.406 (0.743) | 0.435 | 0.421 | 0.5 |

The closest now: Split Unlinked, Detect swept over an ess that never stops, 88 % of its budget at
48 kHz (Harsh's share 84–85 %); 192 kHz Split 1.315 / Unlinked 1.466 % of 2.0. The costliest 512-sample
block of a Split case: at most 0.97 % of its duration at 48 kHz, 3.92 % at 192 kHz.

The filter alone (a probe, scratch: `-O3`, core 5; ns per channel-sample, the depth moving on the
chain's spline, before / after the two-lane kernel, bit for bit the same output): Linked 18.9 / 13.4,
Unlinked 23.7 / 16.5, Unlinked with the band swept 24.4 / 17.1, one signal 24.5 / 28.7. Unlinked had run
at two monos' cost: scalar code is throughput-bound there (two cubics and two recurrences per section
and sample), and the compiler did not pair them; in one register's two lanes they cost what Linked did.
One signal pays a little for the lane it leaves empty (Mid 0.284 % against 0.270), within its headroom.
Earlier: the first build (the study's straight lines every 16 samples, two mono filters per stereo
pair, coefficients stepped sample by sample in memory) read Split Unlinked 0.559 % and the worst case
0.501 % (at load average 11–16); one design pair per hop, a stereo pair in one filter and the
section-by-section kernel brought them to the M2b column.

### Golden renders

Re-recorded once (2026-11-14) for the intended sound change, exactly the six files that run Split's
filter: `split-voice-ess`, `split-voice-ess-harsh`, `split-allround-ess`, `split-allround-ess-harsh`
(the Ess cut, with the Harsh bell in the second of each) and `wide-voice-ess-harsh`,
`wide-allround-ess-harsh` (Wide carries the Harsh bell). Against the M2 files they differed by −9.2 to
−12.9 dB (Split) and −12.9 / −21.7 dB (Wide with Harsh) of what each removes. The other six (Spectral,
`wide-*-ess`) passed untouched before the re-recording and came out byte-identical; they were left as
they were.

### Tests and mutations

`"[split]"`, 15 cases (one `[!mayfail]`): the prototypes (their polynomials against the study, every root row a root, the
analog response from the polynomials equal to the one from the poles), the study's vectors, the
near-Nyquist gate and its sine cross-check, the collateral reading on the same grids (within 0.1 dB to 16 dB of
cut at every rate, the deepest held to 0.2 dB at 44.1/48 kHz, the written gate at the deepest under
`[!mayfail]`), depth 0 exactly 0 after cuts and through a band change,
smoothness of the design over depth, each segment's last sample running its own design exactly at any
chunk size, the coefficients between designs following the cubic (rebuilt from independent designs,
within 10⁻¹⁰, on a smooth depth and on the chain's steepest spline), two signals on one design or a design each equal to one filter per signal bit for bit, a
restart ending the segment without reading past it, stability on the chain's spline of a random depth
every hop with 0 ↔ deepest jumps and a new band every four hops or every hop at 44.1, 48 and 192 kHz
(finite, every section's frozen spectral radius under 1 at every sample, at most 0.997; the removed
part under 1.5 × the input's peak, at most 0.89 of it), a step on a segment's last sample and a random depth
every sample (*A step is not a slope*), the glide, the clamp past the deepest cut. (The M2b stability
test held one depth per hop on the segment grid, so its end slope was always 0: it could not see the
overshoot.) **Mutations:** 12 single-line mutations of the new code (the cubic made
quadratic, no slope from the depth, the restart limit ignored, no snap at the segment's end, a glide
twice as fast, the boost shelf's roots not swapped, the shelf's level dropped, the base table read off
its nodes, the grid's phase ignored, the grid not kept across a restart, no lookahead rendered by the
chain, Unlinked sharing one design), each built alone against a binary that passes: **all 12 caught**.
After the review, 5 more, each caught, among them the step guard removed, the guard at half its
threshold (it then acts on the chain's spline) and a still path's cubic steps left stale beside a
moving one.

## Not verified

- No listening, no DAW: the ringing (3–10 ms) and the transient energy below the band (*Real
  material*) are measured, not heard.
- The CPU figures on any other machine; the golden renders on Windows and macOS (compiled with other
  compilers' rounding; the gate is −60 dB of what each removes, so rounding passes). The two-lane
  kernel was run with SSE2 (the build), AVX2 and JUCE's no-SIMD fallback here, each bit for bit the
  scalar cascade's; not run with NEON (macOS, arm64) or MSVC, which CI builds.
- The base table's provenance: the minimax fit was run in the study (scripts not committed); its result
  is checked by the near-Nyquist gate on every band of the grids, not refitted here.

## History: M2's matched bell

M2 (2026-11-14) built a 2nd-order bell, its half-depth points at the edges (RBJ's peaking EQ), as
`(1 + G ρ) / (1 + ρ / G)` with a 4-number digital shape fitted once per band (equation-error least
squares towards minimax, 2.4 µs, at most once per 5 ms while the band moved) and every depth in closed
form on one TPT SVF, designed every sample the depth moved. Near Nyquist it held 0.29 dB at any rate;
its shape failed the accuracy gate (a band-wide ess came down by 72–79 % of the depth in dB: +0.93 dB
over at 6 dB over, +4.76 at 18) and collateral (−0.19 dB at 1 kHz, −2.54 dB a third of an octave out
at 8 dB), which Settled 38 answered with this cut. CPU then: Split 0.343 %, the worst case 0.421 %.

## Options (for the user)

Settled 41–43 (after M2b, taken on the user's behalf) took 1, 2, 4 and 5: the own-band reading and
the edges as built, both collateral readings accepted as measured (real material reported, not
gated). 3, the ringing, goes to the listening pass (`PLAN.md`, *Open* 3).

1. **Settled 39's reading for Split** (applied in M2b): accuracy is the band's own power losing the
   reduction (−0.00 … −0.08 dB on every test row, 100 % of real events); the landing on B is reported
   (−0.53 … +0.08 dB on the test rows, +1.22 at 24 dB; Speed Auto's overshoot where it misses). Read as
   written, Split fails where Wide does (3 and 6 dB over, by 0.01–0.03 dB), and its rates check at
   176.4/192 kHz.
2. **The edges**: the full depth over Detect Low … High and the transition in the third of an octave
   past them (M2: half the depth at the edges). The collateral gate reads from that third of an octave
   on, so the cut acts where it does not look.
3. **Ringing**: 3–3.6 ms on the default band, about 10 ms on a 1.5 … 3 kHz band (M2: 0.2–1.2 ms):
   for listening in M3.
4. **Collateral on real material: fail** as written (every event within 0.1 dB): within 0.1 dB on 99.3 %
   (VCTK) and 100 % (VocalSet) of esses and 98.7 / 94.3 % of Harsh episodes, the outliers in near-empty
   third octaves (transients of the moving cut). Accept as measured, gate the synthetic reading only, or
   weigh the reading by level.
5. **Collateral at the deepest cuts near Nyquist: fail** as written: −0.146 / −0.190 dB a third of an
   octave above a top-octave band at 44.1 / 48 kHz and 24 dB (*The edges*). Accept it (within 0.1 dB to
   16 dB of Range everywhere), or redesign the prototypes with margin there (not studied: the base's
   near-Nyquist error, 0.05–0.09 dB, is most of the excess, and a prototype alone does not remove it).
