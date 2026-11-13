#!/usr/bin/env python3
"""An independent reference for Inkcap's frozen seed functions (src/dsp/common/SeedRng.h, SeedCode).

Prints the golden values tests/common/SeedRngTests.cpp and SeedCodeTests.cpp check. Written from
the specification in src/dsp/common/NOTES.md, not from the C++: if the two ever disagree, one of
them broke the frozen hash.

Usage: python3 plugins/inkcap/tests/common/validation/seed_reference.py
"""

import math
import unicodedata

M64 = (1 << 64) - 1
GAMMA = 0x9E3779B97F4A7C15
LANE_GAMMA = 0xD1B54A32D192ED03
KEY_SALT = 0x696E6B6361702121


def mix64(z):
    z &= M64
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
    return z ^ (z >> 31)


def stream_key(seed, stream):
    return mix64(((seed << 32) | stream) ^ KEY_SALT)


def lane_key(key, lane):
    return mix64(key + LANE_GAMMA * (lane + 1))


def bits(key, index):
    return mix64(key + GAMMA * ((index + 1) & M64))


def uniform(key, index):
    return (bits(key, index) >> 11) * 2.0 ** -53


def bipolar(key, index):
    return 2.0 * uniform(key, index) - 1.0


def smooth(key, position):
    k = math.floor(position)
    f = position - k
    w = [(1 - f) ** 3 / 6, (3 * f ** 3 - 6 * f ** 2 + 4) / 6, (-3 * f ** 3 + 3 * f ** 2 + 3 * f + 1) / 6, f ** 3 / 6]
    return sum(w[i] * bipolar(key, k - 1 + i) for i in range(4))


WHITE_SPACE = set(range(0x09, 0x0E)) | {0x20, 0x85, 0xA0, 0x1680} | set(range(0x2000, 0x200B)) \
    | {0x2028, 0x2029, 0x202F, 0x205F, 0x3000, 0xFEFF}


def normalise(word):
    assert unicodedata.unidata_version == "15.0.0", "the frozen fold is Unicode 15.0"
    cps = [ord(c) for c in word]
    while cps and cps[0] in WHITE_SPACE:
        cps.pop(0)
    while cps and cps[-1] in WHITE_SPACE:
        cps.pop()
    out = []
    for cp in cps:
        lower = chr(cp).lower()
        out.append("i" if cp == 0x130 else (lower if len(lower) == 1 else chr(cp)))
    return "".join(out)


def fnv1a32(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def cpp(text):
    """A C++ string literal of text's UTF-8 bytes (hex escapes, split where a hex digit follows)."""
    out, escaped = "", False
    for b in text.encode("utf-8"):
        c = chr(b)
        if b < 0x20 or b >= 0x7F:
            out += f"\\x{b:02X}"
            escaped = True
            continue
        if escaped and c in "0123456789abcdefABCDEF":
            out += '" "'
        out += '\\"' if c == '"' else c
        escaped = False
    return '"' + out + '"'


def code(seed):
    return f"{seed >> 16:04X}-{seed & 0xFFFF:04X}"


def main():
    print("// SeedRng: (seed, stream, index) -> bits")
    for seed, stream, index in [(0, 0x0101, 0), (0, 0x0101, 1), (0x3F2A91C7, 0x0101, 0), (0x3F2A91C7, 0x0101, 17),
                                (0x3F2A91C7, 0x0102, 17), (0x3F2A91C7, 0x0501, 48000 * 3600), (0xFFFFFFFF, 0x0601, -1),
                                (0x43930A3D, 0x0103, -96), (1, 0x0401, (1 << 40) + 5)]:
        print(f"{{ 0x{seed:08X}u, 0x{stream:04X}u, {index}ll, 0x{bits(stream_key(seed, stream), index):016X}ull }},")

    key = stream_key(0x3F2A91C7, 0x0103)
    print("// lanes of (3F2A91C7, glitchShape) at index 5")
    for lane in range(3):
        print(f"{{ {lane}u, 0x{bits(lane_key(key, lane), 5):016X}ull }},")

    print("// uniform and bipolar of (3F2A91C7, glitchSlots) at 0 .. 3")
    key = stream_key(0x3F2A91C7, 0x0101)
    for i in range(4):
        print(f"{{ {i}, {uniform(key, i)!r}, {bipolar(key, i)!r} }},")

    print("// smooth of (43930A3D, warbleDrift)")
    key = stream_key(0x43930A3D, 0x0201)
    for pos in [0.0, 0.25, 0.5, 1.0, 7.75, -3.5, 1234567.125]:
        print(f"{{ {pos!r}, {smooth(key, pos)!r} }},")

    print("// words")
    for word in ["inkcap", "thrift", "Thrift", "  thrift\t", "THRIFT SHOP", "Äpfel", "äpfel", "ÇA VA", "İstanbul",
                 "Straße", "ΣΟΦΙΑ", "Ёлка", "Tiếng Việt", "ＡＢＣ", " spaced　", "日本語", "😀 emoji",
                 "deadbeefs", "3F2A-91C"]:
        n = normalise(word)
        print(f"{{ {cpp(word)}, {cpp(n)}, 0x{fnv1a32(n.encode('utf-8')):08X}u }},  // {code(fnv1a32(n.encode('utf-8')))}")


if __name__ == "__main__":
    main()
