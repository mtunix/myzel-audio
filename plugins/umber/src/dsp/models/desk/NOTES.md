# Desk: transformer-coupled console line amplifier

Generic vintage console line-amp topology: input transformer, discrete op-amp gain stage, output
transformer into a bridging load. The transformers carry most of the character. Their cores are
nonlinear magnetising inductances with Jiles–Atherton hysteresis, so low frequencies saturate the
iron first (flux = ∫V dt).

## Circuit

```
            INPUT TRANSFORMER (1:1, 10k:10k, 80 % Ni)             LINE AMP                OUTPUT TRANSFORMER (1:1, 600:600, 50 % Ni)
 Vs ─[600 Ω src + 350 Ω DCR]─┬─[30 mH leak]─[350 Ω]─┬───┬──►(+)─ A ─[S]─┬─[SR]─[47 Ω + 40 Ω]─[2200 µF]─┬─[10 mH leak]─[40 Ω]─┬────┬── out
                             │                       │   │    │-          │                              │                       │    │
                            Lm1 (JA)              150 pF 10k  └── β ───────┘                             Lm2 (JA)               250 pF 10k
                             │                       │   │                                               │                       │    │
 ────────────────────────────┴───────────────────────┴───┴───────────────────────────────────────────────┴───────────────────────┴────┴──
```

All values are referred to the primary. Both transformers are 1:1, so no turns-ratio scaling is
needed.

| | Input transformer | Output transformer |
|---|---|---|
| Source R1 (source / build-out + primary DCR) | 600 + 350 Ω | 47 + 40 Ω |
| Series coupling cap Cc | – | 2200 µF |
| Leakage inductance | 30 mH | 10 mH |
| Secondary DCR R2 | 350 Ω | 40 Ω |
| Winding / stray capacitance | 150 pF | 250 pF |
| Load | 10 kΩ (amp input termination) | 10 kΩ (bridging) |
| Small-signal Lm (demagnetised) | 46 H | 2.5 H |
| Saturation flux linkage λsat = μ0·N·A·Ms | 0.06 V·s | 0.04 V·s |
| LF corner, small signal | ≈ 3 Hz | ≈ 5.5 Hz (+1.5 Hz Cc/Lm resonance, Q ≈ 0.5) |
| HF resonance (leakage × C), damping | ≈ 75 kHz, Q ≈ 0.7 | ≈ 100 kHz, Q ≈ 1.5 (slight "bright" lift) |

Core geometry enters through two constants: K = μ0·N·A, from λsat, and l/N, from the small-signal
inductance. With them, λ = K(H + M) and i_m = (l/N)·H.

### Jiles–Atherton core materials

| | Ms (A/m) | a (A/m) | k (A/m) | c | α | μr initial |
|---|---|---|---|---|---|---|
| Input, 80 % Ni (mu-metal class), Bsat ≈ 0.69 T | 5.5e5 | 3 | 1 | 0.5 | 3e-7 | ≈ 31 000 |
| Output, 50 % Ni, Bsat ≈ 1.38 T | 1.1e6 | 20 | 5 | 0.7 | 2e-6 | ≈ 12 800 |

Both sets keep 2·α·Ms < (1−c)·k, so the irreversible denominator cannot change sign. It is also
clamped. The loops are deliberately thin (Br/Bsat ≈ 3–5 %, Hc ≈ 0.5 / 1.5 A/m), as in sheared
stacked-lamination audio cores. They give odd harmonics and a level-dependent LF phase lag, not a
square-loop memory effect.

### Line amp

Discrete op-amp, non-inverting, closed-loop gain G = 8 (+18 dB), loop gain L = Aβ = 3 (modest
feedback, vintage discrete). The output stage is S(u) = u / sqrt(1 + (u/ceil)²), into rails of
+15 V and −13.8 V (un = 0.92: the quasi-complementary pull-down saturates first). Slew rate is
5 V/µs.

- **Loop.** y = Vr·S(u), u = (A/Vr)(x − βy). This is memoryless because the closed-loop
  bandwidth is far above audio. The exact solution s(w) of u + L·S(u) = w is tabulated once per
  (L, un) in a shared table (cubic Hermite with exact slopes, step 1/16, |w| ≤ 16, error
  < 1e-8). Beyond it a second Hermite table in z = 16/|w| (256 steps per side) covers the far
  field out to |w| → ∞, where s approaches the rail smoothly in 1/w (error < 1e-9). The
  safeguarded Newton solver (from the asymptotic guess, where it converges monotonically) only
  builds the tables.
- **Slew limit.** The output follows the loop exactly until the per-sample step nears SR·T, then
  bends through a quadratic knee into a hard rate limit. This is rate-independent below the
  limit and only acts on large HF content at extreme drive.
- **Asymmetry.** It is tiny at low level (even/odd ≈ 0.16 at ≤ 0 dBFS, 1 kHz) and appears as the
  rails are hit.

Input mapping: sample 1.0 (0 dBFS) is a 2.5 V peak source EMF. Make-up is the reciprocal of the
analytic small-signal 1 kHz gain of the linearised circuit, which is rate-independent. Polarity is
non-inverting everywhere.

## Solver

Each transformer is a wave digital filter (bilinear transform; Fettweis, Werner) with the
nonlinear magnetising inductance as the root of the tree:

```
root: Lm (JA)
  └─ parallel
       ├─ series [ resistive source (Vs, R1), Cc ]
       └─ series [ Lleak, R2, parallel [ Cw, RL ] ]
```

The adaptors are written out by hand rather than with chowdsp_wdf, so every state is visible for
denormal flushing. The root sees a resistive port (wave a, port resistance R0), so v = a − R0·i.

The flux uses the same trapezoidal rule as every other reactance, so the whole circuit is one
consistent trapezoidal discretisation:

λ[n] = λ[n−1] + T/2·(v[n] + v[n−1]), with λ = K(H + M) and i = (l/N)·H

This reduces to one scalar equation:

K(H + M(H)) + Rk·H = p, with Rk = T/2·R0·l/N

M(H) follows the JA ODE dM/dH = χ(H, M, δ). This is Chowdhury's form, with the δM gating that
suppresses negative susceptibility after reversals.

1. **Direction.** δ = sign of the flux increment. This is exact, because dB/dH > 0.
2. **Predictor.** Evaluate χ at the start point and solve the (linear) circuit equation.
3. **Corrector.** Evaluate χ at the predicted midpoint (explicit midpoint rule in H). Re-solve
   and keep the magnetisation step ΔM. Clamp |M| ≤ Ms.
4. **H from flux balance.** H = (p − K·M)/(K + Rk). The circuit always sees the exact flux, so
   the scheme is robust even when a step jumps across the steep part of the loop (+60 dBFS
   squares).

The cost is fixed: two χ evaluations, two divisions, no iteration. χ is returned as an
unevaluated ratio so its division folds into the circuit solve. The Langevin function and its
slope come from a shared static table (cubic Hermite with exact derivatives, step 1/32 up to
x = 20, asymptotic 1 − 1/x beyond, error ~1e-10).

Why not full Newton–Raphson on the JA root: at 88.2 kHz and above, the step in H per sample is
small compared with the JA field scale except in abuse cases. The reference comparison below
shows the midpoint scheme within ~1e-3 of a converged ODE solution, so an iteration loop would
buy nothing audible at 2–3× the cost.

Real-time: no allocation, locks or exceptions in process()/reset(). Work per sample is fixed
(no iteration anywhere in the audio path). Non-finite inputs are replaced by 0. Decaying states are flushed below 1e-30 after each block, so the model
is denormal-safe without FTZ. The shared tables are built off the audio thread (in the
constructor), and the amp table cache is mutex-guarded at construction only.

## Validation

There is no SPICE. The independent reference is `reference/ref.py`. It integrates the same
equivalent circuit as a continuous ODE in (H, M, i_leak, v_Cw, v_Cc) with classical RK4 at
≈ 4 MHz (step-halving to 8 MHz changes the output by < 4e-9). The amp loop is solved by Newton
with a bisection safeguard; slew is not modelled because it is inactive at these levels.

The C++ model is compared at its own sample rate.

The table reports THD as C++ / reference, plus the worst waveform error over the settled window
(max |y_cpp − y_ref| / peak).

| Case | 88.2 kHz THD | 88.2 kHz err | 192 kHz THD | 192 kHz err |
|---|---|---|---|---|
| Chain 30 Hz −24 dBFS | 0.436 / 0.436 % | 5e-6 | 0.436 / 0.436 % | 5e-6 |
| Chain 30 Hz 0 dBFS | 49.950 / 49.953 % | 3.2e-3 | 49.953 / 49.953 % | 1.1e-4 |
| Chain 50 Hz −24 dBFS | 0.264 / 0.264 % | 4e-6 | 0.263 / 0.264 % | 4e-6 |
| Chain 50 Hz −12 dBFS | 0.472 / 0.472 % | 2e-6 | 0.472 / 0.472 % | 2e-6 |
| Chain 50 Hz 0 dBFS | 18.343 / 18.345 % | 8.5e-4 | 18.345 / 18.345 % | 2.8e-5 |
| Chain 50 Hz +12 dBFS | 32.086 / 32.099 % | 5.0e-3 | 32.099 / 32.099 % | 3.9e-4 |
| Chain 100 Hz 0 dBFS | 8.577 / 8.577 % | 6e-6 | 8.577 / 8.577 % | 2e-6 |
| Chain 1 kHz −24 dBFS | 0.020 / 0.020 % | 6.8e-4 | 0.020 / 0.020 % | 8.8e-5 |
| Chain 1 kHz 0 dBFS | 8.653 / 8.639 % | 4.1e-4 | 8.653 / 8.651 % | 5.4e-5 |
| Chain 1 kHz +12 dBFS | 35.088 / 35.079 % | 2.6e-3 | 35.087 / 35.086 % | 3.5e-4 |
| Input transformer alone, 50 Hz, 40 V (deep saturation) | 56.723 / 56.722 % | 5.8e-2 | 56.722 / 56.722 % | 7.1e-2 |
| Output transformer alone, 50 Hz, 15 V | 26.713 / 26.717 % | 1.8e-3 | 26.717 / 26.717 % | 4.6e-5 |

H3 levels agree to ±0.01 dB in every case. Peak flux agrees to three digits.

The one larger waveform error is the input transformer driven to 1.6× its saturation flux. It does
not shrink with sample rate. The cause is the |M| ≤ Ms clamp in the C++ model, which the
unclamped reference does not have; it acts only on the saturation current spikes, and THD agrees
to 1e-5. The remaining error falls roughly with T² (88.2 k → 192 k), which is what a second-order
scheme should do.

**B–H loops** (20 Hz, one transformer alone, C++ / reference):

| Core | Flux | Hc (A/m) | Br (T) | Bpk (T) | Loss (J/m³/cycle) |
|---|---|---|---|---|---|
| Input | 0.25 × sat | 0.432 / 0.432 | −0.0308 / −0.0308 | 0.155 / 0.155 | 0.205 / 0.205 |
| Input | 0.6 × sat | 0.489 / 0.489 | −0.0376 / −0.0376 | 0.375 / 0.375 | 0.683 / 0.683 |
| Input | 3 × sat | 0.489 / 0.489 | −0.0376 / −0.0376 | 0.685 / 0.685 | 1.50 / 1.38 |
| Output | 0.25 × sat | 1.379 / 1.379 | −0.0320 / −0.0320 | 0.333 / 0.333 | 1.386 / 1.386 |
| Output | 0.6 × sat | 1.476 / 1.476 | −0.0351 / −0.0351 | 0.799 / 0.799 | 4.452 / 4.452 |
| Output | 3 × sat | 1.476 / 1.476 | −0.0351 / −0.0351 | 1.353 / 1.353 | 8.43 / 8.12 |

In deep saturation the loop area is 4–9 % high: in that regime the explicit midpoint in H takes
large steps. Coercivity, remanence and peak flux are exact.

To reproduce, from `src/dsp/models/desk/reference`:

```
python3 -m venv venv && venv/bin/pip install numpy scipy numba
g++ -O2 -std=c++20 -I../../../.. dump_main.cc ../*.cpp -o dump
venv/bin/python validate.py && venv/bin/python loops.py
```

The `.cc` extension keeps the dump tool out of the `src/dsp/*.cpp` build glob.

## THD (192 kHz; settled 1.5 s, H2–H10)

| | −24 dBFS | −12 dBFS | 0 dBFS | +18 dBFS | +38 dBFS |
|---|---|---|---|---|---|
| **1 kHz** | 0.020 % | 0.32 % | 8.7 % | 40.4 % | 43.0 % |
| 5 kHz | 0.020 % | 0.32 % | 8.8 % | 42.2 % | 44.0 % |
| 100 Hz | 0.11 % | 0.42 % | 8.6 % | 40.1 % | 51.8 % |
| **50 Hz** | 0.26 % | 0.47 % | 18.3 % | 34.6 % | 79.0 % |
| 30 Hz | 0.44 % | 1.0 % | 50.0 % | 62.1 % | 105 % |
| 20 Hz | 0.58 % | 6.1 % | 71.6 % | 89.1 % | 143 % |

Even/odd at 1 kHz is 0.16 up to 0 dBFS (odd-dominated, a touch of H2) and falls to 0.02 when
clipped hard. The 1 kHz THD at 88.2 k and 768 k matches 192 k to within 0.03 % relative.

Reading the table:

- **1 kHz** follows the contract: clean, then warm, then driven. +18 dBFS sits at the top of the
  "heavy" band.
- **50 Hz versus 1 kHz** at the same level: about 13× higher at −24 dBFS (iron hysteresis),
  1.5× at −12 dBFS and 2.1× at 0 dBFS. At 20–30 Hz the iron dominates from −12 dBFS up.
- **Above the amp's clip point** the output transformer sees a squared, rail-limited waveform.
  The extra volt-seconds make the LF droop and collapse at the end of each half-cycle. The input
  transformer then takes over at extreme drive.

## Low-level frequency response (−60 dBFS)

| | 10 Hz | 20 Hz | 40 Hz | 100 Hz | 1 kHz | 10 kHz | 15 kHz | 20 kHz | 30 kHz |
|---|---|---|---|---|---|---|---|---|---|
| 192 kHz | −1.24 | −0.33 | −0.09 | −0.01 | 0.00 | +0.07 | +0.15 | +0.27 | +0.60 dB |
| 88.2 kHz | −1.24 | −0.33 | −0.09 | −0.01 | 0.00 | +0.07 | +0.18 | +0.36 | +1.10 dB |

The intended colouration is a gentle transformer LF roll-off and a +0.3 dB "air" lift at 20 kHz
from the output transformer's leakage/capacitance resonance. Bilinear warping pushes that lift to
+0.36 dB at the 2× rate.

## Performance (Release, one channel; machine shared with other builds)

`UmberTests_desk "[.perf]"`, median of 3 runs:

| | amp 0.1 | amp 4 |
|---|---|---|
| 192 kHz | 72 ns/sample (was 97) | 75 ns/sample (was 116) |
| 384 kHz | 74 ns/sample (was 97) | 74 ns/sample (was 116) |

In the plugin at Standard quality (4x, 48 kHz stereo, chain perf test): **3.5 %** of one core
(was 5.9 %). Each transformer stage alone costs ≈ 53 ns: its sample is one serial dependency
chain (χ → divide → χ → divide → H, M), so the model is latency-bound, not throughput-bound.
Two changes, both leaving the output bit-identical at float precision (golden file −331 dB,
i.e. identical):

1. **Software pipelining** (`DeskModel::process`, `TransformerStage::processPair`). The output
   stage of sample n−1 and the input stage of sample n are independent, so they run phase by
   phase side by side (predict / correct / scatter of both, interleaved), where the CPU
   overlaps the two chains: 97 → 71 ns. Merely reordering whole calls (output stage n−1, then
   input stage n) gained nothing measurable; the stages have to be interleaved at a finer grain.
2. **Far-field amp table** (see Line amp). At heavy drive the amp's input runs past the table
   (|w| > 16 is only ~+20 dB over the clip point; the chain perf condition sits there), and the
   2–4 Newton iterations with a square root and a division each cost 30–60 ns per sample:
   135 → 74 ns at 220 Hz, amplitude 7.4 (192 kHz).

Not needed after this, not tried: a lower-latency Langevin lookup (centred Estrin form, as
`tubes::CubicTable`) would shorten each chain further but change the output in the last bits.

## References

- D. C. Jiles, D. L. Atherton, "Theory of ferromagnetic hysteresis", J. Magn. Magn. Mater. 61
  (1986).
- J. Chowdhury, "Real-time physical modelling for analog tape machines", DAFx 2019. Source of
  the JA susceptibility form and the δM gating.
- M. Holters, U. Zölzer, "Circuit simulation with inductors and transformers based on the
  Jiles–Atherton model of magnetization", DAFx 2016. JA cores inside a circuit solver.
- K. J. Werner, "Virtual analog modeling of audio circuitry using wave digital filters", PhD
  thesis, Stanford 2016. Also Werner, Smith, Abel, "Wave digital filter adaptors for arbitrary
  topologies and multiport linear elements", DAFx 2015.
- A. Fettweis, "Wave digital filters: theory and practice", Proc. IEEE 74 (1986).
- Jensen Transformers application notes AN-002 / AN-008: LF distortion depends on source
  impedance, magnetic distortion is level-independent at low level and rises as frequency falls.

## Bias

An input-offset trim on the line amp (±0.6 V at the + input). It moves the stage towards one
rail, so it clips asymmetrically (`LineAmp::setInputOffset`). The 2200 µF output coupling
capacitor holds the resulting DC, so only the swing around the new operating point is passed on.

Measured at 1 kHz, 0 dBFS in (192 kHz):

| Bias | Gain | H2 | H3 | THD |
|---|---|---|---|---|
| −1 | −2.5 dB | −16.7 dBc | −22.2 dBc | 16.6 % |
| nominal | −1.9 dB | −37.1 dBc | −21.4 dBc | 8.7 % |
| +1 | −2.3 dB | −18.6 dBc | −22.3 dBc | 14.1 % |

Nominally the stage is almost symmetric. Either direction adds strong even harmonics (the
negative rail is a little closer, so −1 and +1 are not quite mirror images).

The chain smooths the control (80 ms) and each model glides per sample (`BiasRamp`, 64 samples).
The operating point moves quasi-statically: the coupling capacitors are treated as already
holding the new DC, so turning the knob never thumps (`tests/BiasTests.cpp`: silence stays below
-88 dBFS while the bias sweeps its full range four times in 0.6 s). At 0 the model is
bit-identical to the nominal circuit.
