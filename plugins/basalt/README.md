# Basalt

A compressor by **Myzel Audio** for everything from a single vocal to the master bus. You pick a
**model** (Clean, VCA, FET, Opto, Vari-mu or Upward) and each one reacts to the music the way that
family of gain cell and detector does. A separate **Character** section adds analogue colour when
you want it, and Learn sets the threshold from the material for the reduction you ask for.

**Status: in development (M2 integrated).** The plugin builds in every format with its full, frozen
parameter set, its buses and its session state, on JUCE's generic editor. All six v1 models run on
their own engines (VCA with its Peak and RMS detectors as two): key source and key EQ, the stereo
modes and Link, look-ahead in Normal latency, Range, Floor (Upward), Gain, Auto Gain, Mix, Listen,
Output and Bypass, a model or detector switch crossfading over 30 ms, gated through the processor
(PLAN.md, *Milestones*: where each gate stands). Diode bridge, PWM and Pumping run as Clean until
1.1, and Character arrives in M3. The plan, the models' references and every gate:
[PLAN.md](PLAN.md).

Formats: VST3, CLAP, LV2, Standalone (Linux first; Windows/macOS build unsigned in CI).
Licence: AGPLv3. Second plugin of the tools lane after [Halite](../halite); sibling of
[Umber](../umber), [Cerulean](../cerulean), [Mulberry](../mulberry), [Viridian](../viridian) and
[Inkcap](../inkcap).

## Controls

Frozen from M0 (PLAN.md, *Controls* and *Parameter IDs*), in the host's order:

**Model** (Clean, VCA, FET, Opto, Vari-mu, Upward; Diode bridge, PWM and Pumping hold their places
for 1.1 and run as Clean until then) · **VCA Detector** (Peak, RMS) · **Threshold** (−60 … 0 dBFS,
default −18) · **Ratio** (1 … 20 logarithmic, then one step to ∞; default 4) · **Knee** (0 … 24 dB,
default 6) · **Attack** (20 µs … 300 ms, default 10 ms) · **Release** (5 ms … 5 s, default 150 ms) ·
**Auto Release** (on) · **Range** (0 … 40 dB, default 40) · **Floor** (−90 … −30 dBFS, default −70;
Upward) · **Gain** (±24 dB) · **Auto Gain** (off) · **Mix** (0 … 100 %) · **Character** (Off, Warm,
Iron, Edge) with **Drive** (0 … 24 dB, default 6), **Placement** (Post, Pre) and **Drive Link** ·
**Target** (1 … 12 dB, default 3; Learn's aim) · **Look-ahead** (0 … 10 ms) · **Stereo** (Linked,
Unlinked, Mid, Side, Mid/Side) · **Link** (0 … 100 %) · **Key** (Internal, External) · **Key HPF**
(Off, 20 Hz … 2 kHz) · **Key LPF** (2 … 20 kHz, Off) · **Key Bell** Freq (40 Hz … 16 kHz, default
1 kHz), Gain (±18 dB) and Q (0.3 … 6, default 1) · **Listen** (Output, Key, Delta) · **Output**
(±24 dB) · **Bypass**.

Ratio stores ∞ as 21: an LV2 host's plain slider (1 … 21) then still reads 1 … 20 and then ∞. On the
normalised travel, 1 … 20 takes the first 90 % (equal travel per doubling) and the last 10 % is the
step, its lower half 20:1 and its upper half ∞.

**Latency** is a session setting, not a parameter: **Normal** or **Live** (PLAN.md, *Latency*).
Normal reports 10 ms (441 samples at 44.1 kHz, 480 at 48 kHz), the look-ahead's budget, whatever
Look-ahead is set to; Live reports 0 and has no look-ahead. M3 adds Character's oversampler delay
to Normal.

Buses: **Input** and **Output** (mono or stereo, the same on both) and **Side Chain** (stereo or
mono, off by default).

## Building

From the repository root (see the [root README](../../README.md)):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMYZEL_PLUGINS=basalt
cmake --build build
```

Plugins land in `build/plugins/basalt/Basalt_artefacts/Release/` (VST3, LV2, CLAP, Standalone).

## Testing

```sh
ctest --test-dir build/plugins/basalt --output-on-failure            # every Basalt suite
build/plugins/basalt/tests/BasaltTests                                # parameters, buses, state, chain, identity, allocation, the fast gates
build/plugins/basalt/tests/BasaltTests "[parameters]"                 # the frozen IDs, order, ranges, mappings, defaults, choice lists, read-outs
build/plugins/basalt/tests/BasaltTests "[ratio]"                      # Ratio's log travel and the step to ∞
build/plugins/basalt/tests/BasaltTests "[buses]"                      # main mono/stereo, side-chain key off/mono/stereo
build/plugins/basalt/tests/BasaltTests "[identity]"                   # nothing to do: the input bit for bit, every layout, rate, block size 1 … 4096 and variable, every model
build/plugins/basalt/tests/BasaltTests "[state]"                      # session round trips, the latency mode, foreign, damaged and older state
build/plugins/basalt/tests/BasaltTests "[latency]"                    # the reported latency at every rate, before prepare too; a mode change; Character's delay moves nothing else
build/plugins/basalt/tests/BasaltTests "[realtime]"                   # no allocation in processBlock while every parameter moves; NaN and Inf reach the detectors as silence
build/plugins/basalt/tests/BasaltTests "[chain]"                      # the signal path: key, key EQ, stereo, look-ahead, Listen, Mix, Auto Gain, meters
build/plugins/basalt/tests/BasaltTests "[chain][switch]"              # model, key and stereo switches: warm-up, seed, crossfade; a Linked lane starts linked; only the heard engine runs
build/plugins/basalt/tests/BasaltTests "[chain][blocks]"              # the same bits at every block size, settings still and across switches
build/plugins/basalt/tests/BasaltTests "[gates]"                      # the fast gates through the processor, reduced grids, every model: identity, static, ballistics, clicks, stereo, distortion
build/plugins/basalt/tests/BasaltTests "[gates][opto]"                # one model's fast gates (clean, vca, fet, opto, varimu, upward)
build/plugins/basalt/tests/BasaltTests "[golden]"                     # every model's render of the committed excerpts against tests/golden
BASALT_UPDATE_GOLDEN=1 build/plugins/basalt/tests/BasaltTests "[golden]"  # re-record them: only for an intended sound change, said so in the commit
build/plugins/basalt/tests/BasaltTests "[measure]"                    # BasaltMeasure's self-tests: every measurement against a known answer
build/plugins/basalt/tests/BasaltTests "[.perf]"                      # CPU against the budget, every model and stereo mode at 48 kHz (local only)
build/plugins/basalt/tests/BasaltTests "[.corpus]"                    # the committed excerpts' reduction statistics (local only)
build/plugins/basalt/tests/BasaltTests_dynamics                       # the dynamics core and Clean alone (src/dsp/dynamics, src/dsp/models/clean); -s prints the figures
build/plugins/basalt/tests/BasaltTests_dynamics "[curve]"             # eq. 4, its slope and inverse, the curve fade
build/plugins/basalt/tests/BasaltTests_dynamics "[ballistics]"        # h(ρ), the smooth decoupled release, the feedback loop's times, Clean's grid
build/plugins/basalt/tests/BasaltTests_dynamics "[feedback]"          # the feedback loop: eq. 4 to the cap, the knobs' times, Floru, Range on the state, a model's own demand
build/plugins/basalt/tests/BasaltTests_dynamics "[clean]"             # Clean: static curve, ballistics, Auto release, look-ahead, knob jumps, stereo, idle
build/plugins/basalt/tests/BasaltTests_dynamics "[clicks]"            # knob jumps at Attack 0.02 ms: no step, no corner in the reduction
build/plugins/basalt/tests/BasaltTests_dynamics "[realtime]"          # no allocation once prepared; no denormals in long silence
build/plugins/basalt/tests/BasaltTests_dynamics "[.perf]"             # Clean with the gain and the matrix, without the chain (local only)
build/plugins/basalt/tests/BasaltTests_vca                            # one model against its NOTES alone, the core and that model only: vca, fet, opto, varimu, upward
build/plugins/basalt/tests/BasaltTests_opto "[.perf]"                 # that model's cost with the gain and the matrix (local only)
xvfb-run -a tools/pluginval.sh basalt build 10 vst3,lv2                # pluginval, strictness 10, VST3 and LV2
```

## Measuring

`BasaltMeasure` (`tools/measure`, the measurements in `tests/support`, shared with the tests) runs
the gates of PLAN.md (*Measurement and quality gates*) through the processor, its controls set
through the parameters as a host sets them, and prints every row with its verdict; it exits 1 when
a gated row fails and 2 on a usage error (an option without its value included):

```sh
build/plugins/basalt/tools/BasaltMeasure all                          # identity (every model), static, ballistics, clicks, stereo, distortion (Clean) on the full grids
build/plugins/basalt/tools/BasaltMeasure all --model vari-mu --jobs 2 # one model's gates with the rows of its NOTES (clean, vca, fet, opto, vari-mu, upward)
build/plugins/basalt/tools/BasaltMeasure static --model vca --set vcaDetector=rms   # VCA's gates run both detectors, or the one set
build/plugins/basalt/tools/BasaltMeasure all --quick                  # the reduced grids CI runs
build/plugins/basalt/tools/BasaltMeasure all --rate 44100 --tsv rows.tsv   # another rate; every row as TSV
build/plugins/basalt/tools/BasaltMeasure distortion                   # THD+N at 6 dB of reduction over Attack x Release
build/plugins/basalt/tools/BasaltMeasure perf --jobs 1                # CPU: every model and stereo mode, the whole key path, Live, at 48 kHz
build/plugins/basalt/tools/BasaltMeasure corpus --set threshold=-30   # the committed excerpts through a model
build/plugins/basalt/tools/BasaltMeasure ballistics --device reference  # on BasaltMeasure's transcription of the NOTES: what a correct engine reads
build/plugins/basalt/tools/BasaltMeasure selftest                     # the measurements against known answers
build/plugins/basalt/tools/BasaltMeasure ballistics --quick --dump DIR # also the raw samples of each step, to check a row by hand
build/plugins/basalt/tools/BasaltMeasure --help                       # every option
```

The gain is read exactly: by default the test signal goes to the side-chain key and the main bus
carries a constant-magnitude probe of amplitude 0.25, so output / input is the engine's float gain
at every sample, digital silence on the key included (`--probe internal` puts the signal on the
main bus instead). The click gate runs each knob on a key it can reach (two channels 26 dB apart,
a level at Threshold, tones in the key EQ's bands, releases for the time knobs; Upward's keys sit
in its lift region) and reports what this build cannot exercise yet. Each model's expectations come
from its own transcription of its NOTES, `tests/models/<m>/Reference.h`, never from the engine. `--dump DIR` writes each ballistics step and click render as
`NAME.f32` (float32 per sample and main channel: input, output, key) with the rate, latency and
columns in `NAME.txt`. The performance budget is gated at 48 kHz, where PLAN.md sets it; other
rates are reported.

## Licence

AGPLv3. See [LICENSE](../../LICENSE). Built with [JUCE](https://juce.com) under its AGPLv3 option.
