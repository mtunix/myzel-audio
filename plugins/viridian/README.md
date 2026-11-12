# Viridian

A circuit-modelled analogue equaliser by **Myzel Audio**: five EQ models, each its real network
with its own amplifier stages and transformers, behind one panel layout that every model
relabels.

**Status: in development.** The parameter layout and the slot mapping of all five models are in
place and tested; the plugin builds in every format and passes pluginval, but runs on the generic
editor and passes audio through unchanged (Output and Bypass only) until the models arrive.

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
build/plugins/viridian/tests/ViridianTests                       # parameters, state, slot mapping, chain, processor
build/plugins/viridian/tests/ViridianTests "[modelinfo]"         # the slot mapping of every model
xvfb-run -a tools/pluginval.sh viridian build                    # pluginval, strictness 10
```

## Licence

AGPLv3. See [LICENSE](../../LICENSE). Built with [JUCE](https://juce.com) under its AGPLv3 option.
