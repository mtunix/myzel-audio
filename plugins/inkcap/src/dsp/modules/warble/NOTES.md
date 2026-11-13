# Warble

Module 2: pitch wander from a short modulated delay. **Status: voiced (M3)** by measurement
(*Voicing*); nothing has been judged by ear yet.

| File | Contents |
|---|---|
| `WarbleShape.h` | each type's rate, depth and synced window; the note values |
| `WarbleModule` | the module |
| `WarbleControls` | Rate's read-out per type and Sync |

## Types

| Type | Rate at 50 % (free) | Synced window (slow … nominal … fast) | Depth at full intensity | Random share |
|---|---|---|---|---|
| **Wow** | 0.55 Hz (a warped record at 33⅓ rpm) | 8/1 … 1/1 … 1/4D | ±2.0 % speed (±34 cents: seasick) | Chaos |
| **Flutter** | 7 Hz (a capstan past saving at full) | 1/16D … 1/16 … 1/32 | ±0.6 % (±10 cents) | Chaos |
| **Drift** | 0.12 Hz (a motor that cannot hold its speed) | 16/1 … 4/1 … 1/1 | ±2.0 %, capped by the 15 ms swing (±1.1 % at 0.12 Hz) | √Chaos (random sooner) |

Free Rate spans two octaves either side of the type's rate (Wow 0.14–2.2 Hz, Flutter 1.75–28 Hz,
Drift 0.03–0.48 Hz); synced, the position picks a note value in the type's window (18 values
from 16/1 to 1/32, dotted and triplet, `noteValues`), so a type change re-reads Rate, never
moves it.

## How it works

The read head sits behind the input in a short capture buffer (`CaptureBuffer`, 30 ms past its
rest):

```
delay (t) = rest + centre + swing · m (t),   m in [-1, 1], rest = 6 samples
m (t)     = (1 - r) · sin (2 pi phase) + r · noise (t)
```

- **phase**: free, it advances by Rate / fs each sample (a Rate change bends, never jumps); at a
  jump it is set from the song position (rate × seconds), so a render from any start has the
  same phase. Synced, it is the song position in note values (PPQ / note), locked to the bar
  through tempo changes; a new note value glides into place at most 25 % faster than the LFO.
- **noise**: value noise (`SeedRng::smooth`, stream `warbleDrift`) on a lattice 4 × the LFO's
  rate, advanced with the phase: the same from any start, C2 (never a corner in the delay).
- **r**, the random share: Chaos (Drift: √Chaos). Chaos 0 is a pure LFO (the gate).
- **swing**: the delay that gives the type's depth at the LFO's rate (depth / (2π Rate)),
  × the depth law `i^1.3` (*Voicing*), capped at 15 ms. **centre** follows it at no more than
  0.5 % of the speed, and so does the swing, which never exceeds the centre: the delay never
  goes below the rest, and a depth change bends the pitch by at most 0.5 %. (A swing that
  followed its glide alone collapsed a deep Wow in 30 ms: a lurch the click gate read at −48 dB.)
- **A type change waits for the head to come home**: the old type's swing and centre glide back
  to the rest at the speed limit (a deep Wow: up to 1.2 s), then the new type glides out. A slow,
  deep wobble cannot become a fast one in place: 143 samples of swing at Flutter's 7 Hz is a 13 %
  speed lurch.
- **sin** is a polynomial (`warble::lfoSine`, within 1e-9 of the library's): half the cost, and
  the same on every platform's libm.
- **Spread** (right channel): its phase a quarter cycle ahead at 100 %, and its own wander
  (stream `warbleSpread`) mixed in by Spread. At Spread 0 the right channel is computed exactly
  as the left: a mono source stays mono, to the bit. Spread is the only widening besides Room.
- Chaos, Type and Spread glide (two one-poles in series, 30 ms each: a glide whose slope never
  steps, since a step in the delay's slope is a step in pitch); so does the swing.
- A new seed's wander fades in over 250 ms.

**The rest (M3).** The head rests 6 samples behind the input, the first delay the 12-tap
windowed sinc (`common/dsp/Interpolators.h`) reads without looking ahead, and swings from there,
so every read is the flat sinc. M1's head rested at delay 0 and read linear and Hermite
interpolation below 6 samples; every LFO cycle took it down there, where an interpolator at half
a sample has a zero at Nyquist: the top octave dipped once per cycle (12 kHz by 3 dB, 18 kHz by
8 dB) at every type and depth, and K-weighted pink noise lost 0.64 dB under a light Flutter. From
the rest it holds within ±0.05 dB at 12 kHz and ±0.8 dB at 18 kHz (the sinc's own ripple there;
`InkcapTests_warble`, "the top octave"). The 6 samples are the module's latency, constant, on or
off, and the chain aligns its dry to them (9 samples with Crush's pair; PLAN.md *Settled*).

**Damage 0 is exact.** At intensity 0 the centre glides to 0 and the head reads the input exactly
6 samples back (an integer read, to the bit); there the module stops working but keeps writing
its buffer, so the head can glide out again without reading a gap (M1 restarted the buffer there,
and coming back after Damage 0 read zeros under the interpolator: a −34 dB click). Off passes the
same integer read. When it comes back, the centre starts at the rest and glides out (a slight
flattening, at most 0.5 %), so neither Damage nor the module key needs a crossfade between the
dry input and a delayed copy, except the plan's 20 ms On/Off fade. A render's first block starts
at its full depth at once, as that point does in a render from earlier on.

## Controls

| ID | Stored | Meaning |
|---|---|---|
| `warbleOn` | bool, default on | On/Off crossfades over 20 ms |
| `warbleAmount` | 0–100 %, default 50 | with Damage: depth (`i^1.3` of the type's full) |
| `warbleType` | choice: Wow, Flutter, Drift (index frozen) | |
| `warbleRate` | 0–100 % position, default 50 | free: Hz around the type's rate; synced: a note value from its window |
| `warbleSync` | bool, default off | Rate follows the host tempo (the fallback BPM without one) |
| `warbleSpread` | 0–100 %, default 0 | the right channel's own phase and wander |

## Gates (`InkcapTests_warble`, `InkcapMeasure`)

| Gate | Result (M3) |
|---|---|
| Damage 0 and Off: the input 6 samples back, to the bit (every type, Chaos 0 and 1); back to it after Damage returns to 0 (at the speed limit: 1.2 s for a full Wow) | exact |
| The top octave through every cycle (12 / 18 kHz, every type, light and full) | ±0.05 / ±0.8 dB |
| Chaos 0 is a pure LFO: the frequency of a 1 kHz tone swings as a sine at the rate, with the type's depth | depth within 3 %, residual beside the sine 0.1–0.2 % |
| Chaos 1 is not periodic | residual > 30 % of the swing |
| Sync: the LFO is the note value at the host tempo | exact |
| Spread 0 keeps a mono source mono; Spread 1 decorrelates | exact / > −30 dB difference |
| Clicks on a 330 Hz sine through On/Off, Damage to 0 and back out, type, rate, sync, seed and Chaos changes (gate −60 dB) | passes |
| Golden renders, every type free and synced (`golden/<type>[-sync].f32`) | error energy below −90 dB |
| Any block size: the same to the bit, free and synced | yes |
| From bar 9 vs from bar 1, after 500 ms | below −100 dB (the phase from the position differs from the accumulated one by rounding only) |

## Voicing (M3)

**Target:** the middle of the pad a clearly worn machine, full Damage seasick; equal steps of
Damage should sound like equal steps of wobble, which for pitch deviation means a ratio per step.
Real references: a good deck's wow is under 0.1 % (2 cents), a cheap cassette's 0.3 % (5 cents), a
warped record 1–2 % (17–35 cents). The depth follows `i^1.3` of the type's full deviation
(`warble::depthShare`), about ×2.5 per doubling of intensity, so the Damage curve stays linear.

| Peak deviation (cents) | Amount 100 %: Damage 25 / 50 / 100 % | Amount 50 % (default) |
|---|---|---|
| Wow | 5.7 / 14.0 / 34.3 | 2.3 / 5.7 / 14.0 |
| Flutter | 1.7 / 4.2 / 10.4 | 0.7 / 1.7 / 4.2 |
| Drift | 5.7 / 14.0 / 19.5 (capped by the 15 ms swing) | 2.3 / 5.7 / 14.0 |

(`InkcapCalibrate --voicing`, a 1 kHz tone at Chaos 0.) The default (Amount 50, Damage 50) is a
cheap cassette's wow. Warble changes pitch, not level: no Match table (−0.64 dB at worst on pink
noise, under a light Flutter, before the rest; −0.1 dB now).

## Open

Not judged by ear: rates, the noise lattice's rate, Drift's random share; whether Flutter wants a
second, faster component (scrape) at Chaos above 0. Mulberry's `Transport` was not used: its wow
and flutter run from a running generator and a running phase, which a render from bar 17 cannot
reproduce.
