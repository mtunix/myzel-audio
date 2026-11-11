# Reel: tape record electronics + magnetic tape

The gentlest model in Umber. It is the record path of a generic vintage 15 ips tape machine:
a tube record amplifier, NAB-style record pre-emphasis, the record head, the tape coating
(Jiles–Atherton hysteresis), and the playback head response. There is no wow, flutter or noise,
because this is a saturator and not a tape emulator.

| File | Contents |
|---|---|
| `ReelModel.{h,cpp}` | Wiring, calibration and constants (`CircuitModel` implementation) |
| `ReelRecordAmp.{h,cpp}` | Triode record amp: circuit equations, offline NR solver, shared Hermite table |
| `ReelHysteresis.h` | Jiles–Atherton tape core, field-space RK4 solver |
| `ReelFilters.h` | TPT and bilinear linear sections with exact responses and denormal flushing |
| `tests/models/reel/` | Conformance, character, component, report and dump tests, plus the Python reference |

## Signal flow

```
 x ─► ×0.5 V ─► RECORD AMP ─► Cc ─► RECORD EQ ───► RECORD HEAD ─► TAPE ───────► DE-EMPHASIS ─► LF ROLL-OFF ─► HEAD BUMP ─► GAP LOSS ─► ×g ─► y
               12AX7 stage    HPF   50 µs pre-     core sat.      Jiles–        exact inverse   2nd-order HP   +2 dB peak    1-pole,
               (inverting;    3 Hz  emphasis,      h/√(1+(h/12)²) Atherton,     of record EQ    12 Hz, Q 0.6   75 Hz, Q 0.9  −1 dB @ 20 kHz
               flipped back)        pole 24 kHz    ×0.115 /V      RK4 in h      (z-domain)
```

### Record amplifier (schematic)

```
                     B+ 250 V
                        │
                      [Rp] 100k
                        │
                        ├────||──────┬──► record EQ / head driver
                        │    Cc     [RL] 470k
             plate    │/             │
 vin ──[Rs 33k]───────┤  12AX7      GND
             grid     │\
                        │ cathode
                      [Rk] 1.5k  (unbypassed: local feedback)
                        │
                       GND
```

- Plate current uses Koren's 12AX7 model (μ 100, ex 1.4, kG1 1060, kP 600, kVB 300). Grid current
  uses the Dempwolf/Holters/Zölzer law (DAFx 2011), Ig = 6.06e-4·(ln(1+e^(13.9·Vgk))/13.9)^1.354.
  This gives soft grid conduction through Rs on positive peaks and a Koren soft cutoff on
  negative peaks.
- The quiescent point is Vp = 170.10 V, Vk = 1.198 V, Ip = 0.80 mA. The small-signal gain is
  −27.7 (AC load Rp‖RL).
- **Why a table and not a per-sample WDF:** with an unbypassed cathode resistor and Cc acting as a
  short at audio, the stage is memoryless. A WDF would solve the same static equations every
  sample. The 3-unknown system (grid, cathode and plate nodes) is solved once with damped
  Newton–Raphson to a residual below 1e-11 V. The solver marches from the operating point and
  warm-starts each node from its neighbour. Each node stores the plate swing and its exact slope
  from the implicit-function theorem. Lookup uses cubic Hermite interpolation (C¹), with 8192
  intervals over ±64 V and the input clamped beyond that range. The table is built once on the
  first `prepare()` (under 50 ms), is static and is shared by every instance (131 KB). PLAN.md
  allows this: "precomputed lookup tables for the nonlinear root solutions … compare against the
  full NR solve in tests".
- Character: 2nd harmonic ≈ 0.17 % per volt of grid swing. The record amp's contribution
  reaches H2 ≈ −44 dB at 0 dBFS. Cutoff clipping starts around +14 dBFS (the input maps to
  0.5 V/FS).

### Interstage coupling

Cc into the next stage's 470k grid leak is modelled as a 1st-order high-pass at 3 Hz, applied
after the static table (Cc is a short at audio, so the table uses the AC load line with Cc
holding the quiescent plate voltage). This matches the real circuit, including its blocking
behaviour. After an asymmetric overload, Cc holds the clipping DC and discharges over about
0.2 s, feeding a slowly decaying field into the tape. The playback roll-off then shapes that into
a slow, inaudible settle (the conformance check allows slow DC drift but no oscillation). Because
Cc removes the sustained DC of the triode's rectification, the tape stays centred on long,
heavily clipped notes. At high drive the record amp's H2 therefore survives into the output
(−30 dB at +12 dBFS).

### Record EQ, head and playback

- **Pre-emphasis:** E(s) = (1 + s·50 µs)/(1 + s/(2π·24 kHz)), which is the NAB 15 ips HF time
  constant with the record amp's bandwidth limiting the boost to +17.6 dB. Each corner is
  prewarped. **De-emphasis** is the exact z-domain inverse 1/E(z) (numerator and denominator
  swapped), so the pair is bit-transparent at low level at every rate. Highs reach the tape
  hotter than mids (+10 dB at 10 kHz, +14 dB at 20 kHz) and saturate first. The result is
  progressive HF softening with drive, which plays the role of self-erasure.
- **Record head:** h ← h/√(1 + (h/12)²). The head core saturates around 7× the field of
  0 dBFS at 1 kHz. This limiter bounds the solver's input and only engages at extreme drive.
- **Tape → field scale:** h = 0.115 per plate volt, so 0 dBFS at 1 kHz gives a peak field of
  h ≈ 1.6 a.
- **Playback LF roll-off and head bump:** a 2nd-order high-pass (12 Hz, Q 0.6) for the playback
  head's LF loss, followed by a band-pass-added peak (+2 dB at 75 Hz, Q 0.9) for the contour
  effect. Together they give the classic shape: +1.9 dB at 75 Hz, −0.1 dB at 30 Hz, −1.4 dB at
  20 Hz.
- **Gap and spacing loss:** the physical loss at 38.1 cm/s with a 2.5 µm gap and 0.25 µm spacing
  is −0.96 dB at 20 kHz. It is fitted by a 1-pole low-pass at 40 kHz (−0.97 dB). Its integrator
  gain is matched at 15 kHz, so the in-band response is the same at 88.2 kHz and at 768 kHz.
- **Calibration:** in `prepare()` the 1 kHz small-signal gain is computed analytically as the
  product of the exact digital responses of every linear section, the table slope, the field
  scale and the JA initial susceptibility. The make-up gain is its reciprocal. Measured: +0.00 dB
  at −60 dBFS at all rates.

### Tape: Jiles–Atherton

Units are normalised: m = M/Ms, h = H/a, q = h + α·m.

```
dm/dh = [ (1−c)·δM·(L(q) − m) / ((1−c)·δ·k − α·(L(q) − m))  +  c·L'(q) ] / [ 1 − c·α·L'(q) ]
L(q) = coth q − 1/q,  δ = sign(dh),  δM = 1 if (L − m)·δ > 0 else 0
```

This is Jiles & Atherton (1986) in the form used by J. Chowdhury, *Real-time physical modelling
for analog tape machines*, DAFx 2019. ChowTapeModel (GPLv3) informed the approach. This is an
independent implementation.

Parameters: α = 0.01, k = 0.5, c = 0.9. In physical units these would be, for example,
Ms = 350 kA/m, a = 22 kA/m, k = 11 kA/m and α = 6.3e-4.

**Solver: RK4 in field space.** JA is rate-independent. Between samples the field is piecewise
linear and δ is constant, so the solver integrates dm/dh directly over [h[n−1], h[n]] instead of
integrating dM/dt with an estimated dH/dt. This is exact for the sampled path and needs no
derivative filter. Its accuracy depends on field movement per sample rather than on the sample
rate.
- One RK4 step per sample when |Δh| ≤ 0.5, otherwise equal sub-steps of at most 0.5. The
  stiffest dynamics are the irreversible relaxation toward the branch, with h-scale
  ((1−c)k − 2α)/(1−c) = 0.3. RK4's real-axis stability limit is 2.78, and 0.5/0.3 = 1.67 is well
  inside it.
- **Bounded work with fading memory:** along a monotone sweep, the irreversible term pulls m onto
  the major branch with an h-scale of about 0.3–0.5. After 3.0 units (`memorySpan`) the starting
  state is forgotten to about 1e-3 of its offset (e^-10 … e^-6). For jumps longer than that, which only happen
  at absurd drive, only the last 3.0 units are integrated. The starting point is the
  major-branch estimate L − δ·k·L', limited so that m only moves with the field
  (max(m, est) going up, min going down). The worst case is 6 RK4 steps per sample. Validation
  error with this shortcut active is at most 7e-4.
- **Guards:**
  - δM removes negative susceptibility.
  - The irreversible denominator cannot reach zero, because (1−c)k = 0.05 > 2α = 0.02. A floor
    at 25 % of (1−c)k also protects against future parameter changes.
  - Below |q| = 0.1, L and L' use the Taylor series (to q⁷ and q⁶). Above it, one `exp` gives
    L = coth − 1/q and L' = 1/q² − 4e/(1−e)², with no cancellation of coth².
  - m is clamped to [−1, 1].
  - A NaN or zero field step holds the state.
  - The work per sample is bounded and the solver has no iterations.
- **Bias.** Real HF bias (≈100+ kHz, about 10× the signal field) is far above any oversampled
  rate this model runs at. Because JA is rate-independent, the bias frequency would not matter
  anyway, but its amplitude would. Simulating it explicitly would mean sweeping the full loop
  several times per audio sample, which is 10–50× the cost. A correctly biased tape responds
  close to the anhysteretic curve: AC bias is exactly how anhysteretic magnetisation is produced,
  and the recorded transfer becomes nearly linear with a narrow residual loop. The model
  reproduces that effect through the parameters, which is the same mapping ChowTape uses for its
  bias and "width" controls:
  - A high reversible fraction (c = 0.9) makes the curve follow the anhysteretic Langevin.
  - A small pinning (k = 0.5 a) keeps the residual loop narrow.

  With the wide-loop set from the DAFx paper (c = 0.17, k = 1.23 a) the gain rises about 10 dB
  before saturating and THD is already 2.1 % at h = 0.1 (Rayleigh-region hysteresis). That is the
  grainy, expanding sound of unbiased tape. With
  c = 0.9 the gain expansion is under 0.2 dB, low-level THD is 0.1 %, and the narrow loop still
  gives the level-dependent phase lag, the Rayleigh-type odd harmonics and a remanent "memory".
  I considered and rejected an explicit "bias relaxation" term that pulls M toward the
  anhysteretic with a time constant. It adds an in-band shelf at low level, and in its slow form
  it fails the original recovery test.
- **Remanence:** after loud material the tape keeps a small remanent magnetisation (|m| ≤ 0.03).
  The playback roll-off removes the resulting DC. What remains is a faint even harmonic on quiet
  material that follows, much like a slightly magnetised head: H2 stays below −54 dB at
  −40 dBFS and the gain is unchanged (both tested).

## Validation (independent Python reference)

`tests/models/reel/validation/reel_reference.py` uses numpy and scipy. To reproduce:

```
REEL_DUMP_DIR=/tmp/reel build-reel/tests/UmberTests_reel "[.dump]"
python3 tests/models/reel/validation/reel_reference.py /tmp/reel --plots
```

**1. Jiles–Atherton core.** The C++ core is compared against scipy DOP853 (rtol 1e-12) in two ways:

- *Same path:* the reference integrates the exact sampled field that the C++ solver saw, in
  h-space, run by run. This measures pure solver error.
- *Continuous:* the reference integrates the time-domain ODE dM/dt = (dM/dH)·dH/dt for the true
  continuous sine, split at the turning points. This measures the total error, including field
  peaks that fall between samples.

The table shows 4 periods from the demagnetised state. m is normalised to Ms = 1.

| rate | freq | amp (h) | same-path max\|Δm\| | continuous max\|Δm\| | fundamental error |
|---|---|---|---|---|---|
| 192k | 1 kHz | 0.3 / 1 / 3 / 10 | 7e-8 / 1.3e-6 / 1.2e-6 / 6.0e-5 | same | < 0.0001 dB |
| 192k | 10 kHz | 0.3 / 1 / 3 / 10 | 2.0e-5 / 6.8e-5 / 2.4e-4 / 5.5e-4 | 5.5e-5 / 2.7e-4 / 4.1e-4 / 5.5e-4 | ≤ 0.0024 dB |
| 768k | 10 kHz | 0.3 / 1 / 3 / 10 | 1e-6 / 5e-6 / 1.2e-5 / 1.6e-4 | 4e-6 / 1.7e-5 / 2.4e-5 / 1.6e-4 | ≤ 0.0001 dB |
| 88.2k | 1 kHz | 0.3 / 1 / 3 / 10 | 1e-6 / 3e-6 / 7e-6 / 8.7e-5 | 2e-6 / 1.6e-5 / 2.2e-5 / 8.7e-5 | ≤ 0.0002 dB |
| 88.2k | 10 kHz | 0.3 / 1 / 3 / 10 | 7.5e-5 / 1.6e-4 / 5.4e-4 / 7.4e-4 | 2.7e-4 / 1.5e-3 / 2.2e-3 / 7.4e-4 | ≤ 0.006 dB |

The worst case, 10 kHz at 88.2k, has only 8.8 samples per period. Most of its continuous-path
error comes from field peaks between samples, which is inherent to sampling and not a solver
error. The amp-10 rows run through the fading-memory shortcut. The M–H loops overlay the
reference to plotting accuracy.

**2. Record amp.** The C++ table was checked against the stage re-solved with nested Brent root
finding (no Newton, no table) at 60 points over −40…+40 V. Maximum error: **4.4e-8 V**. The C++
test also compares the table against the direct NR solve at 300 random points (worst error below
1e-6 V).

**3. EQ.** The whole model's measured response at −50 dBFS was compared with the analog
prototypes (coupling × roll-off × bump × gap loss; pre- and de-emphasis must cancel exactly):

| rate | max deviation, 20 Hz–10 kHz | 10–20 kHz | response, 20 Hz–20 kHz |
|---|---|---|---|
| 88.2k | 0.033 dB | 0.090 dB | −1.37 … +1.90 dB |
| 192k | 0.012 dB | 0.013 dB | −1.37 … +1.90 dB |
| 768k | 0.006 dB | 0.009 dB | −1.37 … +1.90 dB |

The table below shows THD, gain and harmonics for a 1 kHz sine at 192 kHz. Results at 88.2k and
768k are within ±0.1 % THD of these. The harmonic columns are in dB relative to the fundamental.

| level | gain | THD | H2 | H3 | H5 | H7 |
|---|---|---|---|---|---|---|
| −60 dBFS | +0.00 dB | 0.002 % | −101 | −95 | −115 | −127 |
| **−24 dBFS** | +0.04 dB | **0.11 %** | −65 | −60 | −81 | −90 |
| **−12 dBFS** | +0.13 dB | **0.43 %** | −54 | −49 | −79 | −77 |
| −6 dBFS | +0.13 dB | 1.18 % | −48 | −39 | −66 | −77 |
| **0 dBFS** | −0.41 dB | **3.53 %** | −42 | −29 | −54 | −83 |
| +6 dBFS | −2.44 dB | 8.8 % | −37 | −21 | −40 | −57 |
| +12 dBFS | −6.29 dB | 16.2 % | −30 | −16 | −29 | −39 |
| **+18 dBFS** | −11.46 dB | **23.6 %** | −27 | −13 | −23 | −31 |
| +24 dBFS | −17.22 dB | 28.0 % | −29 | −12 | −20 | −26 |
| **+38 dBFS** | −31.02 dB | **28.5 %** | −31 | −12 | −19 | −24 |

Every level sits inside its contract window. The tape's odd harmonics dominate, and the falling
H3 > H5 > H7 spectrum gives a "rounded square" even at +38 dBFS. The record amp adds H2 at
−54 dB (at −12 dBFS) to −27 dB (at +18 dBFS, where it clips asymmetrically). Output peaks stay at or below 2.4.

Frequency response, gain in dB at 192 kHz:

| freq | −40 dBFS | −12 dBFS | 0 dBFS | +12 dBFS | +24 dBFS |
|---|---|---|---|---|---|
| 20 Hz | −1.37 | −1.25 | −1.70 | −7.30 | −18.18 |
| 30 Hz | −0.14 | −0.02 | −0.48 | −6.10 | −16.99 |
| 50 Hz | +1.15 | +1.27 | +0.81 | −4.84 | −15.73 |
| 75 Hz | +1.90 | +2.02 | +1.55 | −4.10 | −14.99 |
| 200 Hz | +0.44 | +0.56 | +0.09 | −5.57 | −16.47 |
| 1 kHz | +0.01 | +0.13 | −0.41 | −6.29 | −17.22 |
| 5 kHz | −0.07 | +0.07 | −1.96 | −10.36 | −21.99 |
| 10 kHz | −0.25 | −0.32 | −4.66 | −14.69 | −26.60 |
| 15 kHz | −0.56 | −1.01 | −6.89 | −17.54 | −29.49 |
| 20 kHz | −0.98 | −1.82 | −8.64 | −19.59 | −31.53 |

HF compression grows with level. Relative to 1 kHz, 10 kHz sits at −0.3 dB at −40 dBFS, −0.5 dB
at −12 dBFS, −4.3 dB at 0 dBFS and −8.4 dB at +12 dBFS. This is the pre-emphasised highs
saturating first. Lows track the mids.

Transients: for a 150 Hz decaying burst, the crest factor goes 12.89 → 12.49 → 9.83 → 6.31 dB
at −24 / 0 / +12 / +24 dBFS, against 12.48 dB at the input (the low-level value differs only
through the linear LF EQ). Transients soften progressively and
are never sharpened.

## Performance

Release build, single channel, i9-12900K.
Re-measure with `build-reel/tests/UmberTests_reel "[.perf]"` and `"[.report]"`.

| case | ns/sample |
|---|---|
| 192 kHz, −20 dBFS / 0 dBFS sine (`[.perf]` uses amp 0.1 / 4) | **95 / 116** |
| 384 kHz, same | 96 / 116 |
| 88.2 kHz, +24 dBFS sine (sub-stepping) | 176 |
| 192 kHz, +60 dBFS white noise (absolute worst case: 6 RK4 sub-steps every sample) | 448 |

The cost per sample is about 1 table lookup, 1 sqrt, 4 Langevin evaluations (1 `exp` each) and
7 linear sections, all in double precision. The record-amp table is 131 KB, static and shared.

## Real-time properties

- `process()` and `reset()` never allocate, lock or throw.
- The RK4 work per sample is bounded and there are no runtime iterations.
- Output is deterministic and bit-identical after `reset()`.
- Denormals: the linear states are flushed below 1e-30 once per block, and output samples below
  1e-30 are written as 0. Silence in gives exact silence out. This holds without FTZ.
- NaN and Inf input are contained. The table clamps them, and the tape holds its state on a
  non-finite step.

## Tests

- `ReelModelTests.cpp` runs the conformance suite and the character tests: THD centred in the
  windows, harmonic balance, flat response apart from the bump, HF compression growing with
  level, crest-factor softening, no latch-up after +12/+38/+60 dBFS bursts (measured after a 1 s
  settle for the coupling cap), silence and
  denormal handling, NaN containment, and THD consistency across rates.
- `ReelComponentTests.cpp` checks the Langevin function against the closed form, loop sanity
  (dm/dh ≥ 0, closed steady-state loop, remanence sign, odd symmetry, saturation ≤ 1),
  small-signal susceptibility, rate independence, abuse of the JA core, and the table against
  the direct NR solve.
- `ReelReportTests.cpp` (`[.report]`) prints the tables above. `ReelValidationDump.cpp`
  (`[.dump]`) writes the data for the Python reference.

## Bias

The record amp's grid sits up to ±1.6 V away from its nominal DC (a grid-bias trim). The static
transfer is evaluated at `vin + Vb` and its quiescent value at `Vb` is subtracted, since the
coupling capacitor Cc holds the new plate voltage.

Measured at 1 kHz, 0 dBFS in (192 kHz):

| Bias | Gain | H2 | H3 | THD |
|---|---|---|---|---|
| cold (−1) | −2.1 dB | −29.9 dBc | −31.7 dBc | 4.1 % |
| nominal | −0.4 dB | −42.1 dBc | −29.3 dBc | 3.5 % |
| hot (+1) | 0.0 dB | −54.0 dBc | −28.1 dBc | 4.0 % |

Cold makes the stage strongly even-harmonic; hot makes it near-symmetric (odd-dominant).

The chain smooths the control (80 ms) and each model glides per sample (`BiasRamp`, 64 samples).
The operating point moves quasi-statically: the coupling capacitors are treated as already
holding the new DC, so turning the knob never thumps (`tests/BiasTests.cpp`: silence stays below
-88 dBFS while the bias sweeps its full range four times in 0.6 s). At 0 the model is
bit-identical to the nominal circuit.
