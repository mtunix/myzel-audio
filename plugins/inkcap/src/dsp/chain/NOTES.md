# The chain, the module interface and the display's telemetry

`InkcapChain` runs the six modules in the plan's fixed order (PLAN.md, *Signal flow*):

```
in ─► clock ─► Glitch ─► Warble ─► Crush (2×) ─► Bits ─► Hiss (+) ─► Room
   ─► Match ─► Mix (vs the latency-aligned dry) ─► Output ─► Bypass ─► out
```

**Status (M5):** the frame is complete, every module is built and voiced by measurement, each in
its own directory with its own `InkcapTests_<m>` and `NOTES.md`; Match compensates each module's
level from calibrated tables (below); Mix's dry follows the wet's phase (*Mix's dry*).

## Per block

1. A host loop that wraps inside the block (its loop points say where; hosts that split the
   block there never get here) splits it at the wrap: each part is a block of its own below, the
   second starting a new timeline segment at the loop's start (`loopWrapOffset`,
   `afterLoopWrap`), so every module sees the jump where it happens.
2. `GridClock::advance` turns the host's position (`HostPosition`, from the processor's play
   head) into the song position of every sample (`src/dsp/common/NOTES.md`).
3. The input is copied for the dry paths; the modules run in place, in order, each with its own
   settings (`ChainSettings.h`) and one shared `BlockContext` (the clock, Damage, Chaos, the
   seed). A block longer than prepared is processed in pieces of the prepared size (the modules
   are prepared for that size); the result is the same to the bit.
4. Mix's dry is built from the modules' records of the same samples (`MatchedDry`, below).
5. Mix (linear, PLAN.md *Settled* 20), Output (±24 dB) and Match follow their targets through
   two-pole glides (30 ms for Mix and Output, 50 ms for Match), whose slope never steps and which
   track automation at a constant lag whatever the host's block size; they land exactly (Mix to
   1e-6, the dB glides to 1e-4 dB). Bypass (20 ms) fades along the C2 fade shape
   (`dsp/common/Crossfade.h`) to the integer-aligned dry. At rest the output is exactly the wet,
   exactly the dry, or (Damage 0, where the wet is the dry) the input delayed. M1 to M4 moved
   Output and Mix with a 50 ms C2 fade restarted at each new value: under automation it crawled
   (at 64-sample blocks Output was −1 dB when the automation reached −24, then stepped 23 dB in
   50 ms; *Settled* 59).
6. The telemetry is published (below).

## Mix's dry (`MatchedDry.h`)

Bypass takes the input delayed by the latency, to the bit. Mix takes a dry that has been through
what the wet's clean parts do to the phase, so a partial Mix does not comb (*Settled* 53):

- **Crush's pair:** per sample Crush records how much of its output came through its 2x pair
  (`pairShares()`, -1 on the clean ring); the dry takes an identical pair with nothing between
  its halves, blended in by that share (its phase is the pair's to the bit; it is flat to
  0.003 dB below 0.48 fs). A tape head's group delay at 2 kHz (`colourDelays()`) follows as a
  fractional delay, so a worn head does not comb either.
- **Bits' converter:** its share and its delay (`converterShares()`, `converterDelays()`: half a
  clock period plus the filters' group delay, `bits/NOTES.md`), a fractional delay.
- The fractional delays are one windowed-sinc read inside the 6 samples of Warble's rest (no
  extra latency), blended in by the larger share that has one, so the dry comes to rest on the
  exact integer read as Crush and Bits do; the sinc is flat to 0.35 fs and 1.7 dB down at 0.4 fs.
- While Mix rests at 100 % only the delay lines run; the pair starts afresh, under Mix's glide,
  when the dry is needed again.

Measured (`InkcapTests_chain` `[mix]`, `InkcapMeasure`): at Mix 50 %, 100 Hz to 0.4 fs, every
Crush and Bits type at a light and a middle Damage stays within 0.4 dB of the in-phase sum of
wet and dry at 44.1 and 48 kHz (before: −18 dB at 17 kHz with Crush, −28 dB at 8 kHz with Bits;
Phone from 1 kHz, below which its 300 Hz low cut leads the dry, as any filter blended with its
own input). The Damage-0 null stays exact at every Mix.

The latency is the sum of the modules' `latencySamples()`: Crush's 2× pair (3 samples) and
Warble's rest (6 samples, where its head reads with the flat windowed sinc), 9 samples at every
rate, on or off (`InkcapChain::fixedLatencySamples()`); the dry path is delayed by exactly that. `maxTailSeconds` is the sum of the
modules' tails (Glitch's longest event, Room's 4.5 s); the processor reports it.

## The module interface (`src/dsp/modules/Module.h`)

Every module is a class `<Name>Module` in `src/dsp/modules/<m>/` that satisfies the
`ChainModule` concept with its settings struct (`static_assert` in its header):

```cpp
void prepare (const ModuleSpec&);          // allocates: sample rate, max block, 1 or 2 channels
void reset() noexcept;                     // forgets the past (a new render)
void process (const AudioBlock&, const <Name>Settings&, const BlockContext&) noexcept;
int latencySamples() const noexcept;       // constant; 0 except Crush (3) and Warble (6)
float intensity() const noexcept;          // the Damage-scaled Amount of the last block, 0 .. 1
float activity() const noexcept;           // what it does right now, 0 .. 1 (the display's dot)
static constexpr double maxTailSeconds;
```

- `AudioBlock`: the channel pointers, their count, the length, and `firstSample`, the
  `GridClock` index of sample 0 (use it, not `clock.blockStart()`: the chain may hand a block
  over in pieces). `clock.jumpedAt (block.firstSample)` says a new timeline segment starts here.
- `BlockContext`: the clock (`ppqAt`, `secondsAt`, `sampleAt`, `getBpm`), `damage`, `chaos`
  (0 .. 1) and the instance's `seed`.
- **Damage 0 is exact**: the output is the input (Crush, Warble: delayed by their latency) to the
  bit. `moduleIntensity (module, amount, damage)` (`dsp/DamageCurves.h`) is 0 at Damage 0.
- **On/Off** crossfades over 20 ms: `OnOffFade` (in `Module.h`). Idle (off, at rest) means skip
  the work and leave the input untouched.
- **Determinism**: every random value from `SeedRng` indexed by the timeline (a slot, a sample
  index from `firstSample`, a lattice point), never a running counter; per-sample smoothing
  only, so any block size gives the same render; the first block after `reset()` jumps to its
  settings. A seed change must not click (Glitch: next slot; Warble: a 250 ms fade of the
  wander).
- No allocation, locks or I/O in `process`.

The typed controls' read-outs (Length, Rate, Drive, Bits Rate and Depth, Size) live in each
module's `<Name>Controls.{h,cpp}` in its namespace `inkcap::dsp::<m>`; `ModuleInfo.cpp` only
dispatches to them.

## Damage curves and Chaos (M3)

Each module's intensity is Amount × its own Damage curve, d^e (`dsp/DamageCurves.h`), and each
module maps its intensity onto its controls so that equal steps sound like equal steps (Glitch's
events per bar, Warble's cents, Crush's drive in dB, Bits' clock and depth from where a converter
is first heard, Hiss's bed in dB, Room's wet in dB). The curve only places those steps on the pad:

| Module | e | Heard as (Amount 100 %: Damage 25 / 50 / 100 %) |
|---|---|---|
| Glitch | 1.0 | 0.5 / 2 / 8 events a bar at 1/16 |
| Warble | 1.0 | Wow 6 / 14 / 34 cents |
| Crush | 0.6 | Tape at programme peaks (−6 dBFS, Drive 50 %): 0.4 / 0.8 / 2 % THD |
| Bits | 0.75 | Sampler 12: 15.7 / 13.7 / 10.9 kHz top, 14.6 / 13.6 / 12 bits |
| Hiss | 1.0 | −45 / −36 / −22 dBFS bed |
| Room | 1.0 | wet −15 / −8 / −1 dB re the dry |

The targets and the measured ladder are in each module's `NOTES.md`; `InkcapCalibrate --voicing`
prints it. Chaos moves every module now: Glitch's pattern into a probability, Warble's LFO into
value noise, Crush's drive wanders (±4 dB at full), Bits' jitter widens, Hiss's crackle stops
repeating and its levels breathe, Room's tank modulation turns to noise. At Damage 0 it does
nothing (the null gate).

## Match (M3)

`Match.h`, `Match.cpp` and the generated `MatchTables.h`: output compensation so Damage changes
character, not loudness (PLAN.md, Match). Each module that changes the level has a table of the
gain (dB) that restores the K-weighted loudness (BS.1770) of pink noise at −18 dBFS RMS:

| Table | Axes | Notes |
|---|---|---|
| Glitch | type, Length (7), Rate (one row per grid value, 12), the share of slots that fire (none, then 1/128 … 1/2) | the share is exact at Chaos 0 (every Nth slot) and blends towards the density with Chaos (`glitch::meanDensity`); an intensity axis could not follow the steady pattern's steps |
| Crush | type, Drive (7), Tone (7), intensity (9) | |
| Bits | type, Rate (7), Depth (7), intensity (9) | |
| Hiss | type, Tone (7), Follow (7), intensity (9) | the noise it adds, at the calibration's level |
| Room | type, Size (7), Duck (7), intensity (9) | |

Warble moves pitch, not level, and has none (within 0.11 dB without one, since its head rests where the sinc is flat). The chain sums the
modules that are on, multilinearly interpolated, into one gain before Mix; it glides in dB
(two poles, 50 ms in all) and lands exactly, so intensity 0 everywhere is exactly unity (the
Damage-0 null holds to the bit) and an exp is computed only while it moves. **Boosts stop at
+12 dB**, per module and in all: where a module takes most of a quiet mix away (a 2-bit converter
rounding it to zero, a transport stalled most of the time) the boost that matches the calibration
level would blow up a louder programme. Cuts are not limited.

**Calibration** (`tools/calibrate`, `InkcapCalibrate`): one module alone, Match off, Mix 100 %, on
every core, 4 s of pink noise (Glitch: the mean of four seeds over 12 s, since its level depends
on where a seed puts its events), the first second skipped. Run it from `plugins/inkcap` after
any change to a module's level; when a table's shape changes, first reset `MatchTables.h` to empty
tables (`inline constexpr ModuleTable crushTable {};` …) so the tool compiles. Measured
(`--report`, every type, Damage 5 … 100 % in 5 % steps at Amount 100 and 50 %): at the controls'
defaults within ±0.4 dB for every table, Warble −0.11 dB without one (the gate: ±1 dB); with the controls between the table's
points within ±0.9 dB except where the boost stops: Bits Sampler 8 at 2–3 bits (+2.7 dB) and Phone
at its lowest Rate (−1.4 dB). Without Match, at full Damage: Glitch Chew −4.0, Crush Fuzz −5.4,
Bits Phone −5.5, Hiss Tape +2.5 dB.

## Working on one module

A module's work stays inside its two directories: `src/dsp/modules/<m>/` and
`tests/modules/<m>/`. Every source list is a glob, so a new file there needs no CMake edit. The
module's suite, `InkcapTests_<m>`, builds the module in isolation (its sources and
`src/dsp/common`, no JUCE, no other module), so a module under construction never breaks
another's. A module that needs more (a definition, a library) adds
`tests/modules/<m>/module.cmake`, which `tests/CMakeLists.txt` includes with the target in
`target`. `tests/support/ModuleHarness.h` runs a module on its own with a simulated host
transport; `tests/support/Analysis.h` has the signals and measurements (null, crest, tone
amplitude, frequency track). Then check the whole chain: `InkcapTests_chain` (Damage-0 null with
every module, determinism, bar 17, clicks) and `InkcapMeasure`.

The files the modules share (`Module.h`, `ChainSettings.h`, `DamageCurves.h`, the chain, the
support headers) are the frame: change them only together with every module that uses them.

## Telemetry (`Telemetry.h`): what the display reads

The audio thread writes these once per block, relaxed atomics; the editor reads them on the
message thread through `InkcapProcessor::getTelemetry()`.

| Value | Meaning | Display |
|---|---|---|
| `modules[m].intensity` | the module's Damage-scaled Amount (0 when off) | the module row's amount bar |
| `modules[m].activity` | what it does now: Glitch the event's envelope, Warble the speed deviation against its type's full depth, Hiss the added noise's peak (−72 … −12 dBFS), Room the wet's peak (−60 … −6 dBFS), Crush the saturated band's distortion (−50 … −10 dB), Bits the converter's noise against the signal (72 … 12 dB) | the row's dot |
| `modules[m].on` | the On key as the audio thread saw it | |
| `wear` | mean intensity of the modules that are on | the wear matrix |
| `inputPeak[c]`, `outputPeak[c]` | linear peak since the last read: read with `exchange (0.0f)` | meters, with the editor's ballistics |
| `ppq`, `bpm`, `followingHost`, `timeSigNumerator`, `timeSigDenominator` | the song position at the end of the last block; free-running when not following the host | status line (sync), the Glitch page's bar ruler |
| `barStartPpq`, `hasBarStart` | the host's last bar start, when it gives one and the clock follows it | the Glitch page's bars after a signature change |
| `glitchSeed` | the seed Glitch's events use now (a new seed waits for the next slot) | |
| `glitchEventType`, `glitchEventSlot`, `glitchEventProgress` | the event playing (-1: none) | the Glitch page's playhead |
| `blocks` | blocks since prepare | whether audio runs at all |

**Glitch's coming events** are not sent: they are a pure function of the seed, the settings and
the slot. The Glitch page computes them itself for the next 4 bars:

```cpp
const auto s = processor.readSettings();                                    // atomics: fine on the message thread
const auto p = dsp::glitch::patternFor (s.glitch, s.damage, s.chaos);
const auto barBeats = 4.0 * numerator / denominator;
for (auto k = p.grid.firstSlotFrom (ppq); p.grid.slotStart (k) < ppq + 4 * barBeats; ++k)
    if (const auto e = dsp::glitch::eventAt (glitchSeed, p, k))
        draw (e->startBeat, dsp::glitch::durationSeconds (*e) * bpm / 60.0, e->type);
```

with `ppq`, `bpm`, the time signature and `glitchSeed` from the telemetry. This is exactly what
the audio thread will play, except for an event whose source lies before a transport jump (not
played) and a seed change, which the audio takes on at the next slot.
