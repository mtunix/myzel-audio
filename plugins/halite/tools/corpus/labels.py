"""Phone labels for Halite's test corpora: the format, the phone set and the converters.

A label file (.lab) is UTF-8 text, one segment per line, tab-separated:

    start   end     phone       (seconds, three decimals; ARPAbet without stress, or sil / spn)

The segments are contiguous, start at 0 and end at the file's duration. `sil` is silence or a gap
the source left unlabelled, `spn` spoken noise (the aligner's word it could not place).

    python3 -I labels.py textgrids <textgrid-dir> <out-dir>
    python3 -I labels.py vocalset <annotation-dir> <audio-dir> <out-dir>
"""

import csv
import os
import re
import sys
import wave

ARPABET = ("AA AE AH AO AW AY B CH D DH EH ER EY F G HH IH IY JH K L M N NG OW OY P R S SH T TH "
           "UH UW V W Y Z ZH").split()
NON_SPEECH = ("sil", "spn")
PHONES = frozenset(ARPABET) | frozenset(NON_SPEECH)
SIBILANTS = frozenset(("S", "Z", "SH", "ZH", "CH", "JH"))
VOWELS = frozenset(("AA AE AH AO AW AY EH ER EY IH IY OW OY UH UW").split())
FRONT_VOWELS = frozenset(("IY", "IH", "EY", "EH", "AE"))

# VocalSet's five sung vowels (Italian-style) on the nearest ARPAbet monophthong.
VOCALSET_VOWELS = {"a": "AA", "e": "EH", "i": "IY", "o": "AO", "u": "UW"}

MERGE_GAP = 0.05    # an unlabelled gap shorter than this joins its neighbours (annotation frame jitter)


def normalise(phone):
    """An aligner's phone on our set: stress digits dropped, the silence spellings unified."""
    p = phone.strip()
    if p in ("", "sil", "sp", "SIL", "<sil>"):
        return "sil"
    if p.lower() == "spn":
        return "spn"
    p = re.sub(r"[0-9]$", "", p).upper()
    if p not in PHONES:
        raise ValueError(f"phone outside the ARPAbet set: {phone!r}")
    return p


def tidy(segments, duration):
    """Contiguous segments from 0 to duration: gaps become sil, neighbouring sil merges."""
    out = []
    t = 0.0
    for start, end, phone in sorted(segments):
        start = max(start, t)
        end = min(end, duration)
        if end <= start:
            continue
        if start > t:
            out.append([t, start, "sil"])
        out.append([start, end, phone])
        t = end
    if duration > t:
        out.append([t, duration, "sil"])
    merged = []
    for seg in out:
        if merged and merged[-1][2] == seg[2] == "sil":
            merged[-1][1] = seg[1]
        else:
            merged.append(seg)
    return [(round(s, 3), round(e, 3), p) for s, e, p in merged if round(e, 3) > round(s, 3)]


def write_lab(path, segments):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + ".part"
    with open(tmp, "w", encoding="utf-8") as f:
        for start, end, phone in segments:
            f.write(f"{start:.3f}\t{end:.3f}\t{phone}\n")
    os.replace(tmp, path)


def read_lab(path):
    """The segments of a label file; raises ValueError on a malformed line."""
    segments = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            fields = line.rstrip("\n").split("\t")
            if len(fields) != 3:
                raise ValueError(f"{path}:{n}: expected start<TAB>end<TAB>phone")
            try:
                segments.append((float(fields[0]), float(fields[1]), fields[2]))
            except ValueError:
                raise ValueError(f"{path}:{n}: times must be numbers") from None
    return segments


def read_textgrid_tier(path, tier):
    """One interval tier of a Praat TextGrid in the long text format (what MFA writes)."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    xmax = float(re.search(r"^xmax = ([0-9.]+)", text, re.M).group(1))
    for block in re.split(r"\n\s*item \[\d+\]:", text)[1:]:
        name = re.search(r'name = "([^"]*)"', block).group(1)
        if name != tier and not name.endswith(" - " + tier):    # also "<speaker> - phones"
            continue
        intervals = re.findall(r'xmin = ([0-9.]+)\s*xmax = ([0-9.]+)\s*text = "([^"]*)"', block)
        return xmax, [(float(a), float(b), t) for a, b, t in intervals]
    raise ValueError(f"{path}: no tier named {tier!r}")


def convert_textgrids(src, dst):
    count = 0
    for root, _, files in os.walk(src):
        for name in sorted(files):
            if not name.endswith(".TextGrid"):
                continue
            duration, intervals = read_textgrid_tier(os.path.join(root, name), "phones")
            segments = [(a, b, normalise(t)) for a, b, t in intervals]
            write_lab(os.path.join(dst, name[:-len(".TextGrid")] + ".lab"), tidy(segments, duration))
            count += 1
    print(f"  {count} label files from TextGrids in {dst}")


def vocalset_vowel(stem):
    """The sung vowel of a VocalSet exercise from its file name (…_a.wav), or None for an excerpt."""
    m = re.search(r"_([aeiou])$", stem)
    return VOCALSET_VOWELS[m.group(1)] if m else None


def read_vocalset_annotation(path):
    """Duration and (start, end, type) rows of an Annotated-VocalSet 'extended' CSV with header."""
    with open(path, encoding="utf-8-sig", newline="") as f:
        lines = f.read().splitlines()
    duration = float(next(l for l in lines[1:12] if re.fullmatch(r"\s*[0-9]+\.[0-9]+\s*", l)))
    header = next(i for i, l in enumerate(lines) if l.startswith("Sequence"))
    rows = []
    for row in csv.reader(lines[header + 1:]):
        if len(row) > 4 and row[1].strip():
            rows.append((float(row[1]), float(row[2]), row[4].strip()))
    return duration, rows


def vocalset_segments(path, vowel, duration=None):
    """Sound and Transition rows as the sung vowel, Rest as sil; short gaps between notes close."""
    annotated, rows = read_vocalset_annotation(path)
    duration = annotated if duration is None else duration
    voiced = []
    for start, end, kind in rows:
        if kind not in ("Sound", "Transition"):
            continue
        if voiced and start - voiced[-1][1] < MERGE_GAP:
            voiced[-1][1] = end
        else:
            voiced.append([start, end])
    return tidy([(a, b, vowel) for a, b in voiced], duration)


def convert_vocalset(src, audio, dst):
    """Labels for the vowel exercises, ending on each WAV's own length."""
    wavs = {}
    for root, _, files in os.walk(audio):
        for name in files:
            if name.endswith(".wav"):
                wavs[name[:-4]] = os.path.join(root, name)
    count = missing = 0
    for root, _, files in os.walk(src):
        for name in sorted(files):
            stem, ext = os.path.splitext(name)
            vowel = vocalset_vowel(stem)
            if ext != ".csv" or vowel is None:
                continue
            if stem not in wavs:
                missing += 1
                continue
            try:
                with wave.open(wavs[stem], "rb") as w:
                    duration = w.getnframes() / w.getframerate()
            except wave.Error:
                duration = None    # a format the wave module can't read: the annotation's own length
            write_lab(os.path.join(dst, stem + ".lab"), vocalset_segments(os.path.join(root, name), vowel, duration))
            count += 1
    print(f"  {count} label files from VocalSet annotations in {dst} (vowel exercises; excerpts have no phones)"
          + (f", {missing} annotations without audio" if missing else ""))


def main(argv):
    if len(argv) == 3 and argv[0] == "textgrids":
        convert_textgrids(argv[1], argv[2])
        return 0
    if len(argv) == 4 and argv[0] == "vocalset":
        convert_vocalset(argv[1], argv[2], argv[3])
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
