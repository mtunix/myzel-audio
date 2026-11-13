# Seeds, the clock and the capture: src/dsp/common

Everything random in Inkcap comes from one 32-bit seed and the song position (PLAN.md, *Seeds
and determinism*). This folder holds what every module shares: `SeedRng` and `SeedCode` (the
seed), `ValueNoise` (`SeedRng::smooth` with its lattice cached, for per-sample wander),
`GridClock` (the song position), `CaptureBuffer` (read heads through the recent input) and
`Crossfade` (the fade shape). No JUCE; `InkcapTests_common` tests them.

**The seed's two pieces are frozen.** A changed number changes the glitches of every saved song, and a word must
give the same seed in every version. The golden values in `tests/common/` come from an
independent Python implementation of this page (`tests/common/validation/seed_reference.py`),
not from the C++. If they disagree, the C++ is wrong.

## SeedRng: counter-based random numbers

A value is a pure function of `(seed, stream, index)`, never the next output of a running
generator. Nothing depends on how many numbers were drawn before, so a render started at bar 17
draws exactly what bar 17 drew in a render from bar 1, and any block size gives the same numbers.

```
mix64 (z)    = SplitMix64's finaliser: z ^= z >> 30; z *= 0xBF58476D1CE4E5B9;
                                       z ^= z >> 27; z *= 0x94D049BB133111EB; z ^= z >> 31
key          = mix64 ((seed << 32 | stream) ^ 0x696E6B6361702121)          ("inkcap!!")
bits (index) = mix64 (key + 0x9E3779B97F4A7C15 * (index + 1))              (mod 2^64)
lane (l)     = a new key: mix64 (key + 0xD1B54A32D192ED03 * (l + 1))
```

- `index` is a signed 64-bit integer, taken as its two's-complement bits: a pre-roll before
  bar 1 (negative slots) is as valid as any other index.
- `uniform = (bits >> 11) * 2^-53` in [0, 1); `bipolar = 2 uniform - 1` in [-1, 1). Both are
  exact (dyadic), so they match the reference to the bit.
- `below (n)` = `((bits >> 32) * n) >> 32`: 0 .. n - 1, bias below n / 2^32.
- `chance (p)` = `uniform < p`.
- `lane` gives several independent values at one index (an event's type, length and count).
- Distinct `(seed, stream)` pairs give distinct keys (the packing is injective and `mix64` is a
  bijection). Two keys can only produce the same sequence shifted by an offset of order 2^64.

**Value noise** (`smooth`): lattice values `bipolar (k)` at the integers, joined by a uniform
cubic B-spline over four of them. It is C2 (no corners, no steps in slope, so modulating a delay
with it never clicks), stays inside [-1, 1] (the weights are a partition of unity) and has an RMS
of about 0.4 (uniform's 0.577 times the spline's 0.69). Evaluate it at the timeline position
times a rate, computed from the sample index, never accumulated: then it is the same from any
start and for any block size. `held` is the lattice value under a position (sample and hold).
`ValueNoise` gives the same numbers to the bit with the four lattice values cached until the
position leaves their cell: per sample it costs the spline, not four hashes.

### Streams

`Stream` (in `SeedRng.h`) names every user of random numbers. The values are frozen once a
module's golden renders exist; add a purpose with the next number in its module's block
(0x01nn Glitch, 0x02nn Warble, 0x03nn Crush, 0x04nn Bits, 0x05nn Hiss, 0x06nn Room) and never
renumber or reuse one. The scaffold names the purposes the plan implies; a module may leave one
unused.

## SeedCode: seeds as text

- **Code:** 8 upper-case hex digits in two groups of four, `3F2A-91C7`. Copy puts exactly this
  text on the clipboard.
- **Paste** reads three forms as that seed: `3F2A-91C7`, `3f2a91c7` and `0x3F2A91C7` (hex in
  either case, `0x` or `0X`, the dash only between the 4th and 5th digit, `0x3F2A-91C7` too).
  Anything else is a word.
- **Words** hash with FNV-1a 32 (offset 2166136261, prime 16777619) over the normalised UTF-8:
  1. Decode UTF-8. A byte that does not start a valid, shortest-form sequence (stray
     continuation bytes, overlong forms, surrogates, values above U+10FFFF, cut-off sequences)
     becomes U+FFFD, one per byte.
  2. Trim, at both ends, Unicode's White_Space code points (U+0009–U+000D, U+0020, U+0085,
     U+00A0, U+1680, U+2000–U+200A, U+2028, U+2029, U+202F, U+205F, U+3000) and U+FEFF.
  3. Fold every code point with **Unicode 15.0's simple lowercase mapping** (UnicodeData.txt
     field 13; U+0130 İ becomes U+0069 i). Nothing else: no normalisation (NFC and NFD spellings
     of a word are different words), no collapsing of inner spaces.
  4. Encode as UTF-8 and hash.
- An empty or white-space-only text is nothing: the seed stays and the display says so.
- A word keeps its trimmed text, as typed, as the seed's label (`seed "Thrift Shop" F99E-6E2A`),
  cut after 64 code points. The hash covers the whole text.
- A fresh instance's seed is the word `inkcap`, `4393-0A3D`.

**Why our own lower case:** the platforms' `towlower` (and JUCE's `toLowerCase`, which calls it)
depends on the C library and on the locale the host happens to run in. The
fold is a generated table of 1433 mappings in 182 runs (`generated/LowerCaseTable.h`, from
`tools/unicode/lowercase.py`, which refuses to run on any Unicode version but 15.0). The test
suite checks the table's size, idempotence and spot values; it was checked once against
Python's `str.lower` for every code point (identical). Never regenerate it with a newer Unicode.

**Examples** (all in `tests/common/SeedCodeTests.cpp`):

| Text | Hashed as | Seed |
|---|---|---|
| `inkcap` | `inkcap` | `4393-0A3D` |
| `thrift`, `Thrift`, ` thrift⇥` | `thrift` | `8719-8E02` |
| `THRIFT SHOP` | `thrift shop` | `F99E-6E2A` |
| `Äpfel`, `äpfel` | `äpfel` | `8393-A579` |
| `İstanbul` | `istanbul` | `3F2B-ED47` |
| `ΣΟΦΙΑ` | `σοφια` | `841F-44F1` |
| `ＡＢＣ` (full width) | `ａｂｃ` | `E940-B6DB` |
| `3F2A-91C` (7 digits: a word) | `3f2a-91c` | `7AA2-0433` |

## GridClock: the song position of every sample

`GridClock::advance (HostPosition, fallbackBpm, numSamples)` once per block, then `ppqAt`,
`secondsAt` and `sampleAt` for any sample (its index counts from `reset()`).

- **One anchor per timeline segment.** A position is `anchorPpq + (sample - anchorSample) ×
  tempo / (60 fs)`. While the host's position continues the last block's (within one sample),
  the anchor stays, so a render is the same to the bit for any block size and the host's own
  rounding never enters. A tempo change re-anchors on the host's position where it happens (not
  a jump: slots keep their indices).
- **Jumps:** the first block after `reset()`, play, stop, a locate, a loop. `jumpedAt (sample)`
  is true for the block's first sample; modules drop what was under way and take their phase
  from the position (Warble) or skip what would read from before (Glitch).
- **No transport** (stopped, the Standalone, a host without a position): the position runs free
  from 0 at the host's tempo, or the fallback BPM, from the block where it started.
- **`Grid`:** slots of `slotBeats` from beat 0 in pairs, the second starting at `swing` of the
  pair (50 % straight … 75 %), negative before bar 1. A position within 10⁻⁹ beats of a
  boundary counts as on it (far below a sample, far above double rounding), so a boundary lands
  on the same sample whether the timeline was anchored at bar 1 or at bar 17.
- Tempo clamps to 20–999 BPM; a position that is not finite counts as no position.
- `HostPosition` also carries the host's loop points (`looping`, `loopStartPpq`, `loopEndPpq`),
  which the chain uses to split a block that wraps inside (`chain/NOTES.md`), and its last bar
  start (`hasBarStart`, `barStartPpq`), which only the display reads; the clock itself uses
  neither.

## CaptureBuffer: read heads through the recent input

One `MirroredRing` per channel (`common/dsp/Interpolators.h`, moved from Mulberry in M1). Write a
frame, then read it: delay 0 is that frame, to the bit. A fractional read never needs a sample
newer than the frame just written: linear below 1 sample, a 4-point Hermite from 1 to 5, the
12-tap windowed sinc from 6, faded in over 5 to 6. A head that starts at the input and falls
behind (Stall, Bend, Chew's slur, Warble coming on) is click-free from its first sample
(`InkcapTests_common`: a 330 Hz sine through a head sliding from 0 to 12 samples stays at the
sine's own crest). Reads before the last `restart()` give 0. Glitch keeps 4 s, Warble 30 ms past its rest (its
head rests at 6 samples, where the sinc reads: below that the interpolators' half-sample zero
at Nyquist dulls the top octave, see `modules/warble/NOTES.md`). `prepare()` builds the sinc's
table (a function-local static, `WindowedSinc::instance()`): the audio thread's first read would
otherwise allocate and compute it, tens of milliseconds (M3; `InkcapTests_chain`, "[realtime]").

## Crossfade: the fade shape

`fadeShape (u) = u - sin (2πu) / (2π)`: 0 → 1 with zero slope and curvature at both ends (C2).
A linear fade's corners show at −35 dB in the 3rd-difference click gate on a full-scale sine;
this one keeps a 2 ms jump below −60 dB. `equalGain` for a signal against a coloured copy of
itself (On/Off, Mix, Bypass), `equalPower` for unrelated sources (Glitch's jumps in time); both
exact at the ends. `Fade` moves a value over a fixed time along the shape and lands exactly.

