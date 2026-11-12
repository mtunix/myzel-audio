# Umber

An analog-modelled saturation plugin by **Myzel Audio**. Five circuit-level models on one
screen.

![Umber](docs/screenshots/02-valve-slam-hot.png)

The studio view: the same controls on one flat screen, with exact values and meters.

![Umber studio view](docs/screenshots/12-studio-fuzz-weight.png)

Formats: VST3, CLAP, LV2, Standalone (Linux first; Windows/macOS planned). Licence: AGPLv3.

**How to use it:** see the [manual](docs/MANUAL.md).

## The circuits

Each model is a real circuit, solved sample by sample at the oversampled rate, and validated
against an independent Python simulation of the same netlist (see each model's `NOTES.md`).

| Model | Circuit | Character |
|---|---|---|
| **Reel** | 12AX7 record amp, NAB pre-emphasis, Jiles–Atherton tape hysteresis, playback EQ with head bump | Gentlest. Soft, rounded; highs soften as you push |
| **Desk** | Input transformer, discrete line amp with soft rails and slew, output transformer (both cores hysteretic) | Bright and forward; lows saturate the iron first |
| **Class A** | Common-emitter BJT stage into a class-A emitter follower and a saturating output transformer | Fat low mids; asymmetric, punchy |
| **Valve** | 12AX7 common-cathode stage with grid conduction, Miller capacitance, cathode bias and supply sag | Warm, dense, 2nd-harmonic rich; squashes on hits |
| **Fuzz** | 12AX7 driver into an overdriven EL84 pentode and output transformer | Aggressive and odd-harmonic; sputters when slammed |

## Controls

**Drive** (with **Slam**, +20 dB) · **Circuit** selector · **Bias** (each circuit's operating
point: cold ↔ hot) · **Emphasis** (tilts the signal into the circuit and back out, so you choose
whether lows or highs saturate first without changing the tone) · **Stereo** (L/R, Mid only or
Side only) · **Tone** (tilt) · **Low Cut** with **Weight** (resonant bump) · **High Cut** ·
**Mix** · **Output** · **Auto** (loudness-matched output) · **Delta** (hear only what the circuit
adds) · VU meter (Output or Drive level). Two views: the hardware-style panel and a flat studio view
with exact values and meters. Header: presets (38 factory + user), Bypass, A/B,
undo/redo, and a settings menu with quality (Eco 2x / Standard 4x / High 8x, plus a separate
offline-render setting up to 16x), a mix lock for preset browsing, and UI scale (75–200 %).

## How it's built

- **Mixing inside the oversampled domain.** Dry and wet go through identical resampling
  filters, so any Mix setting blends without phase smearing.
- **One pre-allocated engine per quality setting.** Quality changes and offline bounces
  crossfade between engines: no clicks, no audio-thread allocation, constant latency.
- **Calibrated Auto gain.** `tools/calibrate` measures every circuit through the real chain
  (K-weighted loudness, ITU-R BS.1770) over drive, Bias and Emphasis and generates the
  compensation tables: loudness stays within ±0.7 dB from Drive 0 to Drive 10 + Slam, at any
  Bias and Emphasis setting.
- **Golden-file regression tests** for every model, so refactors can't change the sound silently.

## Building (Linux)

Dependencies (Ubuntu/Debian):

```sh
sudo apt install build-essential cmake ninja-build git pkg-config \
    libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxi-dev \
    libxinerama-dev libxrandr-dev libxrender-dev
```

JUCE, the CLAP extensions, chowdsp_wdf, melatonin_blur and Catch2 are fetched by CPM. Build from the
repository root (`../..`); `-DMYZEL_PLUGINS=umber` builds only Umber, leave it out to build every plugin:

```sh
export CPM_SOURCE_CACHE=~/.cache/CPM   # optional: share downloads between build dirs
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMYZEL_PLUGINS=umber
cmake --build build
```

Plugins land in `build/plugins/umber/Umber_artefacts/Release/{VST3,CLAP,LV2,Standalone}`. Add
`-DMYZEL_COPY_PLUGINS=ON` to install them into `~/.vst3`, `~/.lv2` and `~/.clap` after each
build. For faster development builds add `-DMYZEL_ENABLE_LTO=OFF`.

## Testing

```sh
ctest --test-dir build --output-on-failure                           # all suites (chain + one per model)
xvfb-run -a tools/pluginval.sh umber build                       # pluginval, strictness 10
build/plugins/umber/tests/UmberTests "[.perf]"                # CPU report
build/plugins/umber/tests/UmberTests "[.quality]"             # aliasing report
UMBER_UPDATE_GOLDEN=1 build/plugins/umber/tests/UmberTests "[golden]"   # re-record after an intended sound change
build/plugins/umber/tools/UmberCalibrate_artefacts/Release/UmberCalibrate   # regenerate Auto gain tables
```

## References

Every circuit model is our own implementation, built on published research. Each model's
`NOTES.md` says which parts come from where.

- D. T. Yeh, J. S. Abel, J. O. Smith, *Automated physical modeling of nonlinear audio circuits for
  real-time audio effects — Part I* (IEEE TASLP 18(4), 2010): the nodal DK method, in Class A,
  Valve and Fuzz.
- M. Holters, U. Zölzer, *A generalized method for the derivation of non-linear state-space models
  from circuit schematics* (EUSIPCO 2015): the nodal DK method.
- A. Fettweis, *Wave digital filters: theory and practice* (Proc. IEEE 74, 1986): Desk's
  transformers.
- K. J. Werner, *Virtual analog modeling of audio circuitry using wave digital filters* (PhD
  thesis, Stanford 2016) and K. J. Werner, J. O. Smith, J. S. Abel, *Wave digital filter adaptors
  for arbitrary topologies and multiport linear elements* (DAFx 2015): Desk's transformers.
- K. J. Werner et al., *Resolving wave digital filters with multiple/multiport nonlinearities*
  (DAFx 2015): the choice of solver for Valve and Fuzz.
- N. Koren, *Improved vacuum tube models for SPICE simulations* (Glass Audio 8(5), 1996): the
  triode and pentode models in Reel, Valve and Fuzz.
- K. Dempwolf, U. Zölzer, *A physically-motivated triode model for circuit simulations*
  (DAFx 2011): grid current.
- J. J. Ebers, J. L. Moll, *Large-signal behavior of junction transistors* (Proc. IRE 42(12),
  1954): Class A's transistors.
- D. C. Jiles, D. L. Atherton, *Theory of ferromagnetic hysteresis* (J. Magn. Magn. Mater. 61,
  1986): tape and transformer cores in Reel and Desk.
- J. Chowdhury, *Real-time physical modelling for analog tape machines* (DAFx 2019): the
  Jiles–Atherton solver form in Reel and Desk.
- M. Holters, U. Zölzer, *Circuit simulation with inductors and transformers based on the
  Jiles–Atherton model of magnetization* (DAFx 2016): Desk's transformer cores.
- V. Zavalishin, *The Art of VA Filter Design* (Native Instruments, 2012) and A. Simper, *Linear
  trapezoidal integrated SVF* (Cytomic, 2013): the filters.

ChowTapeModel (J. Chowdhury, GPLv3) was read for its hysteresis approach. Umber's implementation
is independent.

## Licence

AGPLv3. See [LICENSE](../../LICENSE). Built with [JUCE](https://juce.com) under its AGPLv3 option. The
bundled Inter font is under the SIL Open Font License (see `common/ui/assets/fonts/`).
