"""The committed tier's manifest: what each excerpt is and the levels a compressor test leans on.

    python3 -I manifest.py plugins/basalt/tests/corpus           writes manifest.tsv
    python3 -I manifest.py plugins/basalt/tests/corpus --check   recomputes and compares

One row per excerpt in excerpts.tsv: file, category, seconds, rate, channels, bits, then

- peak_dbfs: the largest sample, any channel;
- rms_dbfs: over every sample of every channel (a full-scale square wave is 0 dBFS, a full-scale
  sine -3.01);
- crest_db: peak_dbfs - rms_dbfs;
- range_db: the 95th minus the 10th percentile of 50 ms mean-square levels, how far the quiet
  moments sit below the loud ones, over the frames that are not digital silence;
- silence_pct: the share of 50 ms frames that are digital silence (raw stems are edited to
  silence between phrases);
- sha256 of the FLAC file.
"""

import hashlib
import os
import sys

import numpy as np

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import audio  # noqa: E402
import make_excerpts  # noqa: E402

COLUMNS = ("file", "category", "seconds", "rate", "channels", "bits", "peak_dbfs", "rms_dbfs",
           "crest_db", "range_db", "silence_pct", "sha256")
TSV = "manifest.tsv"


def measure(folder, entry):
    path = os.path.join(folder, entry["file"] + ".flac")
    rate, bits, x = audio.decode(path)
    f = audio.to_float(x)
    peak = 20 * np.log10(np.max(np.abs(f)))
    rms = 10 * np.log10(np.mean(f ** 2))
    n = int(round(audio.RANGE_FRAME * rate))
    frames = x[:len(x) // n * n].reshape(-1, n * x.shape[1])
    silent = ~np.any(frames, axis=1)
    levels = audio.frame_levels(x, rate, audio.RANGE_FRAME)[~silent]
    p95, p10 = np.percentile(levels, [95, 10])
    with open(path, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()
    return dict(file=entry["file"], category=entry["category"], seconds=f"{len(x) / rate:.3f}",
                rate=str(rate), channels=str(x.shape[1]), bits=str(bits), peak_dbfs=f"{peak:.2f}",
                rms_dbfs=f"{rms:.2f}", crest_db=f"{peak - rms:.2f}", range_db=f"{p95 - p10:.2f}",
                silence_pct=f"{100 * np.mean(silent):.1f}", sha256=digest)


def render(rows):
    return "\t".join(COLUMNS) + "\n" + "".join("\t".join(r[c] for c in COLUMNS) + "\n" for r in rows)


def main(argv):
    if not argv or argv[0].startswith("-") or len(argv) > 2 or argv[1:] not in ([], ["--check"]):
        print(__doc__, file=sys.stderr)
        return 2
    folder = argv[0]
    text = render([measure(folder, e) for e in make_excerpts.read_table(folder)])
    path = os.path.join(folder, TSV)
    if argv[1:] == ["--check"]:
        with open(path, encoding="utf-8") as f:
            if f.read() != text:
                print(f"{path} does not match the excerpts; rebuild it", file=sys.stderr)
                return 1
        print(f"{path}: matches")
        return 0
    with open(path + ".part", "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(path + ".part", path)
    print(f"{path}: {text.count(chr(10)) - 1} excerpts")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
