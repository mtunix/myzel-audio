"""Validate phone labels and report their sibilants.

    python3 -I plugins/halite/tools/corpus/check_labels.py [dir]      (default: tests/corpus)
    python3 -I plugins/halite/tools/corpus/check_labels.py --no-audio ~/.cache/myzel/halite-corpus/labels/vctk
    python3 -I plugins/halite/tools/corpus/check_labels.py --manifest ~/.cache/myzel/halite-corpus/labels/m1/*.tsv

Checks every .lab (labels.py's format): three tab-separated fields, times in order and contiguous
from 0, every segment longer than zero, phones in the set. With audio (the default) every .lab
needs a mono 16-bit .wav beside it whose length the labels end on (±1 ms), every .wav a .lab, and
excerpts.tsv, when present, must list exactly those files at their rates. Prints per file the
sibilant segments by phone and their total duration. Exit status 1 when a check fails.

With --manifest it checks a subset manifest (subset.py) instead: every row's labels as above,
ending on its audio (FLAC or WAV, mono, at the listed rate), and reports per split the speakers,
recordings, sibilants by phone, sibilant and vowel time; --per-speaker adds a row per speaker.
"""

import argparse
import collections
import os
import sys
import wave

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import labels  # noqa: E402
import manifest  # noqa: E402

DEFAULT = os.path.normpath(os.path.join(HERE, "..", "..", "tests", "corpus"))
TOLERANCE = 0.0005    # labels carry three decimals
END_TOLERANCE = 0.001
SIBILANT_ORDER = ("S", "Z", "SH", "ZH", "CH", "JH")


def check_segments(segments, duration):
    errors = []
    if not segments:
        return ["no segments"]
    if abs(segments[0][0]) > TOLERANCE:
        errors.append(f"starts at {segments[0][0]:.3f}, not 0")
    for i, (start, end, phone) in enumerate(segments):
        if end - start <= 0:
            errors.append(f"segment {i + 1} ({phone}) has no length: {start:.3f}–{end:.3f}")
        if i and abs(start - segments[i - 1][1]) > TOLERANCE:
            errors.append(f"segment {i + 1} starts at {start:.3f}, the previous ends at {segments[i - 1][1]:.3f}")
        if phone not in labels.PHONES:
            errors.append(f"segment {i + 1}: unknown phone {phone!r}")
    if duration is not None and abs(segments[-1][1] - duration) > END_TOLERANCE:
        errors.append(f"labels end at {segments[-1][1]:.3f}, the audio at {duration:.4f}")
    return errors


def wav_info(path):
    with wave.open(path, "rb") as w:
        return w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()


def check_manifest(path, cache, per_speaker):
    """Failures of one subset manifest; prints its sibilant and vowel totals per split."""
    with open(path, encoding="utf-8") as f:
        header = f.readline().rstrip("\n").split("\t")
        rows = [dict(zip(header, line.rstrip("\n").split("\t"))) for line in f if line.strip()]
    failures = []
    halves = collections.defaultdict(set)
    for r in rows:
        halves[r["speaker"]].add(r["split"])
    failures += [f"{os.path.basename(path)}: speaker {s} is in both halves" for s, h in sorted(halves.items()) if len(h) > 1]
    groups = collections.defaultdict(lambda: dict(people=set(), sexes={}, n=0, time=0.0, counts=collections.Counter(),
                                                    sibilant=0.0, vowel=0.0, front=0.0))
    for r in rows:
        where = f"{os.path.basename(path)}: {r['id']}"
        audio = os.path.join(cache, r["audio"])
        try:
            rate, channels, _, frames = manifest.audio_info(audio)
        except (OSError, ValueError) as e:
            failures.append(f"{where}: {e}")
            continue
        if channels != 1:
            failures.append(f"{where}: {channels} channels (expected mono)")
        if rate != int(r["rate"]):
            failures.append(f"{where}: listed at {r['rate']} Hz, the audio is at {rate} Hz")
        try:
            segments = labels.read_lab(os.path.join(cache, r["labels"]))
        except (OSError, ValueError) as e:
            failures.append(f"{where}: {e}")
            continue
        failures += [f"{where}: {e}" for e in check_segments(segments, frames / rate)]
        keys = [r["split"], "total"] + ([f"{r['split']} {r['speaker']}"] if per_speaker else [])
        for key in keys:
            g = groups[key]
            g["people"].add(r["speaker"])
            g["sexes"][r["speaker"]] = r["sex"]
            g["n"] += 1
            g["time"] += frames / rate
            for s, e, p in segments:
                if p in labels.SIBILANTS:
                    g["counts"][p] += 1
                    g["sibilant"] += e - s
                elif p in labels.VOWELS:
                    g["vowel"] += e - s
                    g["front"] += (e - s) * (p in labels.FRONT_VOWELS)

    print(f"{path}: {len(rows)} recordings")
    print(f"{'split':<20}  speakers  F/M      files  audio h  " + "  ".join(f"{p:>5}" for p in SIBILANT_ORDER)
          + "  sibilant s  vowel s  front s")
    for key in sorted(groups, key=lambda k: (k == "total", k)):
        g = groups[key]
        women = sum(x == "F" for x in g["sexes"].values())
        print(f"{key:<20}  {len(g['people']):>8}  {women:>2}/{len(g['people']) - women:<2}  {g['n']:>8}  "
              f"{g['time'] / 3600:>7.2f}  " + "  ".join(f"{g['counts'][p]:>5}" for p in SIBILANT_ORDER)
              + f"  {g['sibilant']:>10.1f}  {g['vowel']:>7.1f}  {g['front']:>7.1f}")
    print()
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("dir", nargs="?", default=DEFAULT)
    parser.add_argument("--no-audio", action="store_true", help="labels only (the cache's label folders)")
    parser.add_argument("--manifest", nargs="+", metavar="TSV", help="subset manifests (subset.py) instead of a folder")
    parser.add_argument("--cache", default=os.environ.get("HALITE_CORPUS_DIR",
                                                          os.path.expanduser("~/.cache/myzel/halite-corpus")),
                        help="what the manifests' paths are relative to")
    parser.add_argument("--per-speaker", action="store_true", help="with --manifest: a row per speaker")
    args = parser.parse_args()

    if args.manifest:
        failures = []
        for path in args.manifest:
            failures += check_manifest(path, args.cache, args.per_speaker)
        if failures:
            print(f"{len(failures)} problems:")
            for f in failures:
                print(f"  {f}")
            return 1
        print("All checks passed.")
        return 0

    names = sorted(os.listdir(args.dir))
    labs = [n[:-4] for n in names if n.endswith(".lab")]
    wavs = [n[:-4] for n in names if n.endswith(".wav")]
    failures = []
    rates = {}
    if not args.no_audio:
        for stem in sorted(set(wavs) - set(labs)):
            failures.append(f"{stem}.wav: no .lab")

    totals = collections.Counter()
    total_duration = total_sibilant = 0.0
    rows = []
    for stem in labs:
        path = os.path.join(args.dir, stem + ".lab")
        duration = None
        if not args.no_audio:
            wav = os.path.join(args.dir, stem + ".wav")
            if not os.path.exists(wav):
                failures.append(f"{stem}.lab: no .wav")
            else:
                channels, width, rate, frames = wav_info(wav)
                if channels != 1 or width != 2:
                    failures.append(f"{stem}.wav: {channels} channels, {8 * width} bits (expected mono 16-bit)")
                duration = frames / rate
                rates[stem] = rate
        try:
            segments = labels.read_lab(path)
        except ValueError as e:
            failures.append(str(e))
            continue
        failures += [f"{stem}.lab: {e}" for e in check_segments(segments, duration)]
        sibilants = [(e - s, p) for s, e, p in segments if p in labels.SIBILANTS]
        counts = collections.Counter(p for _, p in sibilants)
        totals.update(counts)
        sibilant_time = sum(d for d, _ in sibilants)
        front = sum(e - s for s, e, p in segments if p in labels.FRONT_VOWELS)
        length = segments[-1][1] if segments else 0.0
        total_duration += length
        total_sibilant += sibilant_time
        rows.append((stem, length, rates.get(stem), counts, sibilant_time, front))

    manifest = os.path.join(args.dir, "excerpts.tsv")
    roles = {}
    if not args.no_audio and os.path.exists(manifest):
        with open(manifest, encoding="utf-8") as f:
            header = f.readline().rstrip("\n").split("\t")
            listed = [dict(zip(header, line.rstrip("\n").split("\t"))) for line in f if line.strip()]
        for r in listed:
            roles[r["file"]] = r["role"]
            if r["file"] not in rates:
                failures.append(f"excerpts.tsv lists {r['file']}, which has no .wav and .lab")
            elif int(r["rate"]) != rates[r["file"]]:
                failures.append(f"excerpts.tsv: {r['file']} at {r['rate']} Hz, the file is at {rates[r['file']]} Hz")
        for stem in sorted(set(rates) - set(roles)):
            failures.append(f"{stem}: not listed in excerpts.tsv")

    width = max([len(r[0]) for r in rows] + [7])
    print(f"{'excerpt':<{width}}  role     rate     length  " + "  ".join(f"{p:>4}" for p in SIBILANT_ORDER)
          + "  sibilants  sibilant s  front vowel s")
    for stem, length, rate, counts, sibilant_time, front in rows:
        print(f"{stem:<{width}}  {roles.get(stem, ''):<6}  {(rate or 0) / 1000:>5.1f}k  {length:>9.3f}  "
              + "  ".join(f"{counts[p]:>4}" for p in SIBILANT_ORDER)
              + f"  {sum(counts.values()):>9}  {sibilant_time:>10.3f}  {front:>13.3f}")
    print(f"{'total':<{width}}  {'':<6}  {'':>6}  {total_duration:>9.3f}  "
          + "  ".join(f"{totals[p]:>4}" for p in SIBILANT_ORDER)
          + f"  {sum(totals.values()):>9}  {total_sibilant:>10.3f}")
    size = sum(os.path.getsize(os.path.join(args.dir, n)) for n in os.listdir(args.dir))
    print(f"\n{len(rows)} label files, {size / 1e6:.2f} MB in {args.dir}")

    if failures:
        print(f"\n{len(failures)} problems:")
        for f in failures:
            print(f"  {f}")
        return 1
    print("All checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
