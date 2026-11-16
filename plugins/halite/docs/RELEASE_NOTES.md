# Halite release notes

## 0.1.0: the first release

Halite is a de-esser that measures each ess against the voice around it and takes out only the
part that sticks out, with an optional second band for a harsh upper-mid edge. How to use it: the
[manual](MANUAL.md).

### What is in it

- **Detection** in two modes. **Voice**: a relative threshold, the sibilance band's level over the
  voice's body, held through each ess, with a noise-likeness and a spectral-shape cue (Focus) that
  keep bright and sung vowels from being cut. **Allround**: the band's level in dBFS, for cymbals,
  instruments, buses and mixes.
- **Three shapes:** **Split**, a steep dynamic cut over the sibilance band; **Spectral**, which cuts
  only the frequencies that stick out of the voice's own learned top end; **Wide**, the whole signal.
  Shape changes crossfade.
- **The Harsh band**, an optional dynamic band from 1.5 to 6 kHz measured against the same voice,
  holding its cut during esses so it never answers an ess.
- **Learn:** listens for 8 seconds and sets Threshold (and Harsh Threshold) from the esses it heard,
  as one undo step.
- **Listen:** the output, what Halite removes (both bands or each), or what each detector hears.
- **Stereo:** Linked, Unlinked, Mid, Side. **Side chain:** an external key with its own high-pass.
- **Latency:** Normal (one spectral frame, 10.7 ms at 48 kHz, every shape and up to 10 ms of
  look-ahead) or Live (0 samples, Split and Wide). A session setting: presets never change it.
- **Transparent when idle:** with nothing to remove, the output is the latency-aligned input, bit
  for bit. Bypass is latency-aligned and crossfaded.
- **The workbench editor:** one freely resizable window (from 720 × 420) in a dark and a light theme.
  A Timeline and a Cloud show every ess with its cut, a spectrum shows the input, the output and
  each band's live cut with draggable bands, and the sidebar lists every ess of the last minute with
  its song position, peak and cut, beside Learn's histogram. Full keyboard control, help in the status
  line for whatever is under the pointer.
- **25 factory presets** in five categories (Utility, Vocals, Speech, Instruments, Mix), plus user
  presets and A/B.

### Formats and platforms

| System | Formats | State |
|---|---|---|
| Linux x86_64 | VST3, CLAP, LV2, Standalone | built and tested in this repository; pluginval passes at strictness 10 on the VST3 and the LV2, clap-validator 0.4.1 on the CLAP (no failures; at most two warnings, *Known issues*). Needs glibc 2.38 and the GCC 13 C++ runtime or newer (Ubuntu 24.04, Fedora 39, Debian 13) |
| Windows x64 | VST3, CLAP, Standalone | from CI, unsigned; pending the first CI run; not tried by hand |
| macOS (Apple Silicon and Intel) | VST3, CLAP, AU, Standalone | from CI, unsigned; pending the first CI run; not tried by hand |

Mono or stereo in and out; the side-chain key mono or stereo. 44.1 to 192 kHz. Install with the
Myzel Audio installer for your system ([Installing](https://github.com/mtunix/myzel-audio#installing)).

### Measured

Tuned on labelled public speech and singing (VCTK and VocalSet, with LibriSpeech as a check; CC BY
4.0), split by speaker; read on the speakers it was not tuned on:

- at the threshold that catches 95 % of /s/, 0.35 % of vowel frames are cut;
- at the defaults, 0.9 % of vowel frames carry a release tail over 1 dB;
- Split's band loses the reduction it asks for within ±0.5 dB on every ess measured, Spectral's on
  97 %;
- CPU at 48 kHz stereo on an Intel i9-12900K: Split about 0.4 % of one core, Spectral about 0.7 %,
  the Harsh band about 0.2 % more. The editor's drawing: about 4–5 % of a core at full clock at
  200 % display scale on Linux.

The manual's *Measured, not claimed* has the rest.

### Known issues

- **Set by measurement, not yet by ear.** The defaults and the 25 factory presets come from the
  measurements; a listening pass is still to come. Trust your ears, and start from Learn.
- **Not yet tried in a DAW by hand**, and not tried by hand on Windows or macOS. Nobody has looked
  at the editor on Windows or macOS, which draw it with other renderers (Direct2D, CoreGraphics)
  than Linux.
- **The editor's CPU** on Linux reads higher on a CPU meter than its work costs: 9–12 % of a core at
  200 % on a loaded machine, about 20 % on a quiet one (one run of a pre-release build) where the
  power-saving governor slows the core under 1 GHz. These were taken on a virtual X display (Xvfb),
  not a desktop. On a 120 or 144 Hz display expect about two to two and a half times that
  (estimated, not measured). Not measured on Windows, macOS, a desktop compositor or in a DAW. The
  audio runs the same with the editor closed.
- **Sung vowels:** very rarely a cut on a noise burst before a note (0.03 % of frames in the
  singing corpus, up to 8 dB).
- **"Sh", "zh", "ch", "j"** are caught somewhat less than /s/ and /z/ at the same threshold.
- **Very quiet recordings:** Voice detection reads the same at any level only while the voice's
  body stays above about −40 dBFS; well under that it reads less sibilant. Raise the gain before
  Halite.
- **Live latency** lets the first milliseconds of an ess pass; **Spectral** needs about 2 seconds
  of voice after a start or a transport jump.
- Many hosts take up a change of latency only at the next transport stop.
- **clap-validator** gives up to two warnings, neither a fault in Halite: its denormal check times the
  validator's own making of denormal input against Halite's light load (it reads over 2× in some runs;
  Halite processes denormal input no slower than silence), and a state of random bytes counts as
  loaded, because the CLAP wrapper cannot report that Halite ignored it.

### Credits and licences

- Halite is free software under the GNU Affero General Public License v3 (`LICENSE`), built with
  [JUCE](https://juce.com) under its AGPLv3 option, with the CLAP extensions for JUCE.
- The editor embeds **Inter** 4.1 (The Inter Project Authors) and **JetBrains Mono** 2.304 (The
  JetBrains Mono Project Authors), unmodified, under the SIL Open Font License 1.1.
- Halite was measured and tested on the **VCTK** corpus (J. Yamagishi, C. Veaux, K. MacDonald,
  University of Edinburgh), **LibriSpeech** (V. Panayotov, G. Chen, D. Povey, S. Khudanpur) with
  L. Lugosch's alignments, **VocalSet** (J. Wilkins, P. Seetharaman, A. Wahl, B. Pardo) and
  **Annotated-VocalSet** (B. Faghih, J. Timoney), all under CC BY 4.0. The repository's short test
  excerpts and golden renders are adaptations of VCTK and VocalSet under the same licence
  (`plugins/halite/tests/corpus/ATTRIBUTION.md`); none of this material is in the plugin.
- The phone labels were made with pocketsphinx (Carnegie Mellon University, BSD-style); LibriSpeech's
  with the Montreal Forced Aligner (MIT).
