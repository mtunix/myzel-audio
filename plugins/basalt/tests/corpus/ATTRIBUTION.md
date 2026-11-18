# Test excerpts: attribution

Basalt's committed test excerpts, the material of its CI tests on recorded audio. Each recording
here is a short cut from a public dataset released under the
[Creative Commons Attribution 4.0 International licence](https://creativecommons.org/licenses/by/4.0/)
(CC BY 4.0). The excerpts are distributed under the same licence. They are test material only:
never shipped in a plugin or package.

## Sources

- **AlbumDB**: Thomas McKenzie, Eloi Moliner, Alec Wright, *AlbumDB: A multitrack dataset of a
  ten-song album with stereo and immersive 7.1.4 masters*, version 1.0, Zenodo, 2026,
  https://doi.org/10.5281/zenodo.19683001, CC BY 4.0; described in their paper at the AES 160th
  Convention, Copenhagen, May 2026. The album is *Extraordinary* by Tom McKenzie (indie rock and
  folk). Used: `stems_raw.zip` (the recorded tracks with no mixing or processing; the record says
  the only changes are temporal edits and removal of silences) and `masters_stereo.zip`.
- **Guitar-TECHS**: Hegel Emmanuel Pedroza Villalobos, Termeh Taheri, Wallace Abreu, Ryan Corey,
  Iran R. Roman, *Guitar-TECHS: An Electric Guitar Dataset Covering Techniques, Musical Excerpts,
  Chords and Scales Using a Diverse Array of Hardware*, Zenodo record 14963133, 2025,
  https://zenodo.org/records/14963133, CC BY 4.0; paper at ICASSP 2025,
  https://doi.org/10.1109/ICASSP49660.2025.10887996. Used: `P3_music.zip`, player 3's musical
  excerpts, recorded as the direct (DI) signal and through a miked guitar amplifier.
- **VCTK** (the publisher's citation): Yamagishi, Junichi; Veaux, Christophe; MacDonald, Kirsten.
  (2019). CSTR VCTK Corpus: English Multi-speaker Corpus for CSTR Voice Cloning Toolkit (version
  0.92), [sound]. University of Edinburgh. The Centre for Speech Technology Research (CSTR).
  https://doi.org/10.7488/ds/2645. CC BY 4.0. The `mic2` channel of `wav48_silence_trimmed`.

## What was changed

- **Cut** to 5 s, with a 5 ms raised-cosine fade in and out. The windows were chosen by a rule over
  levels (`make_excerpts.py`), not by listening. The five Extraordinary stems share one window and
  stay in time with each other; the Guitar-TECHS DI and amp excerpts are cut at the same time from
  the same take, not aligned to each other: the dataset's record notes that its signals may be
  misaligned by up to 100 ms, and this pair's offset is not known.
- **Not resampled, not normalised, no gain:** the source rate (44.1 kHz AlbumDB, 48 kHz Guitar-TECHS
  and VCTK) and bit depth (16-bit; 24-bit for the Guitar-TECHS DI). Sample values are unchanged
  outside the fades.
- **Channels:** stereo where the source's two channels differ. The Guitar-TECHS files carry the
  same signal on both channels and are stored as one; mono sources stay mono.
- **Format:** FLAC (lossless), from the sources' WAV (AlbumDB, Guitar-TECHS) or FLAC (VCTK).

Made by `plugins/basalt/tools/corpus/make_excerpts.py` from sources fetched by `fetch.sh excerpts`;
`excerpts.tsv` names each source file and its SHA-256, and `manifest.tsv` the excerpts' levels.

## Excerpts

| File | Category | Source recording | Cut from (s) | Length (s) | Channels |
|---|---|---|---|---|---|
| `albumdb-extraordinary-kick` | kick | AlbumDB 1.0, `stems_raw/01 Extraordinary/drums_kick.wav` | 52.300 | 5.000 | 1 |
| `albumdb-extraordinary-snare` | snare | AlbumDB 1.0, `stems_raw/01 Extraordinary/drums_snare.wav` | 52.300 | 5.000 | 1 |
| `albumdb-extraordinary-overheads` | overheads | AlbumDB 1.0, `stems_raw/01 Extraordinary/drums_overheads.wav` | 52.300 | 5.000 | 2 |
| `albumdb-extraordinary-bass` | bass | AlbumDB 1.0, `stems_raw/01 Extraordinary/bass.wav` | 52.300 | 5.000 | 1 |
| `albumdb-extraordinary-vocal` | vocal | AlbumDB 1.0, `stems_raw/01 Extraordinary/vox_lead.wav` | 52.300 | 5.000 | 1 |
| `albumdb-karma-piano` | piano | AlbumDB 1.0, `stems_raw/03 Karma/keys_piano.wav` | 82.700 | 5.000 | 2 |
| `albumdb-karma-acoustic-guitar` | acoustic-guitar | AlbumDB 1.0, `stems_raw/03 Karma/guitar_acoustic_main.wav` | 0.000 | 5.000 | 1 |
| `albumdb-it-aint-easy-mix` | mix-open | AlbumDB 1.0, `masters_stereo/10 - It Ain't Easy (To Mend A Heart).wav` | 30.000 | 5.000 | 2 |
| `albumdb-diva-mix` | mix-dense | AlbumDB 1.0, `masters_stereo/05 - Diva.wav` | 133.600 | 5.000 | 2 |
| `guitar-techs-p3-05-di` | electric-guitar-di | Guitar-TECHS, `P3_music/audio/directinput/directinput_05.wav` | 17.700 | 5.000 | 1 |
| `guitar-techs-p3-05-amp` | electric-guitar-amp | Guitar-TECHS, `P3_music/audio/micamp/micamp_05.wav` | 17.700 | 5.000 | 1 |
| `vctk-p228-009` | speech | VCTK 0.92, `p228_009_mic2.flac`, speaker p228 (F, English, Southern England) | 0.000 | 5.000 | 1 |

*Category:* `mix-open` is the album's master with the highest crest factor, `mix-dense` the one
with the lowest, each cut from the body of its song. AlbumDB does not say whether its raw bass was
recorded direct or from an amplifier.
