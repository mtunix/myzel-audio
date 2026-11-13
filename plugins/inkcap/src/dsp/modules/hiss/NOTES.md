# Hiss

Module 5: the noise floor of old media, synthesised from the seed and the song position (no
recordings in v1, PLAN.md *Settled* 10), added to the signal after Bits (so the floor itself is
not bit-crushed) and before Room (so it sits in the same space). **Status: voiced (M3)** by
measurement (*Voicing*); nothing here has been listened to yet (renders for that pass: see *Open*).

| File | Contents |
|---|---|
| `HissModule` | the module: the three beds, Tone, Follow, the fades, the timeline; the voicing functions (`hiss::bedLevelDb`, `crackleRate`, `buzzShare`, Tone's corners) |
| `Crackle` | dust and scratches on a turning record: a seeded impulse process on the timeline |
| `HissSources.h` | `gaussian` (one draw → unit-variance noise), `RiaaPlayback` (the phono stage), `HumSource` (hum and buzz cycles) |

## Types

| Type | Built from | At Tone 0 (neutral) |
|---|---|---|
| **Vinyl** | surface noise (white, lateral L+R and vertical L−R at 0.6, so L/R correlate at 0.47) and crackle through the **RIAA playback curve** (3180/318/75 µs, 0 dB at 1 kHz), plus rumble (value noise on a 70 Hz lattice: below ~30 Hz, mostly vertical, as a platter's). The surface swells ±10 % once a turn (an off-centre record) | the bed lifted +15 dB at 63 Hz and −11 dB at 8 kHz re 1 kHz (the RIAA curve, measured); crackle on top, brighter than the bed, as real clicks are |
| **Tape** | white per channel (two tracks: unrelated) through a playback head: 300 Hz low cut, +3.4 dB presence at 5.5 kHz, 2-pole top at 15 kHz | rising ~+6 dB/oct in third-octaves from 1 to 8 kHz: the classic "sss" |
| **Hum** | 50 Hz mains (`HumSource`): the transformer's **hum** (odd harmonics strongest, sine phase) and a full-wave rectifier's **buzz** (64 harmonics in cosine phase: a spike every half cycle, the 100 Hz family leading), over an amplifier's hiss (white, 9 kHz low-pass, −28 dB) | 30 % buzz |

Each bed is normalised to RMS 1 at prepare (white noise through its own filters, measured from
the impulse response), so the level mapping holds at every rate and type. Hum and buzz are
orthogonal (sine against cosine phase), so their mix is renormalised exactly:
`(1 − s) hum + s buzz` over `sqrt ((1 − s)² + s²)`.

### Crackle (`Crackle`)

- **A turning record.** The timeline is cut into 1 ms cells (`round (fs / 1000)` samples),
  1800 to a turn (33⅓ rpm, 1.8 s; 1.804 s at 44.1 kHz). Everything about a cell is a pure
  function of the seed and the cell index.
- **Dust** sits at one cell of one turn and is hit again on the next turns at the same angle and
  the same sample offset, each time 20 % weaker (the stylus moving inwards across it). Chaos 0:
  a particle is heard on 1–4 turns (steady wear repeats with the record: of the clicks in a
  turn, 56 % came round from the turn before); Chaos 1: one turn only (0.6 %, chance
  coincidences) and the density wanders by a factor of about 0.4–2.5 in bursts (value noise on
  two lanes of `hissCrackle`). The rate is per second of dust *events*, so Chaos changes the
  pattern, not how much crackle there is.
- **Scratches** (one from intensity 0.45, two from 0.8) tick once per turn at a seeded angle;
  Chaos varies their level per turn (±40 %) and lets them miss (up to 30 %).
- **Click shapes**, as the stylus's velocity (the cartridge reads velocity, so a bump has zero
  area): a **tick** (a two-sample doublet), **grit** (a decaying noise burst, 0.08–0.38 ms time
  constant, ~30 %), a **pop** (one cosine cycle over 0.35–1.25 ms, 3 % + 6 % × Chaos);
  scratches are 0.3 ms pops. A click is closed-form in the samples since its start, so a click
  picked up halfway (a render from bar 17, a jump) is the same click: `restartAt` rebuilds what
  is under way and the crackle from any index is the same to the bit (tested).
- **Levels**: a click of level 1 peaks at the bed's RMS after the phono stage, measured per kind
  at prepare (ticks and pops by their peak through `RiaaPlayback`, grit by its RMS), so clicks
  have the same size at every sample rate. Dust is `1 + 7 u³` (0 … +18 dB over the bed, mostly
  small), scratches +15.6 dB. Rate: `4 + 260 · i^1.6` events per second. At full Damage the
  loudest click peaks at −0.7 dBFS over a −22 dBFS bed (crest 21 dB).

## Controls

| ID | Stored | Meaning |
|---|---|---|
| `hissOn` | bool, default on | On/Off fades over 20 ms |
| `hissAmount` | 0–100 %, default 50 | with Damage (i = Amount × Damage, Hiss's curve is linear): the bed's RMS, `−62 + 40 · i^0.6` dBFS, fading to exactly nothing below i = 5 % along the C2 fade shape; the crackle's rate and scratches |
| `hissType` | choice: Vinyl, Tape, Hum (index frozen) | a change crossfades the beds over 40 ms, equal power (unrelated noises: each type has its own noise lanes) |
| `hissTone` | −100 … +100, default 0 (Neutral) | dark closes the top to 3.3 kHz (one-pole, from 16 kHz) and opens the bottom to 5 Hz; bright lifts everything above 3 kHz by up to 6 dB and thins the bottom to 80 Hz. Hum: also the buzz share, `0.3 + 0.7 · Tone` (dark: pure hum) |
| `hissFollow` | −100 … +100, default 0 (Steady) | an envelope of the input (8 ms attack, 160 ms release) between −54 and −18 dBFS: + lets the noise ride on the input (at +100 it is silent when the input is), − ducks it (−100: −20 dB under a loud input) |

Measured (`InkcapTests_hiss`): the bed at i = 0.40 / 1 is −39.5 / −22.0 dBFS (Vinyl, crackle
included), −39.3 / −22.6 (Tape), −39.1 / −22.4 (Hum) against −38.7 / −22; within ±0.8 dB at
44.1, 48, 96 and 192 kHz. Tone moves the energy above 4 kHz against below 1 kHz by −6.7 / +7.6 dB
(Vinyl), −7.2 / +5.1 dB (Tape), −13.1 / +11.2 dB (Hum). Follow −100 ducks by 19.8 dB.

Level, Tone, Follow and Chaos glide (two one-poles of 20 ms that land exactly); intensity 0 has
its own 30 ms fade (`presence`) that lands exactly on 0, where the module stops working: so
Damage 0 adds exactly nothing, however the glide was moving.

## Streams and determinism

| Stream | Lanes | Used for |
|---|---|---|
| `hissNoise` (0x0501) | 0, 1 Vinyl lateral / vertical; 2, 3 Tape L / R; 4, 5 Hum's amplifier L / R | one draw per timeline sample: four 16-bit uniforms summed (Irwin–Hall, ±3.46σ) |
| `hissCrackle` (0x0502) | the stream: does a particle sit in this cell; lane 0 density wander, lane 1 bursts | |
| `hissCrackleShape` (0x0503) | 0 level, 1 kind, 2 balance, 3 width, 4 grit noise, 5 per-turn variation, 6 offset in the cell, 7 turns heard; 10–19 the scratches (angle, offset, level, miss, balance) | |
| `hissRumble` (0x0504) | 0 lateral, 1 vertical | value noise, 70 Hz lattice |
| `hissWander` (0x0505, new in M2) | 0 Tape's level breathing (7 Hz lattice, ±30 % × Chaos), 1 Hum's level (0.5 Hz, ±25 % × Chaos), 2 Hum's drift (0.35 Hz: the mains frequency off by up to ~0.1 Hz × Chaos), 3 Hum's buzz bursts (0.6 Hz: a load switching on), 4 Hum's L/R balance (±12 % × Chaos), 5 the surface's swell phase | |

- **The timeline index** of a sample is the song position in samples, `round (seconds × fs)`,
  fixed per segment of continuous playback (from `GridClock::secondsAt` at each jump). Every
  draw and every value noise is indexed by it, never by a running counter: the same noise for
  any block size (tested at 1, 7, 64, 333, 4096, also through type, Tone and Damage changes), and
  a render from bar 17 has bar 17's noise to the bit after the filters' first milliseconds
  (−300 dB after 500 ms, all three types).
- **Hum's phase** is `(index · 50 mod fs) / fs` in integers at an integer rate: exact, never
  drifting, whatever the song length. Chaos 0 is perfectly steady (no wander at all).
- **A new seed** or **a transport jump** crossfades the smooth parts (hum, rumble, the wanders,
  the swell) from the old seed or position to the new over 250 ms or 20 ms, equal gain; white
  noise and crackle simply continue from the new ones (noise has no shape to step). No clicks:
  tested on Hum, where the 3rd difference would see a step.
- Chaos 0 is steady and periodic: tape and hum are constant; vinyl repeats with the turn.

## Gates (`InkcapTests_hiss`, 12 cases)

| Gate | Result |
|---|---|
| Damage 0 and Off: the input to the bit (every type, Chaos 0 and 1); back to the input to the bit after Damage returns to 0; Off with silence in: silence out | exact |
| Each bed at its level, and Amount × Damage moves it | within −0.8 … +0.7 dB |
| Any block size, to the bit (also through changes on shared boundaries) | yes |
| From bar 17 vs from bar 1, after 500 ms | −300 dB (identical), every type |
| A crackle restarted halfway is the same crackle | identical |
| Chaos 0 dust comes round with the turn; Chaos 1 is fresh | 56 % / 0.6 % repeats |
| Tone, Follow | see above |
| Seed change, transport jump, On/Off and type change on Hum: no sharper than the steady hum (3rd difference) | < 1.5 × |
| Every rate (44.1–192 kHz), mono | finite, at its level |
| `process` allocates nothing (types, seeds, a loop, Damage dips) | 0 allocations |
| Golden renders, each type (`golden/<type>.f32`, `INKCAP_UPDATE_GOLDEN=1` re-records) | < −60 dB error energy |

Not in the chain's click gate: noise and crackle are broadband by design, so the 3rd-difference
crest on a sine sees them as clicks. `InkcapTests_chain` and `InkcapMeasure` switch Hiss off for
that gate (as Glitch); its fades are checked here, on Hum.

**CPU** (48 kHz stereo, at full Damage, thread time, `-O3`): Vinyl 0.30 %, Tape 0.21 %, Hum
0.31 % (a 4096-point cycle per waveform, built at prepare; the 64-harmonic sum per sample cost
0.94 %). Off or at Damage 0: nothing.

## Voicing (M3)

**Target:** a noise floor as a lo-fi record has it on a mix at 0 VU (−18 dBFS): barely there at a
quarter of the pad (−45 dBFS, 27 dB under the mix), a clear bed in the middle (−36), the noise as
part of the sound at full (−22). The level law is already in dB (`bedLevelDb`), so Hiss's Damage
curve is linear.

| Bed RMS (dBFS) | Amount 100 %: Damage 25 / 50 / 100 % | Amount 50 % (default) |
|---|---|---|
| Vinyl | −45.3 / −35.9 / −21.6 | −51.3 / −45.3 / −35.9 |
| Tape | −45.1 / −36.1 / −22.5 | −51.1 / −45.1 / −36.1 |
| Hum | −44.9 / −35.9 / −22.3 | −50.8 / −44.9 / −35.9 |

(`InkcapCalibrate --voicing`, silence in, Follow and Tone neutral.)

**Match** counts the noise Hiss adds at the calibration level (pink noise at −18 dBFS): at full
Damage Vinyl adds 1.4 dB, Tape 2.5, Hum 0.9, and Match takes them back (within ±0.1 dB). On a
quieter mix the noise stands out more, as a noise floor does.

## Open

- Voicing by ear against reference noise (the M2 listening test of PLAN *Settled* 10): the
  level curve, the crackle's density, size distribution and click shapes, the rumble's level,
  the tape head's presence, the hum's harmonic recipe. Renders made for that pass (dry drum loop,
  each type at Chaos 0 and 1) are not in the repository.
- **50 Hz only.** Hum is the European mains frequency; a 60 Hz choice has no parameter in the
  frozen layout. A type or a setting for it is an M6 candidate.
