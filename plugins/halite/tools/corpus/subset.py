"""Named subsets of the full tier: a fixed, held-out-aware choice of data for a milestone's gates.

    python3 -I subset.py members <name> vctk <url-or-zip> <cache>    zip members to fetch (needs pocketsphinx)
    python3 -I subset.py members <name> vocalset <url-or-zip>        zip members to fetch
    python3 -I subset.py align-list <name> vctk <cache>              labels/vctk.align.tsv, subset only
    python3 -I subset.py manifest <name> <corpus> <cache>            labels/<name>/<corpus>.tsv
    python3 -I subset.py split <name> <corpus> <cache>               who is in which half
    python3 -I subset.py names                                       the presets

fetch.sh --subset <name> runs these. Every choice is a rule over metadata (speaker lists, file
names, transcripts through CMUdict, the zip's listing), never a listening or detector pass, and
the committed tier's speakers and singers are left out, so the CI tier stays held out.

Subset m1 (the M1 detector gates):

- VCTK, 40 speakers. Not eligible: the committed tier's ten (make_excerpts.SPEAKERS); p280 and
  s5, whose accents are given only as Unknown and British; speakers with fewer than 40 usable
  recordings. Usable: a transcript whose words are all in CMUdict (so the aligner can label it)
  and an MKH 800 (mic2) file. Per sex, speakers are taken round-robin over the accents in
  alphabetical order, within an accent in SHA-256 order of the speaker id, until there are 20.
- Per VCTK speaker, 40 usable recordings: 001-005 (the paragraph every speaker reads, dense in
  sibilants); then up to 8 whose transcript has a /ʒ/, in SHA-256 order of the recording id (at
  random, 40 recordings hold about one /ʒ/, too few to report the class); then the rest in
  SHA-256 order.
- VocalSet: every arpeggio, scale and long tone on /i/ or /e/ sung belt, straight, with vibrato,
  forte or messa di voce that has an Annotated-VocalSet note file (extended 1), from every singer
  but the committed tier's two (f2, m2). VocalSet has no phone labels for its sung excerpts, so
  there are no sung sibilants.
- LibriSpeech: all of test-clean (16 kHz).
- The CMUdict is pocketsphinx 5.0.4's; with another version installed the VCTK choice refuses to run.

The split: per corpus and sex, people sorted by accent (VCTK) and then SHA-256 of their id, and
dealt alternately to "tune" (constants are chosen on it) and "holdout" (only the final gates are
reported on it). A preset is frozen once fetched: a different rule gets a new name.

A subset manifest has a header row and one row per labelled recording: id, audio, labels (paths
relative to the cache), speaker, sex (F/M), accent (VCTK only), rate (Hz, from the audio header),
role, split, labeller. Roles: speech (VCTK, 48 kHz), speech-16k (LibriSpeech: timing and bands
below 8 kHz only), bright (VocalSet sung front vowels: the bright-vowel gate).
"""

import hashlib
import os
import posixpath
import re
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import manifest  # noqa: E402
from make_excerpts import SPEAKERS as COMMITTED_SPEAKERS, VOCALSET as COMMITTED_VOCALSET  # noqa: E402

PRESETS = {
    "m1": dict(
        vctk_per_sex=20,
        vctk_recordings=40,
        vctk_shared=5,
        vctk_zh=8,
        vctk_unclear_accents=("Unknown", "British"),
        vocalset_contexts=("arpeggios", "scales", "long_tones"),
        vocalset_techniques=("belt", "straight", "vibrato", "forte", "messa"),
        vocalset_vowels=("i", "e"),
        librispeech_subsets=("test-clean",),
        # VCTK's choice reads the transcripts through the CMUdict this release bundles.
        pocketsphinx="5.0.4",
    ),
}
VOCALSET_NOTES = "VocalSet/annotations/extended 1/with file header"
ROLES = {"vctk": "speech", "librispeech": "speech-16k", "vocalset": "bright"}
HEADER = ("id", "audio", "labels", "speaker", "sex", "accent", "rate", "role", "split", "labeller")
LIBRISPEECH_LABELLER = "mfa (Lugosch 2019 alignments)"
VOCALSET_LABELLER = "Annotated-VocalSet extended 1 (notes as the sung vowel)"


def preset(name):
    if name not in PRESETS:
        raise SystemExit(f"unknown subset {name!r}; known: {', '.join(sorted(PRESETS))}")
    return PRESETS[name]


def digest(key):
    return hashlib.sha256(key.encode("utf-8")).hexdigest()


def committed_singers():
    return {stem.split("_")[0] for stem, _ in COMMITTED_VOCALSET}


def parse_speaker_info(text):
    """VCTK's speaker-info.txt: id -> (sex, accent)."""
    info = {}
    for line in text.splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 4:
            info[fields[0]] = (fields[2], fields[3])
    return info


def read_speaker_info(cache):
    with open(os.path.join(cache, "vctk", "speaker-info.txt"), encoding="utf-8") as f:
        return parse_speaker_info(f.read())


def usable_vctk(cache, names):
    """Speaker -> {recording id: phones} for recordings with a mic2 file and a transcript in CMUdict."""
    from align_pocketsphinx import load_dictionary, words_of
    pron = load_dictionary()
    mic2 = {m.group(1) for m in map(re.compile(r"wav48_silence_trimmed/[^/]+/([^/]+)_mic2\.flac$").match, names) if m}
    found = {}
    root = os.path.join(cache, "vctk", "txt")
    for speaker in sorted(os.listdir(root)):
        for name in sorted(os.listdir(os.path.join(root, speaker))):
            utt = name[:-len(".txt")]
            if not name.endswith(".txt") or utt not in mic2:
                continue
            with open(os.path.join(root, speaker, name), encoding="utf-8") as f:
                words = words_of(f.read())
            if words and all(w in pron for w in words):
                found.setdefault(speaker, {})[utt] = [p for w in words for p in pron[w]]
    return found


def choose_vctk_speakers(p, info, usable):
    committed = {s for s, _ in COMMITTED_SPEAKERS}
    chosen = []
    for sex in ("F", "M"):
        by_accent = {}
        for speaker, (s, accent) in info.items():
            if (s == sex and speaker not in committed and accent not in p["vctk_unclear_accents"]
                    and len(usable.get(speaker, ())) >= p["vctk_recordings"]):
                by_accent.setdefault(accent, []).append(speaker)
        queues = [sorted(by_accent[a], key=digest) for a in sorted(by_accent)]
        taken = []
        while len(taken) < p["vctk_per_sex"] and any(queues):
            for q in queues:
                if q and len(taken) < p["vctk_per_sex"]:
                    taken.append(q.pop(0))
        chosen += taken
    return chosen


def choose_vctk_recordings(p, speaker, phones):
    """phones: recording id -> CMUdict phones of the speaker's usable recordings."""
    shared = [u for u in (f"{speaker}_{n:03d}" for n in range(1, p["vctk_shared"] + 1)) if u in phones]
    rest = sorted((u for u in phones if u not in shared), key=digest)
    zh = [u for u in rest if "ZH" in phones[u]][:p["vctk_zh"]]
    fill = [u for u in rest if u not in zh]
    return sorted((shared + zh + fill)[:p["vctk_recordings"]])


def vctk_members(p, zf, cache):
    import importlib.metadata
    installed = importlib.metadata.version("pocketsphinx")
    if installed != p["pocketsphinx"]:
        raise SystemExit(f"this subset was frozen with the CMUdict of pocketsphinx {p['pocketsphinx']}, but {installed} is "
                         f"installed: another dictionary can choose other recordings (pip install pocketsphinx=={p['pocketsphinx']})")
    usable = usable_vctk(cache, [i.filename for i in zf.infolist()])
    info = read_speaker_info(cache)
    members = [f"# CMUdict of pocketsphinx {installed}"]
    for speaker in sorted(choose_vctk_speakers(p, info, usable)):
        for utt in choose_vctk_recordings(p, speaker, usable[speaker]):
            members.append(f"wav48_silence_trimmed/{speaker}/{utt}_mic2.flac")
    return members


def vocalset_members(p, zf):
    names = {i.filename for i in zf.infolist()}
    excluded = committed_singers()
    members = []
    for name in sorted(names):
        m = re.match(r"VocalSet/FULL/[^/]+/([^/]+)/([^/]+)/(([fm][0-9]+)_.*_([a-z]))\.wav$", name)
        if not m:
            continue
        context, technique, stem, singer, vowel = m.groups()
        notes = f"{VOCALSET_NOTES}/{technique}/{stem}.csv"
        if (context in p["vocalset_contexts"] and technique in p["vocalset_techniques"]
                and vowel in p["vocalset_vowels"] and singer not in excluded and notes in names):
            members += [name, notes]
    return members


def deal(people):
    """(id, sex, group) -> split: per sex, sorted by group and digest, alternately tune and holdout."""
    split = {}
    for sex in sorted({s for _, s, _ in people}):
        ordered = sorted((g, digest(i), i) for i, s, g in people if s == sex)
        for n, (_, _, i) in enumerate(ordered):
            split[i] = "tune" if n % 2 == 0 else "holdout"
    return split


def chosen_ids(cache, name, corpus):
    path = os.path.join(cache, "subsets", name, corpus + ".members")
    if not os.path.exists(path):
        raise SystemExit(f"{path} is missing: run fetch.sh --subset {name} {corpus}")
    with open(path, encoding="utf-8") as f:
        audio = [line.strip() for line in f if line.strip().endswith((".flac", ".wav"))]
    ids = {posixpath.basename(a).rsplit(".", 1)[0] for a in audio}
    if corpus == "vctk":
        ids = {i[:-len("_mic2")] for i in ids}
    return ids


def rows(name, corpus, cache):
    """(id, audio, speaker, sex, accent) of the subset's recordings present in the cache."""
    p = preset(name)
    if corpus == "vctk":
        ids = chosen_ids(cache, name, corpus)
        info = read_speaker_info(cache)
        for utt, audio, speaker, sex, _ in manifest.vctk_rows(os.path.join(cache, "vctk")):
            if utt in ids:
                yield utt, audio, speaker, sex, info[speaker][1]
    elif corpus == "vocalset":
        ids = chosen_ids(cache, name, corpus)
        for stem, audio, singer, sex in manifest.vocalset_rows(cache):
            if stem in ids:
                yield stem, audio, singer, sex, ""
    elif corpus == "librispeech":
        subsets = [os.sep + s + os.sep for s in p["librispeech_subsets"]]
        for utt, audio, speaker, sex in manifest.librispeech_rows(cache):
            if any(s in audio for s in subsets):
                yield utt, audio, speaker, sex, ""
    else:
        raise SystemExit(f"unknown corpus {corpus!r}")


def splits(name, corpus, cache):
    """Speaker -> half, from the chosen members alone, so a missing file or label can't move anyone."""
    if corpus == "vctk":
        info = read_speaker_info(cache)
        people = {(s, *info[s]) for s in (i.split("_")[0] for i in chosen_ids(cache, name, corpus))}
    elif corpus == "vocalset":
        people = {(s, "F" if s.startswith("f") else "M", "")
                  for s in (i.split("_")[0] for i in chosen_ids(cache, name, corpus))}
    else:
        people = {(speaker, sex, accent) for _, _, speaker, sex, accent in rows(name, corpus, cache)}
    return deal(sorted(people))


def labeller(corpus, cache):
    if corpus == "vctk":
        path = os.path.join(cache, "labels", "vctk", "ALIGNER")
        if not os.path.exists(path):
            raise SystemExit(f"{path} is missing: the VCTK labels have no recorded aligner")
        with open(path, encoding="utf-8") as f:
            return f.read().strip()
    return LIBRISPEECH_LABELLER if corpus == "librispeech" else VOCALSET_LABELLER


def write_manifest(name, corpus, cache):
    split = splits(name, corpus, cache)
    by = labeller(corpus, cache)
    labels_dir = os.path.join(cache, "labels", corpus)
    out = []
    unlabelled = []
    for utt, audio, speaker, sex, accent in rows(name, corpus, cache):
        lab = os.path.join(labels_dir, utt + ".lab")
        if not os.path.exists(lab):
            unlabelled.append(utt)
            continue
        rate = manifest.audio_info(audio)[0]
        out.append((utt, os.path.relpath(audio, cache), os.path.relpath(lab, cache), speaker, sex, accent,
                    str(rate), ROLES[corpus], split[speaker], by))
    path = os.path.join(cache, "labels", name, corpus + ".tsv")
    manifest.write_tsv(path, HEADER, out)
    print(f"  {len(out)} labelled recordings of subset {name} in {path}")
    for utt in unlabelled:
        print(f"  left out, no labels: {utt}")
    if corpus != "librispeech":
        absent = len(chosen_ids(cache, name, corpus)) - len(out) - len(unlabelled)
        if absent:
            print(f"  left out, not in the cache: {absent} chosen recordings")


def write_align_list(name, cache):
    ids = chosen_ids(cache, name, "vctk")
    out = [(u, a, t) for u, a, _, _, t in manifest.vctk_rows(os.path.join(cache, "vctk")) if u in ids]
    manifest.write_tsv(os.path.join(cache, "labels", "vctk.align.tsv"), None, out)
    print(f"  {len(out)} VCTK recordings of subset {name} to align")


def main(argv):
    if argv == ["names"]:
        print("\n".join(sorted(PRESETS)))
        return 0
    if len(argv) in (4, 5) and argv[0] == "members" and argv[2] in ("vctk", "vocalset"):
        import remotezip
        _, zf = remotezip.open_remote(argv[3])
        if argv[2] == "vctk":
            if len(argv) != 5:
                raise SystemExit("members <name> vctk needs the cache, which holds the transcripts")
            members = vctk_members(preset(argv[1]), zf, argv[4])
        else:
            members = vocalset_members(preset(argv[1]), zf)
        print("\n".join(members))
        return 0
    if len(argv) == 4 and argv[0] == "align-list" and argv[2] == "vctk":
        write_align_list(argv[1], argv[3])
        return 0
    if len(argv) == 4 and argv[0] == "manifest":
        write_manifest(argv[1], argv[2], argv[3])
        return 0
    if len(argv) == 4 and argv[0] == "split":
        for person, half in sorted(splits(argv[1], argv[2], argv[3]).items()):
            print(f"{person}\t{half}")
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
