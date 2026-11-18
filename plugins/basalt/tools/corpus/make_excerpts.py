"""Choose and build Basalt's committed corpus tier (plugins/basalt/tests/corpus) from the cache.

    fetch.sh candidates                     what the rule reads (0.6 GB transferred)
    python3 -I make_excerpts.py choose      writes tests/corpus/excerpts.tsv
    fetch.sh excerpts                       only the chosen sources (enough to build)
    python3 -I make_excerpts.py build       writes the FLAC files next to it
    python3 -I manifest.py plugins/basalt/tests/corpus

--cache (default $BASALT_CORPUS_DIR or ~/.cache/myzel/basalt-corpus) and --out (default
plugins/basalt/tests/corpus) override the folders.

The sources are fixed below; the windows are chosen by a rule over levels, never by listening or
by running Basalt, so the tier can't be tuned to the compressor:

- Rhythm section: the raw kick, snare, overheads, bass and lead vocal of the first AlbumDB song
  that has all five (Extraordinary), cut at one shared window, so they stay in time with each
  other and sum to a drum bus or a rough mix.
- Piano and acoustic guitar: the raw stems of the first song that has both (Karma), a window each.
- Mixes: of AlbumDB's ten stereo masters, the one with the highest crest factor (peak over RMS,
  whole song) and the one with the lowest: the most open and the densest. Their windows follow
  their own rule (below), so each excerpt shows the song's body, not its intro.
- Electric guitar: the DI and miked-amp recordings of one Guitar-TECHS excerpt, cut at the same
  time; the excerpt and window score best over all twelve. The two files are the same take but not
  aligned: the dataset's record warns of up to 100 ms of misalignment between its signals, and the
  offset of this pair is not known (each amp file also runs 20-116 samples longer than its DI).
- Speech: VCTK speaker p228, the recording that scores best; recordings shorter than the window are
  skipped.

The rule, on mean-square levels over all channels (dBFS): a window of LENGTH s starts on a GRID s
grid and ends at least OUTRO s before a song or guitar take does (fade-outs, ring-outs; VCTK is
trimmed, so speech has no margin). A source plays in the window when every whole second of it
holds a 10 ms frame within PLAY_RANGE dB of the source's 99th-percentile frame. Its contrast is the
spread between its loudest and quietest 400 ms block (hop 100 ms), with blocks floored FLOOR_RANGE
dB below the source's 99th-percentile block so that silence can't win. A window is valid when
every source of the group plays in it, and the valid window with the highest mean contrast is
chosen: the widest quiet-to-loud swing while everything plays. Ties go to the earlier window.

The mixes take the valid window whose 400 ms blocks stay closest to the song's median block (the
smallest largest deviation, over the song without its last OUTRO s): the widest swing would pick
an intro or a transition, and a mix excerpt stands for the song's density.

Cut sources keep their rate and bit depth; no gain, no resampling. A channel that is digital
silence is dropped and identical channels are kept once, so a mono part stored as stereo becomes
mono. A 5 ms raised-cosine fade at each cut prevents clicks. The same sources give byte-identical
FLAC files: build checks each source's SHA-256 against excerpts.tsv first.
"""

import argparse
import hashlib
import math
import os
import sys

import numpy as np

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import audio  # noqa: E402
import sources  # noqa: E402

RHYTHM_SONG = "01 Extraordinary"
RHYTHM = (("kick", "drums_kick"), ("snare", "drums_snare"), ("overheads", "drums_overheads"),
          ("bass", "bass"), ("vocal", "vox_lead"))
SOLO_SONG = "03 Karma"
SOLO = (("piano", "keys_piano"), ("acoustic-guitar", "guitar_acoustic_main"))
SPEECH_SPEAKER = "p228"
GUITAR_TAKES = 12

LENGTH = 5.0
GRID = 0.1
OUTRO = 10.0
PLAY_RANGE = 30.0
FLOOR_RANGE = 40.0
BLOCK = 0.4
FADE = 0.005
COLUMNS = ("file", "category", "source", "archive", "member", "sha256", "start", "length", "channels")
TSV = "excerpts.tsv"


def cache_dir():
    return os.environ.get("BASALT_CORPUS_DIR", os.path.expanduser("~/.cache/myzel/basalt-corpus"))


def corpus_dir():
    return os.path.normpath(os.path.join(HERE, "..", "..", "tests", "corpus"))


def member_path(cache, archive, member):
    return os.path.join(cache, sources.ARCHIVES[archive].folder, member)


def candidate_globs():
    return [
        ("albumdb-masters", ["masters_stereo/*.wav"]),
        ("albumdb-raw", [f"stems_raw/{RHYTHM_SONG}/{stem}.wav" for _, stem in RHYTHM]
                        + [f"stems_raw/{SOLO_SONG}/{stem}.wav" for _, stem in SOLO]),
        ("guitar-techs-p3", [f"P3_music/audio/{kind}/{kind}_{take:02d}.wav"
                             for take in range(1, GUITAR_TAKES + 1)
                             for kind in ("directinput", "micamp")]),
        ("vctk", [f"wav48_silence_trimmed/{SPEECH_SPEAKER}/{SPEECH_SPEAKER}_*_mic2.flac"]),
    ]


def read_table(folder):
    with open(os.path.join(folder, TSV), encoding="utf-8") as f:
        header = f.readline().rstrip("\n").split("\t")
        if tuple(header) != COLUMNS:
            raise ValueError(f"{TSV}: unexpected columns {header}")
        return [dict(zip(COLUMNS, line.rstrip("\n").split("\t"))) for line in f if line.strip()]


def excerpt_members():
    by_archive = {}
    for row in read_table(corpus_dir()):
        by_archive.setdefault(row["archive"], []).append(row["member"])
    return [(archive, sorted(set(members))) for archive, members in by_archive.items()]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while block := f.read(1 << 20):
            h.update(block)
    return h.hexdigest()


class Source:
    def __init__(self, cache, archive, member):
        self.archive, self.member = archive, member
        self.path = member_path(cache, archive, member)
        self.rate, self.bits, self.x = audio.decode(self.path)
        self.channels = kept_channels(self.x)
        self.frames = audio.frame_levels(self.x[:, self.channels], self.rate)
        hop = int(round(GRID / audio.FRAME))
        span = int(round(BLOCK / audio.FRAME))
        power = np.convolve(10 ** (self.frames / 10), np.ones(span) / span, mode="valid")[::hop]
        self.blocks = 10 * np.log10(np.maximum(power, 1e-14))


def kept_channels(x):
    """Drops digitally silent channels and keeps identical ones once."""
    kept = []
    for c in range(x.shape[1]):
        if not np.any(x[:, c]):
            continue
        if any(np.array_equal(x[:, c], x[:, k]) for k in kept):
            continue
        kept.append(c)
    if not kept:
        raise ValueError("every channel is silent")
    return kept


def window_scores(src, outro):
    """Contrast per GRID start, NaN where the source does not play or the window runs too late."""
    hop = int(round(GRID / audio.FRAME))
    second = int(round(1.0 / audio.FRAME))
    per_window = int(round(LENGTH / audio.FRAME))
    blocks_per_window = int(round((LENGTH - BLOCK) / GRID)) + 1
    loud = np.percentile(src.frames, 99) - PLAY_RANGE
    floor = np.percentile(src.blocks, 99) - FLOOR_RANGE
    last = len(src.frames) - int(round(outro / audio.FRAME)) - per_window
    scores = np.full(max(0, last // hop + 1), np.nan)
    for i in range(len(scores)):
        first = i * hop
        seconds = src.frames[first:first + per_window].reshape(-1, second)
        if np.all(seconds.max(axis=1) >= loud):
            w = np.maximum(src.blocks[i:i + blocks_per_window], floor)
            scores[i] = w.max() - w.min()
    return scores


def best_window(group, outro=OUTRO):
    """(start in s, mean contrast) for sources on one timeline, or None when no window is valid."""
    per_source = [window_scores(s, outro) for s in group]
    n = min(len(s) for s in per_source)
    if n <= 0:
        return None
    mean = np.mean([s[:n] for s in per_source], axis=0)
    if np.all(np.isnan(mean)):
        return None
    i = int(np.nanargmax(mean))
    return round(i * GRID, 3), float(mean[i])


def typical_window(src, outro=OUTRO):
    """(start in s, worst deviation in dB) of the playing window whose 400 ms blocks stay closest
    to the song's median block level, or None when no window is valid."""
    blocks_per_window = int(round((LENGTH - BLOCK) / GRID)) + 1
    plays = window_scores(src, outro)
    body = src.blocks[:len(plays) + blocks_per_window - 1]
    median = np.median(body)
    best = None
    for i in np.nonzero(~np.isnan(plays))[0]:
        deviation = float(np.max(np.abs(src.blocks[i:i + blocks_per_window] - median)))
        if best is None or deviation < best[1]:
            best = (round(i * GRID, 3), deviation)
    return best


def crest_db(src):
    x = audio.to_float(src.x[:, src.channels])
    return 20 * math.log10(np.max(np.abs(x))) - 10 * math.log10(np.mean(x ** 2))


def row(name, category, src, start):
    channels = ",".join(str(c + 1) for c in src.channels)
    return dict(file=name, category=category, source=sources.ARCHIVES[src.archive].source,
                archive=src.archive, member=src.member, sha256=sha256(src.path),
                start=f"{start:.3f}", length=f"{LENGTH:.3f}", channels=channels)


def song_slug(title):
    """'03 Karma' -> karma, "It Ain't Easy (To Mend A Heart)" -> it-aint-easy."""
    title = title.split(" (")[0].lstrip("0123456789 ")
    return "-".join("".join(c for c in title.lower() if c.isalnum() or c == " ").split())


def choose(cache):
    rows = []

    stems = [Source(cache, "albumdb-raw", f"stems_raw/{RHYTHM_SONG}/{stem}.wav") for _, stem in RHYTHM]
    found = best_window(stems)
    if found is None:
        raise SystemExit("no window where the whole rhythm section plays")
    start, score = found
    print(f"  rhythm section ({RHYTHM_SONG}): {start:.1f} s, score {score:.1f} dB", file=sys.stderr)
    for (category, _), src in zip(RHYTHM, stems):
        rows.append(row(f"albumdb-{song_slug(RHYTHM_SONG)}-{category}", category, src, start))
    del stems

    for category, stem in SOLO:
        src = Source(cache, "albumdb-raw", f"stems_raw/{SOLO_SONG}/{stem}.wav")
        found = best_window([src])
        if found is None:
            raise SystemExit(f"no valid window in {src.member}")
        start, score = found
        print(f"  {category} ({SOLO_SONG}): {start:.1f} s, score {score:.1f} dB", file=sys.stderr)
        rows.append(row(f"albumdb-{song_slug(SOLO_SONG)}-{category}", category, src, start))

    masters = [Source(cache, "albumdb-masters", m) for m in sorted(
        os.path.join("masters_stereo", f) for f in os.listdir(
            os.path.join(cache, sources.ARCHIVES["albumdb-masters"].folder, "masters_stereo"))
        if f.endswith(".wav"))]
    if len(masters) != 10:
        raise SystemExit(f"expected AlbumDB's ten masters, found {len(masters)}")
    crests = [crest_db(m) for m in masters]
    for label, src in (("open", masters[int(np.argmax(crests))]), ("dense", masters[int(np.argmin(crests))])):
        found = typical_window(src)
        if found is None:
            raise SystemExit(f"no valid window in {src.member}")
        start, deviation = found
        slug = song_slug(os.path.basename(src.member).split(" - ", 1)[1].rsplit(".", 1)[0])
        print(f"  mix-{label}: {src.member}, crest {crest_db(src):.1f} dB, {start:.1f} s, "
              f"within {deviation:.1f} dB of the median", file=sys.stderr)
        rows.append(row(f"albumdb-{slug}-mix", f"mix-{label}", src, start))
    del masters

    best = None
    for take in range(1, GUITAR_TAKES + 1):
        pair = [Source(cache, "guitar-techs-p3", f"P3_music/audio/{kind}/{kind}_{take:02d}.wav")
                for kind in ("directinput", "micamp")]
        if pair[0].rate != pair[1].rate or abs(len(pair[0].x) - len(pair[1].x)) > pair[0].rate // 100:
            continue
        found = best_window(pair)
        if found is not None and (best is None or found[1] > best[1]):
            best = (take, found[1], found[0], pair)
    if best is None:
        raise SystemExit("no valid Guitar-TECHS window")
    take, score, start, pair = best
    print(f"  electric guitar: take {take:02d}, {start:.1f} s, score {score:.1f} dB", file=sys.stderr)
    rows.append(row(f"guitar-techs-p3-{take:02d}-di", "electric-guitar-di", pair[0], start))
    rows.append(row(f"guitar-techs-p3-{take:02d}-amp", "electric-guitar-amp", pair[1], start))

    folder = os.path.join(cache, sources.ARCHIVES["vctk"].folder, "wav48_silence_trimmed", SPEECH_SPEAKER)
    best = None
    for name in sorted(os.listdir(folder)):
        if not name.endswith("_mic2.flac"):
            continue
        src = Source(cache, "vctk", f"wav48_silence_trimmed/{SPEECH_SPEAKER}/{name}")
        found = best_window([src], outro=0.0)
        if found is not None and (best is None or found[1] > best[1]):
            best = (src, found[1], found[0])
    if best is None:
        raise SystemExit(f"no {SPEECH_SPEAKER} recording holds a valid {LENGTH} s window")
    src, score, start = best
    print(f"  speech: {src.member}, {start:.1f} s, score {score:.1f} dB", file=sys.stderr)
    utt = os.path.basename(src.member).split("_mic2")[0].replace("_", "-")
    rows.append(row(f"vctk-{utt}", "speech", src, start))
    return rows


def write_table(folder, rows):
    path = os.path.join(folder, TSV)
    with open(path + ".part", "w", encoding="utf-8") as f:
        f.write("\t".join(COLUMNS) + "\n")
        for r in rows:
            f.write("\t".join(r[c] for c in COLUMNS) + "\n")
    os.replace(path + ".part", path)


def verify(cache, rows):
    bad = 0
    for r in rows:
        path = member_path(cache, r["archive"], r["member"])
        if not os.path.exists(path):
            print(f"  missing: {r['member']} (fetch.sh excerpts)", file=sys.stderr)
            bad += 1
        elif sha256(path) != r["sha256"]:
            print(f"  SHA-256 differs from {TSV}: {r['member']}", file=sys.stderr)
            bad += 1
    if bad:
        raise SystemExit(f"{bad} source(s) missing or changed")
    print(f"  {len(rows)} excerpt sources match {TSV}")


def fade(n):
    return 0.5 - 0.5 * np.cos(np.pi * (np.arange(n) + 0.5) / n)


def build(cache, folder):
    rows = read_table(folder)
    verify(cache, rows)
    for r in rows:
        rate, bits, x = audio.decode(member_path(cache, r["archive"], r["member"]))
        channels = [int(c) - 1 for c in r["channels"].split(",")]
        first = int(round(float(r["start"]) * rate))
        count = int(round(float(r["length"]) * rate))
        if first + count > len(x):
            raise SystemExit(f"{r['file']}: the window runs past the end of {r['member']}")
        cut = x[first:first + count, channels].astype(np.float64)
        n = int(round(FADE * rate))
        ramp = fade(n)[:, None]
        cut[:n] *= ramp
        cut[-n:] *= ramp[::-1]
        step = 2.0 ** (32 - bits)
        out = (np.rint(cut / step) * step).astype(np.int64)
        audio.write_flac(os.path.join(folder, r["file"] + ".flac"), out.astype(np.int32), rate, bits)
        print(f"  {r['file']}.flac: {r['length']} s from {r['start']} s, {len(channels)} ch, "
              f"{rate} Hz, {bits} bit")


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=("choose", "build", "verify"))
    p.add_argument("--cache", default=cache_dir())
    p.add_argument("--out", default=corpus_dir())
    a = p.parse_args(argv)
    if a.mode == "choose":
        write_table(a.out, choose(a.cache))
    elif a.mode == "verify":
        verify(a.cache, read_table(a.out))
    else:
        build(a.cache, a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
