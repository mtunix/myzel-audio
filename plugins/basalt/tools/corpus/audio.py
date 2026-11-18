"""Decoding, writing and level measurement shared by make_excerpts.py and manifest.py.

Audio is decoded with ffmpeg to 32-bit integers left-justified, so 16- and 24-bit sources keep
every bit; `bits` says how many are significant.
"""

import json
import os
import subprocess
import wave

import numpy as np

FRAME = 0.010      # level frames for the selection rule
RANGE_FRAME = 0.050


def probe(path):
    """(rate, channels, bits, seconds) from the file's header."""
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
                          "stream=sample_rate,channels,bits_per_raw_sample,bits_per_sample,duration",
                          "-of", "json", path], check=True, capture_output=True, text=True).stdout
    s = json.loads(out)["streams"][0]
    bits = int(s.get("bits_per_raw_sample") or 0) or int(s.get("bits_per_sample") or 0)
    return int(s["sample_rate"]), int(s["channels"]), bits, float(s["duration"])


def decode(path):
    """(rate, bits, samples) with samples an int32 array of shape (frames, channels)."""
    rate, channels, bits, _ = probe(path)
    raw = subprocess.run(["ffmpeg", "-v", "error", "-nostdin", "-i", path, "-map", "0:a:0",
                          "-f", "s32le", "-c:a", "pcm_s32le", "-"],
                         check=True, capture_output=True).stdout
    x = np.frombuffer(raw, dtype="<i4").reshape(-1, channels)
    if bits not in (16, 24, 32) or np.any(x & ((1 << (32 - bits)) - 1)):
        raise ValueError(f"{path}: expected {bits}-bit samples")
    return rate, bits, x


def to_float(x):
    return x.astype(np.float64) / 2.0 ** 31


def frame_levels(x, rate, frame=FRAME):
    """Mean-square level per frame over all channels, in dBFS (a full-scale square wave is 0)."""
    n = int(round(frame * rate))
    count = len(x) // n
    power = (to_float(x[:count * n]) ** 2).reshape(count, n, -1).mean(axis=(1, 2))
    return 10.0 * np.log10(np.maximum(power, 1e-14))


def write_flac(path, x, rate, bits):
    """Writes int32 left-justified samples as FLAC at `bits`, with no encoder or date tags."""
    width = bits // 8
    pcm = np.ascontiguousarray(x >> (32 - bits), dtype="<i4")
    data = pcm.view(np.uint8).reshape(len(pcm), pcm.shape[1], 4)[:, :, :width].tobytes()
    wav_path = path + ".wav.part"
    with wave.open(wav_path, "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(width)
        w.setframerate(rate)
        w.writeframes(data)
    try:
        subprocess.run(["ffmpeg", "-v", "error", "-nostdin", "-y", "-i", wav_path,
                        "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                        "-c:a", "flac", "-compression_level", "8", "-f", "flac", path + ".part"],
                       check=True)
        os.replace(path + ".part", path)
    finally:
        if os.path.exists(wav_path):
            os.remove(wav_path)
