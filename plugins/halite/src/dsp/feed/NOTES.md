# The display feed

What the workbench display reads from the audio thread (PLAN.md, *Signal flow*, Display data; *UI*,
*Parts*), built in M4 before the editor. `HaliteChain` owns a `DisplayFeed` and writes it inside
`process()`; the editor reads it through `processor.getChain().feed()` from the message thread. Beside
it, unchanged from M1–M3: `getChain().meters()` (the detector's levels, per block), `status()` (the
running shape, the Live fallback, the latency), `events()` (the event ring) and `getLearn()`.

Every buffer is allocated once, when the chain is constructed (about 5 MB: the sample streams 1 MB,
the history 3.7 MB, Spectral's snapshot 0.1 MB), so a host's `prepareToPlay()` on any thread never
moves memory a reader holds. The audio thread writes only atomics and announces what it is about to
overwrite; it never waits, never allocates, and costs 0.019 % of a core at 48 kHz stereo, 0.075 % at
192 kHz, with Spectral's two bands cutting in every frame (`FeedTests.cpp`, *Perf: the display
feed's own cost*, pinned to one core).

## The interface (types, threads, rates, units)

| What | Type | Written | Read with | Units |
|---|---|---|---|---|
| Sample streams | `SampleFifo` (`feed().samples()`) | every sample, per 256-sample chunk | `read (end, n, out[4])` the newest window; `count()` | input L, R: the latency-aligned input; output L, R: what the plugin sends (after Listen, Output, Bypass); mono writes its channel twice; linear samples |
| History | `HopHistory` of `HistoryRecord` (`feed().history()`) | one record per `recordSamples()` of audio leaving the plugin | `read (cursor, out, max)`; `count()`, `oldest()` | S in dB (Voice: over the voice; Allround: B, dBFS RMS); Ess and Harsh reduction in dB ≥ 0 (the larger of two reductions); input, output (de-essed, before Listen/Output/Bypass), removed sample peaks, linear, largest over channels; output RMS (the same de-essed signal over the record), linear, the louder channel's |
| Spectral's cut and target | `SpectralSnapshot` → `SpectralView` (`feed().spectralSnapshot()`) | 60 times a second while Spectral runs; once more when it stops | `read (view)` then `acknowledge (view.sequence)` | amplitude gains per bin (1: no cut), the deepest since the last acknowledged read; target as mean-square shares per bin |
| Event stamps | `EssEvent::elapsed`, `seconds`, `songPosition` (`events()`) | when an event ends (20 ms hangover) | `EventRing::read (cursor, …)` as before | `elapsed`: input samples since the feed started; `seconds`: song position while the host played, else elapsed seconds |
| Rate, record length, epoch | `sampleRate()`, `recordSamples()`, `epoch()` | `prepare()` | atomics | Hz; samples (32 up to 50 kHz, 64 to 100 kHz, 128 to 200 kHz …); a counter |
| Transport log | `TransportLog` (`feed().transport()`) | once per block, an entry only when the chain's timeline offset, the host's playing or the epoch changed | `read (cursor, out, max)`; `dsp::timeOf (elapsed, entry, rate, songPosition)` with the entry in force at `elapsed` | `from`: the feed's clock; `offset`: the clock less the chain's timeline; the time any record shows, stamped as an event's |

Message-thread helpers, one owner each, no allocation after `prepare()`:

| Helper | Does | Units |
|---|---|---|
| `SpectrumAnalyser` | `update (feed().samples())` at the display's rate (30 fps): input and output spectra at `numPoints()` display points, log-spaced 200 Hz–20 kHz (`frequencies()`, `proportionOf`, `frequencyAt` for the axis); `bandLevels()` puts any per-bin mean-square spectrum (Spectral's target) on the same scale | dB of mean square in the band a sixth of an octave wide around each point (a full-scale sine reads −3.0 dB, as `B` and the Absolute thresholds); floor −150 dB |
| `LevelMeters` | `update (feed().samples())`: per stream the sample peak since the last update and the RMS | dB; RMS as mean square over an exponential window of 300 ms (a full-scale sine −3.0 dB) |
| `cutCurves::ess`, `harsh` (`CutCurves.h`) | each band's cut over the display points from `CutSettings::from (readSettings().detector, status().shape, rate)`, a reduction and the Spectral view | dB ≤ 0 |

## Clocks

- **The feed's clock** counts input samples since the feed started and never goes back: a host's
  `reset()`, a transport start, stop, jump or loop and a `prepareToPlay()` at the same rate leave it
  running. A new sample rate starts it at 0, empties the history and bumps `epoch()`; a reader that
  sees a new epoch drops what it mirrored (history, events' places) and keeps its event cursor.
- **History record m** holds the audio whose input sample numbers lie in [m · R, (m + 1) · R), R =
  `recordSamples()`: what leaves the plugin `latency` samples later, beside the detector's values
  for that same input (the reductions on the audio's clock, not the key's). Records close when the
  next one begins; `count()` is the next record's number. An event's record is
  `feed().recordOf (event.elapsed)`; the newest audio is record `count() − 1`.
- **The transport log** (M4, the events sidebar; PLAN.md Settled 89) holds one entry per change: from input
  sample `from` on the chain's timeline lies `offset` behind the clock and the host plays or not. The chain's
  timeline runs with the clock between jumps, so a host playing straight on adds nothing and a loop, start or
  stop adds one (256 kept). A reader takes the latest entry at or before a record's first sample
  (`record · recordSamples()`) and stamps it as `stamp()` stamps an event: song position (`elapsed − offset`)
  while playing, else elapsed. Entries carry the epoch, so a reader drops those of an old rate. An event is
  stamped when it is pushed (after its hangover), so one that straddles a start or stop may read the other
  clock than its records do; the display shows the event's own stamp.
- A change of latency moves which input leaves: Normal → Live skips a frame of input (its records
  are written with no audio), Live → Normal repeats one (it joins the open record). One record per
  span, either way.
- In Live a record's detector values can be a hop late (the hop that ends after the chunk that
  closes the record); elsewhere they are the record's own.

## Decisions (PLAN.md, Settled 65–71)

- **The streams** carry the latency-aligned input, so the input fill and the output line show the
  same audio and their difference is what Halite took out, and the plugin's real output, so the line
  and the Out meter are what the host receives. Both channels: the analyser averages their powers
  (a Side cut shows; a mono sum would hide it). The In and Out meters read the same streams on the
  message thread: the audio thread has no meter code.
- **The spectrum's scale** is band power, not per-bin level: a sixth of an octave around each point
  (the width of Spectral's level estimate), in dB of mean square. Pink noise reads flat, a sine reads
  its mean square at any FFT size, and Spectral's target, made at 512 points, lands on the same scale
  (Allround on steady noise: −0.06 dB from the spectrum over 2–10 kHz). FFT 4096 at 44.1/48 kHz,
  doubled per doubling of the rate (85 ms), periodic Hann, the newest window at each update; attack
  20 ms and release 300 ms in power, timed by the samples that arrived, so a stopped audio thread
  freezes the display instead of letting it fall.
- **History records** are one detector hop (32 samples) up to 50 kHz and a whole number of hops above,
  so the ring holds at least 84 s at any rate in 3.7 MB (one hop per record at 192 kHz would take
  14.7 MB per instance). The display keeps its 60 s from it and reads older records only while frozen.
- **The output RMS** (M4c, PLAN.md *UI restyle: Navy*, N6) is the Timeline's two-tone waveform: the peak
  outside, the RMS core inside. Each channel's mean square of the de-essed signal (`dry − removed`, as the
  output peak) over the record's samples, the louder channel's root: the channels are not averaged, so a cut on
  one side shows as the peak shows it. Summed in double per record, written with the peaks; a record that held
  no audio (skipped input) reads 0. It is the record's own audio, not a meter's window.
- **Event time:** song position (`start / rate`) when the host's transport played as the event was
  pushed; otherwise the time since the feed started (not since the last `prepareToPlay()`, which some
  hosts call at every start: the list's `~` times would restart).
- **Spectral's cut** is published 60 times a second as each bin's deepest gain since the reader last
  acknowledged a read, carried for at most six publications (100 ms) so a closed editor never shows an
  old cut when it opens. Unlinked takes the deeper of the two gain computers. The gains are published as
  computed, up to one frame (10.7 ms) before their audio leaves. The target: Voice, the learned target
  at the current body level (the floor no bin is cut below); Allround, the signal's own spectrum
  smoothed over an octave and 300 ms; per channel (Linked's summed analysis halved, Unlinked's two
  averaged).
- **Cut curves** for Split and Wide use the analog prototype at the reduction the editor passes; the
  history's records are the source (on the audio's clock, every hop), not `meters()` (the key's
  clock, one value per block). Where a display point spans several Spectral bins, the curve keeps the
  deepest, so a narrow cut never disappears between points.

## Using it (the editor, per frame)

1. `analyser.update (feed.samples())`, `meters.update (feed.samples())`.
2. `feed.history().read (cursor, buffer, n)` into the display's own 60 s mirror (the Timeline blits only
   the new columns); the Cut meter and the curves' reductions are the largest `essDb` / `harshDb` of the
   records read this frame, with the meter's own release.
3. `feed.events()` → `EventRing::read (cursor, …)`: tag each new event at `feed.recordOf (e.elapsed)`;
   Time is `m:ss.cc` of `e.seconds`, with a leading `~` in the secondary colour when `! e.songPosition`.
4. In Spectral (`status().shape`): `snapshot.read (view)`, `snapshot.acknowledge (view.sequence)`,
   `analyser.bandLevels (view.target.data(), view.bins, view.binHz, line)` for the thin line, drawn over
   the Ess band.
5. `cutCurves::ess / harsh (CutSettings::from (processor.readSettings().detector, (Shape) status().shape.load(), rate), …)`.
6. On `feed.epoch()` changing: `analyser.prepare (feed.sampleRate(), …)`, `meters.prepare (…)`, clear the
   mirrors.

Tests: `HaliteTests_reduction "[feed]"` (streams bit for bit, a reader thread against the writer,
history against the chain's input and output, the output RMS (a sine 3 dB under its peak, silence, the
louder channel by RMS, the removed part and Listen's signal left out, mono), events' stamps through play, stop and jump, the cut
curves, Spectral's publication and its target's scale, the spectrum's and the meters' calibration);
`HaliteTests "[realtime]"` (the readers allocate nothing per frame; processBlock with the feed allocates
nothing).
