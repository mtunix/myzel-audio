"""Build the committed corpus tier (plugins/halite/tests/corpus) from sources in the cache.

    fetch.sh excerpts                                    fetch and align the sources (a few MB)
    python3 -I make_excerpts.py --out plugins/halite/tests/corpus

Every choice is a rule over metadata and aligned labels, never a listening or detector pass:

- Speakers are fixed below for sex and accent coverage. Six get an "ess" excerpt, two an "esh"
  one (dense in /ʃ ʒ/, which "ess" sentences seldom carry), two a "bright" one (front vowels, at
  most one sibilant), plus two belted /i/ scales from VocalSet.
- Candidates are each speaker's eight best transcripts by CMUdict phones: sibilants per phone
  ("ess"), /ʃ ʒ/ per phone ("esh") or front vowels per phone with at most one sibilant
  ("bright"). No sentence repeats.
- The window is about 3 s between phone boundaries (at most 0.15 s of silence kept at either
  end), the one with the most whole sibilants ("ess"), the most /ʃ ʒ/, then the most kinds of
  /ʃ ʒ tʃ dʒ/ ("esh"), or the most front-vowel time ("bright"). No sibilant may be cut, or end or start within 40 ms of a cut:
  aligners place edges a frame or two off, so its tail would sit unlabelled in the excerpt.
- Audio keeps its rate and bit depth; a 5 ms raised-cosine fade at each cut prevents clicks.
- The labels come from pocketsphinx alone (fetch.sh pins it for the excerpts and records it in
  the label folder's ALIGNER file); the build refuses labels from anything else.

The helper modes (--print-members, --candidates, --align-list) are for fetch.sh.
"""

import argparse
import math
import os
import subprocess
import sys
import wave

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import labels  # noqa: E402

SPEAKERS = (    # id, role: chosen from speaker-info.txt for sex and accent alone
    ("p225", "ess"),       # F, English (Southern England)
    ("p226", "ess"),       # M, English (Surrey)
    ("p262", "ess"),       # F, Scottish (Edinburgh)
    ("p245", "ess"),       # M, Irish (Dublin)
    ("p294", "ess"),       # F, American (San Francisco)
    ("p311", "ess"),       # M, American (Iowa)
    ("p299", "bright"),    # F, American (California)
    ("p237", "bright"),    # M, Scottish (Fife)
    ("p253", "esh"),       # F, Welsh (Cardiff)
    ("p251", "esh"),       # M, Indian
)
VOCALSET = (("f2_scales_belt_i", "female2/scales/belt"), ("m2_scales_belt_i", "male2/scales/belt"))
CANDIDATES = 8
MIN_PHONES = 20
LENGTH, SHORTEST, LONGEST = 3.0, 2.5, 3.5
EDGE_SILENCE = 0.15
SIBILANT_GUARD = 0.04
FADE = 0.005
MIC = "mic2"
ESH = frozenset(("SH", "ZH"))
POSTALVEOLAR = frozenset(("SH", "ZH", "CH", "JH"))
ALIGNER_FILE = "ALIGNER"


def cache_dir():
    return os.environ.get("HALITE_CORPUS_DIR", os.path.expanduser("~/.cache/myzel/halite-corpus"))


def vctk_member(utt):
    return f"wav48_silence_trimmed/{utt.split('_')[0]}/{utt}_{MIC}.flac"


def print_members():
    for stem, folder in VOCALSET:
        print(f"VocalSet/FULL/{folder}/{stem}.wav")
        print(f"VocalSet/annotations/extended 1/with file header/belt/{stem}.csv")


def transcript(cache, utt):
    path = os.path.join(cache, "excerpts", "vctk", "txt", utt.split("_")[0], utt + ".txt")
    with open(path, encoding="utf-8") as f:
        return " ".join(f.read().split())


def print_candidates(cache):
    """Each speaker's best transcripts for its role, by CMUdict phones; one sentence is used once."""
    from align_pocketsphinx import load_dictionary, words_of
    pron = load_dictionary()
    used = set()
    for speaker, role in SPEAKERS:
        scored = []
        for name in sorted(os.listdir(os.path.join(cache, "excerpts", "vctk", "txt", speaker))):
            utt = name[:-4]
            text = transcript(cache, utt)
            words = words_of(text)
            if not words or any(w not in pron for w in words) or text.lower() in used:
                continue
            phones = [p for w in words for p in pron[w]]
            sibilants = sum(p in labels.SIBILANTS for p in phones)
            if len(phones) < MIN_PHONES:
                continue
            if role == "ess":
                score = sibilants / len(phones)
            elif role == "esh":
                score = sum(p in ESH for p in phones) / len(phones)
            elif sibilants <= 1:
                score = sum(p in labels.FRONT_VOWELS for p in phones) / len(phones)
            else:
                continue
            scored.append((-score, utt, text))
        for _, utt, text in sorted(scored)[:CANDIDATES]:
            used.add(text.lower())
            print(f"{speaker}\t{vctk_member(utt)}\t{role}")


def read_candidates(cache):
    with open(os.path.join(cache, "excerpts", "candidates.tsv"), encoding="utf-8") as f:
        return [line.rstrip("\n").split("\t") for line in f if line.strip()]


def print_align_list(cache):
    for _, member, _ in read_candidates(cache):
        utt = os.path.basename(member).rsplit("_", 1)[0]
        print(f"{utt}\t{os.path.join(cache, 'excerpts', 'vctk', member)}\t{transcript(cache, utt)}")


def edges(segments):
    """Where a window may start and end: phone boundaries, or inside silence near the speech."""
    starts, ends = [], []
    for start, end, phone in segments:
        if phone == "sil":
            starts.append(max(start, end - EDGE_SILENCE))
            ends.append(min(end, start + EDGE_SILENCE))
        else:
            starts.append(start)
            ends.append(end)
    return starts, ends


def inside(segments, a, b):
    return [(s, e, p) for s, e, p in segments if s >= a - 1e-9 and e <= b + 1e-9]


def clip(segments, a, b):
    return [(max(s, a) - a, min(e, b) - a, p) for s, e, p in segments if e > a and s < b]


def near_cut(segments, a, b):
    """A sibilant outside the window that ends or starts within the guard of either cut."""
    return any(p in labels.SIBILANTS and (a - SIBILANT_GUARD < e <= a + 1e-9 or b - 1e-9 <= s < b + SIBILANT_GUARD)
               for s, e, p in segments)


def best_window(segments, role):
    starts, ends = edges(segments)
    best = None
    for a in starts:
        options = [e for e in ends if SHORTEST <= e - a <= LONGEST]
        if not options:
            continue
        b = min(options, key=lambda e: abs(e - a - LENGTH))
        whole = inside(segments, a, b)
        sibilants = [x for x in whole if x[2] in labels.SIBILANTS]
        cut = [x for x in segments if x[2] in labels.SIBILANTS and x[0] < b and x[1] > a and x not in whole]
        if cut or near_cut(segments, a, b):
            continue
        if role == "ess":
            key = (len(sibilants), sum(e - s for s, e, _ in sibilants))
        elif role == "esh":
            post = [p for _, _, p in sibilants if p in POSTALVEOLAR]
            key = (sum(p in ESH for p in post), len(set(post)), len(post), len(sibilants),
                   sum(e - s for s, e, _ in sibilants))
        else:
            if len(sibilants) > 1:
                continue
            key = (sum(e - s for s, e, p in whole if p in labels.FRONT_VOWELS), -len(sibilants))
        if best is None or key > best[0]:
            best = (key, a, b)
    return best


def read_pcm(path):
    """Mono 16-bit samples and rate of a WAV or (through ffmpeg) any file ffmpeg reads."""
    if path.endswith(".wav"):
        with wave.open(path, "rb") as w:
            if w.getnchannels() != 1 or w.getsampwidth() != 2:
                raise ValueError(f"{path}: expected mono 16-bit")
            return w.readframes(w.getnframes()), w.getframerate()
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
                            "stream=sample_rate,channels,sample_fmt", "-of", "default=nw=1", path],
                           check=True, capture_output=True, text=True).stdout
    fields = dict(line.split("=", 1) for line in probe.split())
    rate, channels, fmt = int(fields["sample_rate"]), int(fields["channels"]), fields["sample_fmt"]
    if channels != 1 or fmt not in ("s16", "s16p"):
        raise ValueError(f"{path}: expected mono 16-bit, found {channels} channels {fmt}")
    pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "s16le", "-"],
                         check=True, capture_output=True).stdout
    return pcm, rate


def write_excerpt(path, pcm, rate, a, b):
    import array
    samples = array.array("h", pcm)
    first, last = round(a * rate), round(b * rate)
    cut = samples[first:last]
    n = round(FADE * rate)
    for i in range(n):
        g = 0.5 - 0.5 * math.cos(math.pi * (i + 0.5) / n)
        cut[i] = round(cut[i] * g)
        cut[-1 - i] = round(cut[-1 - i] * g)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(cut.tobytes())
    return len(cut) / rate


def speaker_info(cache):
    info = {}
    with open(os.path.join(cache, "excerpts", "vctk", "speaker-info.txt"), encoding="utf-8") as f:
        next(f)
        for line in f:
            fields = line.split()
            if len(fields) >= 4:
                info[fields[0]] = (fields[2], " ".join(fields[3:]).split("(")[0].strip())
    return info


def aligner_of(cache):
    """The aligner fetch.sh recorded for the excerpt labels: pocketsphinx, or the build stops."""
    path = os.path.join(cache, "excerpts", "labels", ALIGNER_FILE)
    try:
        with open(path, encoding="utf-8") as f:
            aligner = f.read().strip()
    except FileNotFoundError:
        raise SystemExit(f"{path} is missing: run fetch.sh excerpts, which aligns and records the aligner")
    if not aligner.startswith("pocketsphinx "):
        raise SystemExit(f"the excerpt labels come from '{aligner}', but the committed tier is aligned with "
                         "pocketsphinx (its ATTRIBUTION.md says so): run fetch.sh excerpts again")
    return aligner


def build(cache, out):
    aligner = aligner_of(cache)
    os.makedirs(out, exist_ok=True)
    info = speaker_info(cache)
    by_speaker = {}
    for speaker, member, role in read_candidates(cache):
        by_speaker.setdefault(speaker, []).append((member, role))
    rows = []
    for speaker, role in SPEAKERS:
        best = None
        for member, _ in by_speaker.get(speaker, []):
            utt = os.path.basename(member).rsplit("_", 1)[0]
            lab = os.path.join(cache, "excerpts", "labels", utt + ".lab")
            if not os.path.exists(lab):
                continue
            window = best_window(labels.read_lab(lab), role)
            if window and (best is None or window[0] > best[0]):
                best = (window[0], window[1], window[2], utt, member, lab)
        if best is None:
            raise SystemExit(f"no usable window for {speaker}")
        _, a, b, utt, member, lab = best
        pcm, rate = read_pcm(os.path.join(cache, "excerpts", "vctk", member))
        name = "vctk-" + utt.replace("_", "-")
        duration = write_excerpt(os.path.join(out, name + ".wav"), pcm, rate, a, b)
        labels.write_lab(os.path.join(out, name + ".lab"),
                         labels.tidy(clip(labels.read_lab(lab), a, b), round(duration, 3)))
        sex, accent = info[speaker]
        rows.append(dict(file=name, role=role, corpus="VCTK 0.92", source=f"{utt}_{MIC}.flac",
                         speaker=speaker, sex=sex, accent=accent, rate=rate, start=a, duration=duration,
                         text=transcript(cache, utt), labels=aligner))
    for stem, folder in VOCALSET:
        wav = os.path.join(cache, "excerpts", "vocalset", "VocalSet", "FULL", folder, stem + ".wav")
        csv = os.path.join(cache, "excerpts", "vocalset", "VocalSet", "annotations", "extended 1",
                           "with file header", "belt", stem + ".csv")
        pcm, rate = read_pcm(wav)
        segments = labels.vocalset_segments(csv, labels.vocalset_vowel(stem), len(pcm) / 2 / rate)
        first_voiced = next(s for s, _, p in segments if p != "sil")
        a = max(0.0, first_voiced - EDGE_SILENCE)
        b = a + LENGTH
        name = "vocalset-" + stem.replace("_", "-")
        duration = write_excerpt(os.path.join(out, name + ".wav"), pcm, rate, a, b)
        labels.write_lab(os.path.join(out, name + ".lab"), labels.tidy(clip(segments, a, b), round(duration, 3)))
        singer = stem.split("_")[0]
        rows.append(dict(file=name, role="bright", corpus="VocalSet 1.0", source=f"FULL/{folder}/{stem}.wav",
                         speaker=singer, sex="F" if singer.startswith("f") else "M", accent="",
                         rate=rate, start=a, duration=duration, text="belted scale on /i/",
                         labels="Annotated-VocalSet"))
    write_manifest(out, rows)
    write_attribution(out, rows, aligner)
    print(f"{len(rows)} excerpts in {out}")


def write_manifest(out, rows):
    with open(os.path.join(out, "excerpts.tsv"), "w", encoding="utf-8") as f:
        f.write("file\trole\tcorpus\tsource\tspeaker\tsex\trate\tstart\tduration\tlabels\n")
        for r in rows:
            f.write(f"{r['file']}\t{r['role']}\t{r['corpus']}\t{r['source']}\t{r['speaker']}\t{r['sex']}\t"
                    f"{r['rate']}\t{r['start']:.3f}\t{r['duration']:.3f}\t{r['labels']}\n")


ATTRIBUTION = """# Test excerpts: attribution

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
  VCTK: forced alignment of the corpus transcript with {aligner}
  (its CMU en-us acoustic model and CMUdict; BSD-style licences), not edited by hand. VocalSet:
  the corrected Annotated-VocalSet `extended 1` note and transition spans as the sung vowel named
  in the file name (/i/ as `IY`), rests and the rest of the file as `sil`.

Made by `plugins/halite/tools/corpus/make_excerpts.py` from sources fetched by `fetch.sh excerpts`.

## Excerpts

| File | Role | Source recording | Speaker | Cut from (s) | Length (s) | Source sentence (the excerpt is part of it) |
|---|---|---|---|---|---|---|
{rows}

*Role:* `ess` is dense in sibilants (/s z ʃ ʒ tʃ dʒ/), `esh` in /ʃ ʒ/, `bright` has bright vowels
and at most one sibilant. Of the two sung scales, f2 carries bright harmonics in 4–12 kHz; m2 has
little energy there and is a sung control, not a bright-vowel case.
"""


def write_attribution(out, rows, aligner):
    table = []
    for r in rows:
        who = f"{r['speaker']} ({r['sex']}{', ' + r['accent'] if r['accent'] else ''})"
        table.append(f"| `{r['file']}` | {r['role']} | {r['corpus']}, `{r['source']}` | {who} | "
                     f"{r['start']:.3f} | {r['duration']:.3f} | {r['text']} |")
    with open(os.path.join(out, "ATTRIBUTION.md"), "w", encoding="utf-8") as f:
        f.write(ATTRIBUTION.replace("{aligner}", aligner).replace("{rows}", "\n".join(table)))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--cache", default=cache_dir())
    parser.add_argument("--out")
    parser.add_argument("--print-members", action="store_true")
    parser.add_argument("--candidates", metavar="CACHE")
    parser.add_argument("--align-list", metavar="CACHE")
    args = parser.parse_args()
    if args.print_members:
        print_members()
    elif args.candidates:
        print_candidates(args.candidates)
    elif args.align_list:
        print_align_list(args.align_list)
    elif args.out:
        build(args.cache, args.out)
    else:
        parser.print_help()
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
