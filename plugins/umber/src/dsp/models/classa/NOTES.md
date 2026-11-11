# Class A — single-ended discrete transistor console stage

A generic two-transistor class-A line/preamp stage of the British discrete-console school:
a degenerated common-emitter voltage amplifier (Q1), DC-coupled to a class-A emitter
follower (Q2) that drives an output transformer through a large coupling capacitor.
Nothing here copies a specific product; values are chosen for sound and plausibility.

## Circuit

```
                    +24 V ──────┬─────────────────┬──────────────────────────┐
                                │                 │                          │ C
                              R1 47k            Rc 5k6                       │
                                │                 │ C1                  B  ┌─┴─┐
  vin ── Rs 600 ── A ──┤├───────┼── B ── rbb ──Bi─┤            ├───────────┤Q2 │ follower
                      Cin 4µ7   │   │         ┌───┴───┐        │           └─┬─┘
                                │   └─Ccb 33p─┤ Q1    ├─C1─────┘             │ E3
                              R2 6k8    (C1–B)└───┬───┘                      ├── Re3 1k5 ── GND
                                │                 │ E1                       │
                               GND              Re1 22                     Cout 100µ
                                                  │ E2                       │ T0
                                             ┌────┴─────┐                  Rw 40
                                           Re2 1k    Ce 1000µ                │ T
                                             │          │               ┌────┴────┐
                                            GND        GND            Lm 4 H    RL 600 ──── out = V(T)
                                                                   (saturating   │
                                                                      core)     GND
                                                                         │
                                                                        GND
```

| Part | Value | Role |
|---|---|---|
| Vcc | 24 V | single rail |
| Rs | 600 Ω | source impedance |
| Cin | 4.7 µF | input coupling — rectified base current charges it (bias shift) |
| R1 / R2 | 47 k / 6.8 k | base divider, V_B ≈ 2.98 V |
| rbb | 120 Ω | base spreading resistance |
| Q1 | Is 1e-14 A, βF 250, βR 4 | small-signal NPN, full Ebers–Moll (saturates) |
| Re1 | 22 Ω | unbypassed degeneration (≈ 2·re): sets the soft H2 onset |
| Re2 ∥ Ce | 1 k ∥ 1000 µF | bias resistor, bypassed — charges under level (bias shift) |
| Rc | 5.6 k | collector load, Q1 at 2.30 mA, V_C1 = 11.1 V |
| Ccb | 33 pF | collector–base (Miller) compensation incl. Cμ → −0.5 dB at 20 kHz |
| Q2 | Is 4e-14 A, βF 120 | medium-power NPN follower, collector on the rail (forward junction only) |
| Re3 | 1.5 k | class-A pull-down, Q2 at 6.96 mA |
| Cout | 100 µF | output coupling — charges when the follower cuts off |
| Rw | 40 Ω | primary winding resistance |
| Lm | 4 H | magnetising inductance; core saturates above ~0.3·φsat, φsat = 0.016 V·s |
| RL | 600 Ω | load reflected across the primary |

Output = primary voltage V(T), inverted (Q1 inverts; the follower and the transformer don't),
scaled by the make-up gain. Input mapping: 1.0 full scale = **35 mV** source EMF. The driving
line stage is modelled as a soft rail, `v / sqrt (1 + (v / 12 V)^2)`: −0.2 dB at +38 dBFS,
and it bounds absurd inputs (±1000 → ±12 V).

### Where the character comes from

* **Onset (H2):** Q1's exponential with only ~2·re of degeneration. H2 grows ∝ level
  (0.18 % at −24 dBFS, 0.7 % at −12 dBFS), H3 stays ~30 dB below.
* **Clipping (H2 + H3):** at about −3 dBFS the follower runs out of pull-down current on
  negative output swings (cut-off at ≈ Ie2·(Re3 ∥ load) ≈ 3 V), while positive swings only
  clip at the rail / Q1 saturation — strongly asymmetric. By 0 dBFS H2 ≈ H3 ≈ 7 %.
* **Bias shift / compression:** the BE junction rectifies into Cin, the emitter current
  shift charges Ce, and follower cut-off charges Cout. A sustained +12 dBFS burst comes in
  hot and settles ≈ 4 dB lower over ~100 ms ("punch, then squash"); quiet signals don't move.
* **LF:** sub-sonic cut (−0.9 dB at 8.5 Hz, −12 dB at 5 Hz, no peak); the core saturates
  only when driven at low frequency (30 Hz, 0 dBFS: 21 % THD vs 11 % at 1 kHz), giving the
  bottom end weight without smearing the mids.

## Solver: nodal DK method (`DkSolver.h`)

The netlist is stamped into MNA form; capacitors and inductors use trapezoidal companion
models (equivalent to bilinear-transform WDF one-ports). Per sample rate the linear part is
reduced to a discrete state-space system (Yeh, Abel & Smith, "Automated physical modeling of
nonlinear audio circuits for real-time audio effects", IEEE TASLP 2010; Holters & Zölzer,
"A generalized method for the derivation of non-linear state-space models from circuit
schematics", EUSIPCO 2015):

```
p  = Px·x + pu·vin + p0
v  = p + K·i,  i = f(v)          4 ports: Q1 B-E, Q1 B-C, Q2 B-E, core flux
x' = Ax·x + au·vin + Ai·i + x0   5 states: Cin, Ccb, Ce, Cout, Lm
y  = cx·x + cu·vin + ci·i + y0
```

Why DK rather than a chowdsp_wdf R-type root: with two transistors and a transformer core
the nonlinearities are mutually coupled through most of the network, so a WDF would need one
R-type adaptor holding all four ports — the same 4-port Newton problem, plus a hand-derived
scattering matrix. The DK reduction is mathematically equivalent (both are trapezoidal
discretisations of the same MNA system) but is derived automatically from the netlist in
`prepare()`, which made it practical to iterate on topology and values and to validate the
exact same netlist against an independent simulator.

Newton–Raphson (real-time safe):

* Ebers–Moll transport model, each junction a scalar port `Is·(exp(v/Vt) − 1)`, so the
  Jacobian of f is diagonal; `exp()` is continued linearly beyond 40·Vt (no overflow), and a
  junction below −40·Vt is exactly off (no denormals, zero slope).
* Warm start: the first step starts from the previous sample's solution using its stored
  currents and slopes (no `exp()`); usually one more evaluation converges.
* Zero-slope ports (reverse-biased B-C, core below its knee) decouple exactly, so the linear
  solve is normally a closed-form 2×2; 3×3/4×4 only in saturation.
* SPICE `pnjlim` junction-voltage limiting; convergence when every active port moves less
  than 1e-4·Vt (1e-4·φsat for the core), followed by a first-order current update (residual
  error ≈ tol²); hard cap of 16 iterations.
* Average iterations at 192 kHz: 2.0 (−12 dBFS), 2.8 (0 dBFS), 3.1 (+12 dBFS), 2.5 (+38 dBFS).
* Rest state and make-up gain come from a DC Newton solve of the same netlist (capacitors
  open, inductor shorted) and an exact complex evaluation of the linearised *discrete* system
  at 1 kHz, so unity gain holds at every sample rate.
* Denormal safety without FTZ: states and port voltages snap to 0 below 1e-20.
* Switching events (a junction cutting off, turning on or saturating within a sample period)
  are re-solved in 3 sub-steps, and the model plays period averages instead of point samples:
  see "Aliasing" below.

## Validation (independent Python reference)

`validation/reference.py` (run `python3 validation/reference.py`; needs numpy, scipy, g++ —
it compiles a small C++ dump tool against `ClassACircuit` itself; `CLASSA_QUICK=1` stops after
DC + AC) writes the KCL equations of the same netlist by hand — no DK reduction, no companion
models — and uses scipy: `optimize.root` for DC, a complex nodal admittance matrix for AC, and
the adaptive 5th-order implicit **Radau IIA** integrator (rtol 1e-9, 1 pF parasitic per node)
for transients from rest. The C++ circuit output is compared sample-by-sample, and its
period averages (what the model plays, before the equaliser; see "Aliasing") against 4-point
Gauss-Legendre averages of the reference's dense solution.

**DC operating point:** every node within 1e-7 V (Ic1 = 2.302 mA, Ie2 = 6.959 mA,
V_C1 = 11.107 V, V_E3 = 10.438 V).

**Small-signal response** (C++ discrete system at 768 kHz vs analog nodal AC): worst
magnitude difference **0.002 dB from 2 Hz to 40 kHz**; |H(1 kHz)| = 118.206 in both.

**Transients** from rest, 1 kHz sine, 40 ms, C++ at 192 kHz (so the bias shift is still
developing — THD here is lower than the steady-state table below):

| Level | max abs err V(T) | rms err / rms | THD ref | THD C++ | H2/H3 ref (%) | H2/H3 C++ (%) |
|---|---|---|---|---|---|---|
| −24 dBFS | 0.56 mV | 5.3e-5 | 0.18 % | 0.18 % | 0.178 / 0.001 | 0.178 / 0.001 |
| −12 dBFS | 2.2 mV | 5.3e-5 | 0.71 % | 0.71 % | 0.705 / 0.023 | 0.705 / 0.023 |
| 0 dBFS | 8.8 mV | 5.9e-5 | 8.58 % | 8.58 % | 4.747 / 6.083 | 4.748 / 6.083 |
| +18 dBFS | 70 mV | 1.8e-4 | 37.88 % | 37.88 % | 23.80 / 21.61 | 23.79 / 21.62 |
| +38 dBFS | 0.79 V* | 2.6e-3 | 62.74 % | 62.73 % | 53.15 / 5.73 | 53.15 / 5.73 |

Period averages of the same runs (the model's output before its equaliser) against the
reference's exact interval averages: rms err/rms 3.9e-5, 3.9e-5, 6.1e-5, 2.0e-4, 3.7e-3.
(Before the switching events were sub-stepped, +38 dBFS was at 2.4 V / 8.3e-3 and 62.71 % THD.)

40 Hz (core saturation region), 100 ms: rms err/rms 2.1e-6 at 0 dBFS and 3.7e-6 at +12 dBFS,
peak V(T) 4.214 V / 9.517 V in both.

The ~5e-5 floor is the trapezoidal rule's phase warping at 1 kHz / 192 kHz ((ωT)²/12 ≈ 9e-5),
i.e. discretisation, not model error. *At +38 dBFS the error concentrates on the
near-vertical switching edges (one-sample timing), the spectrum still agrees to 0.01 points.

## Measurements (1 kHz, 192 kHz, conformance definitions)

| Level | THD | H2 | H3 | gain |
|---|---|---|---|---|
| −24 dBFS | 0.18 % | 0.18 % | 0.001 % | 0.00 dB |
| −12 dBFS | 0.70 % | 0.70 % | 0.02 % | −0.01 dB |
| −6 dBFS | 1.4 % | 1.4 % | 0.11 % | −0.03 dB |
| 0 dBFS | 10.9 % | 7.0 % | 7.3 % | −1.2 dB |
| +6 dBFS | 27.5 % | 24.2 % | 11.4 % | −4.5 dB |
| +18 dBFS | 35.6 % | 1.8 % | 30.3 % | −14.9 dB |
| +38 dBFS | 64.6 % | 51.9 % | 2.7 % | −37.5 dB |

(Unchanged by the aliasing work: every level within 0.02 THD points of before.)

(+18 dBFS is the level where the two clipping sides happen to balance; H2 returns above it.)

Small-signal response re 1 kHz (circuit): −0.23 dB at 20 Hz, −0.12 dB at 10 kHz, −0.51 dB at
20 kHz, −2.1 dB at 40 kHz. The model's output (period average + equaliser) at 192 kHz: −0.12 dB
at 10 kHz, −0.43 dB at 20 kHz. Abuse (±1000 squares/noise): peak output 3.8–5.2, recovers to rest.

Performance (Release, `UmberTests_classa "[.perf]"`, one channel, 997 Hz sine, 3 runs):

| Rate | −20 dBFS (amp 0.1) | +12 dBFS (amp 4) |
|---|---|---|
| 192 kHz | 72 ns/sample (was 65) | 110–118 ns/sample (was 96) |
| 384 kHz | 69–72 ns/sample (was 63) | 96–100 ns/sample (was 89–95) |

Cost is per sample and nearly rate-independent; heavier drive costs ~one more Newton
iteration. Most of the time goes into the latency chain of the per-sample solve, not `exp()`.
Switching events add 1 + 3 solves each (six per period at +12 dBFS). Chain, Standard 4x at
48 kHz stereo, Drive 7 at 220 Hz (`"Chain performance report"`, median of 7): 4.5 % of one
core (4.2 % before).

## Aliasing: switching events, sub-steps and period averages

**Where it came from.** A 7 kHz tone at +9 dBFS (Drive 5 at 44.1 kHz) at 176.4 kHz (Standard
4x) folded back at −53 dBc (8.4 kHz). The model's sampled output was traced at 2.8 MHz:

* The follower Q2 cuts off at ~16° of every period and turns back on at ~178°. Its base (Q1's
  collector) slews at ~0.5 V/µs while the junction needs only ~4·Vt to go from following the
  base to blocking, so the emitter current goes from "following" to zero (and back) within
  ~0.3 µs; while it is off the output sits flat and Q2's base-emitter junction is reverse
  biased by up to 7 V. Q1 saturates (53°–140°) while Q2 is off, which does not reach the output.
* Each of the two bends carries ≈ −58 dBc of harmonics around 150–200 kHz (H20–H30); the
  true spectrum has H24 at −53, H26 −55, H28 −58 dBc. The in-model fold-back matched these
  true harmonics bin for bin (H24 → 8.4 kHz at −53.1, H26 → 5.6 kHz −55.2, H28 → 19.6 kHz
  −57.9 dBc): genuine aliasing of a genuinely sharp bend, not a solver artefact (no Nyquist
  ringing, Newton converged).
* The output's fold-back equals that of Q2's emitter current (the nonlinear port), so a linear
  part *after* the junction cannot remove it, however much it rolls off: only a smoother port
  current or a different way of sampling helps.

**Physical parts tried (and not adopted).** In-model worst alias at 4x / Drive 5 (−53.1 before):

| Added part | Worst alias |
|---|---|
| Q2 junction capacitances, Cje 100 pF + Cjc 30 pF | −53.1 dBc |
| Q2 Cje 1 nF (power-transistor size) | −39.8 dBc (a capacitive step at cut-off) |
| Q2 diffusion charge τF = 50 / 100 / 200 / 400 ns (charge-control, an extra state) | −53.6 / −55.0 / −53.6 / −55.2 dBc |
| Output-transformer leakage inductance 2 / 5 / 10 mH (± 1–2 nF winding capacitance) | −53.6 … −54.7 dBc |
| combinations of the above | −50 … −57 dBc |

The leakage inductance lowers the true even harmonics by ~10 dB but leaves Re3's resistive
path to carry a current bend, and the odd harmonics, which cancelled between the two bends
before, come up. A real transistor turns off this fast; the bend is physics.

**The fix: sample the circuit as an integrate-and-dump converter would.** The netlist is
unchanged. `ClassACircuit` delivers the circuit's output *averaged over each sample period*
instead of its value at the sampling instant. The average's spectrum (sinc) has nulls at every
multiple of the sample rate, exactly where the harmonics that fold into the audio band sit.

* Smooth periods: the average is the integral of the quadratic through the last three samples
  (weights 5, 8, −1 over 12; exact for quadratics).
* A *switching event* is a junction crossing its knee: its incremental conductance passing a
  quarter of the conductance the circuit presents to it (`DkSolver::conductingMask`, with a
  factor-2 hysteresis band, one scale shared by both rates). When the mask changes, the sample
  period is solved again in 3 sub-steps by a second `DkSolver` of the same netlist at 3× the
  rate (input interpolated cubically through the last four inputs), the state handed over and
  back exactly (`DkSolver::transferFrom`: every capacitor's and inductor's voltage and current
  carry over; only the companion's g·v part depends on the rate), and the period's average is
  trapezoidal over the sub-steps. The sample after a bend is then averaged through the last
  sub-step rather than the sample before the bend (or sub-stepped too if the bend was in the
  last third).
* The average delays by half a sample and droops like a sinc. A three-tap equaliser on the
  averages (131, −46, 11 over 96) makes the whole smooth-period filter flat to second order with
  a quarter-sample delay, so Mix stays phase-coherent with the dry path (the 50 % blend at
  18 kHz is −0.34 dB, limit 0.5) and the audio-band response stays the circuit's (20 kHz:
  −0.43 dB vs −0.51 dB sampled, at 192 kHz). Above the audio band the model's output is a
  little lower (the averaging) — the chain's decimator removes that range anyway.
* `process()` still returns the sample at the instant (more accurate than before across
  switching edges, see Validation); `lastOutput()` is what the model plays.

Ideal reference points (always sub-stepped / averaged, too slow to ship): averaging with 4
sub-steps in every period −76.8 dBc; plain internal 2× oversampling −66.3 dBc at twice the cost.

Worst alias, 7 kHz tone at 44.1 kHz through the whole chain (`[.quality]` aliasing report):

| Quality | Drive 5 before → after | Drive 10 before → after |
|---|---|---|
| 2x | −39.3 → −56.2 dBc | −22.7 → −36.7 dBc |
| 4x | −53.1 → −75.4 dBc | −32.3 → −54.8 dBc |
| 8x | −65.9 → −88.8 dBc | −44.7 → −66.4 dBc |
| 16x | −80.7 → −98.0 dBc | −62.4 → −69.3 dBc |

Cost: one extra solve per sample only across switching events — at +12 dBFS / 1 kHz six events
per period, 1.08 solves per sample; the 7 kHz torture tone at +9 dBFS, 1.55 (237 vs 144 ns per
sample). The golden render changed by −27 dB relative (mostly the quarter-sample phase at high
frequencies; RMS level within 0.03 dB).

## Files

* `DkSolver.h` — generic DK-method engine (stamping, DC solve, Newton, AC analysis, state
  hand-over between rates, switching-event mask).
* `ClassACircuit.{h,cpp}` — the netlist and component values; switching-event sub-steps and
  the period-averaged output.
* `ClassAModel.{h,cpp}` — `CircuitModel` wrapper: input calibration, source rail, make-up gain.

## Bias

A trim draws up to ±600 µA from Q1's emitter node (a pot to the rail in parallel with Re2;
drawing current makes Q1 run hotter). It is a third DC input of the DK solver
(`DkSolver::setBiasCurrent`). Operating points are pre-solved on a 17-point grid reaching 25 %
past the range (DC solve plus a settle of the discrete system each). On a bias change the states
are shifted by the change of their rest values (Catmull-Rom), and `reset()` starts from the
interpolated point.

Measured at 1 kHz, 0 dBFS in (192 kHz):

| Bias | Gain | H2 | H3 | THD |
|---|---|---|---|---|
| cold (−1) | −1.1 dB | −27.5 dBc | −44.1 dBc | 4.3 % |
| nominal | −1.2 dB | −23.0 dBc | −22.8 dBc | 10.9 % |
| hot (+1) | −2.4 dB | −13.3 dBc | −20.4 dBc | 23.9 % |

Hot pushes Q1's collector towards saturation: much more even harmonic content and earlier
clipping. Cold backs it off: cleaner and softer.

The chain smooths the control (80 ms) and each model glides per sample (`BiasRamp`, 64 samples).
The operating point moves quasi-statically: the coupling capacitors are treated as already
holding the new DC, so turning the knob never thumps (`tests/BiasTests.cpp`: silence stays below
-88 dBFS while the bias sweeps its full range four times in 0.6 s). At 0 the model is
bit-identical to the nominal circuit.
