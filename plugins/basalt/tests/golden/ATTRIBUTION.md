# Golden renders: attribution

The thirteen `.f32` files here are Basalt's golden renders (`tests/GoldenTests.cpp`). Each is an
adaptation of one of the committed test excerpts (`../corpus/ATTRIBUTION.md`), whose sources are
released under the
[Creative Commons Attribution 4.0 International licence](https://creativecommons.org/licenses/by/4.0/)
(CC BY 4.0), and is distributed under the same licence. They are test material only: never shipped
in a plugin or package.

## Sources

- **AlbumDB**: Thomas McKenzie, Eloi Moliner, Alec Wright, *AlbumDB: A multitrack dataset of a
  ten-song album with stereo and immersive 7.1.4 masters*, version 1.0, Zenodo, 2026,
  https://doi.org/10.5281/zenodo.19683001, CC BY 4.0. The excerpts `albumdb-extraordinary-vocal`,
  `-overheads`, `-snare`, `-bass`, `albumdb-karma-piano`, `albumdb-it-aint-easy-mix` and
  `albumdb-diva-mix`.
- **VCTK** (the publisher's citation): Yamagishi, Junichi; Veaux, Christophe; MacDonald, Kirsten.
  (2019). CSTR VCTK Corpus: English Multi-speaker Corpus for CSTR Voice Cloning Toolkit (version
  0.92), [sound]. University of Edinburgh. The Centre for Speech Technology Research (CSTR).
  https://doi.org/10.7488/ds/2645. CC BY 4.0. The excerpt `vctk-p228-009`.

## What was changed

- **Cut** as the committed excerpts (`../corpus/ATTRIBUTION.md`).
- **Processed:** each excerpt run through Basalt's signal chain (recorded in M2, 2026-11-17), one
  model per file at the fixed settings in `GoldenTests.cpp`; mono excerpts on both channels.
- **Cut again:** 0.75 s of the left output from 3.5 s of the input, stored as raw 32-bit float,
  mono, at the excerpt's rate (44.1 kHz AlbumDB, 48 kHz VCTK).
