# Viridian

A circuit-modelled analogue equaliser by **Myzel Audio**: five EQ models, each its real network
with its own amplifier stages and transformers, behind one panel layout that every model
relabels.

**Status: in development.** The full signal chain runs, and **Console** is built end to end: both
of its cards' networks generated from their netlists, its control laws and its op-amp stages,
measured through the chain against ngspice. The other four models are placeholders that pass the
signal through flat until they are built; the plugin runs on the generic editor until the panel
exists.

Formats: VST3, CLAP, LV2, Standalone (Linux first; Windows/macOS build unsigned in CI).
Licence: AGPLv3. Sibling of [Umber](../umber), [Cerulean](../cerulean) and
[Mulberry](../mulberry).

## The models

| | Model | What it models | Bands |
|---|---|---|---|
| 1 | **Passive** | Passive LCR program EQ with a valve make-up amp and a mid dip | Low boost and attenuate on one frequency, dip, resonant high boost with bandwidth, high attenuator |
| 2 | **Inductor** | Class-A console module: inductor mid, inductor high-pass, transformers | Stepped high-pass, low shelf, mid bell (Hi-Q on Alt), fixed high shelf, stepped low-pass |
| 3 | **Proportional** | Discrete console module, stepped, Q narrowing as gain rises | Four stepped bands, peak or shelf at the ends, a fixed band-pass on Alt |
| 4 | **Console** | Transformerless console channel EQ, two curve families on Alt | Four continuous bands, high- and low-pass |
| 5 | **Mastering** | Clean discrete parametric, the transparency reference | Four fully parametric bands, shelf or bell at the ends, high- and low-pass |

Each model's network is solved from its schematic and each runs its own amplifier stages, so the
curves, the interaction between controls and the colour come out of the circuit, never out of a
drawn curve. Each model's `NOTES.md` under `src/dsp/models/` records its sources, step tables and
what its gates must reproduce.

**From schematic to filter.** `tools/netlist` reads a model's ngspice netlist, with its pots and
switches named, and solves it exactly (symbolic nodal analysis): H(s) as a function of the pot
positions, split into the blocks the circuit buffers apart, written out as a C++ header and
checked against ngspice. At run time each block's poles and zeros go through a frequency map
fitted to the sample rate, so every band's magnitude matches the analog network to 20 kHz at
every rate (0.008 dB at 44.1 kHz) with no cramping near Nyquist, in TPT state-variable stages
that glide when a control moves; blocks whose bands interact run as cascades of tracked root
pairs that sweep without clicks. A stepped control (a rotary switch that sets a component's
value) glides between its detents in 10 ms. Method and measurements: `tools/netlist/README.md`.

**Amplifier stages.** The Console's op-amps clip as the chip does, from its datasheet's open-loop
gain, output swing and slew rate (`common/dsp/OpAmpClip.h`): below the rails they add almost
nothing, at the rails they clip hard and slew. The clip and the slew run at 8x, solved exactly in
continuous time on a cubic spline of the input and antialiased, on a linear-phase path that runs
only while an amp's input exceeds 0.315 of its swing (-4.75 dBFS at Drive 0), and the chain
band-limits around its 2x stages, so a tone at -1 dBFS folds back below -85 dBc at 44.1 kHz at
every Drive and every EQ setting the gate measures (1, 3, 7, 11 and 15 kHz; each band boosted and
cut, the filters at their ends, random settings, every band at work, Init; read against the tone at
the stage that clips it; console `NOTES.md`, *Measured*). Latency: 30 samples at every rate.

## Controls

The same slots on every model: **HPF** · **Low** (Freq, Gain, Q, Shape) · **Low-Mid** ·
**High-Mid** · **High** (Freq, Gain, Q, Shape) · **LPF** · **Model** · **Alt** (one switch per
model) · **Drive** (−12 … +24 dB into the input stage, level-compensated) · **EQ In** · **Output**
(±24 dB) · **Stereo** (Stereo, Mid, Side) · **Tolerance** and **Noise** (both off by default).
Switching model never moves a knob: each model reads the same knob positions with its own
legends, detents and ranges, and dims what it lacks.

## Building

From the repository root (`../..`), see the root README for dependencies.
`-DMYZEL_PLUGINS=viridian` builds only Viridian:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMYZEL_PLUGINS=viridian
cmake --build build
```

Plugins land in `build/plugins/viridian/Viridian_artefacts/Release/{VST3,CLAP,LV2,Standalone}`.
`tools/package.sh viridian build` builds a release tarball in `dist/` with an `install.sh`.

## Testing

```sh
ctest --test-dir build/plugins/viridian --output-on-failure      # every Viridian suite
build/plugins/viridian/tests/ViridianTests                       # parameters, state, slot mapping, chain, processor, gates, golden files
build/plugins/viridian/tests/ViridianTests "[modelinfo]"         # the slot mapping of every model
build/plugins/viridian/tests/ViridianTests "[gates]"             # PLAN's quality gates through the chain
build/plugins/viridian/tests/ViridianTests "[aliasing]"          # aliasing at every Drive: the worst settings per tone, tones from 2 to 15 kHz, input near Nyquist
build/plugins/viridian/tests/ViridianTests "[gates][.report]"    # aliasing against Drive at Init, THD against level, the long automation runs
build/plugins/viridian/tests/ViridianTests "[.perf]"             # CPU against the budget (medians): up to the rails at Drive 0 against the budget, into them (Drive +24 dB, louder input, boosts) against the ceiling, during a model change; the longest callback around every change and while every knob moves
build/plugins/viridian/tests/ViridianTests "[realtime]"          # no allocation while every parameter is automated
build/plugins/viridian/tests/ViridianTests_network               # frequency map, matched sections, networks
build/plugins/viridian/tests/ViridianTests_network "[.report]"   # error by octave and phase, per rate
build/plugins/viridian/tests/ViridianTests_network "[.perf]"     # CPU of the sections
build/plugins/viridian/tests/ViridianTests_console               # Console: curves vs ngspice, laws, sweeps, amps
build/plugins/viridian/tests/ViridianTests_console "[amp]"       # Console: the op-amp clip against the exact loop and itself
build/plugins/viridian/tests/ViridianTests_console "[.report]"   # Console: error and phase per rate
build/plugins/viridian/tests/ViridianTests_console "[.perf]"     # Console: CPU of the card networks
build/plugins/viridian/tools/ViridianMeasure                     # every gate of every built model, printed (exit 1 on a failed gate)
build/plugins/viridian/tools/ViridianMeasure --quick             # without the curve grid, the aliasing grid, the CPU and the callback timings
build/plugins/viridian/tools/ViridianMeasure --aliasing          # the aliasing grid alone: 14 170 renders, the tone scan, input near Nyquist (every core)
python3 plugins/viridian/tools/measure/compare.py --measure build/plugins/viridian/tools/ViridianMeasure   # the chain against ngspice: magnitude gated, phase reported
python3 plugins/viridian/tools/netlist/validate.py               # the netlist tool: closed forms, ngspice
python3 plugins/viridian/tools/netlist/sources.py                # every generated header's stamp against its sources
python3 plugins/viridian/src/dsp/models/console/netlist/check.py # Console netlists vs the published figures
xvfb-run -a tools/pluginval.sh viridian build                    # pluginval, strictness 10
VIRIDIAN_UPDATE_GOLDEN=1 build/plugins/viridian/tests/ViridianTests "[golden]"   # re-record after an intended sound change
```

`compare.py` renders the Console's grid of settings (650 over both cards: every knob over its
travel, both shapes, the filters from Off, interacting bands, flat and random settings) through
the real chain at 44.1, 48, 96 and 192 kHz, runs ngspice AC on the card netlists for the pots
each setting ran, gates the magnitude (0.1 dB to 20 kHz) and prints the phase per rate (PLAN.md,
Settled 13: phase is reported, not gated; the figures are in the Console's `NOTES.md`). ctest runs
it once. `--curves <file>` re-compares a file `ViridianMeasure --curves` wrote; `--keep <file>`
keeps it.

The netlist checks need python3 with sympy, numpy and mpmath, and ngspice (the ngspice comparison
numpy and scipy); where they are missing ctest skips them (CMake says so) and the C++ tests still
check the generated headers; `sources.py` (python3 alone) still catches a header older than its
netlist, anything the netlist includes or names with `*@ depends`, or (`ConsoleLaws.h`) the
sources its stamp names. After changing a netlist, regenerate its header (`cmake --build
build --target ViridianNetworks`, or `python3 plugins/viridian/tools/netlist/netlist.py
regenerate` from the repository root) and commit both.

Console's generated files and how to rebuild them (each takes the netlists as they are; commit
the outputs with the netlist change):

```sh
python3 plugins/viridian/tools/netlist/netlist.py regenerate                      # every network header (hours: the EQ sections are large)
python3 plugins/viridian/src/dsp/models/console/netlist/laws.py                   # ConsoleLaws.h: the gain and Q laws
python3 plugins/viridian/tests/models/console/reference/make_reference.py         # the ngspice curves the tests compare with
```

## Licence

AGPLv3. See [LICENSE](../../LICENSE). Built with [JUCE](https://juce.com) under its AGPLv3 option.
