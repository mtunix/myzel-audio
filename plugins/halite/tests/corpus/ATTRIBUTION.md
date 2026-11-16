# Test excerpts: attribution

Halite's committed test excerpts, the material of its CI detection tests and golden renders (the
renders, adaptations of two of these excerpts, carry their own note: `../golden/ATTRIBUTION.md`). Each
recording here is a short cut from a public corpus released under the
[Creative Commons Attribution 4.0 International licence](https://creativecommons.org/licenses/by/4.0/)
(CC BY 4.0). The excerpts and their phone labels are distributed under the same licence. They
are test material only: never shipped in a plugin or package.

## Sources

- **VCTK** (the publisher's citation): Yamagishi, Junichi; Veaux, Christophe; MacDonald, Kirsten.
  (2019). CSTR VCTK Corpus: English Multi-speaker Corpus for CSTR Voice Cloning Toolkit (version
  0.92), [sound]. University of Edinburgh. The Centre for Speech Technology Research (CSTR).
  https://doi.org/10.7488/ds/2645. CC BY 4.0. The MKH 800 channel (`mic2`) of
  `wav48_silence_trimmed`.
- **VocalSet & Annotated VocalSet**: deposited by Santiago Donaher, 2023,
  https://doi.org/10.5281/zenodo.10200775, CC BY 4.0. The deposit these excerpts were taken
  from: it bundles the two datasets below, with the Annotated VocalSet revised to include manual
  corrections (missing files, notes and other fixes).
  - **VocalSet** (version 1.0, as the deposit cites it): Julia Wilkins, Prem Seetharaman, Alison
    Wahl, Bryan Pardo, *VocalSet: A Singing Voice Dataset*, https://doi.org/10.5281/zenodo.1193957,
    CC BY 4.0; paper at ISMIR 2018, https://doi.org/10.5281/zenodo.1492453.
  - **Annotated-VocalSet**: Behnam Faghih, Joseph Timoney, *Annotated-VocalSet: A Singing Voice
    Dataset*, Applied Sciences 12(18), 9257, 2022, https://doi.org/10.3390/app12189257; data
    https://doi.org/10.5281/zenodo.7061507, CC BY 4.0. In the corrected form above, the note
    boundaries of the sung excerpts.

## What was changed

- **Cut** to about 3 s at phone boundaries (VCTK) or from just before the first note (VocalSet),
  with a 5 ms raised-cosine fade in and out. VCTK's FLAC was decoded to WAV.
- **Not resampled, not normalised:** 16-bit mono at the source rate (48 kHz VCTK, 44.1 kHz VocalSet).
- **Phone labels added** (`.lab`: start, end, ARPAbet phone without stress, `sil` for silence).
  VCTK: forced alignment of the corpus transcript with pocketsphinx 5.0.4
  (its CMU en-us acoustic model and CMUdict; BSD-style licences), not edited by hand. VocalSet:
  the corrected Annotated-VocalSet `extended 1` note and transition spans as the sung vowel named
  in the file name (/i/ as `IY`), rests and the rest of the file as `sil`.

Made by `plugins/halite/tools/corpus/make_excerpts.py` from sources fetched by `fetch.sh excerpts`.

## Excerpts

| File | Role | Source recording | Speaker | Cut from (s) | Length (s) | Source sentence (the excerpt is part of it) |
|---|---|---|---|---|---|---|
| `vctk-p225-033` | ess | VCTK 0.92, `p225_033_mic2.flac` | p225 (F, English Southern England) | 1.240 | 3.040 | It's really no great surprise, because the price difference is so much. |
| `vctk-p226-180` | ess | VCTK 0.92, `p226_180_mic2.flac` | p226 (M, English Surrey) | 0.820 | 2.980 | I have the first six months of next season to prove myself. |
| `vctk-p262-177` | ess | VCTK 0.92, `p262_177_mic2.flac` | p262 (F, Scottish Edinburgh) | 2.380 | 3.010 | Jacques Chirac, the French president, was surely in his sights. |
| `vctk-p245-212` | ess | VCTK 0.92, `p245_212_mic2.flac` | p245 (M, Irish Dublin) | 0.720 | 3.020 | Jim Wallace, the justice minister, acknowledged that prisoner numbers were a concern. |
| `vctk-p294-410` | ess | VCTK 0.92, `p294_410_mic2.flac` | p294 (F, American San Francisco) | 0.760 | 2.680 | Mr Wilson has received no such request. |
| `vctk-p311-401` | ess | VCTK 0.92, `p311_401_mic2.flac` | p311 (M, American Iowa) | 0.630 | 2.550 | Sadly, in this case, that was not so. |
| `vctk-p299-227` | bright | VCTK 0.92, `p299_227_mic2.flac` | p299 (F, American California) | 0.760 | 2.590 | They have admitted they cannot meet that outlay. |
| `vctk-p237-252` | bright | VCTK 0.92, `p237_252_mic2.flac` | p237 (M, Scottish Fife) | 0.810 | 2.630 | It would still have been a good film, but very different. |
| `vctk-p253-287` | esh | VCTK 0.92, `p253_287_mic2.flac` | p253 (F, Welsh Cardiff) | 0.950 | 3.020 | That might be a punishment but it would also punish the patients. |
| `vctk-p251-251` | esh | VCTK 0.92, `p251_251_mic2.flac` | p251 (M, Indian) | 1.240 | 3.020 | The perception is that we do not care about social issues. |
| `vocalset-f2-scales-belt-i` | bright | VocalSet 1.0, `FULL/female2/scales/belt/f2_scales_belt_i.wav` | f2 (F) | 0.187 | 3.000 | belted scale on /i/ |
| `vocalset-m2-scales-belt-i` | bright | VocalSet 1.0, `FULL/male2/scales/belt/m2_scales_belt_i.wav` | m2 (M) | 0.198 | 3.000 | belted scale on /i/ |

*Role:* `ess` is dense in sibilants (/s z ʃ ʒ tʃ dʒ/), `esh` in /ʃ ʒ/, `bright` has bright vowels
and at most one sibilant. Of the two sung scales, f2 carries bright harmonics in 4–12 kHz; m2 has
little energy there and is a sung control, not a bright-vowel case.
