# Glitch

Module 1, first in the chain: the malfunction happens to the source, everything after it is the
medium playing it back. **Status: voiced (M3).** Every type plays end to end; the Damage curve,
density and Match are voiced by measurement (*Voicing*); nothing has been judged by ear yet.

| File | Contents |
|---|---|
| `GlitchPattern` | which slots fire and what each event plays: a pure function of seed, parameters and slot (the editor calls it too) |
| `GlitchVoice` | one event playing: a read head program through the capture buffer |
| `GlitchModule` | the module: the grid, the seed in use, two voices and the cut between them, On/Off |
| `GlitchControls` | Length's read-out per type |

## Types

Each type is a medium failing. Events sit on the Rate grid and hit both channels together.

| Type | What happens | Length is | Chaos varies |
|---|---|---|---|
| **Skip** | A CD player's buffer sticks: the piece just before the slot (20–120 ms) plays 2–8 times, each jump back crossfaded over 2 ms, then a 3 ms return to the input | the piece, 20–120 ms | count (4 at Chaos 0, drawn from 2–8 at 1), the piece (±1 octave, kept in 20–120 ms) |
| **Chew** | Tape loses the head: the highs fade into a 12 dB/octave low-pass whose loss at 10 kHz is the event's depth (6–30 dB), the level dips (2–8 dB, with the depth), and the head slurs back and forth by up to 0.9 ms (a pitch wobble from the crinkle) in the first 120 ms; then it recovers on the input | the event, 60–600 ms | length, depth |
| **Stall** | The transport stops and restarts: the speed falls to zero (smoothstep), the level with it (from 35 % of the speed down), holds, comes back up over 0.6 × the stop, and the head, now behind, catches up with the input through a 12 ms crossfade | the stop, 50–700 ms | length, a hold (up to half the stop), the restart |
| **Bend** | The sampler's clock sags: the speed drops by the event's semitones (1–12, deeper with Damage) over the first 20 %, holds, glides back over the last 35 %, then catches up (12 ms) | the event, 80–800 ms | length, depth |
| **Any** | Each event plays one of the four. Chaos 0: the seed's own order of the four, one after the other (the list repeats every 4 periods). Chaos up: a free pick with probability Chaos, Stall and Bend weighing 0.5 + Chaos against Skip's and Chew's 1 | each type's own | |

Every speed curve is a smoothstep with a closed-form integral, so the head's lag is exact at
every sample (no integration, nothing that depends on the block). Every fade uses the C2 fade
shape (`dsp/common/Crossfade.h`): equal power between unrelated sources (a jump in time), so an
edge is abrupt but never a click.

**Chew's depth is a low-pass, not a shelf.** The plan's "−6 … −30 dB above 2 kHz" cannot come
from a first-order shelf (its cut reaches 30 dB only a decade and a half above its corner); a
head lifting off the tape loses more the higher the frequency (spacing loss). The event fades
from the input into a 2-pole low-pass (`TptSvf`, Q 0.71) whose cutoff gives the depth at 10 kHz
(the bilinear transform's prewarping makes it a little steeper near Nyquist). At the envelope's
ends the output is the input plus nothing: exact.

## Events

Amount × Damage gives the intensity `i` (`moduleIntensity`; Glitch's Damage curve is linear, d^1.0):

- **density** = 0.5 · i² of the slots fire on average (at most every second slot);
- **severity** = 0.35 + 0.65 · i scales Chew's and Bend's depth.

**Chaos 0, the steady pattern:** every Nth slot, N the musical period nearest 1 / density from
{2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128} (at 1/16: half a beat … 8 bars), at a phase the
seed picks for that N. Types, lengths and counts are fixed, so the event list repeats with N
(Any: 4N). **Chaos c:** slot k fires with probability (1 − c) · [k is a steady slot] + c ·
density, so the pattern melts into a probability per slot as Chaos rises; the mean density stays.

Random numbers (`SeedRng`, never a running generator):

| Stream | Index | Lanes |
|---|---|---|
| `glitchSlots` | slot | the draw that decides a slot; lane 0: the steady pattern's phase (index N) |
| `glitchType` | slot | Any: the draw for a free pick; lane 0: the seed's order of the four; lane 1: the weighted pick |
| `glitchShape` | slot | lane 0 length, 1 Skip's count, 2 depth, 3 Stall's hold, 4 Stall's restart |

Frozen once golden renders exist (M2): append lanes, never renumber.

**On the grid:** an event starts on the first sample at or after its slot's start (the slot's
beat from `Grid`, with Groove's swing; `GridClock::sampleAt`, which counts a position within
10⁻⁹ beats of a boundary as on it, so the same boundary lands on the same sample whether the
render started at bar 1 or bar 17). Slot indices come from PPQ, so a tempo change moves events
in time, never changes which slots fire. The Rate grid comes from `glitchRates`; a Rate or
Groove change takes effect from the next slot of the new grid.

**Overlap:** an event plays until its natural end, or until the next event starts and cuts it
(3 ms equal-power crossfade between the two heads). The longest event (Stall at full Length and
Chaos) is under 2 s; the capture buffer holds 4 s and `maxTailSeconds` is 2 s.

## Determinism

- The seed in use is taken at each slot: a new seed takes effect at the next slot, so it never
  cuts into an event (the display reads `glitchSeed`).
- **After a jump** (the first block, play, stop, a locate, a loop) the module drops an event under
  way (3 ms fade to the input) and plays no event whose source lies before the jump: Skip's
  piece (and its fades' 8 samples). Chew, Stall and Bend start on the input and only fall behind
  it (their heads never read before their own start: Chew's slur moves at under a sample per
  sample, Stall and Bend slow down from speed 1), so they play on the jump's own slot, the
  downbeat after play, a locate or a loop restart; M1 dropped those too, which left every loop's
  first slot silent (*Settled* 57). The decision (and the event log) is the same as in a render
  that played through; only Skip's audio differs there. A render started on a slot that fires
  plays it and is the full render to the bit once a 3 ms cut of what the full render had under
  way is behind it (`InkcapTests_glitch`, every type); from bar 17 the plan's −80 dB after
  500 ms holds at every type (`InkcapMeasure`: −93 … −106 dB with Room's Closet).
- A DAW loop repeats its events exactly (the same slot list every pass), also when the host
  wraps inside a block without splitting it: the chain splits the block at the loop's end
  (`InkcapChain::loopWrapOffset`, from the host's loop points), so the jump is where it happens
  (`InkcapTests_chain`, a one-bar loop in 512-sample blocks).
- Without a playing transport the grid runs free from the fallback BPM (or the host's tempo),
  counted from the block where it started running free.
- Per sample, from the clock's anchor: any block size gives the same render to the bit.

## Controls

| ID | Stored | Meaning |
|---|---|---|
| `glitchOn` | bool, default off | the module key; On/Off crossfades over 20 ms, and no event starts while off |
| `glitchAmount` | 0–100 %, default 50 | with Damage: how many slots fire, and how deep |
| `glitchType` | choice: Skip, Chew, Stall, Bend, Any (index frozen) | |
| `glitchRate` | choice, 12 grid values strictly slow to fast (`glitchRates`, index frozen), default 1/16 | the slot grid |
| `glitchLength` | 0–100 % position, default 50 | per type, log between the type's limits; read-out in ms (Any: %) |
| `glitchGroove` | 50–75 % swing, default 50 (Straight) | where the off-beat slot starts |

## Gates (`InkcapTests_glitch`, `InkcapMeasure`)

| Gate | Result (M1) |
|---|---|
| Damage 0 and Off: the input to the bit, every type, Chaos 0 and 1 | exact |
| Between events: the input to the bit | exact |
| Chaos 0: the event list repeats with its period (Any: 4N) | yes |
| Any block size 1 … 4096: the same to the bit | yes |
| Bar 17 vs bar 1: the same events; the same audio to the bit once the first event both play has cut in; at the defaults −150 dB after 500 ms (gate −80 dB) | yes |
| Events on the grid at 60–200 BPM, swing 50–75 %, four rates: worst start error | 0 samples (810 events) |
| Clicks on a 330 Hz sine (3rd-difference crest, gate −60 dB) | Skip −63, Chew −68, Stall −70, Bend −68, Any −62 dB |
| Skip: the repeats are the piece before the slot, sample for sample | exact |
| Bend: the pitch at the bottom | within 1 % of the event's semitones |
| Chew: 10 kHz loses the depth, 300 Hz only the dip | within 3 dB / 1.5 dB |

## Voicing (M3)

**Target:** an event every bar or two at the middle of the pad, so the first touch of Damage is an
occasional malfunction and full Damage a broken stream; equal steps of Damage should feel like
equal steps of "more broken", which in events per bar means a ratio per step. The density
`0.5 · i²` already doubles the events every sixth of the pad (×4 per doubling of i), so the curve
stays linear.

| Amount | Damage 25 % | 50 % | 100 % |
|---|---|---|---|
| 100 % | 0.53 | 2.0 | 8.1 events per bar (1/16) |
| 50 % (default) | 0.12 | 0.53 | 2.0 |

(`InkcapCalibrate --voicing`.) Chaos keeps the mean density and turns the steady pattern into a
probability per slot; at the default Chaos (25 %) three quarters of the events keep the steady
pattern's places.

**Match:** Chew dips the level and Stall stops the transport, so a dense Glitch is quieter:
without Match, at full Damage, Chew −4.0, Stall −2.7, Any −1.6, Bend −0.7, Skip 0.0 dB on pink
noise. Glitch's table (`src/dsp/chain/Match.h`) runs over the share of slots that fire
(`meanDensity`): at Chaos 0 that share steps with the steady pattern's period, which an
intensity axis interpolated across and missed by up to 2.8 dB; per period it is exact. With
Match: within ±0.04 dB at the controls' defaults, ±0.9 dB between the table's Length points.

**Golden renders** per type (`tests/modules/glitch/golden/<type>.f32`, 1 s, Amount 80 %, Damage
85 %, Chaos 35 %, 1/16D with swing).

## Open

Not judged by ear: N's set, Skip's 2 ms fade (the closest of the click gates, −62 dB for Any),
Chew's slur and dip, Stall's level curve. A Reverse type is an M6 candidate.
