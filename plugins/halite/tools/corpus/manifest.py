"""Manifests of the fetched corpora, and the lists the aligners read.

    python3 -I manifest.py align-list vctk <cache>      labels/vctk.align.tsv: id, audio, transcript
    python3 -I manifest.py manifest <corpus> <cache>    labels/<corpus>.tsv for librispeech, vctk, vocalset

A manifest has a header row and one row per labelled recording: id, audio and labels (paths
relative to the cache), speaker, sex (F/M). Recordings the aligner could not label are left out.
"""

import os
import sys


def librispeech_rows(cache):
    root = os.path.join(cache, "librispeech", "LibriSpeech")
    sexes = {}
    with open(os.path.join(root, "SPEAKERS.TXT"), encoding="utf-8") as f:
        for line in f:
            if line.startswith(";"):
                continue
            fields = [x.strip() for x in line.split("|")]
            if len(fields) >= 2:
                sexes[fields[0]] = fields[1]
    for subset in sorted(os.listdir(root)):
        if not os.path.isdir(os.path.join(root, subset)):
            continue
        for dirpath, _, files in os.walk(os.path.join(root, subset)):
            for name in sorted(files):
                if name.endswith(".flac"):
                    utt = name[:-5]
                    speaker = utt.split("-")[0]
                    yield utt, os.path.join(dirpath, name), speaker, sexes.get(speaker, "?")


def vctk_speakers(path):
    sexes = {}
    with open(path, encoding="utf-8") as f:
        next(f)
        for line in f:
            fields = line.split()
            if len(fields) >= 3:
                sexes[fields[0]] = fields[2]
    return sexes


def vctk_rows(root):
    """VCTK recordings with a transcript: the mic2 file, or mic1 where mic2 is missing (p280)."""
    sexes = vctk_speakers(os.path.join(root, "speaker-info.txt"))
    for dirpath, _, files in sorted(os.walk(os.path.join(root, "txt"))):
        for name in sorted(files):
            if not name.endswith(".txt"):
                continue
            utt = name[:-4]
            speaker = utt.split("_")[0]
            wav = os.path.join(root, "wav48_silence_trimmed", speaker)
            audio = next((os.path.join(wav, f"{utt}_{mic}.flac") for mic in ("mic2", "mic1")
                          if os.path.exists(os.path.join(wav, f"{utt}_{mic}.flac"))), None)
            if audio is None:
                continue
            with open(os.path.join(dirpath, name), encoding="utf-8") as f:
                text = " ".join(f.read().split())
            yield utt, audio, speaker, sexes.get(speaker, "?"), text


def vocalset_rows(cache):
    root = os.path.join(cache, "vocalset", "VocalSet", "FULL")
    for dirpath, _, files in sorted(os.walk(root)):
        for name in sorted(files):
            if name.endswith(".wav"):
                stem = name[:-4]
                singer = stem.split("_")[0]
                yield stem, os.path.join(dirpath, name), singer, "F" if singer.startswith("f") else "M"


def audio_info(path):
    """Rate, channels, bits per sample and frames from a FLAC or WAV header."""
    with open(path, "rb") as f:
        magic = f.read(4)
        if magic == b"fLaC":
            f.read(4)    # the first metadata block is always STREAMINFO
            bits = int.from_bytes(f.read(34)[10:18], "big")
            return bits >> 44, ((bits >> 41) & 7) + 1, ((bits >> 36) & 31) + 1, bits & ((1 << 36) - 1)
        if magic != b"RIFF" or f.read(8)[4:] != b"WAVE":
            raise ValueError(f"{path}: neither FLAC nor WAV")
        fmt = None
        while len(chunk := f.read(8)) == 8:
            kind, size = chunk[:4], int.from_bytes(chunk[4:], "little")
            if kind == b"fmt ":
                data = f.read(size + (size & 1))
                fmt = [int.from_bytes(data[a:b], "little") for a, b in ((2, 4), (4, 8), (12, 14), (14, 16))]
            elif kind == b"data" and fmt:
                channels, rate, block, depth = fmt
                return rate, channels, depth, size // block
            else:
                f.seek(size + (size & 1), os.SEEK_CUR)
    raise ValueError(f"{path}: no fmt and data chunks")


def write_tsv(path, header, rows):
    tmp = path + ".part"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(tmp, "w", encoding="utf-8") as f:
        if header:
            f.write("\t".join(header) + "\n")
        for row in rows:
            f.write("\t".join(row) + "\n")
    os.replace(tmp, path)


def manifest(corpus, cache):
    labels = os.path.join(cache, "labels", corpus)
    if corpus == "librispeech":
        source = librispeech_rows(cache)
    elif corpus == "vctk":
        source = ((u, a, s, x) for u, a, s, x, _ in vctk_rows(os.path.join(cache, "vctk")))
    elif corpus == "vocalset":
        source = vocalset_rows(cache)
    else:
        raise SystemExit(f"unknown corpus {corpus!r}")
    rows = []
    for utt, audio, speaker, sex in source:
        lab = os.path.join(labels, utt + ".lab")
        if os.path.exists(lab):
            rows.append((utt, os.path.relpath(audio, cache), os.path.relpath(lab, cache), speaker, sex))
    out = os.path.join(cache, "labels", corpus + ".tsv")
    write_tsv(out, ("id", "audio", "labels", "speaker", "sex"), rows)
    print(f"  {len(rows)} labelled recordings in {out}")


def main(argv):
    if len(argv) == 3 and argv[0] == "align-list" and argv[1] == "vctk":
        cache = argv[2]
        rows = [(u, a, t) for u, a, _, _, t in vctk_rows(os.path.join(cache, "vctk"))]
        write_tsv(os.path.join(cache, "labels", "vctk.align.tsv"), None, rows)
        print(f"  {len(rows)} VCTK recordings to align")
        return 0
    if len(argv) == 3 and argv[0] == "manifest":
        manifest(argv[1], argv[2])
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
