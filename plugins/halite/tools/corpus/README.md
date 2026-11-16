# Halite test corpora

Halite's detector is measured against speech and singing whose phones are labelled, in two tiers:

| | Committed tier | Full tier (`[.corpus]`) |
|---|---|---|
| Where | `plugins/halite/tests/corpus/` | `~/.cache/myzel/halite-corpus` (`$HALITE_CORPUS_DIR`) |
| What | 12 excerpts of about 3 s: 10 spoken (VCTK), 2 sung (VocalSet) | LibriSpeech test-clean, VCTK, VocalSet |
| Used by | CI, golden renders, every test that runs by default | the detection gates' numbers, run locally |
| Size | 3.3 MB | 8.7 GB on disk when all three are fetched (0.4 + 5.5 + 2.8 GB) |
| Licence | CC BY 4.0, [`ATTRIBUTION.md`](../../tests/corpus/ATTRIBUTION.md), a line in the root `NOTICE` | CC BY 4.0; never committed or shipped |

CI never downloads anything: it sees only the committed tier and the synthetic signals the tests
generate.

## Label format

One `.lab` per recording, UTF-8, one segment per line, tab-separated:

```
0.150	0.260	S
0.260	0.370	AE
```

Start and end in seconds (three decimals), then the phone: ARPAbet without stress digits
(`AA` … `ZH`), `sil` for silence and unlabelled gaps, `spn` for spoken noise. Segments are
contiguous from 0 to the end of the audio. The sibilants are `S` /s/, `Z` /z/, `SH` /ʃ/, `ZH` /ʒ/,
`CH` /tʃ/ and `JH` /dʒ/. `labels.py` reads and writes the format; `check_labels.py` validates it.

## Committed tier

`tests/corpus/` holds the excerpts (16-bit mono WAV at the source rate: 48 kHz VCTK, 44.1 kHz
VocalSet), their labels, `excerpts.tsv` (file, role, corpus, source, speaker, sex, rate, where it
was cut, length, which aligner made the labels) and `ATTRIBUTION.md`. Six excerpts are dense in
sibilants (role `ess`), two in /ʃ ʒ/ (`esh`, because sentences dense in sibilants are mostly /s z/),
four have bright vowels and at most one sibilant (`bright`: two spoken, two belted /i/ scales).

| Sibilants in the tier | /s/ | /z/ | /ʃ/ | /ʒ/ | /tʃ/ | /dʒ/ |
|---|---|---|---|---|---|---|
| tokens (`check_labels.py`) | 31 | 11 | 7 | 0 | 3 | 3 |

That is enough for CI to see each class but /ʒ/, not to measure recall per class: the per-phoneme
numbers come from the full tier. Of the sung scales only f2 has bright harmonics: its /i/ notes
carry 1.5 % of their energy in 4–12 kHz (band level −40 dBFS), m2's 0.03 % (−61 dBFS, below the
median spoken vowel's −59 dBFS; measured with an 8th-order Butterworth band-pass per labelled
segment), so m2 is a sung control, not a bright-vowel case.

```sh
python3 -I plugins/halite/tools/corpus/check_labels.py      # validate, count sibilants per excerpt
```

The tier is rebuilt, never edited by hand:

```sh
plugins/halite/tools/corpus/fetch.sh excerpts               # sources only: about 15 MB of members
python3 -I plugins/halite/tools/corpus/make_excerpts.py --out plugins/halite/tests/corpus
```

`make_excerpts.py`'s docstring has the selection rules. Speakers are fixed by sex and accent from
VCTK's speaker list; candidates come from the transcripts (CMUdict phones), the window from the
aligned labels, with no sibilant cut or within 40 ms of a cut. Nothing is chosen by listening or
by running the detector, so the tier can't be tuned to it. The same sources give byte-identical
files. The excerpt labels are always pocketsphinx's: `fetch.sh excerpts` pins it and writes the
aligner and its version to the label folder's `ALIGNER` file, and `make_excerpts.py` refuses
labels from anything else, so installing MFA can't change the tier or make `ATTRIBUTION.md` wrong.

**Sung sibilants are missing.** VocalSet's sung excerpts (with lyrics) have note annotations
but no phone labels, and no aligner here is trained on singing, so the sung material is vowel
exercises only.

## Full tier

```sh
plugins/halite/tools/corpus/fetch.sh --dry-run              # what it would fetch, sizes, free space
plugins/halite/tools/corpus/fetch.sh librispeech            # or vctk, vocalset, all (the default)
```

| Target | Source | Transfer | Labels |
|---|---|---|---|
| `librispeech` | LibriSpeech test-clean (openslr.org/12), MD5 checked; `--librispeech-subsets` adds test-other, dev-clean, dev-other | 347 MB | the published MFA alignments ([Lugosch 2019](https://zenodo.org/records/2619474), CC BY 4.0): test-clean's 2620 TextGrids, read out of the 623 MB zip |
| `vctk` | VCTK 0.92 (DataShare), the MKH 800 channel (`mic2`; `mic1` for p280, which has no `mic2`) | 5.5 GB | forced alignment here (*Aligners*) |
| `vocalset` | VocalSet 1.0 with the corrected Annotated-VocalSet, as deposited by S. Donaher (zenodo.org/records/10200775) | 2.1 GB (2.8 GB unpacked) | the vowel exercises: the annotation's note and transition spans as the vowel in the file name, rests as `sil` |

By default only the needed members of the VCTK and VocalSet zips are read, with HTTP range
requests (`remotezip.py`); each member's CRC-32 is checked. `--full-archive` downloads the whole
zips instead (11.7 GB, 2.5 GB) and checks the MD5 the hosts publish. Downloads resume, finished
steps leave a stamp in `.stamps/`, and a second run skips them. The script checks free space
before each step (with a 1 GiB margin), refuses a cache inside the repository, and deletes
archives after extraction unless `--keep-archives`.

Each corpus gets `labels/<corpus>/<id>.lab` and a manifest `labels/<corpus>.tsv` (id, audio and
labels relative to the cache, speaker, sex) for the measurement tools. Recordings the aligner
could not label are not in the manifest. A `--subset` run writes only its own manifests
(`labels/<name>/`, below) and leaves `labels/<corpus>.tsv` to the full tier.

**LibriSpeech is 16 kHz.** It carries nothing above 8 kHz, so it can test the detector only
below that; VCTK (48 kHz) is the corpus for the default Detect band (4.5 … 12 kHz).

Needs `curl`, `python3`, `ffmpeg` (VCTK, excerpts), `unzip` (`--full-archive`), coreutils.

## Subsets

A milestone's gates need more than the committed tier but rarely whole corpora. `--subset NAME`
fetches only what a named rule in `subset.py` chooses, on the same path as the full tier (range
requests with a CRC-32 per member, or `--full-archive`; resume, stamps, the disk check), and
writes one manifest per corpus with the tune/holdout split. Every choice is a rule over metadata:
speaker lists, file names, transcripts through CMUdict and the zip listings, never listening or a
detector run. The committed tier's speakers and singers are never chosen, so CI's data stays
held out. A preset is frozen once fetched; a different rule gets a new name.

```sh
plugins/halite/tools/corpus/fetch.sh --dry-run --subset m1 vctk vocalset librispeech
plugins/halite/tools/corpus/fetch.sh --subset m1 vctk vocalset librispeech
python3 -I plugins/halite/tools/corpus/check_labels.py --manifest ~/.cache/myzel/halite-corpus/labels/m1/*.tsv
python3 -I plugins/halite/tools/corpus/subset.py split m1 vctk ~/.cache/myzel/halite-corpus
```

`labels/<name>/<corpus>.tsv` has a header row and one row per labelled recording: `id`, `audio`,
`labels` (both relative to the cache), `speaker`, `sex` (F/M), `accent` (VCTK only), `rate` (Hz,
read from the audio header), `role`, `split` (`tune` or `holdout`) and `labeller` (the aligner
record, or the annotation the labels come from). Roles: `speech` (VCTK, 48 kHz), `speech-16k`
(LibriSpeech: timing and bands below 8 kHz only) and `bright` (sung front vowels, the
bright-vowel gate). `check_labels.py --manifest` validates every row against its audio and
reports each half's counts; `--per-speaker` adds a row per speaker.

### m1: the detector gates

The rules are in `subset.py`'s docstring. In short: VCTK speakers round-robin over the accents
per sex from `speaker-info.txt`, 40 recordings each (the shared Stella paragraph, up to eight with
a /ʒ/, the rest in SHA-256 order), only transcripts CMUdict covers; VocalSet's arpeggios, scales
and long tones on /i/ and /e/ (belt, straight, vibrato, forte, messa di voce) with a note file;
all of LibriSpeech test-clean. The VCTK choice reads CMUdict from pocketsphinx, whose version
heads `subsets/m1/vctk.members`: the preset is frozen with 5.0.4, `fetch.sh` installs exactly that,
and `subset.py` refuses to choose with any other version installed (another dictionary can
choose other recordings).

| m1 (2026-11-13, `check_labels.py --manifest`) | VCTK | VocalSet | LibriSpeech |
|---|---|---|---|
| role, rate | `speech`, 48 kHz | `bright`, 44.1 kHz | `speech-16k`, 16 kHz |
| people | 40 (20 F, 20 M), 10 accents | 18 singers (8 F, 10 M) | 40 (20 F, 20 M) |
| labelled recordings | 1597 of 1600 (pocketsphinx found no alignment for 3) | 302 | 2620 |
| audio | 1.60 h | 0.82 h | 5.40 h |
| /s/, /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/ | 2192, 1754, 356, 293, 160, 183 | none (no sung phone labels) | 9091, 5446, 1571, 92, 1106, 885 |
| vowel time (front vowels) | 1410 s (698 s) | 2572 s, all sung /i/ or /e/ | 6686 s (2943 s) |
| tune / holdout | 20 / 20 speakers, 10 F + 10 M each | 9 / 9 singers, 4 F + 5 M each | 20 / 20 speakers, 10 F + 10 M each |
| fetched | 211 MB of FLAC, 1.8 MB of transcripts (44 583 files, 177 MB on disk) | 261 MB of WAV and note files | already in the cache |

Per VCTK speaker: 39–40 recordings, 41–75 /s/, 24–53 /z/, 5–20 /ʃ/, 3–9 /ʒ/, 1–10 /tʃ/,
0–9 /dʒ/. The halves are close in every class (/s/ 1125 tune, 1067 holdout; /ʒ/ 147, 146). The
sibilant counts come from the aligner's labels, not the transcripts. LibriSpeech's /ʒ/ is thin
(92) and VocalSet has no sung sibilants at all (*Committed tier*), so the sung-sibilant case
stays unmeasured.

Measuring on it: `HaliteMeasure` (`plugins/halite/tools/measure`, built with the plugin's
targets) reads these manifests and the committed tier and prints the gates:

```sh
build-dev/plugins/halite/tools/HaliteMeasure --stream --level                   # tune: VCTK, VocalSet, synthetic
build-dev/plugins/halite/tools/HaliteMeasure --split holdout --stream --level   # the held-out half, reported only
build-dev/plugins/halite/tools/HaliteMeasure --data librispeech --stream        # 16 kHz: reported, not gated
build-dev/plugins/halite/tools/HaliteMeasure --learn --data vctk,vocalset --stream   # Learn's 8 s captures against the labels (M3)
```

`--stream` plays each speaker's recordings back to back, so the detector starts cold once per
speaker rather than once per sentence; without it every recording starts cold. Both are reported
in `src/dsp/detector/NOTES.md`, *Measured (M1)*.

### The split

Each corpus is split by person, never by recording: per sex, people sorted by accent (VCTK) and
then by SHA-256 of their id are dealt alternately to `tune` and `holdout`.

- **Constants are chosen on `tune` alone**: band orders, the noise cue and its mapping, the Focus
  curve, the body cap and hold, the Harsh hold, the relative threshold ranges and defaults.
- **`holdout` reports the final gates**, once the constants are fixed, in the detector's NOTES
  (`src/dsp/detector/NOTES.md`, *Gates at the final constants*), next to the `tune` numbers. A holdout result never feeds back into a constant: if a gate
  fails there, the change is chosen on `tune` and both halves are reported again, before and after.
- **The committed tier is a third set**, disjoint from both halves: CI and the golden renders run
  on it, and it never chooses a constant either.

## Aligners

The plan names the Montreal Forced Aligner (MIT). MFA needs Kaldi, which comes only from
conda-forge (MFA's installation guide: `conda create -n aligner -c conda-forge
montreal-forced-aligner`; even its pip route needs `kaldi` from conda). No conda is installed
here, so:

- **LibriSpeech** uses alignments made with MFA and published under CC BY 4.0: MFA's output
  without running MFA.
- **VCTK** uses **pocketsphinx** 5 (BSD-style, CMU; its en-us acoustic model and CMUdict are
  BSD-style too) when `mfa` is not on `PATH`. It installs with pip alone (`fetch.sh` puts it in
  `<cache>/venv`) and returns phone-level alignments. With MFA installed (`conda activate aligner`),
  `fetch.sh` uses it instead (`english_us_arpa` acoustic model and dictionary), or choose with
  `--aligner mfa|pocketsphinx`. The MFA path is written to MFA's documented CLI but has not been
  run here. A label folder whose `ALIGNER` record names another aligner or version is aligned
  again from scratch.
- **The committed excerpts** always use pocketsphinx, whatever is installed.

pocketsphinx against the published MFA alignments, every 10th LibriSpeech test-clean
utterance (`align_pocketsphinx.py compare <cache>`, 2026-11-13, pocketsphinx 5.0.4):

| | |
|---|---|
| utterances aligned | 195 of 262 (67 skipped: a word outside CMUdict) |
| 10 ms frames with the same phone | 86.1 % of 137 509 |
| MFA sibilant frames pocketsphinx also calls sibilant | 91.7 % |
| pocketsphinx sibilant frames MFA also calls sibilant | 85.3 % |
| MFA sibilant segments overlapped by the same phone | 100 % of 1274 |
| boundary difference of matched sibilants | start: median 10 ms, 90th percentile 30 ms; end: 10 ms, 20 ms |

So every sibilant is found and placed to within a few frames. The edges disagree by a frame or
two, which matters for frame counts: a gate that counts vowel frames should leave out a guard of
about 30 ms next to each sibilant. pocketsphinx skips an utterance with an unknown word rather
than guess it: 1.5 % of VCTK's 44 583 transcripts (663; *refraction*, *headteacher*, Scottish
place names), 26 % of the LibriSpeech sample (old spellings and names), which is why LibriSpeech
keeps its MFA alignments. The aligner resets its feature state per utterance, so its labels do
not depend on order or `--jobs`.

## Files

| | |
|---|---|
| `fetch.sh` | the full tier and the excerpt sources, into the cache |
| `make_excerpts.py` | the committed tier from the excerpt sources |
| `check_labels.py` | validation and sibilant counts, for the committed tier or a cache label folder (`--no-audio`) |
| `labels.py` | the label format, phone set, TextGrid and Annotated-VocalSet converters |
| `align_pocketsphinx.py` | alignment, and the comparison against MFA |
| `manifest.py` | the manifests and aligner lists |
| `subset.py` | named subsets (`--subset`): the rules, their manifests and the tune/holdout split |
| `remotezip.py` | members of a remote zip through HTTP range requests |

Run the Python tools with `python3 -I` (isolated mode); they import only each other.
