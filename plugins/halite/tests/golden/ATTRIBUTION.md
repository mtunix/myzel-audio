# Golden renders: attribution

The twelve `.f32` files here are Halite's golden renders (`tests/reduction/GoldenTests.cpp`). Each
is an adaptation of two recordings of the VCTK corpus, released under the
[Creative Commons Attribution 4.0 International licence](https://creativecommons.org/licenses/by/4.0/)
(CC BY 4.0), and is distributed under the same licence. They are test material only: never shipped
in a plugin or package.

## Source

- **VCTK** (the publisher's citation): Yamagishi, Junichi; Veaux, Christophe; MacDonald, Kirsten.
  (2019). CSTR VCTK Corpus: English Multi-speaker Corpus for CSTR Voice Cloning Toolkit (version
  0.92), [sound]. University of Edinburgh. The Centre for Speech Technology Research (CSTR).
  https://doi.org/10.7488/ds/2645. CC BY 4.0. The MKH 800 channel (`mic2`) of
  `wav48_silence_trimmed`: `p251_251_mic2.flac` (speaker p251) and `p225_033_mic2.flac` (speaker p225).

## What was changed

- **Cut** as the committed excerpts `vctk-p251-251` and `vctk-p225-033` (`../corpus/ATTRIBUTION.md`).
- **Processed:** the two excerpts back to back, run through Halite's signal chain (recorded in M2,
  2026-11-14; re-recorded for intended sound changes: the six that run Split's filter in M2b, the four
  Spectral ones in M3, all twelve for M3's Speed Auto) in each Shape (Split, Spectral, Wide), Detection (Voice, Allround) and band (Ess, Ess and Harsh), at
  the fixed settings in `GoldenTests.cpp`.
- **Cut again:** two half-second windows of each output (from 2.0 s and 5.0 s of the input), stored
  as raw 32-bit float, mono, 48 kHz.
