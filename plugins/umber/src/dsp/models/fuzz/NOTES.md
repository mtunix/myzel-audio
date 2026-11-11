# Fuzz — 12AX7 driver into a slammed single-ended EL84 with output transformer

The most aggressive model: a triode driver into a power pentode with its output transformer,
both tubes coupled in one circuit solve. The fuzz is emergent: the pentode squares the wave
off between cutoff and plate saturation (odd harmonics), the driver and the cold bias add the
uneven edge, and interstage grid conduction starves the EL84's bias so slammed material gates
and sputters.

## Circuit

```
                                       B+ 285 V
                                          |
                  +------- Rscr 1k -------+-----------------------+
                  |                       |                       |
           screen node -- Cscr 22u        |      OT primary:      |
           (= EL84 g2)                    |   Rw 250 + Lp 40H     |
                  |                       |   || RL' 5k || Cw 1n  |
               Ra 100k                    |                       |
                  |       Cc 47n          +-----------+-----------+
                  +--------||-----+                   | plate2
                  | plate1        |                   |
in -||-+- Rs1 33k-+ grid1         +-- Rs2 10k --- g1 EL84 (Koren pentode + division)
Cin 22n|        12AX7           Rg2 220k              | cathode2
     Rg1 1M       | cathode1      |            Rk2 180 || Ck2 220u
       |    Rk1 1.5k || Ck1 6.8u  gnd                 |
      gnd         |                                  gnd
                 gnd
```
Output: the primary voltage (what the secondary delivers), make-up normalised.

| Part | Value | Why |
|---|---|---|
| Cin1 / Rg1 / Rs1 | 22 nF / 1 MΩ / 33 kΩ | Driver input; driver grid conduction only with Slam-level input. |
| Ra1 from the screen node | 100 kΩ | Driver shares the screen supply, as in real SE amps: screen sag modulates the driver too. |
| Rk1 / Ck1 | 1.5 kΩ / 6.8 µF | Driver bias −1.35 V; partial bypass (full gain above ~30 Hz) keeps the lows tight. |
| Cc / Rg2 / Rs2 | 47 nF / 220 kΩ / 10 kΩ | Interstage. Grid conduction of the EL84 charges Cc through the driver's plate resistance → bias starvation; it recovers over Cc·Rg2 ≈ 10 ms → gate / sputter on decays. |
| Rk2 / Ck2 | 180 Ω / 220 µF | EL84 at −8.8 V, 42 mA: a little colder than the datasheet's −7.3 V / 48 mA, so it starves sooner. Large bypass keeps the cathode shelf below 15 Hz. |
| Rscr / Cscr | 1 kΩ / 22 µF | Screen supply: sags when screen current rises (current division at plate bottoming). |
| OT | Rw 250 Ω, Lp 40 H, RL' 5 kΩ, Cw 1 nF | SE transformer: magnetising inductance as the DC path (HPF, −3 dB ≈ 50 Hz with the driver shelf), reflected load, winding capacitance (top end), flyback on cutoff. |
| Source limit | linear to 10 V, tanh to 20 V | As in Valve: finite swing of whatever drives it; above ≈ +38 dBFS only. |
| kInputVolts | 0.12 V per unit | Places the contract levels (see table). |

Removed after measurement (each < 0.3 dB / < 0.5 % THD difference at every level, ~16 ns saved):
driver Miller capacitance (the OT's winding capacitance sets the top), driver plate stray, and the
reservoir sag cap (the screen RC carries the sag).

## Tube models

* Driver: Koren 12AX7 + smooth grid diode — see `valve/NOTES.md`.
* EL84 (`src/dsp/common/tubes/KorenPentode.h`): Koren's pentode equations
  `E1 = Vg2k/Kp · ln(1 + exp(Kp(1/µ + Vg1k/Vg2k)))`, `Ik = 2E1^Ex/Kg1`, `Ip = Ik·atan(Vpk/Kvb)`.
  Koren publishes no EL84 set, so it was fitted (µ = 19 = µg1g2, Ex = 1.35, Kp = 200 as for his
  pentodes; Kg1, Kg2 solved) to the Philips datasheet class-A point Va = Vg2 = 250 V,
  Vg1 = −7.3 V → Ia 48.0 mA (datasheet 48), Ig2 5.5 mA (5.5), gm 10.9 mA/V (11.3).
  Kvb = 30 is a compromise: Koren's atan knee cannot match both the knee sharpness and ra
  (ra = 64 kΩ vs 38 kΩ datasheet — irrelevant into a 5 kΩ load; the knee shape is what you hear).
  Result: Kg1 = 659, Kg2 = 2774.
* Screen current: Koren's `(Vg2k/µ + Vg1k)^Ex / Kg2` is evaluated as `E1^Ex/Kg2 = Ik·Kg1/(2Kg2)`
  (identical when conducting since E1 → Vg2k/µ + Vg1k; smooth instead of max(0, ·) at cutoff;
  free), **plus current division**: `kDiv·Ik·(π/2 − atan(Vpk/Kvb))`, kDiv = 0.4 — electrons that
  can no longer reach a bottomed-out plate land on the screen. Zero above the knee (fit
  untouched), gives the real pentode's steep screen-current rise when the plate saturates
  (Ig2 15 mA at Va = 25 V), which sags the screen and adds compression and rasp.
* Grid 1 conduction: smooth grid diode with RGI = 1 kΩ (Koren's pentode value).

## Solver

Same nodal DK solver as Valve (`NodalCircuit.h`; see `valve/NOTES.md` for the method, the
references and why it was preferred over a hand-built R-type WDF), here with **five coupled
nonlinear ports** (Vgk1, Vpk1, Vg1k2, Vg2k2, Vpk2) and 7 states including the transformer's
inductance. Specifics:

* 5×5 Newton system, analytic Jacobian, compile-time sparsity of both the device Jacobian and
  the netlist (see Performance). The Newton matrix −(I + Z·G) (Z the passive port impedances,
  G the device conductances) is inverted explicitly through a block split: grid 1 and plate 2
  only touch their own tube's rows, so that block is diagonal, and the 3×3 Schur complement is
  inverted by the adjugate (two divisions on the chain instead of five). The inverse is cached
  across samples for the predictor step; every Newton solve is a matrix-vector product.
* Junction limiting on **both** plates. The pentode needed it too: `atan(Vpk/Kvb)` is flat on
  both sides of the knee, and with the inductive load in flyback Newton 2-cycled between
  −330 V and +510 V (seen at +38 dBFS / 88.2 kHz) until the plate step was limited.
* Tolerances 3 mV (grids), 30–50 mV (plate, screen; plate swings ~500 V): output within
  −124 dB of a fully converged solve at 0.1/1/8 amplitude; cap 16 evaluations, worst seen ≤ 7.

## Validation against an independent reference

`validate_fuzz.py` (see "Reproducing" below) simulates the same netlist in continuous time as a
DAE: Radau IIA (rtol 1e-10) over the 7 reactive states with the 3 algebraic nodes
(grid1, plate1, grid2) solved by a nested Newton in every RHS call; exact Koren equations, its own
operating point and make-up gain. 40 ms from the quiescent point, 1 kHz sine.

Operating point (both): Vgk1 −1.348 V, Vpk1 187.9 V, Vg1k2 −8.785 V, Vg2k2 270.4 V,
Vpk2 265.8 V, Ip2 41.8 mA; small-signal gain 68.45 dB.

| A (dBFS) | rate | RMS error | max error after 1 ms | H1…H5 reference = C++ (dB) |
|---|---|---|---|---|
| 0.063 (−24) | 192 k | −91.4 dB | −98.4 dB | −24.0 −78.3 −93.6 −129.3 −134.7 |
| 0.063 | 768 k | −116.1 dB | −122.4 dB | |
| 0.25 (−12) | 192 k | −91.3 dB | −98.0 dB | −12.2 −55.0 −57.5 −84.3 −101.8 |
| 0.25 | 768 k | −115.9 dB | −122.0 dB | |
| 1 (0) | 192 k | −88.6 dB | −91.1 dB | −2.8 −31.4 −22.0 −45.6 −45.2 |
| 1 | 768 k | −113.2 dB | −115.1 dB | |
| 8 (+18) | 192 k | −62.1 dB | −46.0 dB (edge timing) | −2.0 −13.3 −14.9 −15.1 −31.2 |
| 8 | 768 k | −86.6 dB | −69.2 dB | |

Harmonics agree to 0.1 dB at every level; errors fall ~25 dB per 4× rate (second-order
trapezoidal discretisation error, largest on the hard-clipped edges at +18 dBFS).

## Measurements (192 kHz, 1 kHz sine)

| Input | THD | H2 | H3 | H4 | H5 | even/odd |
|---|---|---|---|---|---|---|
| −24 dBFS (0.063) | 0.20 % | −54.2 | −69.7 | −108 | −136 | 5.9 |
| −12 dBFS (0.25) | 0.90 % | −42.9 | −45.4 | −72.1 | −87.9 | 1.3 |
| 0 dBFS (1) | 11.4 % | −30.5 | −19.2 | −44.3 | −42.5 | 0.28 |
| +18 dBFS (8) | 46.0 % | −11.6 | −12.7 | −13.3 | −27.2 | 1.6 |
| +38 dBFS (80) | 66.7 % | −5.2 | −23.6 | −13.2 | −12.8 | 2.3 |

Clean-ish at −24 (well inside the Fuzz's 5 % allowance), balanced H2/H3 at −12, **odd-dominant
when driven** (H3 11 dB over H2 at 0 dBFS), and past +18 dBFS the starved bias makes the waveform
gated and asymmetric (even/odd > 1, THD above 40 %: beyond the "heavy" guideline on purpose —
this is the fuzz).

Frequency response (−60 dBFS): −11.4 dB @ 20 Hz, −4.2 @ 40, −2.8 @ 50, −0.8 @ 100, flat to
5 kHz, −0.4 @ 10 kHz, −1.4 @ 20 kHz, −2.9 @ 30 kHz.

Dynamics (test "slammed, the interstage bias starves"): during a +30 dB slam the EL84 grid
averages −39.8 V (quiescent −8.8 V); a moderate 1 kHz tone right after it comes out 47 dB down
for ~10–20 ms (gate), then returns to normal within 250 ms. Each release also produces the SE
transformer's flyback "whump" (plate kicks to ~530 V as the tube cuts off).

## Performance

Release, one channel, `UmberTests_fuzz "[.perf]"` (median of 3 runs, shared machine):

| rate | amp 0.1 | amp 4 |
|---|---|---|
| 192 kHz | 81 ns/sample (was 148) | 105 ns/sample (was 186) |
| 384 kHz | 81 ns/sample (was 146) | 96 ns/sample (was 172) |

Silence 18 ns (was 46). In the plugin at Standard quality (4x, 48 kHz stereo, chain perf test):
**4.4 %** of one core (was 7.0 %).

The solve is ≈ 1.1–1.4 device evaluations per sample, and each sample is one dependency chain
(predict → device → Newton matrix → solve → state update), so the work went into instruction
count and chain latency. Three changes (heavy-drive chain condition, 220 Hz into 192 kHz at
amplitude 7.4: 166 → 150 → 112 → 91 ns/sample):

1. **Explicit block inverse** of the Newton matrix (see Solver) instead of LU + two triangular
   solves: 5 chained divisions → 2, and both Newton solves per sample become matrix-vector
   products. Output identical to float precision.
2. **Netlist structure at compile time** (`tubes::analyseStructure`, `NodalCircuit.h`). The
   netlist is `constexpr` (`fuzz::makeNetlist`, `FuzzModel.h`), so its topology is known at
   compile time: ideal voltage sources pin their node, and the remaining nodes fall apart into
   islands (each cathode, each grid network, the driver plate / screen / EL84 grid, the output
   plate). An entry of the state-space matrices is structurally zero when its row and column
   touch no common island: only 63 of the 178 per-sample state-update multiply-adds and 17 of
   the 25 entries of F remain, and the Newton matrix gets the zeros that make the block split
   above diagonal. The skipped terms are exact zeros (unrolled `if constexpr`), so this is
   bit-exact; `prepare()` verifies the pattern (test: "compile-time sparsity holds at every
   rate").
3. **Table-based tube currents** (`TubeMath.h`). `Ik = 2/Kg1·(V/Kp)^Ex·softplus(a)^Ex` with
   `softplus^Ex` from a per-exponent cubic-Hermite table and `(V/Kp)^Ex` from a mantissa-indexed
   power table, `atan(Vpk/Kvb)` from a table on [−1, 1] (plus π/2 − atan(1/q) outside), the
   grid diodes from a softplus table. No exp / log / atan left on the critical path (they were
   30–80 cycles each, in series): device latency 42 → 31 ns. Tables are shared and built once
   (~215 kB in total); relative current error < 1.1e-8 where the tube conducts, < 2e-7 deep in
   cutoff (where the current is < 1e-4 of the operating current), atan < 1e-9 absolute.

Difference to the previous model (192 kHz, 1 kHz unless noted, settled): output error
−147 dB (A = 0.063) … −157 dB (A = 8) relative to the signal, fundamental level within
2e-7 dB, THD identical to 4 decimals (0.1969 %, 0.8992 %, 11.379 %, 46.026 %, 66.741 % at
A = 0.063 / 0.25 / 1 / 8 / 80), Newton evaluation counts identical, and `validate_fuzz.py`
reproduces the validation table above digit for digit. Golden file: −144.5 dB
(threshold −100 dB), not re-recorded. Valve shares the triode and the tables: −144 dB,
level within 4e-7 dB, 60 → 49 ns/sample.

Tried and not kept: keeping the block factors and solving by block substitution (fewer
instructions, but a longer chain: slower); inlined table-based exp / log (fewer instructions
than libm, but without FMA a longer latency: no net gain). Remaining options, not needed now: chord
step (reuse the previous inverse for the converged check, ≈ −15 ns, costs accuracy at the
tolerance), screen port explicit (4×4 Newton).

## Tests

`tests/models/fuzz/FuzzModelTests.cpp`: conformance suite; operating points; the solver's
compile-time sparsity holds at every rate from 88.2 to 800 kHz; THD windows;
odd-rich when driven (H3 ≥ H2 + 6 dB, even/odd < 0.5) with the H2 still present; bias starvation
gates a following tone by > 10 dB and recovers; OT band-limits; Newton convergence budget at
88.2/768 kHz up to +56 dB of drive; no denormals after 30 s silence; rate-independent signature.

## Reproducing the validation

`tests/models/fuzz/reference/validate_fuzz.py` (and `tests/models/valve/reference/validate_valve.py`)
build a small waveform-dump tool against the model sources with the system C++ compiler, then run
the reference. Needs Python 3 with numpy and scipy:

```
python3 -m venv /tmp/venv && /tmp/venv/bin/pip install numpy scipy
/tmp/venv/bin/python tests/models/fuzz/reference/validate_fuzz.py
```

## Bias

The EL84's grid leak Rg2 returns to a fixed-bias supply (`Netlist::biasSupply`, 0 V nominally,
±6.5 V at the extremes), on top of the cathode bias: the classic power-tube bias adjustment. The
solver mechanism is the same as Valve's (`NodalCircuit::setBiasVolts`). The netlist stays
`constexpr`, and the supply pins its node, so the compile-time sparsity analysis is unchanged.

Measured at 1 kHz, 0 dBFS in (192 kHz):

| Bias | Gain | H2 | H3 | THD |
|---|---|---|---|---|
| cold (−1) | −5.0 dB | −12.8 dBc | −22.2 dBc | 24.6 % |
| nominal | −2.8 dB | −30.5 dBc | −19.2 dBc | 11.4 % |
| hot (+1) | −1.9 dB | −20.0 dBc | −21.3 dBc | 13.3 % |

Cold starves the EL84: it cuts off early on one half-wave, so it gates and sputters, with strong
even content. Hot is fuller and earlier to compress.

The chain smooths the control (80 ms) and each model glides per sample (`BiasRamp`, 64 samples).
The operating point moves quasi-statically: the coupling capacitors are treated as already
holding the new DC, so turning the knob never thumps (`tests/BiasTests.cpp`: silence stays below
-88 dBFS while the bias sweeps its full range four times in 0.6 s). At 0 the model is
bit-identical to the nominal circuit.
