"""Phone alignment with pocketsphinx (BSD), the aligner for corpora without published alignments.

    python align_pocketsphinx.py align <list.tsv> <out-dir> [--jobs N]
    python align_pocketsphinx.py compare <cache> [--every N] [--jobs N]

`align` reads rows of id, audio, transcript and writes <out-dir>/<id>.lab (labels.py's format).
Audio is decoded to 16 kHz mono with ffmpeg for the CMU en-us model; the labels' times hold for
the original file. Words must be in CMUdict; an utterance with an unknown word is skipped and
reported, never guessed.

`compare` aligns every Nth LibriSpeech utterance (default 10) and measures the result against
the published MFA alignments in the cache (labels/librispeech): how far this aligner can be
trusted where both exist.

Needs the pocketsphinx package (fetch.sh installs it into the cache's venv) and ffmpeg.
"""

import argparse
import multiprocessing
import os
import re
import statistics
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import labels  # noqa: E402

RATE = 16000
FRAME = 0.01    # pocketsphinx's default frame rate is 100 per second

decoder = None
dictionary = None


def load_dictionary():
    """CMUdict as shipped with pocketsphinx: each word's first pronunciation, on our phone set."""
    import pocketsphinx
    pron = {}
    with open(os.path.join(pocketsphinx.get_model_path(), "en-us", "cmudict-en-us.dict"), encoding="utf-8") as f:
        for line in f:
            word, *phones = line.split()
            if "(" not in word:
                pron[word] = [labels.normalise(p) for p in phones]
    return pron


def init_worker():
    global decoder, dictionary
    from pocketsphinx import Decoder
    decoder = Decoder(samprate=RATE, bestpath=False, loglevel="FATAL")
    dictionary = load_dictionary()


def words_of(text):
    """A transcript as CMUdict spells words: lower case, punctuation gone, apostrophes inside words kept."""
    words = re.sub(r"[^a-z' ]+", " ", text.lower().replace("-", " ")).split()
    return [w.strip("'") for w in words if w.strip("'")]


def decode_pcm(audio):
    return subprocess.run(["ffmpeg", "-v", "error", "-i", audio, "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"],
                          check=True, capture_output=True).stdout


def align_one(job):
    utt, audio, text, out = job
    words = words_of(text)
    unknown = [w for w in words if w not in dictionary]
    if not words or unknown:
        return utt, ("unknown words: " + " ".join(unknown)) if unknown else "empty transcript"
    try:
        pcm = decode_pcm(audio)
        # The noise estimate carries over between utterances; a fresh one keeps each result independent of order.
        decoder.reinit_feat()
        decoder.set_align_text(" ".join(words))
        decoder.start_utt()
        decoder.process_raw(pcm, full_utt=True)
        decoder.end_utt()
        decoder.set_alignment()
        decoder.start_utt()
        decoder.process_raw(pcm, full_utt=True)
        decoder.end_utt()
        alignment = decoder.get_alignment()
        segments = []
        for word in alignment:
            for phone in word:
                start = phone.start * FRAME
                segments.append((start, start + phone.duration * FRAME, labels.normalise(phone.name)))
    except Exception as e:  # pocketsphinx raises RuntimeError when no path reaches the end
        return utt, f"no alignment ({e})"
    if not segments:
        return utt, "no alignment"
    duration = len(pcm) / 2 / RATE
    labels.write_lab(out, labels.tidy(segments, duration))
    return utt, None


def align(rows, out_dir, jobs):
    todo = [(u, a, t, os.path.join(out_dir, u + ".lab")) for u, a, t in rows]
    todo = [j for j in todo if not os.path.exists(j[3])]
    print(f"  {len(rows) - len(todo)} already aligned, {len(todo)} to go")
    failures = []
    with multiprocessing.Pool(jobs, initializer=init_worker) as pool:
        for n, (utt, error) in enumerate(pool.imap_unordered(align_one, todo, chunksize=4), 1):
            if error:
                failures.append((utt, error))
            if n % 1000 == 0:
                print(f"  {n}/{len(todo)}", flush=True)
    for utt, error in sorted(failures):
        print(f"  skipped {utt}: {error}")
    print(f"  {len(todo) - len(failures)} aligned, {len(failures)} skipped")
    return failures


def frames(segments, step=FRAME):
    n = int(round(segments[-1][1] / step))
    out = [""] * n
    for start, end, phone in segments:
        for i in range(int(round(start / step)), min(n, int(round(end / step)))):
            out[i] = phone
    return out


def compare(cache, every, jobs):
    root = os.path.join(cache, "librispeech", "LibriSpeech", "test-clean")
    reference = os.path.join(cache, "labels", "librispeech")
    rows = []
    for dirpath, _, files in sorted(os.walk(root)):
        for name in sorted(files):
            if name.endswith(".trans.txt"):
                with open(os.path.join(dirpath, name), encoding="utf-8") as f:
                    for line in f:
                        utt, text = line.rstrip("\n").split(" ", 1)
                        if os.path.exists(os.path.join(reference, utt + ".lab")):
                            rows.append((utt, os.path.join(dirpath, utt + ".flac"), text))
    rows = sorted(rows)[::every]
    out = os.path.join(cache, "compare", "pocketsphinx")
    print(f"Aligning {len(rows)} LibriSpeech test-clean utterances (every {every}th) into {out}")
    align(rows, out, jobs)

    both = mfa_only = ps_only = agree = total = 0
    found = missed = 0
    d_start, d_end = [], []
    for utt, _, _ in rows:
        path = os.path.join(out, utt + ".lab")
        if not os.path.exists(path):
            continue
        ref = labels.read_lab(os.path.join(reference, utt + ".lab"))
        hyp = labels.read_lab(path)
        fr, fh = frames(ref), frames(hyp)
        for a, b in zip(fr, fh):
            total += 1
            agree += a == b
            sa, sb = a in labels.SIBILANTS, b in labels.SIBILANTS
            both += sa and sb
            mfa_only += sa and not sb
            ps_only += sb and not sa
        for start, end, phone in ref:
            if phone not in labels.SIBILANTS:
                continue
            match = [(s, e) for s, e, p in hyp if p == phone and s < end and e > start]
            if match:
                found += 1
                s, e = max(match, key=lambda m: min(m[1], end) - max(m[0], start))
                d_start.append(abs(s - start) * 1000)
                d_end.append(abs(e - end) * 1000)
            else:
                missed += 1

    def pct(x, n):
        return f"{100 * x / n:.1f} %" if n else "n/a"

    def quantiles(v):
        q = statistics.quantiles(v, n=10)
        return f"median {statistics.median(v):.0f} ms, 90th percentile {q[-1]:.0f} ms"

    print("\npocketsphinx against the published MFA alignments (10 ms frames)")
    print(f"  frames with the same phone            {pct(agree, total)} of {total}")
    print(f"  MFA sibilant frames pocketsphinx also calls sibilant   {pct(both, both + mfa_only)}")
    print(f"  pocketsphinx sibilant frames MFA also calls sibilant   {pct(both, both + ps_only)}")
    print(f"  MFA sibilant segments overlapped by the same phone     {pct(found, found + missed)} "
          f"of {found + missed}")
    if d_start:
        print(f"  |start difference| of matched sibilants  {quantiles(d_start)}")
        print(f"  |end difference| of matched sibilants    {quantiles(d_end)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    a = sub.add_parser("align")
    a.add_argument("list")
    a.add_argument("out")
    a.add_argument("--jobs", type=int, default=os.cpu_count())
    c = sub.add_parser("compare")
    c.add_argument("cache")
    c.add_argument("--every", type=int, default=10)
    c.add_argument("--jobs", type=int, default=os.cpu_count())
    args = parser.parse_args()
    if args.command == "align":
        with open(args.list, encoding="utf-8") as f:
            rows = [tuple(line.rstrip("\n").split("\t")) for line in f if line.strip()]
        align(rows, args.out, args.jobs)
    else:
        compare(args.cache, args.every, args.jobs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
