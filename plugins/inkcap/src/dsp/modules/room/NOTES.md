# Room

Module 6, last: the small space the whole worn sound sits in. **Status: voiced (M3)** by
measurement (*Voicing*); nothing has been judged by ear yet. Built from
the family's reverb blocks, moved from Cerulean to `common/dsp/` for it (`EarlyReflections`,
`Fdn`, `ReverbBlocks`, `DispersionCascade`; namespace only, Cerulean bit-identical: its 227
tests and golden renders pass, and a byte comparison of every algorithm's render before and
after).

| File | Contents |
|---|---|
| `RoomModule` | the module: intensity and the dry step-back, type changes, Duck, the song-position modulation, the sub-block grid, sleeping on silence |
| `RoomSpaces` | the four spaces: `ClosetSpace`, `TankSpace` (Room, Hall, with their `TankVoicing`), `SpringTank` |
| `RoomVoicing.h` | Size per type (ranges, the position mapping), each type's RT60 and ring time |
| `RoomControls` | Size's read-out and typed entry |

## Types

| Type | Built from | Size (position 0 … 50 … 100 %) | Mid-band RT60 |
|---|---|---|---|
| **Closet** | `EarlyReflections<16>` (1–9 ms at 1.4 m, 7 kHz bandwidth) smeared by three short allpasses (0.5–2.3 ms, g 0.5); no tank | 0.80 … 1.41 … 2.50 m | none: 60 dB down 0.15 s after the input stops |
| **Room** | `EarlyReflections<14>` (3–32 ms at 5 m), 4 input allpasses per side, an **8-line `Fdn`** (8.5–30 ms at 5 m) fed by the dry *and* the reflections on four orthogonal Hadamard rows; lows 0.85 × the mid decay, highs 0.45 × above 3.5 kHz | 2.5 … 5.0 … 10 m | 0.45 s × (size / 5 m)^0.7: 0.28 … 0.45 … 0.73 s |
| **Spring** | three springs (round trips 61 / 71 / 83 ms): a 64-stage stretched-allpass cascade (`DispersionCascade`, corner 4.6 kHz) and a transit per spring, a decay filter (lows 0.7 ×, highs 0.5 × above 2.8 kHz), two pickups each (far end: half a round trip in), a gentle cubic drive into the tank (band-limited to 5 kHz first, so nothing above 15 kHz), the cascade's images cut (4th-order Butterworth at 0.92 × the corner) | decay 0.80 … 1.55 … 3.0 s | Size itself |
| **Hall** | `EarlyReflections<14>` (9–85 ms at 20 m), 4 input allpasses per side, an 8-line `Fdn` (41–121 ms at 20 m); highs 0.4 × above 3 kHz, 7.5 kHz bandwidth: a worn, dark hall | 10 … 20 … 40 m | 1.7 s × (size / 20 m)^0.7: 1.05 … 1.7 … 2.76 s |

- **Size** is the longest dimension (metres) for Closet, Room and Hall, the decay for Spring.
  It glides through two poles of 50 ms (per 32-sample sub-block) before the spaces' own
  one-pole length glides, so a step never steps the lines' speed: one pole alone turned a step
  into an instant pitch step on every tank line (−53 dB crest on Room, *Settled* 58).
  It maps geometrically onto the 0–100 % position (each step the same ratio): a type change
  re-reads it, never moves it. Every space's reflections, allpasses and tank lengths scale with
  it and glide (Cerulean's `GlideValue`), and the decay follows: RT60 ∝ size^0.7, between a fixed
  absorption's V / S law and a furnished room's flatter one. Read-outs: `1.41 m`, `5.0 m`,
  `40 m`, `1.55 s` (two decimals below 3, one below 30, so every step reads back within 1.5 %);
  typed entry takes `5`, `5 m`, `1.8 s`, `1800 ms`, clamped to the type's range.
- **Glue, not smear**: short pre-delays from the size, the late field grown out of the
  reflections, tight lows (a 2nd-order low cut on the wet: Closet 160 Hz, Room 110, Spring 140,
  Hall 90 Hz) and damped highs.
- **The reflections' pattern is the type's**, fixed (its own seed in the voicing), not the
  instance's seed: a space's shape is part of its sound, and a pattern rebuilt per seed would
  mean reallocating and clicking. The seed moves only the tanks' modulation.
- **8 lines, not 16.** The 16-line `Fdn` alone cost 1.05 % of a core, a third of the plugin's
  whole budget. With 8 the late tail's spectral fine structure (std of the dB magnitude about a
  smoothed one, 200 Hz–6 kHz, the late impulse response) is 8.1 / 6.3 / 6.0 dB for Room at 2.5 /
  5 / 10 m and 5.6 / 5.2 / 5.3 dB for Hall (16 lines: 6.8 / 6.3 / 5.7 and 5.9 / 5.7 / 5.3;
  Gaussian noise: 5.6; Cerulean accepted 6.5–8.1 for its halls).

## Controls

| ID | Stored | Meaning |
|---|---|---|
| `roomOn` | bool, default off | On/Off fades over 20 ms; a space that comes back starts empty |
| `roomAmount` | 0–100 %, default 50 | with Damage (i = Amount × Damage, Room's curve is linear): the wet rises to `i ×` its type's level while the dry steps back by up to 3 dB (`1 − 0.3 i`): the source moves into the space. Glides 2 × 30 ms; intensity 0 has its own 30 ms fade that lands exactly |
| `roomType` | choice: Closet, Room, Spring, Hall (index frozen) | a change: the new space fills (its input fades in over 30 ms), the old one stops taking input and rings out under a 300 ms fade, then sleeps and is cleared when next used |
| `roomSize` | 0–100 % position, default 50 | see Types |
| `roomDuck` | 0–100 %, default 0 | the room ducks under the input: up to 24 dB, 0.6 dB per dB of the input's envelope above −40 dBFS (5 ms attack, 100 ms release), gain gliding 2 × 12 ms; −17 dB while a −10 dBFS programme plays, fully back 300 ms after it stops |

**Levels** (pink noise at full intensity, re the dry at unity): Closet and Room −4 dB, Spring
and Hall −6 dB (per-type trims in `RoomModule.cpp`), against the stepped-back dry at −3 dB. Match
(the chain's, from calibrated tables) compensates the rest.

## Modulation, determinism, silence

- The spaces run in **32-sample sub-blocks aligned to the timeline index** (the song position
  in samples, fixed per segment of playback, as Hiss's): each sub-block's size, decay, Duck and
  modulation are set at its start and ramp per sample inside it, so every block size renders
  the same to the bit (tested at 1, 7, 64, 333, 4096, also through type, Size and Damage
  changes).
- **The tanks' modulation** (`roomModulation`, 0x0601: lanes 0–7 value noise, lanes 8–15 the
  lines' phases) is a function of the song position: line i at `f + spread · i / 7` (Room
  0.31–0.65 Hz, Hall 0.22–0.47 Hz), Chaos 0 a sine per line, Chaos 1 value noise; peak depth
  0.12 ms (Room) or 0.3 ms (Hall), three times that at Chaos 1. It is slew-limited to twice a
  sine's own top speed, so a new seed or a transport jump glides over within half a second
  instead of stepping the delays. Passed to `Fdn::update` with the modulation per line (the
  overload `Fdn` gained in the move; Cerulean's own LFOs are a running generator, which a render
  from bar 17 cannot reproduce).
- A render from a later bar matches the render from bar 1 once the tail of the music before it
  has rung down 90 dB (0.6 × the ring time): −300 / −120 / −112 / −108 dB for Closet / Room /
  Spring / Hall. The chain's bar-17 gate (500 ms) holds for the Closet and Room only: a hall's
  tail of what came before is still there after half a second, physically (PLAN *Settled*).
- **Silence**: with the input below −300 dBFS for the type's ring time (2.5 × RT60 + 60 ms, Hall
  + 200 ms: 150 dB down), the module stops working; its output is then the input × the dry gain,
  so exact zeros in give exact zeros out. Before that the tanks flush tiny values (Cerulean's
  `flushTiny`), and the output never holds a denormal (tested for every type at its longest).
- `maxTailSeconds` = 4.5 s: the longest Hall and Spring 90 dB down.

## Gates (`InkcapTests_room`, 11 cases)

| Gate | Result |
|---|---|
| Damage 0 and Off: the input to the bit (every type, Chaos 0 and 1); back to the input 100 ms after Damage returns to 0 under a ringing hall | exact |
| Mid-band RT60 (the octave at 1 kHz, Schroeder, −5 … −25 dB) at Size 0 / 50 / 100 % | Room 0.26 / 0.44 / 0.73 s (0.28 / 0.45 / 0.73), Spring 0.73 / 1.48 / 2.79 s (0.8 / 1.55 / 3.0), Hall 0.98 / 1.85 / 2.57 s (1.05 / 1.7 / 2.76): within ±10 % |
| Silence: exact silence after the ring time, no denormal output, the module asleep | every type at its longest |
| Any block size, to the bit | yes |
| From bar 9 vs from bar 1, after 0.6 × the ring time | ≤ −108 dB |
| No clicks on a 330 Hz sine through On/Off, every type change, Size, Duck, seed and Damage | −70 dB (gate −60) |
| A step in Size on the tank spaces (preset loads, typed values, stepped automation), Room and Hall, 50 → 5 % and 50 → 100 % | −64 … −72 dB (gate −60; −52 … −56 before the second pole) |
| Duck | −17.4 dB while playing, 0 dB after |
| Wet L/R correlation on noise (a mono source) | Closet −0.07, Room −0.05, Hall 0.00, Spring 0.30 (narrow, as a tank); mono sum −3 dB |
| Every rate (44.1–192 kHz), mono | finite; Damage 0 exact |
| Size read-outs read back | within 1.5 % |
| `process` allocates nothing (type changes, Size, Duck, seeds, a loop, silence) | 0 allocations |
| Golden renders, each type | < −60 dB error energy |

**CPU** (48 kHz stereo, at full intensity, Size 100 %, thread time, `-O3`): Closet 0.41 %,
Room 0.95 %, Spring 0.67 %, Hall 0.95 %. Off, at Damage 0 or asleep on silence: nothing. Of
Room's and Hall's, the 8-line `Fdn` is about half; the reflections 0.11 %, the input allpasses
0.18 %.

## Voicing (M3)

**Target:** glue at a quarter of the pad (the wet 15 dB under the dry), the source in the room in
the middle (−8 dB), far away at full (about level). The wet's amplitude is linear in intensity, so
its level in dB already spaces the steps and Room's Damage curve is linear.

| Wet re the dry (pink noise, Size 50 %) | Amount 100 %: Damage 25 / 50 / 100 % | Amount 50 % (default) |
|---|---|---|
| Closet | −15.3 / −8.6 / −0.8 dB | −21.7 / −15.3 / −8.6 |
| Room | −14.5 / −7.8 / 0.0 | −20.9 / −14.5 / −7.8 |
| Spring | −13.4 / −6.7 / +1.0 | −19.8 / −13.4 / −6.7 |
| Hall | −16.6 / −9.8 / −2.1 | −23.0 / −16.6 / −9.8 |

(`InkcapCalibrate --voicing`.) **Match:** Room's table runs over type, Size, Duck and intensity;
without it, at full Damage, from Hall −0.5 to Spring +1.0 dB; with it within ±0.1 dB.

## Open

- Voicing by ear: decays and colours per size, early/late balance, the levels against Match,
  the spring's drip (dispersion 0.48–0.56) and drive.
- Chaos does nothing to Closet and Spring yet (no tank modulation there); whether a worn spring
  should wobble is a voicing question.
- A cheaper tank (a half-rate late field, or in-loop diffusion with fewer lines) if the plugin's
  3 % budget needs Room under 0.6 %.
