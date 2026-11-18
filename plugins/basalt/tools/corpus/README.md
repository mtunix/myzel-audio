# Basalt test corpus

Basalt is measured against recorded music and speech, openly licensed (PLAN.md, *Test material*),
in two tiers, as Halite's:

| | Committed tier | Full tier (`[.corpus]`) |
|---|---|---|
| Where | `plugins/basalt/tests/corpus/` | `~/.cache/myzel/basalt-corpus` (`$BASALT_CORPUS_DIR`) |
| What | 12 excerpts of 5 s in FLAC: a rhythm section (kick, snare, overheads, bass, lead vocal) cut at one shared window, piano, acoustic guitar, electric guitar DI and amp, speech, two full mixes | AlbumDB's ten masters and 62 raw stems, Guitar-TECHS' twelve DI/amp pairs, GuitarSet's 180 strummed takes, 1599 VCTK sentences |
| Used by | CI, golden renders, every test that runs by default | the corpus gates (Learn, Auto Gain), run locally |
| Size | 3.5 MB | 3.2 GB on disk (2.1 GB transferred) |
| Licence | CC BY 4.0, [`ATTRIBUTION.md`](../../tests/corpus/ATTRIBUTION.md), a line in the root `NOTICE` | CC BY 4.0; never committed or shipped |

CI never downloads anything: it sees only the committed tier and the signals the tests generate.

## Sources

Every licence was read on the host's own record page (2026-11-17), and, where the archive carries
its own licence or readme, there too.

| Source | What Basalt uses | Licence | Record |
|---|---|---|---|
| **AlbumDB** (McKenzie, Moliner, Wright 2026): *Extraordinary*, a ten-song indie rock and folk album by Tom McKenzie, as raw multitrack stems and stereo masters, 16-bit 44.1 kHz | masters; raw kick, snare, overheads, bass, lead vocal, piano, acoustic and electric guitar | CC BY 4.0 | [zenodo.org/records/19683001](https://zenodo.org/records/19683001), v1.0 |
| **Guitar-TECHS** (Pedroza et al. 2025): electric guitar; player 3's twelve musical excerpts, each as the DI signal and a miked amplifier, 48 kHz | `P3_music.zip`: `directinput/`, `micamp/` | CC BY 4.0 (record and project page) | [zenodo.org/records/14963133](https://zenodo.org/records/14963133) (the record carries no version number) |
| **GuitarSet** (Xi et al. 2018): acoustic guitar, mono microphone, 44.1 kHz | the 180 accompaniment (`comp`) takes | CC BY 4.0 | [zenodo.org/records/3371780](https://zenodo.org/records/3371780), v1.1.0 |
| **VCTK** 0.92 (Yamagishi, Veaux, MacDonald 2019): read English speech, 48 kHz | the `mic2` channel of four speakers: p228 (F, English), p232 (M, English), p335 (F, New Zealand), p326 (M, Australian), none in Halite's committed tier | CC BY 4.0 (DataShare record, `dc.rights`) | [doi.org/10.7488/ds/2645](https://doi.org/10.7488/ds/2645) |

**Not used, and why** (checked 2026-11-17):

- *3D-MARCo* (Huddersfield, Zenodo 3477602): the record says CC BY 3.0, but the licence file
  and readme inside the documentation archive say CC BY-NC 3.0, "not for any commercial purpose".
  Where they disagree, the stricter one stands.
- *Field Report on 3D Audio Capture of Solo Piano* (Zenodo 8023129): tagged CC BY 4.0, but the
  record asks researchers to contact the authors for permission.
- *MUSDB18, MedleyDB, Dagstuhl ChoirSet, The Spheres, Freischütz Digital*: NC or SA terms.
  *IDMT-SMT* (bass, drums, audio effects), *ENST-Drums*, *ToneTwist AFx*: NC or NC-ND.
- *StemGMD* (CC BY 4.0): drum stems synthesised from sample libraries, 1.1 TB in split archives.
  *WaivOps* loop sets (CC BY 4.0): generated from sample kits. Both are worth a look if the
  corpus needs isolated drums at scale; recorded kits came first here.
- *Freesound*, *Free Music Archive*, *ccMixter*, *Jamendo*: licences per file or track, with NC
  common, and mostly lossy downloads; not searched file by file this time, since the datasets
  above had stable archives with published checksums.
- *Open Multitrack Testbed*: its server's TLS certificate did not verify, so its licences were not
  checked.

**Gaps:** no openly licensed bass amp recording was found, and AlbumDB does not say whether its
raw bass is DI or miked. AlbumDB has no room microphones, only overheads. Apart from the guitars,
the music is one album: one band and one production, and no electronic or hip-hop material.

## Fetching

```sh
plugins/basalt/tools/corpus/fetch.sh --dry-run          # what it would fetch, sizes, free space
plugins/basalt/tools/corpus/fetch.sh                    # the full tier (all), or name targets
plugins/basalt/tools/corpus/fetch.sh guitar-techs vctk
```

| Target | Members | Transfer | On disk |
|---|---|---|---|
| `albumdb-mixes` | the ten stereo masters | 340 MB | 359 MB |
| `albumdb-stems` | 62 raw stems: per song, kick, snare, overheads, bass, lead vocal, and piano and guitars where it has them | 1.14 GB | 1.95 GB |
| `guitar-techs` | 24 files: twelve excerpts, DI and amp | 110 MB | 231 MB |
| `guitarset` | 180 `comp` takes | 337 MB | 484 MB |
| `vctk` | 1599 sentences of four speakers | 202 MB | 202 MB |
| `candidates` | what `make_excerpts.py choose` reads | 606 MB | 809 MB |
| `excerpts` | only the committed excerpts' sources | 184 MB | 266 MB |

Every source is a zip on Zenodo or DataShare, and only the members a target needs are read, with
HTTP range requests (`remotezip.py`, copied from Halite). Before reading an archive, `fetch.sh`
asks its host for the archive's MD5 and compares it with the one pinned in `sources.py`; a
replaced archive stops the run. zipfile checks each member's CRC-32 as it is read, and the
excerpts' sources are also checked against the SHA-256 in `excerpts.tsv`. Members already at full
size are skipped, each finished full-tier target leaves a stamp in `.stamps/`, and a second run
does nothing (`candidates` and `excerpts` follow `make_excerpts.py`, so they are checked member by
member every time). The script checks free space before each archive (with a 1 GiB margin) and refuses a
cache inside the repository. Zenodo served about 0.45 MB/s per connection here (2026-11-17), so
members are read over four connections (`--jobs`), which gave about 1.8 MB/s.

The files land at their archive paths under `albumdb/`, `guitar-techs/`, `guitarset/` and `vctk/`,
and `manifest.tsv` in the cache lists every fetched recording: id, category (`kick`, `snare`,
`overheads`, `bass`, `vocal`, `piano`, `acoustic-guitar`, `electric-guitar`,
`electric-guitar-di`, `electric-guitar-amp`, `speech`, `mix`), source, path, rate, channels, bits
and length. `BasaltMeasure`'s corpus gates will read it (M1).

Needs `curl`, `python3` with NumPy, `ffmpeg` and `ffprobe`, coreutils.

## Committed tier

`tests/corpus/` holds the excerpts (FLAC at the source rate and bit depth, 5 s each),
`excerpts.tsv` (the frozen choice), `manifest.tsv` (what each measures) and `ATTRIBUTION.md`.
From `manifest.tsv` (peak and RMS in dBFS, crest and range in dB; range is the spread between the
95th and 10th percentiles of 50 ms levels, digital silence left out):

| Excerpt | Category | Rate, ch, bits | Peak | RMS | Crest | Range | Silent |
|---|---|---|---|---|---|---|---|
| `albumdb-extraordinary-kick` | kick | 44.1 kHz, 1, 16 | -6.89 | -24.87 | 17.98 | 68.01 | 9.0 % |
| `albumdb-extraordinary-snare` | snare | 44.1 kHz, 1, 16 | -8.67 | -31.39 | 22.72 | 82.77 | 23.0 % |
| `albumdb-extraordinary-overheads` | overheads | 44.1 kHz, 2, 16 | -7.69 | -34.68 | 26.99 | 18.84 | 0.0 % |
| `albumdb-extraordinary-bass` | bass | 44.1 kHz, 1, 16 | -13.00 | -23.95 | 10.95 | 18.67 | 0.0 % |
| `albumdb-extraordinary-vocal` | vocal | 44.1 kHz, 1, 16 | -1.68 | -18.36 | 16.68 | 40.13 | 0.0 % |
| `albumdb-karma-piano` | piano | 44.1 kHz, 2, 16 | -7.74 | -26.09 | 18.35 | 15.66 | 19.0 % |
| `albumdb-karma-acoustic-guitar` | acoustic-guitar | 44.1 kHz, 1, 16 | -7.46 | -22.72 | 15.25 | 36.64 | 0.0 % |
| `albumdb-it-aint-easy-mix` | mix-open | 44.1 kHz, 2, 16 | -4.30 | -17.27 | 12.97 | 7.37 | 0.0 % |
| `albumdb-diva-mix` | mix-dense | 44.1 kHz, 2, 16 | -0.71 | -10.75 | 10.04 | 5.15 | 0.0 % |
| `guitar-techs-p3-05-di` | electric-guitar-di | 48 kHz, 1, 24 | -13.86 | -27.95 | 14.09 | 26.81 | 0.0 % |
| `guitar-techs-p3-05-amp` | electric-guitar-amp | 48 kHz, 1, 16 | -12.89 | -28.58 | 15.68 | 25.03 | 0.0 % |
| `vctk-p228-009` | speech | 48 kHz, 1, 16 | -2.39 | -21.87 | 19.49 | 37.94 | 0.0 % |

The five Extraordinary stems are one passage of one performance: they sum to a drum bus (kick,
snare, overheads) or a rough mix. AlbumDB edited its raw stems to digital silence between hits and
phrases (its record: "removal of silences"), which the *Silent* column shows. The two mixes are
passages from the body of their songs (whole-song crest 14.6 and 10.6 dB), so the open/dense
contrast holds in the excerpts too. The DI and amp excerpts are one passage of one take, but not
aligned: Guitar-TECHS' record warns that its signals may be misaligned by up to 100 ms, and this
pair's offset is not known (cross-correlation on the distorted amp signal was ambiguous; each amp
file also runs 20 to 116 samples longer than its DI). A test that compares them aligns them by
envelope or onsets first, never by assuming a few samples.

The tier is rebuilt, never edited by hand:

```sh
plugins/basalt/tools/corpus/fetch.sh excerpts                        # the sources (verified by SHA-256)
python3 -I plugins/basalt/tools/corpus/make_excerpts.py build        # the FLAC files, byte-identical
python3 -I plugins/basalt/tools/corpus/manifest.py plugins/basalt/tests/corpus --check
```

`excerpts.tsv` is the frozen choice: per excerpt the source member, its SHA-256, the window and
the channels kept. `make_excerpts.py choose` made it from the `candidates` target by the rule in
its docstring. In short: the sources are fixed by role; each stem, guitar and speech window is
the 5 s passage, with every part playing in every second, that swings widest between quiet and
loud; each mix window is the playing passage whose short-term level stays closest to the song's
median (the widest swing picked the songs' intros). Nothing is chosen by listening or
by running Basalt, so the tier can't be tuned to the compressor. Running `choose` again on the
same sources writes the same table.

`manifest.tsv` holds what each excerpt measures: length, rate, channels, bits, sample peak, RMS
(0 dBFS is a full-scale square wave, so a sine reads 3.01 dB under its peak; Basalt's detector
scale, on which Threshold and Auto Gain's reference sit, reads a sine at its peak), crest factor, the spread between the 95th and 10th
percentiles of 50 ms levels, the share of digital silence, and the file's SHA-256. CI can run
`manifest.py --check` to catch a changed or truncated file.

**The tiers overlap.** The excerpts' sources (Extraordinary's and Karma's stems, two masters,
Guitar-TECHS take 05, VCTK p228) are part of the full tier. A gate that must not see CI's material
leaves those recordings out, by the paths in `excerpts.tsv`.

## Files

| | |
|---|---|
| `fetch.sh` | the full tier, the candidates and the excerpts' sources, into the cache |
| `sources.py` | the archives and their pinned MD5, the targets, the cache's `manifest.tsv` |
| `remotezip.py` | members of a remote zip through HTTP range requests |
| `make_excerpts.py` | `choose` the windows, `build` the committed tier, `verify` its sources |
| `manifest.py` | the committed tier's `manifest.tsv`, and `--check` |
| `audio.py` | decoding, FLAC writing and frame levels, shared by the two above |

Run the Python tools with `python3 -I` (isolated mode); they import only each other and NumPy.
