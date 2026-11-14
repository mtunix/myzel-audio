# tools/netlist: netlist to network

Viridian's linear networks come from their schematics, never from drawn curves (`PLAN.md`,
*Signal flow*). This tool reads an ngspice netlist with named pots and switches, solves it
symbolically and writes a C++ header that `Network` (`src/dsp/common/Network.h`) runs at control
rate. ngspice checks the result.

Needs python3 with sympy, numpy and mpmath; `check` and `validate.py` also need ngspice.

```sh
python3 plugins/viridian/tools/netlist/netlist.py show <netlist.cir>      # H(s) and its blocks, per state
python3 plugins/viridian/tools/netlist/netlist.py check <netlist.cir>     # the blocks against ngspice AC
python3 plugins/viridian/tools/netlist/netlist.py generate <netlist.cir> <header.h> [--vectors <vectors.h>]
python3 plugins/viridian/tools/netlist/netlist.py regenerate              # every netlist with '*@ generate'
python3 plugins/viridian/tools/netlist/validate.py                        # the reference circuits (ctest runs it)
cmake --build build-dev --target ViridianNetworks                         # = regenerate
```

Generated headers are checked in, like Umber's Auto gain tables, and never edited by hand: each
one starts with the command that rebuilt it and the hash of its netlist and every file the
netlist includes or names with `*@ depends` (`sources.py`), so a changed card netlist behind a
wrapper makes its header stale too. A header derived some other way (the Console's
`ConsoleLaws.h`) names its sources and their hash on a `// Sources in <folder> (sha256 ...)` line.
`python3 plugins/viridian/tools/netlist/sources.py` compares every stamp with the files as they
are; it needs python3 only, and ctest runs it wherever python3 exists.
`validate.py` runs it too and fails when a reference header differs from a fresh regeneration
(`--all` regenerates every netlist with a `*@ generate` directive: hours for the Console's).

## Netlists

Plain ngspice: the same file runs in ngspice unchanged. The tool reads R, C, L, V, I, E, G and X
(subcircuits with `params:`), `.param`, `.subckt`/`.ends`, `.include`, `.if`/`.elseif`/`.else`/
`.endif`; it skips `.control` blocks, analyses and options. Values are SPICE numbers (`4.7n`,
`10k`, `1meg`; `m` is milli) or `{expressions}` over parameters (`+ - * / ^ **`, comparisons,
`&& || !`, `? :`, `min max abs sqrt exp log pow`), read as exact rationals.

The rest goes in comments, which ngspice ignores:

| Directive | Meaning |
|---|---|
| `*@ input <V source>` | the AC input (default: the only V source with an AC value), from a node to ground |
| `*@ output <node>` | the output, measured to ground |
| `*@ pot <param> [default]` | a `.param` that is a pot's rotation, 0 ... 1 |
| `*@ switch <param> <positions> [default]` | a `.param` that is a switch, 0 ... positions - 1 |
| `*@ step <param> <value> <value> ...` | a `.param` that a rotary switch sets to one of these component values (a stepped control) |
| `*@ nullor <E element>` | an ideal op-amp: inside a `.subckt`, for every instance |
| `*@ name <identifier>` | the generated struct's name (default: from the file name) |
| `*@ generate <header> [vectors <header>]` | where `regenerate` writes, relative to the netlist |
| `*@ depends <netlist>` | a netlist this one copies parts from: its header's stamp covers it too |

- **Pots** are parameters in element values: a pot is two resistors, `{r*(1-pos)}` and
  `{r*pos}` (a `.subckt pot` with `params: r pos` keeps it to one line per pot). A taper is an
  expression in the position; H(s) must stay rational in it. A resistor may reach 0 Ω at a pot's
  end: the analysis is exact there (ngspice puts 1 mΩ in its place, so `check` compares those
  settings against the peak level instead of in dB).
- **Switches** are parameters used in `? :` values or `.if` conditions. Each position is a state
  of its own, solved separately.
- **Steps** are a component value on a rotary switch's detents (`*@ step cap 47n 100n 220n`): one
  topology, so the analysis keeps the value as a variable, its coordinate 0 ... 1 over the
  values' range (rational in it, as a pot); the `.param` line's value is the default detent and
  `check` gives ngspice the component value. At run time the value glides between detents
  (*Network*), where a switch's position would be a new state and a fresh design.
- **Pot conditions:** a `.if` or `? :` on a pot (a wiper crossing a centre tap) splits the pot's
  range into regions, each solved exactly; the generated code tests the condition at run time.
- **Ideal op-amps:** ngspice needs a finite gain (`E1 out 0 inp inn 1e9`); with `*@ nullor e1`
  the tool stamps the exact nullor (V+ = V-, any output current). The difference against
  ngspice is then the 1e9 gain's, about 1e-5 dB.
- Other V sources are AC shorts (supplies), I sources are open; AC current sources and
  behavioural sources are not part of a linear network.

## How it works

1. **Cuts** (`mna.py`). An op-amp output that alone joins the input's side of the network to the
   output's is an ideal source: nothing after it loads it, so H is the product of the pieces on
   either side, exactly, and each piece is solved and factored on its own. A buffered filter in
   front of an equaliser then costs its own size instead of multiplying the equaliser's. Each
   switch state runs in its own process.
2. **Nodal analysis** (`mna.py`), per piece. Shorted nodes merge; the input node is fixed at 1 V; a
   nullor's output row becomes V+ = V-. Each row is cleared of denominators (a row factor scales
   both determinants alike) and the system goes through fraction-free elimination over ZZ[s,
   pots] (Bareiss) with Markowitz pivoting and the output's column last: the last pivot is
   det A, the last right-hand side det A_out, and H = det A_out / det A by Cramer's rule, with the
   common factor divided out. A console HF/HMF stage (68 elements, 5 pots, 9th order) takes about
   20 s per state.
3. **Blocks** (`sections.py`). N and D are factored over QQ[s, pots]. Every irreducible factor
   of D is a block: a band that the circuit buffers from the others is a factor of its own, so it
   stays a block of its own; a network whose controls interact (one denominator in several pots)
   is one block. Numerator factors join blocks by degree; among the assignments that fit, the tool
   takes the one whose worst block varies least over frequency anywhere in the pot range (a pot
   grid, ends included), so no block's gain runs away while another's falls in step (a
   Baxandall shelf's two first-order factors at the bass pot's end). Factors free of s form the
   gain.
4. **Code** (`codegen.py`). Each block's numerator and denominator in sigma = s / omega_ref
   (omega_ref = 2 pi 1 kHz, so the coefficients stay near 1), scaled at the defaults, with common
   subexpressions shared; one function per state, its shared temporaries and its outputs in
   functions of at most 32 statements or 16 kB (`PART_STATEMENTS`, `PART_CHARACTERS`, kept out of
   line), so no compiler meets one function of megabytes. The struct carries the controls (name,
   kind, positions, default; a step's component values), `maxBlocks`, `maxDegree` and
   `maxSections` for allocation-free sizing, and per block the controls it depends on, so
   `Network` re-designs only what moved.

`--vectors` also writes test points: control settings, frequencies and the exact H(j 2 pi f) of
the unfactored N / D at 40 digits (mpmath). `tests/network/NetworkTests.cpp` checks the generated
code against them.

## Reference circuits

`reference/*.cir`, generated into `tests/network/generated/`:

| Netlist | What it exercises | Closed form |
|---|---|---|
| `rc_lowpass` | a pot as a rheostat | 1 / (1 + s C (Rmin + x Rpot)) |
| `rlc_bell` | a passive LC dip, depth pot | (s² LC + s R2 C + 1) / (s² LC + s (R1 + R2) C + 1) |
| `gyrator_bell` | two op-amps (a gyrator's simulated inductor), boost/cut and frequency pots, a range switch | (ab + Z (a + b) + R2 a) / (ab + Z (a + b) + R1 b), Z = Rs + Rg1 (1 + s Cg Rg2) / (1 + s Cg Rg1) + 1 / (s C2) |
| `baxandall_shelf` | an inverting op-amp, the wiper at the virtual earth, a turnover switch | -Zf / Zi, Zi = R1 + (P (1 - x) ∥ C), Zf = R2 + (P x ∥ C) |
| `ladder3` | an interacting third-order LC ladder, two pots in one denominator (factored at control rate) | ngspice only |
| `hpf18` | an 18 dB/oct high-pass, a dual-gang pot over a buffered first- and second-order stage | ngspice only |
| `stepped_bell` | a stepped control: the RLC dip with its capacitor on a five-position switch (47 nF ... 1 uF) | the `rlc_bell` form at the step's value |

`validate.py` (measured 2026-11-12): every closed form holds exactly (sympy, every switch state);
against ngspice over a control grid, 10 Hz ... 100 kHz, the worst is 1.5e-5 dB and 6e-5 degrees
(the gyrator's two 1e9-gain op-amps; 1e-12 dB without op-amps; the stepped bell 5e-14 dB),
gate 0.01 dB; gain times the product of the blocks equals N / D to 1.5e-15. Regenerating every
reference netlist reproduces its checked-in header and vectors to the byte.

## Matched sections (NOTES)

`MatchedSection` (`src/dsp/common/MatchedSection.h`) turns one analog section of order 1 or 2
into a digital filter whose magnitude matches it to 20 kHz at every rate, without cramping.
`FrequencyMap` holds the method's core.

### Why not Vicanek's or Orfanidis' biquad alone

`PLAN.md` names Vicanek's matched second-order section (impulse-invariant poles, numerator
matched at DC, the centre and Nyquist) and Orfanidis' prescribed Nyquist gain. Measured
(`matched_study.py`), worst |dB| to 20 kHz at 44.1 kHz:

| Bell | Vicanek | Best any biquad can do |
|---|---|---|
| 1 kHz, Q 0.4, −18 dB | 0.264 | 0.055 |
| 3 kHz, Q 1, +15 dB | 0.180 | 0.055 |
| 5 kHz, Q 0.4, −18 dB | 2.26 | 0.168 |
| 10 kHz, Q 1, −18 dB | 9.14 | 0.179 |
| 10 kHz, Q 4, +18 dB | 0.315 | 0.078 |
| 16 kHz, Q 1, +15 dB | 1.46 | 0.105 |

(48 kHz: Vicanek up to 6.6 dB, the best biquad up to 0.10 dB.) The best biquad is a minimax fit
of all five coefficients; no closed-form design does better. So no single biquad per analog
section meets the plan's 0.1 dB (Mastering 0.05 dB) to 20 kHz at 44.1 kHz: a digital section
needs more order than its analog one there. Third and fourth order bring the same cases to
0.009 and 0.0005 dB.

### The method: a matched frequency map

The squared magnitude of a digital filter is a rational function of t = sin²(w/2); an analog
one's is rational in Omega². The exact axis between them, (Omega / 2 fs)² = asin²(√t), is not
rational. `FrequencyMap` fits a rational M (t) of degree D to it (minimax relative error,
Loeb / Sanathanan-Koerner iteration with Lawson weights, 1024 points denser towards the top,
with M (0) = 0 and M'(0) = 1 exact) over the band, and picks the smallest D within 2e-5. The band
is the gates' 20 kHz below 64 kHz; above, 32 kHz, past the top bands' centres (26 kHz), where the
rate leaves the room (at most 0.46 fs). Then:

- every analog root r becomes the D roots of M (t) = -(r / 2 fs)², each mapped to z by
  z + 1/z = 2 - 4t (inside the unit circle); the digital squared magnitude is then exactly the
  analog one at Omega² = 4 fs² M (t). The error is a pure frequency-axis error of M, so a
  section's dB error is its slope times that, wherever it acts, at any Q, gain or frequency,
  above Nyquist included (a 26 kHz band at 44.1 kHz is just a root further out).
- Nyquist reads the analog response at 21.3 kHz (44.1 kHz), 22.9 kHz (48 kHz), 44.8 kHz
  (96 kHz): no cramping. Forcing M (1) to the exact Nyquist (Orfanidis' prescription) costs 3.4
  times the error to 20 kHz at 44.1 kHz and 6 times at 48 kHz, so the fit spends its freedom on
  the band.
- In band, a pole's primary digital root lands where impulse invariance puts it (z = e^(r/fs),
  as in Vicanek's design); the other D - 1 roots correct the map.

| Rate | Band | D | Relative error of Omega² |
|---|---|---|---|
| 44.1 kHz | 20 kHz | 3 | 1.8e-4 |
| 48 kHz | 20 kHz | 3 | 3.2e-5 |
| 88.2 / 96 kHz | 32 kHz | 3 | 3.0e-6 / 8.2e-7 |
| 176.4 / 192 kHz | 32 kHz | 2 | 5.2e-7 / 2.6e-7 |

### Realisation

- **Main stage:** the primary roots of the poles and zeros, as Simper's TPT state-variable
  filter with output mixing (y = m0 x + m1 bp + m2 lp), or a TPT one-pole for a first-order
  section. Its coefficients come from products of (1 ∓ z), so a 10 Hz corner at 192 kHz stays
  exact.
- **Correction:** the other (D - 1) n roots as one transposed direct-form filter with unity DC
  gain. Its roots stay near the map's fixed roots (z ≈ −0.05 ... −0.7, well inside the unit
  circle); its coefficients are symmetric functions of them, so nothing has to be grouped into
  stages and nothing jumps while a control moves.
- **Which root is primary:** inside 0.8 times the radius where the inverse map has its first
  branch point (18.9 kHz at 44.1 kHz, 42.5 kHz at 96 kHz), the root nearest the inverse map's own
  power series about r = 0 (48 terms by series reversion, converging there). Outside, a moving
  root keeps the digital root nearest its previous primary (continuous through any path); a fresh
  design continues the series' branch radially outwards. A root that goes around a branch point
  and comes back inside is re-labelled once; nothing else re-labels.
- **Phase:** a right half-plane zero keeps its primary root outside the unit circle, so an
  all-pass stays an all-pass with its phase. Gain and sign come from the analog section at the
  lowest of four reference frequencies where it is not near a zero (the identity
  |H_d| = |H_a (Omega (w))| holds at every w).
- **Modulation:** between designs g, k, m0 ... m2 and the correction's coefficients follow a
  quartic B-spline through the last five designs (`BSpline.h`), one segment of `rampSamples` per
  design, about two control blocks behind the controls (convex weights: g and k stay positive,
  the SVF is stable at every sample); three segments after the last design the section runs that
  design itself. `Network` uses its control block (*Network*), and its output gain and every
  cascade follow the same spline (*Interacting blocks* says why quartic).

### Measured (2026-11-12, `ViridianTests_network`)

Worst |dB| against the analog prototype over 744 sections (bells Q 0.4 ... 4 and ±6/18 dB,
second-order shelves, first-order shelves, low- and high-pass, 20 Hz ... 40 kHz):

| Rate | 10 Hz–2.5 kHz | 2.5–5 kHz | 5–10 kHz | 10–14 kHz | 14–17 kHz | 17–20 kHz |
|---|---|---|---|---|---|---|
| 44.1 kHz | 0.0013 | 0.0075 | 0.0080 | 0.0081 | 0.0076 | 0.0070 |
| 48 kHz | 0.0003 | 0.0014 | 0.0015 | 0.0015 | 0.0015 | 0.0013 |
| 88.2 kHz | 0.00001 | 0.00009 | 0.00014 | 0.00011 | 0.00014 | 0.00013 |
| 96 kHz | < 1e-5 | 0.00002 | 0.00004 | 0.00003 | 0.00004 | 0.00004 |
| 176.4 kHz | < 1e-5 | 0.00001 | 0.00002 | 0.00002 | 0.00001 | 0.00001 |
| 192 kHz | < 1e-5 | < 1e-5 | 0.00001 | 0.00001 | 0.00001 | 0.00001 |

(`ViridianTests_network "[.report]"` prints it; the tests' gates are 0.012 dB at 44.1 kHz, 0.003
at 48, 0.001 above.) Above the band the map leaves the exact axis by design: near Nyquist a
section acting there is off by up to 2.5 dB at 44.1 kHz, 5.2 dB at 88.2 kHz.

- **Response function = realised filter:** impulse-response DFT against `response()`, within
  the tests' 1e-7 dB (sections) and 1e-6 dB (networks), 25 Hz to 0.95 fs/2.
- **Stable:** every section of the grid at every rate (g, k > 0, correction poles inside), and
  an unstable analog pole is reflected.
- **Sweeps** (330 Hz sine, re-designed every 32 samples, ramped): 3rd-difference crest within
  -60 dB for bells 50 Hz → 30 kHz and back, gain −18 → +18 dB through real and complex poles,
  Q 0.3 → 4 at 15 kHz, shelves to 25 kHz, low-pass 22 → 3 kHz, at 44.1 ... 192 kHz; four of the
  reference networks with every pot swept 0 → 1 → 0 in 2 s, ends included, at 44.1 and 96 kHz.
- **Denormals:** states below 1e-30 flush once per control block; silence in gives exact zeros
  out, no subnormal on the way.
- **CPU** (`"[.perf]"`, eight second-order sections, stereo, D = 3): 0.37 % of one core at
  48 kHz at rest, 0.73 % at 96 kHz; with all eight re-designed every 32 samples, 2.7 % and 5.5 %.
  One design costs 1.8 µs.

### Network

`Network<Topology>` runs a generated topology. Every control block the pots that moved glide one
step, the generated `evaluate()` writes every block's polynomials, and only the blocks that
depend on a moved control are re-designed; their coefficients and the output gain follow one
quartic B-spline through the designs (`BSpline.h`), a segment per control block.

The control block is 64 samples up to 72 kHz and 128 above (`networkControlBlock()`): 1.45 ms at
44.1 kHz, 1.33 ms at 48 and 96, 0.67 ms at 192. Designing more often is not smoother: where roots
of different cascade stages meet, each design puts the corner into the coefficients' spline
(*Interacting blocks*), and at 44.1 and 48 kHz 32 samples left a click that 64 removes. A model's
networks take turns in it (`setStagger()`,
`align()`: the boundaries sit at a fraction of the block on the model's own sample clock), so
above 72 kHz a host callback of a quarter block holds one network's design: at 96 kHz in blocks
of 32 with every Console knob automated, a callback had held both channels' fronts and backs
(0.45 ms of a 0.33 ms period; console `NOTES.md`, *Measured*). `processPair()` runs two channels
whose turns differ, from either's boundary to the next.

The glide is two one-pole stages, 20 ms each (`setSmoothingTime`), in the coordinate
u = ln ((x + 0.02) / (1 − x + 0.02)) of the pot position x: the log ratio of its two segments,
each kept above 2 % of the pot. In the pot's own units a wiper near an end, where a segment is a
few ohms, moved the circuit by a large factor in one block: a host jumping the Console's
high-pass from one end to the other moved its corner fivefold in the first 32 samples, and the
coefficients' ramp between designs that far apart came out as a thump (+11 dB, crest -10 dB at
44.1 kHz on the early card; +32 dB on the later). In u every block moves the segments by a
bounded factor, and the second stage takes the corner out of the path where it starts to move
(a one-pole's path starts at full speed). A jump is within 10 % of its way (in u) at 80 ms and
within 1 % at 0.13 s.
Measured through the chain (`ViridianTests "[gates][automation]"`): see the console `NOTES.md`,
*Measured*.

A stepped control (`*@ step`) takes a detent position; its component value glides the same way,
two one-pole stages of 1.25 ms (`setStepTime`) in ln of the value, so a detent step is within 1 %
of its way 10 ms after it (PLAN.md, *Signal flow*: the hardware's rotary switch clicks there).
On the stepped reference (`ViridianTests_network "[stepped]"`, the bell at full depth, a 330 Hz
sine) a detent at a time every 50 ms measures -71.8 dB at 44.1 kHz, -74.1 at 48, -92.1 at 96 and
-110.2 at 192 kHz; jumps from one end of the switch to the other every 100 ms -71.1, -73.5, -91.5
and -110.3 dB (gate -60; on the quadratic spline the jumps measured -65.7, -67.7, -82.6 and
-94.0 dB, with 32-sample control blocks at 44.1 and 48 kHz -72.0 and -74.2). At 10 ms a step is
within 0.25 % of its way at every rate (0.24 % at 192 kHz, 0.17 % at 44.1). A switch's position, which selects a topology, still re-designs from scratch.

Each block is designed from its polynomials divided by their Euclidean norms (numerator and
denominator apart), and the output gain carries the generated gain times the norms' ratio. The
split `evaluate()` writes between a block and the gain is arbitrary: the early Console card's
high-pass block grows 14 % per control block against its gain while the corner glides, and the
joined later front (`PortComposition`) scales by its largest coefficient, whose index changes
with the HF frequency. Unnormalised, two factors moving against each other do not cancel inside
a control block: the cards alone click at -55 dB (early high-pass jumped end to end) and -53 dB
(later HF frequency jumped from its top); normalised, a block's level follows its roots alone,
both measure at the clean sine's floor (-78 / -77 dB at 48 kHz), and the output does not depend
on how a block is scaled (`ViridianTests_network`, "the output does not depend on how evaluate()
scales a block").

The norms' ratio moves with the roots, so the gain must move with them too: every coefficient of
every block and the gain follow the same spline, a cascade's roots about two control blocks behind
the controls and its scale with them. With the gain ramped linearly instead (a block ahead of a
cascade's spline) the scale and the roots parted whenever several of a cascade's controls moved
fast at once: every knob of the early card jumped together clicked at -57.2 dB (44.1 kHz) and
-54.3 dB (48 kHz), every knob jumped between random settings at -56.6 dB; on one spline they
measure -69.5 and -68.1 dB (console `NOTES.md`, *Measured*).

A switch selects other blocks: `reset()` designs from scratch. `beginReset()` and `resetStep()`
do the same in bounded steps, for a caller that spreads a fresh design over callbacks: the
evaluation, then the sections, then each cascade a step at a time (*Interacting blocks*); the
design is the same to the bit however it is split, and `process()` waits until it is complete.
A model that switches builds its incoming networks that way while the outgoing ones still play,
then crossfades (the Console takes one step per 32 samples). `clear()` clears the states and
keeps the designs and glides: a host's reset needs no design (PLAN.md Settled 19).

### Interacting blocks

A block of degree above 2 (a denominator in several pots: the bands of an equaliser around one
summing amp) is a `RootCascade`. Its poles and zeros are found at control rate (Aberth, warm from
the previous design, each root matched to the label it continues), so every root keeps its label
while a control moves. At a fresh design the poles are paired once (conjugates, then the nearest
real roots within the same range: below 10 Hz, to 30 kHz, above), the zeros alike, and the zero
pairs fill the pole pairs' stages in order of size (the smallest zeros with the smallest poles):
each stage is one pole pair and its zeros, their digital roots in transposed direct form with
complex coefficients. Nothing is ever re-grouped.

Tracking: the warm Aberth iteration returns each root in its label's slot; when a slot's root is
nearer another label than its own (two events in one design: a pair turning real while two real
roots meet), the roots are matched to the labels by a log-polar distance, closest first. Its
angle wraps at the negative real axis, where a root just below it reads -pi and one on it +pi:
compared unwrapped, a pair near the axis that turned real gave its lower root to whichever
complex root lay nearest, the stages re-grouped mid-glide and the early Console card clicked at
-50.3 dB with every knob jumped at once (`ViridianTests_network "[cascade]"` holds the case). The
distances are taken once per design, an n x n table, and matched from it: recomputed per round,
as at first, the matching of a degree-21 block cost 0.12 ms of logarithms in one callback.

**Roots at infinity.** A cascade gives the root finder a radius, a thousand times the sample rate
(in sigma; 44 ... 192 MHz): a root beyond it is at infinity for the digital design (in the band it
changes the magnitude by less than 1e-6 and the phase by less than 1e-3 rad), and the finder drops
the leading coefficient while a lower bound on the largest root, max over k of (|c[n-k]| /
(C(n, k) |c[n]|))^(1/k), lies beyond it. The size of the leading coefficient alone does not say:
the later Console card's high-pass joined to its bands (degree 22, roots from 4 mHz to 293 kHz)
has its leading coefficient at 9.3e-15 of the largest during a glide at 192 kHz, below the 1e-14
under which the other callers' rule (`poly::roots()` without a radius) drops it. Dropped, the real
root at -292.6 kHz goes to infinity and two others move to fill its place (the pair at -141.8 ±
17.1i kHz comes out at -69.3 ± 63.2i), the coefficients of two of the cascade's stages jump 0.22
and 0.26 in one design (0.004 ... 0.02 per design before), and the card clicks at -36.8 dB
(`ViridianTests_network "RootFinder: a finite root*"`, the design itself and its roots by mpmath;
`ViridianTests_console "Console: every pot jumped at once*"`, the jump). A root beyond the radius
still drops however large its coefficient, and moves the others by its pull, their size over its.
Any radius from 1e3 to 1e6 designs the Console's blocks the same, from 1e7 the warm designs broke
down (+34 dB), and static accuracy at 192 kHz gains: over 600 random settings the worst error in
the band went from 5.8e-4 to 6e-6 dB (both measured in M1c's design step).

Two real roots that met within a design come back with real warm starts, and from real starts
every Aberth iterate of a real polynomial stays real: the pair bounced on the axis until the
200-sweep cold restart (0.1 ms at degree 21, then polished where it stood). Each real warm start
therefore begins 1e-6 of its size off the axis, by turns above and below, and `symmetrise()`
puts the real roots back on it (`ViridianTests_network "RootFinder: two real roots*"`).

Why in order of size: each stage then keeps its zeros on its poles' scale, so no stage amplifies
what an earlier one attenuated. Placed nearest first, the zeros at DC come last, in whatever
stage has room: on the later Console card's high-pass (degree 22) they land with the 200 kHz
poles, the stages' gains at 0.01 Hz span -242 ... +78 dB and the rounding between them comes out
as a sub-audio swell (+108 dB over `response()` at 20 Hz at 192 kHz). In order of size they span
-65 ... +24 dB. The pairing runs on the audio thread at a fresh design, so it sorts in place
(a stable insertion sort; `std::stable_sort` takes a buffer from the heap).

Why so: grouping roots into real second-order sections has to re-pair them whenever two real
roots meet and turn complex (or the reverse), which the Console's networks do all the time
(coupling capacitors' near-coincident roots, a band's two poles at low Q, a shelf's zeros), and
every re-pairing clicked (crest up to -17 dB). Inside a pair the meeting is smooth: the stage's
coefficients are the pair's sums and products. One stage per root avoids the re-pairing too, but
a root's own path has a corner where it meets another (it moves like a square root there): that
clicked at -53 dB. Roots held in different stages still meet: on the Console in a fast glide two
real poles of the later card's bands (stages 1 and 5) meet and turn complex, and on the early card
a pair of stage 0 splits and one half meets the real pole of stage 2. Each stage then takes the
square-root corner of its root's path, so the coefficients follow a B-spline through the designs,
one segment per control block (`BSpline.h`). The spline's continuity decides what reaches the
audio: a quadratic one (C1) has its second derivative jump at each design near the corner, and
the output's 3rd difference shows it. Measured on the early card alone (random pair 45 of the
chain's gate, 48 kHz), designing more often made it worse (designs every 64 samples -53.0 dB, 32
-38.0, 16 -35.5, 8 -25.9, 4 -14.0, every sample -0.2 dB), the spline's order better (quadratic
-38.0, cubic -62.6, quartic -78.4 dB; the later card's pair 11 at 44.1 kHz -45.6, -65.6, -70.5).
The coefficients follow the uniform quartic B-spline through the last five designs (C3), about
two control blocks behind the controls (one more than the quadratic); when no new design comes,
three more segments settle on the last, and the stages then run that design itself, not a
weighted sum of its copies that differs in the last bit (`ViridianTests_network "Network: at rest
after a glide*"`). Through the chain, 1000 random every-knob jumps per card, each from the five
bases' filter and shape states, four rates (40 000 renders of 2000 knob draws, M1c's design step): the quadratic spline with the old degree rule (*Roots at
infinity*) had 108 over -60 dB, -35.9 dB at worst; quartic with the radius none over -62 dB,
-62.5 at worst (the LMF at full boost and Q about 4.2 gliding past the 330 Hz tone, which it
swells by 13 dB: the same with any spline order or control block; first read as the LF bell); cubic left -58.5 dB. Every Console knob ramped over 10, 50 or 200 ms:
-44.7 dB at worst before, -66.4 after. Re-pairing the roots at run time, a lattice or a direct
form of the whole block, and a crossfade to fresh networks on a large jump were tried and dropped
(PLAN.md *Open* 14).

The spline smooths a meeting; it does not remove it. `ViridianMeasure`'s own seeds (`measureSeeds`
in `tests/support/ChainGates.h`, settings the ctest's pairs do not sample) found the early card's
back, both filters Off, pair 719, at -51.2 dB at 44.1 kHz and -53.4 dB at 48 with a design every
32 samples: its real poles at -578 Hz (stage 1) and -825 Hz (stage 3) meet and turn complex
(-737 ± 80i Hz), the two stages' coefficient steps rise to 2.7e-2 and 4.0e-2 in one design
(1.1e-2 ... 2.5e-2 in the designs either side), and the crest follows the control block as pair 45's did (the card
alone at 44.1 kHz: 16 samples -31.3 dB, 32 -51.2, 64 -70.4, 128 -73.1). The control block is
therefore 64 samples up to 72 kHz (*Network*): -70.4 dB at 44.1 kHz and -73.5 dB at 48, -91.6
and -88.9 dB at 96 and 192, where it was already 128 (`ViridianTests_console "Console: every pot
jumped at once*"`, which fails at 32). The control block was chosen on `measureSeeds`; on two seed
sets M1c had not used (offsets 31337 and 999983, every knob jumped between 1000 random settings per
card, each from the five bases' filter and shape states, both cards, 44.1 and 48 kHz) no jump reads
over -61.6 dB; a third (offset 271828) has one pair over -60 dB, the LMF gliding past the tone
(PLAN.md *Provisional* 9). 128 samples would gain 3 dB more on this pair and nothing
on the three traced before, for twice the lag. What would remove the corner is a stage that holds
both meeting roots: two stages merged into one of fourth order while their roots approach, whose
coefficients are symmetric functions of all four roots and pass through the meeting smoothly. The
switch maps the states between the two realisations, the same conditioning problem across stages
whose other roots lie far apart that dropped the re-pairing (PLAN.md *Open* 14); not built.

The stages are complex and their product real; the output is its real part. A conjugate pair's
lower root copies the upper's digital roots, so the set stays closed under conjugation. A stage
whose roots are closed under conjugation has exactly real coefficients and runs in real
arithmetic while its input is real (the same numbers, a quarter of the work); at rest that is
every stage. Each stage is normalised to unity at fs / 4 and the block's gain matches the analog
response at the lowest of four reference frequencies, as in a section. A cascade is minimum
phase throughout: a zero crossing into the right half-plane (or through infinity, where a
leading coefficient changes sign) would make a mirrored root jump.

A fresh design in steps (`beginFresh()`, `freshStep()`): the cold Aberth sweeps over the poles,
then over the zeros (`poly::RootFinder`, the same iterations as `poly::roots()` paused between
sweeps), the pairing, each root's digital roots, then the stages and the gain. A step does about
40 µs of work by fixed estimates (a sweep 300 + 11 m² ns at degree m, a root's map 3 µs, or
55 µs where a fresh root lies outside the inverse series' radius and continues it radially), at
least one item: the estimates are never timings, so the split, and the design, do not depend on
the run. The later Console card's high-pass joined to its bands (degree 22, 0.41 ms at once)
takes about a dozen steps.

At rest every stage is real; the stages then run a block at a time, and two channels (or one
channel of each of two cascades of the same structure, a stereo pair with a Tolerance spread:
`processPair()`) in one loop, with the same operations per sample.

Measured on the Console (`ViridianTests_console`, both cards, every pot swept end to end and
back in 2 s with the others at work): 3rd-difference crest at worst -68.0 dB at 44.1 kHz,
-70.3 dB at 48, -88.3 dB at 96 and -106.4 dB at 192 kHz (the early LMF gain from full cut to full
boost); against ngspice within 0.0026 dB to
20 kHz at 44.1 kHz.

### Phase

Matched magnitude means minimum phase over 0 ... fs/2; the analog section's phase also carries
its magnitude above Nyquist, which the digital filter cannot. Worst phase difference to 10 kHz
(`"[.report]"`):

| Section | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| bell 1 kHz, Q 1, +12 dB | 1.5° | 1.2° | 0.3° | 0.1° |
| high-pass 80 Hz | 0.1° | 0.1° | 0.02° | 0.01° |
| high shelf 6 kHz, −12 dB | 6.2° | 5.2° | 1.3° | 0.3° |
| bell 8 kHz, Q 0.8, −15 dB | 12.1° | 10.6° | 3.4° | 0.9° |
| bell 16 kHz, Q 0.8, +15 dB | 10.1° | 10.3° | 5.3° | 1.7° |
| low-pass 15 kHz | 42.6° | 40.6° | 24.8° | 13.5° |
| all-pass 2 kHz (right half-plane zero) | 4.0° | 3.4° | 0.8° | 0.2° |

Sections acting below 2 kHz meet the plan's 2° to 10 kHz at every rate (tested); sections
acting near the top do not, and no minimum-phase design can (the difference is close to a pure
delay: the analog filter's extra lag from its stopband above Nyquist). See `PLAN.md`, *Open*.

### Limits

- A switch change, or a pot condition crossing into a region with other degrees, re-designs
  from scratch (no ramp): it can click, as the hardware's switch does. The fade belongs to the
  model (the Console crossfades two sets of networks) and to the chain (the model switch).
- A fresh design is the costliest work: up to 0.41 ms for the later Console card's high-pass
  joined to its bands (degree 22: cold root finding and the map's roots), 0.05 ... 0.25 ms for the
  others (this machine). In steps it is spread over callbacks, at most one radial root map
  (about 55 µs) per step; designed at once (`reset()`) it belongs in `prepare()`.
- The symbolic analysis grows steeply with interaction: a Console EQ section (two bands around
  one summing amp, 9 ... 15 poles, five pots) takes 10 ... 45 CPU minutes per state, and the
  later card's high-pass interacting with it (about 21 poles, seven pots) did not finish in 50.
  Cuts at op-amp outputs keep buffered parts out of it; where none exists, generate the sides
  alone and join them at run time (`src/dsp/common/PortComposition.h`, console `NOTES.md`). The
  generated headers grow alike (the early front's 32 states: 4.6 MB), in functions of bounded
  size (*How it works*, 4): GCC builds the early front's in 16.6 s and 0.53 GB, against 38.8 s
  and 2.1 GB as one evaluate() of 4.3 MB (-O3, this machine), and every output is the same to the
  bit (200 000 settings per network, every state, compared with the one-function headers).
