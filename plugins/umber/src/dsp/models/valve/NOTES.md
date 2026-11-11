# Valve — 12AX7 common-cathode preamp stage

The "warm, musical" model: a single triode gain stage, solved at circuit level every sample at
the oversampled rate.

## Circuit

```
                                  B+ supply 262 V
                                        |
                                    Rsup 10k
                                        +------ Csup 22u --- gnd   (supply sag)
                                     Ra 100k
                                        |                Cout 47n
                      +---- Cgp 1.7p ---+------------+-----||-----+---- out
                      |                 | plate      |            |
in --||--+-- Rs 47k --+ grid         12AX7         Cs 10p       RL 1M  (next stage)
 Cin 100n|                         (Koren)          |            |
        Rg 220k                         | cathode  gnd          gnd
          |                             |
         gnd                    Rk 1.5k || Ck 47u
                                        |
                                       gnd
```

| Part | Value | Why |
|---|---|---|
| Cin / Rg | 100 nF / 220 kΩ | 7 Hz input corner; blocking recovery τ = 22 ms. Rg/(Rs + RGI) ≈ 4.5 sets *how far* the bias walks on hits: 1 MΩ made +18 dBFS a half-wave rectifier (55 % THD), 220 kΩ keeps the squash musical. |
| Rs (grid stopper) | 47 kΩ | Limits grid current, softens the clamp, and with the Miller capacitance sets the top end (-0.4 dB @ 10 kHz, -1.6 dB @ 20 kHz). |
| Cgp | 1.7 pF | 12AX7 datasheet grid–plate capacitance → ~105 pF Miller at gain 60, collapsing as the stage saturates. |
| Ra, B+ | 100 kΩ, 262 V via 10 kΩ / 22 µF | Classic load line; the decoupling RC sags with the average current (τ ≈ 0.2 s). |
| Rk / Ck | 1.5 kΩ / 47 µF | Bias ≈ −1.2 V, full bypass above ~10 Hz; the cathode voltage follows the programme (τ ≈ 40 ms). |
| Cs | 10 pF | Plate stray + next-stage input capacitance; also stops ±600 V grid steps (abuse) feeding straight through Cgp to the plate. |
| Cout / RL | 47 nF / 1 MΩ | Output coupling into the next stage's grid leak (3.4 Hz). |
| Source limit | linear to 10 V, tanh shoulder to 20 V | The driving stage's finite swing (any real preceding stage clips somewhere). Acts only above ≈ +24 dBFS input; keeps the blocking charge after +60 dBFS abuse bounded. |
| kInputVolts | 0.6 V per unit | Places the contract levels on the stage (see table). |

Operating point (C++ and reference agree): Vgk = −1.218 V, Vpk = 171.4 V, Ip = 0.812 mA,
small-signal gain 35.4 dB. Make-up gain = sign / (|H(1 kHz)| · kInputVolts) from the
linearised circuit, so unity small-signal gain and non-inverted polarity hold at every rate.

### Triode model

`src/dsp/common/tubes/KorenTriode.h`

* Plate current: Norman Koren, *Improved vacuum tube models for SPICE simulations*, Glass Audio
  8(5), 1996 — `E1 = Vpk/Kp · ln(1 + exp(Kp(1/µ + Vgk/√(Kvb + Vpk²))))`, `Ip = 2·E1^Ex / Kg1`
  with his published 12AX7 values (µ 100, Ex 1.4, Kg1 1060, Kp 600, Kvb 300) and the 1 GΩ
  plate leak of his netlists.
* Grid current: Koren uses an ideal diode (IS = 1 nA) behind RGI = 2 kΩ. We use the smooth
  equivalent `Ig = softplus(c(Vgk − v0)) / (c·RGI)`, c = 11.6 /V (thermal voltage of a ~1000 K
  oxide cathode, cf. Dempwolf & Zölzer, *A physically-motivated triode model for circuit
  simulations*, DAFx 2011), v0 = 0.2 V: exponential onset, then RGI. Smooth → well-conditioned
  Newton and no needless high-order curvature.
* Speed: `Ip = 2/Kg1 · (Vpk/Kp)^Ex · softplus(a)^Ex`, with both powers from shared, static
  cubic-Hermite tables (`TubeMath.h`: `SoftplusPowerTable`, `PowerTable`; value + exact slope →
  C1; relative error < 1.1e-8 where the tube conducts). No exp/log on the critical path. The
  whole model went from 60 to 49 ns per sample at 192 kHz (the change is −144 dB against the
  previous output; golden file unchanged). The tables bind to `Ex` when the tube is constructed.

## Solver

`src/dsp/common/tubes/NodalCircuit.h` — **nodal DK method** (D. T. Yeh, J. S. Abel, J. O. Smith,
*Automated physical modeling of nonlinear audio circuits for real-time audio effects — Part I*,
IEEE TASLP 18(4), 2010; M. Holters, U. Zölzer, *A generalized method for the derivation of
non-linear state-space models from circuit schematics*, EUSIPCO 2015).

Why not a hand-built chowdsp_wdf tree: the Miller capacitor makes the topology non
series-parallel, and the triode has two coupled nonlinear ports (Vgk, Vpk), so a WDF needs an
R-type adaptor with every nonlinearity at the root plus a vector Newton solve anyway
(K. J. Werner et al., *Resolving wave digital filters with multiple/multiport nonlinearities*,
DAFx 2015). chowdsp's `RtypeAdaptor` needs the scattering matrix derived by hand (or by an
external tool) for each topology. The DK method is the same mathematics — trapezoidal
(bilinear) discretisation of the same MNA equations, so the same discrete system as that WDF —
but the matrices are derived automatically from a netlist in `prepare()` for any sample rate,
which makes circuits easy to edit, review and validate, and is shared with the Fuzz model.

* Netlist → MNA with trapezoidal companions (capacitors and inductors) → discrete state space
  `p = Dx + e·u`, `v = p + F·i(v)`, `x' = Ax + Cu + Ci`, `y = …`.
* Per sample, Newton–Raphson on the 2 port voltages:
  * warm start: the first step re-uses the previous sample's evaluation and its cached
    (inverse) Jacobian — it is exactly the linear prediction and costs no device evaluation;
  * analytic Jacobian; compile-time sparsity (dIg/dVpk = 0);
  * stop when every Newton step < tolerance (1 mV grid, 10 mV plate) and accept the linearised
    current there (error O(step²)); measured against a fully converged solve (1e-8 V) the
    output differs by < −124 dB at every level;
  * SPICE-style junction limiting on the plate: Vpk may fall at most 4× per iteration. Without
    it, a strongly positive grid made Newton 2-cycle between Vpk < 0 (where Koren's current is
    zero and flat) and Vpk ≈ B+;
  * hard cap of 12 evaluations (never reached in tests; worst seen: 4 up to +18 dBFS, 6 with
    +56 dB of drive).
* Next sample's prediction p' = (DA)x + (DC)i + … is formed from precomputed products, so the
  large matrix work does not sit on the solve's dependency chain.
* `reset()` puts the circuit exactly at its quiescent point: the discrete fixed point
  x = (I − A)⁻¹(…) solved once in `prepare()` by damped Newton with supply stepping.
* Real-time: no allocation, locks or exceptions in `process()`/`reset()`; bounded work;
  deterministic; states flushed below 1e-200 once per block (denormal insurance; a test runs
  30 s of silence and finds no subnormal output).

## Validation against an independent reference

There is no SPICE here, so `tests/models/valve/reference/validate_valve.py` simulates **the same
netlist in continuous time**: a 6-state stiff ODE (every node
is pinned by a capacitor or source) integrated by scipy's Radau IIA (implicit, order 5,
rtol 1e-10, atol 1e-12, max step 10 µs), exact Koren/grid equations (no tables), its own operating
point (brentq + fsolve) and its own make-up gain (linearised ODE at 1 kHz). Both start from the
quiescent point with a 1 kHz sine at t = 0; 40 ms compared.

| A (dBFS) | rate | RMS error | max error after 1 ms | H1…H5 reference vs C++ (dB) |
|---|---|---|---|---|
| 0.063 (−24) | 192 k | −92.3 dB | — | identical to 0.1 dB |
| 0.063 | 768 k | −117.0 dB | | identical |
| 0.25 (−12) | 192 k | −92.3 dB | | identical |
| 0.25 | 768 k | −116.9 dB | | identical |
| 1 (0) | 192 k | −92.3 dB | −105 dB | −0.1 −28.2 −50.2 −87.4 −106.1 (both) |
| 1 | 768 k | −116.9 dB | | identical |
| 8 (+18) | 192 k | −77.1 dB | −60 dB (edge timing) | 8.8 −0.9 −8.6 −6.4 −26.4 (both) |
| 8 | 768 k | −101.6 dB | | identical |

Operating point and |H(1 kHz)| (35.36 dB) match. Errors fall ≈ 25 dB per 4× rate: the expected
second-order convergence of the trapezoidal rule, i.e. the C++ model solves the circuit
correctly and the residual is discretisation error, smallest where it matters (low drive).
The largest absolute error is the first sample (different input-start convention).

## Measurements (192 kHz, 1 kHz sine)

| Input | THD | H2 | H3 | H4 | H5 | even/odd |
|---|---|---|---|---|---|---|
| −24 dBFS (0.063) | 0.24 % | −52.3 | −98.6 | | | 207 |
| −12 dBFS (0.25) | 0.96 % | −40.3 | −74.7 | | | 52 |
| 0 dBFS (1) | 3.95 % | −28.1 | −50.2 | −85 | −93 | 12.8 |
| +18 dBFS (8) | 39.8 % | −9.8 | −17.4 | −15.3 | −35.6 | 2.6 |
| +38 dBFS (80) | 46.8 % | −11.7 | −12.5 | −13.3 | −25.7 | 1.5 |

(harmonics in dB relative to the fundamental). H2 is 20–46 dB above H3 up to 0 dBFS: a clean
second-order curvature. At +18 dBFS the stage clips asymmetrically (cutoff vs grid-conduction
clamp) into a rounded rectangular wave with unequal duty; above that the source limit holds the
waveform shape.

Frequency response (−60 dBFS): −0.8 dB @ 20 Hz, −0.13 @ 50 Hz, flat to 5 kHz (−0.1),
−0.4 @ 10 kHz, −1.6 @ 20 kHz, −3.3 @ 30 kHz (Miller).

Dynamics: after a 50 ms hit at +18 dBFS the grid bias sits > 1 V colder (blocking) and
recovers within 500 ms; a +18 dBFS burst squashes by ≈ 0.4 dB over its first 20 ms (bias walk),
then cathode and supply move it slowly.

## Performance

Release, one channel, `UmberTests_valve "[.perf]"` (minimum of 3 runs on a shared machine):

| rate | amp 0.1 | amp 4 |
|---|---|---|
| 192 kHz | 60 ns/sample | 87 ns/sample |
| 384 kHz | 58 ns/sample | 62 ns/sample |

Silence costs 14 ns (no device evaluation). Average device evaluations per sample: 1.0 at
moderate level, ≈ 1.4 at heavy drive.

## Tests

`tests/models/valve/ValveModelTests.cpp`: conformance suite; operating point; THD windows;
H2 dominance (≥ 15 dB over H3 up to 0 dBFS); blocking bias shift and recovery; attack squash;
frequency response; Newton convergence budget at 88.2/768 kHz up to +56 dB of drive;
no denormals after 30 s silence; harmonic signature independent of rate.

To reproduce (Python 3 with numpy + scipy; the script compiles a small waveform-dump tool
against the model sources with the system C++ compiler):

```
python3 -m venv /tmp/venv && /tmp/venv/bin/pip install numpy scipy
/tmp/venv/bin/python tests/models/valve/reference/validate_valve.py
```

## Knobs for voicing

`kInputVolts` (where it starts to bite), Rg / Rs (depth of blocking), Ck (cathode release),
Rsup/Csup (sag), Cgp (Miller top end). Changing values only means editing `makeNetlist()`:
matrices, operating point and make-up gain are re-derived in `prepare()`.

## Bias

The grid leak Rg returns to a bias supply (`Netlist::biasSupply`, 0 V nominally, ±1.3 V at the
extremes), like a fixed-bias trim added to the cathode bias. In the nodal solver it is a
separate DC input column. Its operating points are solved exactly on a 17-point grid reaching
25 % past the range (`NodalCircuit::setBiasVolts`). The extra node and source change the nominal
output by round-off only: the golden file is unchanged within its tolerance.

Measured at 1 kHz, 0 dBFS in (192 kHz):

| Bias | Gain | H2 | H3 | THD |
|---|---|---|---|---|
| cold (−1) | −2.4 dB | −22.1 dBc | −46.9 dBc | 7.9 % |
| nominal | −0.1 dB | −28.1 dBc | −50.2 dBc | 4.0 % |
| hot (+1) | +0.4 dB | −33.0 dBc | −31.6 dBc | 3.6 % |

Cold moves the triode towards cutoff: more 2nd harmonic. Hot moves it towards grid conduction:
less 2nd, and the 3rd rises as the positive peaks clamp.

The chain smooths the control (80 ms) and each model glides per sample (`BiasRamp`, 64 samples).
The operating point moves quasi-statically: the coupling capacitors are treated as already
holding the new DC, so turning the knob never thumps (`tests/BiasTests.cpp`: silence stays below
-88 dBFS while the bias sweeps its full range four times in 0.6 s). At 0 the model is
bit-identical to the nominal circuit.
