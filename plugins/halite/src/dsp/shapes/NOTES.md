# Reduction: the chain and the time-domain shapes

M2 core (2026-11-14): the latency model, Wide and Split with the Harsh bell, stereo modes, side
chain, Listen, the Shape crossfade, Bypass and Output (`PLAN.md`, *Reduction*, *Latency*, *Signal
flow*). Spectral joined the same lanes as their third engine, with the timeline's grids and the
transport restarts it needs (M2 Spectral, `spectral/NOTES.md`). The M2 gates round measured every
gate, on real material too, and recorded the golden renders (*Measured (M2 gates)*). **M2b**
(Settled 38–40) replaced Split's filter, M2's matched 2nd-order bell, with a steep cut for both bands,
and re-graded the gates as settled (*M2b*, `split/NOTES.md`). Numbers marked *measured* come from
`HaliteTests_reduction` at 48 kHz unless stated.

## What was built

| File | What |
|---|---|
| `Shapes.h` | `TimeDomainShape`: one signal or a stereo pair through Wide or Split, then the Harsh bell; returns what each band removes. `DesignGrid`: where Split's design segments lie |
| `ShapeLane.{h,cpp}` | one shape in one stereo mode: channels or mid/side in, each channel's removed part per band out; Spectral's start from the frame before it, its readiness, its restart on the timeline, the target carried over |
| `split/SplitFilter.h` | the Split band (a steep bell or high shelf) in subtractive form, a cascade on the hop grid; also the Harsh bell |
| `split/SplitDesign.{h,cpp}` | the steep design (M2b): the elliptic prototypes, their roots per dB, the bases' table, the sections for a depth |
| `spectral/SpectralShape.{h,cpp}` | Spectral: the per-bin gains against the learned target, on `../common/Stft.h` (`spectral/NOTES.md`) |
| `../chain/HaliteChain.{h,cpp}` | detectors, dry delay, reductions on the audio's clock, two lanes and their crossfade, Listen, Output, Bypass, latency |
| `../chain/ReductionTrack.h` | the detector's per-hop reductions stored by the input hop they act on, with the stereo mode each was made in, rendered per sample for a lane in its own mode, and its Ess targets per hop; `ReductionClock`: two tracks across a transport restart; `LaneReductions`: one lane's view |
| `../chain/Fades.h`, `../chain/DelayLine.h` | the raised-cosine crossfade, the Output smoother, the delay lines |

The detector gained three things and changed no result: `BandTaps` (its band signals for Listen's
triggers), a public `Detector::bandsFor`, so the shapes act on exactly the bands it measures, and
`anchor()`, which puts its hops and its periodicity cue's decimation on the timeline's grid
(`spectral/NOTES.md`, *The timeline*).

## Signal flow

Per block, in chunks of up to 256 samples (the detector gives the same hops at any block size, so
chunking changes nothing): the detector runs over the chunk's key; the input goes into the dry delay
and comes out `latency` samples later; the reductions for those delayed samples are rendered as
depths; the lanes compute what each band removes (Spectral from the input as it arrives, a frame
ahead of the dry samples); then per sample

```
out = bypass-fade ( Output × Listen (dry, removed) , dry ) × latency-fade
Listen Output:  removed == 0 ? dry : dry − removed      (removed = ess + harsh)
```

The comparison is not decoration: under flush-to-zero `dry − 0` turns a denormal input into 0, so
the identity gate needs the sample itself. Likewise Output 0 dB does not multiply.

## Latency (PLAN.md, Latency)

- **Normal** = one Spectral frame, `HaliteChain::frameSize`: 512 × 2^round(log2(fs / 48 kHz)),
  doubled until it holds the longest look-ahead and one hop (32 · (hops(10 ms) + 1)). 512 at 44.1
  and 48 kHz (11.6 / 10.7 ms), 1024 at 88.2/96, 2048 at 176.4/192; 256 at 22.05 kHz, 512 at 32 kHz
  and 1024 at 64 kHz, where the plain rule would not hold 10 ms. The same for every shape.
- **Live** = 0 samples. The detector runs without look-ahead; Spectral falls back to Split and
  `ChainStatus::fallback` says why (`live`).
- **A change** of the setting fades the output out over 10 ms (raised cosine), switches while
  silent (delay, look-ahead, and the lane at once if its configuration changes: Spectral cannot
  run in Live), and fades in over 10 ms once the lane is ready (a Spectral lane after its first
  frame, *Crossfades*: 10.7 ms more of silence at 48 kHz). Until the switch the detector keeps the
  look-ahead of the latency the audio has, so the reductions stay lined up. A change taken back
  before the audio has faded out fades it back in (as first built it stayed silent: nothing asked
  for the fade-in again until the next switch; found while fixing the review's findings, *measured*
  now). The processor reports the new latency at once (as M0 built it); the chain follows within the fade.
- Reported equals measured for every shape at 44.1, 48, 88.2, 96, 176.4 and 192 kHz, Normal and
  Live (*measured*: the lag of the largest correlation between output and input, white noise under
  full reduction).

## Timing: the reductions on the audio's clock

The detector shapes one reduction per 32-sample hop and moves it back by the look-ahead
(`DetectorFrame::reductionPosition`), so each value belongs to an input hop. `ReductionTrack`
stores it there as a depth `d = 1 − 10^(−dB/20)`, and renders each sample `t` of input time as a
uniform quadratic B-spline through the hop values: over hop `h` the curve runs from
`(d[h−2] + d[h−1]) / 2` to `(d[h−1] + d[h]) / 2`.

- The gain and its slope never step, so a reduction moving hop by hop does not click on the
  3rd-difference gate, which sees every step of the slope.
- Every sample is a convex mix of three hop values, so the depth stays within them and three
  zeros give exactly zero.
- The curve lags the hop values by about one hop (0.67 ms at 48 kHz); the look-ahead covers it.
- **Normal** reads hop `h` for a sample of hop `h`, which needs the hop's value `look-ahead + 31`
  samples after the sample arrives: this is why the frame must hold the look-ahead and a hop (at
  48 kHz, 15 hops + 31 = 511 of 512). **Live** reads one hop late, the newest complete hop.
- A look-ahead that grows maps several detector hops onto one input hop (the first kept); one that
  shrinks skips input hops, which keep the value before.
- A depth under 10⁻⁶ (−120 dB) counts as none: the detector's release is exponential in dB and
  would otherwise stay above zero until the float underflows, about 90 time constants. With the
  snap, the output is the input bit for bit again ln (R / 8.7·10⁻⁶ dB) time constants after a
  reduction of R dB: *measured* 560 ms after an 8 dB ess with a fixed 40 ms release, about 1.1 s
  at Speed Auto's longest (80 ms).
- *Measured*: at the last sample of every hop the applied depth equals the mean of the detector's
  two hop values (probe, moved back by the look-ahead) within 10⁻⁶, look-ahead 0, 5 and 10 ms and
  Live; a reduction one hop early or late fails it.
- **Positions are the timeline's** (M2 Spectral): the first block after a reset anchors the
  detector's hops, the track's hop indices and Spectral's frames to the host's position; a
  transport start or jump restarts the analysis at the new position, and the samples still in the
  delay read the previous track at their old positions (`ReductionClock`, `spectral/NOTES.md`).
- **Split's design grid** (M2b, `split/NOTES.md`, *Modulation*): the filter designs once per 32-sample
  segment for the segment's last sample, so the chain renders each chunk's depths on to the end of the
  segment holding its last sample (`SplitFilter::lookahead`, at most 31 more). Segments lie on the hop
  grid of the track each sample reads (`ReductionClock::gridTime`), inside one hop, whose depths are all
  known once its first sample's are: the lookahead adds no latency and no block size changes the
  result. Across a restart the audio in flight keeps the previous track's grid and its last segment ends
  at the restart (`DesignGrid::restartAt`).
- The track also keeps the detector's **Ess targets** per hop, by the hop they were decided for
  (before the look-ahead moves them): Spectral learns from the frames where they are 0.
- **Each hop keeps the stereo mode it was made in.** Reduction 0 is the louder channel's in Linked,
  the left's in Unlinked, the component's in Mid and Side; a lane reads only its own mode's hops
  (`LaneReductions`): after its mode's last hop it holds that one, before its first it takes the
  first, in a track that never had its mode (after a transport restart) it reads 0, and it learns
  from no frame another mode decided. The detector runs in the mode of the lane the chain is
  heading to (*Crossfades*).

## The shapes

- **Wide:** `removed = d · x`.
- **Split:** `removed = x − split (x)`, the steep bell or shelf of `split/NOTES.md`, its depth per sample.
- **Harsh** (Split and Wide): the same filter as a bell over the Harsh band the detector measures
  (Harsh Freq, Width; under 0.45 fs), on what the Ess band leaves: `removed = x − harsh (ess (x))`.
  It runs while Harsh is on or its depth is not yet 0, and starts from rest when it starts again.
- The shapes read their input with NaN and Inf as 0 and clamped to ±10⁶ (+120 dBFS); the dry path
  passes any sample on untouched. Up to the clamp they are linear in the signal: a sine at +26 dBFS
  loses what it loses at −6 dBFS within −138 dB (*measured*, every shape; with the first build's ±16,
  the detector's key clamp, a hot signal came out as dry − f (clipped dry): −16 to −18 dB of
  distortion and a smaller cut). A sample beyond ±10⁶ is reduced as if it were 10⁶, which adds what
  the clamp removes; no signal comes near it, and the float frames would overflow only near 10³⁰.
  NaN and Inf: the output is the render with 0 in their place bit for bit, but at those samples
  (*measured*, every shape).

## Stereo (PLAN.md, Reduction)

Linked: reduction 0 on both channels, through one Split design for both (M2b). Unlinked: reduction
`c` on channel `c` (a mono key gives one, used for both), a design path each in the same filter. Mid and Side: the component `(L ± R) / 2` is reduced and what it loses is decoded,
`removed_L = r`, `removed_R = ±r`, so the other component is untouched to rounding (*measured*:
the mid of a Side render within 10⁻⁷ of the input's) and 0 removed is exactly 0. A mono bus has
no stereo mode.

## Crossfades

- **Lanes.** A lane is a shape in a stereo mode (and, for Split, bell or shelf). Any change of it
  starts the other lane from rest, unheard, and crossfades what the two remove over 30 ms (raised
  cosine) once it can be heard, both running. A change while one waits or fades waits for its end.
  - A lane that starts Spectral is handed the frame of input before it (from the dry delay) and is
    heard once its first frame's overlap-add is complete, one frame later (10.7 ms at 48 kHz); it
    takes the target of a Spectral lane it replaces (`spectral/NOTES.md`, *Crossfades*). No block
    runs more frames than its own hops (as first built, the lane ran its frames over the three
    frames of input before it in one block: 12 frames, 539 µs of a 333 µs block at 192 kHz).
  - After a **Stereo** change the new lane fades in once everything it removes is made of the new
    mode's reductions: once the detector's first hop in that mode has reached the output (the
    look-ahead earlier than the audio: 7–11 ms at 48 kHz with 5 ms of look-ahead and 256-sample
    chunks, Spectral a frame later). The outgoing lane holds its own mode's last reduction meanwhile (as first built,
    both lanes read whichever mode the detector was in: about 5.6 ms after a change the outgoing
    lane, still at about 0.9 of the fade, cut a channel by a reduction that channel never had,
    7 dB within 1.3 ms; the reviewer's trace).
  - *Measured*: before the switch the old shape exactly, during it the two mixed by the raised
    cosine within 10⁻⁶ (into Wide), after it the new shape (Wide exactly; Split within 10⁻⁴ once its
    filters settle). A Stereo change with Range holding a steady 8 dB on one channel's key (Wide,
    Unlinked ↔ Linked): the other channel's cut follows the raised cosine within 1.6·10⁻⁸, starting
    512 samples after the change; the first channel stays exact.
- **Listen** crossfades the same way (30 ms); a trigger's delay line starts clean when first heard.
- **Bypass:** 20 ms raised cosine between the result and the aligned input; bypassed it is the
  aligned input bit for bit, the lanes keep running.
- **Output:** two one-poles of 8 ms in series (critically damped): its slope never steps however
  often the target moves; it lands on the target within 10⁻⁶ and stops.
- A linear fade's corners step the slope: for an 8 dB Wide bypass fade over 20 ms on the click
  gate's 330 Hz sine that is about −61 dB (computed, not measured); the raised cosine measures
  −76.7 dB.

## Listen

Output; Removed (both bands); Ess removed; Harsh removed; Ess trigger and Harsh trigger: the
detector's band signal (after Key HPF, routing and Mid/Side), delayed by the latency, one key path
per channel, one path on both, two on a mono bus as their mean. Harsh trigger is silent while Harsh
is off. *Measured*: Output + Removed equals the aligned input within 10⁻⁶, Removed is Ess removed +
Harsh removed bit for bit, the triggers are the detector's band filters bit for bit.

## Constants

| Constant | Value | Why |
|---|---|---|
| chunk | 256 samples | scratch buffers fixed in size, so any host block works without allocation |
| frame (Normal latency) | 512 at 44.1/48 kHz, see *Latency* | PLAN; must hold look-ahead + hop |
| depth snap | 10⁻⁶ | identity returns after a release; a step of −120 dB |
| shape, stereo, Listen fade | 30 ms raised cosine | PLAN (Shape); the others alike |
| bypass fade | 20 ms raised cosine | M0's time |
| latency change | 10 ms out, 10 ms in | short; the host re-reads the latency on its own |
| Output | 2 × 8 ms one-pole | settles to 0.1 % in about 75 ms |
| shapes' input clamp | ±10⁶ (+120 dBFS), NaN and Inf as 0 | far beyond any signal; the shapes stay linear up to it |

## Measured (M2 core, 2026-11-14)

`HaliteTests_reduction`, 48 kHz unless stated.

- **Identity: pass.** Vowels, sines (110, 440, 1000 Hz), pink noise under the threshold (both bands
  at 0 dB; it reads about −5 dB) and silence, in Split, Wide and Spectral (Split in Live), every
  stereo mode, Normal and Live, Harsh off and on, mixed block sizes: the aligned input bit for bit,
  no event. Also a mono bus with a stereo key, Shape and Stereo changes with denormals in the input,
  and processor-level runs at 44.1–192 kHz (both Ranges 0).
- **Reduction accuracy, Wide: the chain applies what the detector asks.** What B loses is the
  detector's mean reduction within 0.03–0.07 dB. **End to end the gate fails by 0.06–0.07 dB:**
  the ess comes out 0.56–0.57 dB under the threshold, because the shaped reduction is about
  0.5 dB larger than the mean target (instant attack, look-ahead maximum and a slower release
  follow the peaks of S's 0.45 dB ripple: 0.54 dB over the target at Speed Auto, 0.17 dB with a
  fixed 5 ms release, 0.65 dB with 200 ms). That is Speed's, M3.
- **Split near Nyquist: pass** (`split/NOTES.md`): the real filter within 0.5 dB of the analog bell
  and shelf up to 0.45 · fs, worst +0.283 dB (bell) and −0.134 dB (shelf) at 44.1 kHz, +0.162 dB
  for the Harsh bell; at most +0.290 dB at any rate 44.1–192 kHz.
- **Reduction accuracy, Split: fails** (the bell's shape; `split/NOTES.md`, *Options*): the ess comes
  out +0.19, +0.93, +2.63, +4.76 dB over the threshold at 3, 6, 12, 18 dB over (Range 8, 8, 24, 24),
  against +0.45, +1.40, +3.58, +6.12 with the provisional bilinear bell; 3 dB over passes.
- **Harsh accuracy: fails from about 9 dB over** (the bell, in Split and Wide alike): +0.13 / +0.37 dB
  at 3 / 6 dB over (Range 4: pass), +0.77 at 9 dB and +1.36 at 15 dB over (Range 12).
- **Collateral: fails** for both bands outside their bands, a little more than with the cramped
  bilinear bell (Ess at 8 dB: −0.19 dB at 1 kHz, −0.99 at 2.25 kHz; `split/NOTES.md`).
- **Clicks, Normal: pass**, worst −61.1 dB (Wide, Stereo through all four modes; −74.6 dB since
  each lane reads its own mode's reductions, *After the review*); Spectral at worst −75.3 dB; Shape −76.1, Detection −76.1, Bypass −76.7, Output −70.8, Threshold
  −73.6, Range −74.9, Detect −73.4, Look-ahead −74.6, Harsh −78.0, Listen −75.2; the sines' own
  −78.8. On a 330 Hz sine Split's band cut is almost invisible (0.01 dB there), so for Split the
  gate checks the mechanisms, not the cut.
- **Clicks, Live:** every case under −60 dB but Wide with Detect Low and High automated every block
  (−59.2 dB): without look-ahead a target that jumps lands within two hops (*Open*).
- **Latency:** reported equals measured, every shape and rate 44.1–192 kHz, both settings.
- **Blocks:** 1, 32, 33, 4096 and mixed sizes give the 512-sample render bit for bit (Split, Wide,
  Spectral, Normal, Live, Harsh on, Unlinked).
- **Allocation:** none in `processBlock` with every parameter and the latency setting changing,
  nor in the chain with every cue, shape, Listen, bypass, block length 1–4096 and a transport that
  starts, stops and jumps.
- **Spectral** (`spectral/NOTES.md`, *Measured*): identity, latency, blocks, collateral, clicks and
  the Warm-up gate pass; accuracy as Wide's up to 12 dB, short of the deepest cuts.
- **CPU** (`taskset -c 2 HaliteTests_reduction "[.perf]"`, 512-sample blocks, load average 4.8;
  detector included; the matched Split filter): 48 kHz stereo Split Linked 0.338 %, with Harsh
  0.542 %; Wide 0.227 / 0.428 %; Unlinked about the same; Mid 0.192 / 0.292 %; a Shape crossfade
  running all the time 0.612 % (Harsh on). Split's worst cases: an ess all the time 0.370 %, Detect
  swept every block with it 0.416 % (`split/NOTES.md`, *CPU*). 96 kHz: Split 0.626 / 0.983 %,
  crossfading 1.126 %, worst 0.758 %. Budget (≤ 0.5 %, Harsh ≤ +0.3 %, fade ≤ 2 %, doubled at
  96 kHz): within.
- **Mutations:** eight single-line mutations of the chain, lanes, track, fades and filter (Live's
  hop offset, the hop index, the side's decode sign, the Unlinked index, the output's exact
  comparison, the raised cosine, the depth snap, the bell's mix) each fail at least one test; the
  exact comparison survived until the identity test through crossfades with denormals was added.
  The matched filter: 13 of 14 caught (`split/NOTES.md`).

## Measured (M2 gates, 2026-11-14)

The gates M2 still owed after its three builds: the reduction gates on real material, the golden
renders, the family gates through the processor and the CPU budget at every rate. No DSP changed in
this round (the budget holds everywhere, so nothing was optimised).

### Real material (`HaliteMeasure --reduction`)

Every event the detector finds (on the input, with its plugin constants and the default settings,
Voice, Speed Auto), run through each shape; per event, on the energy of the detector's own band
filter (`tools/measure/Reduction.cpp`):

- *asked*: what the shaped reduction takes off that energy over the event; *lost*: what the output
  lost. Lost − asked is the chain's **fidelity**.
- *landing*: over the event's hops whose target is over 0.5 dB, the energy the output kept against
  the energy cut hop by hop by exactly the target. 0 puts the ess at the threshold, Range permitting:
  the accuracy gate's reading on material whose esses do not hold still. It includes the detector's
  ballistics (the shaped reduction against the target), as the synthetic gate does.
- *below* / *above*: of the third octaves from 200 Hz to a third of an octave under the band, and
  from a third over it to 0.45 fs, the one that changed most during the event (Hann spectra of about
  43 ms): the collateral gate's reading.
- *reach*: the share of the band filter's energy in bins outside the band (the filter's 12 dB/oct
  skirts count it, no in-band cut touches it), and so the most a cut inside the band, at most Range
  per bin, can take off that energy.
- The Harsh band: the same per episode (hops it cuts by more than 0.5 dB), leaving out episodes with
  any Ess depth the chain applies within 60 ms (first built with 20 ms and Ess over 0.01 dB: Spectral's
  Harsh then read up to −4.5 dB *above* its band, all of it 23–106 ms from an Ess release tail, which
  in Spectral still cuts the Ess band's loudest bins; with the wider rule that reading is 0.000 dB).
- Listen Removed: two renders, Output and Removed, Harsh on, against the aligned input.

The m1 tune split, each speaker's recordings back to back (`--stream --jobs 2`, 74 s, 0.55 GB at
most); the gate wants every event within ±0.5 dB (accuracy) and 0.1 dB (collateral). Median [p5,
p95], share within the gate:

| VCTK tune (2409 events, 2925 s) | Lost − asked | Landing | Below / above the band |
|---|---|---|---|
| Wide | −0.07 [−0.32, +0.01], 97.9 % | −0.26 [−1.12, +0.07], 72.5 % | (Wide cuts everything) |
| Split | −0.93 [−3.01, −0.17], 26.5 % | +0.64 [−0.09, +3.25], 42.6 % | −0.45 / −0.64 dB median, worst −1.90 / −1.81; 5.7 / 5.2 % |
| Spectral | −0.46 [−3.92, −0.02], 52.3 % | +0.18 [−0.55, +4.32], 56.5 % | 0.000 on every event |
| Spectral within its reach (1301) | −0.22 [−0.80, −0.01], 83.2 % | −0.05 [−0.62, +0.67], 81.4 % | 0.000 |

- **Reach:** during VCTK's esses a median 20.1 % of the band filter's energy lies outside the band
  (p90 74.2 %, /ʃ/-like and low events nearly all of it); 46 % of the events ask more than an in-band
  cut can take (reach median 4.8 dB, p10 1.1 dB). That is *Open* 4's "deepest cuts", and on real
  speech it is not a corner case: it is most of Spectral's shortfall. Within its reach Spectral
  loses what is asked within 0.5 dB on 83 % of events (synthetic: within 0.07 dB); what holds the
  rest back (the Voice floor, the frame's depth at its centre) was not separated.
- **Split** loses about 70 % of the ask in dB (the bell's shape, *Open* 3), and its skirts reach
  outside the band during 94 % of the events.
- **Wide** applies what is asked within 0.5 dB on 97.9 % of events (worst −1.04 dB, not analysed
  further); it lands 0.26 dB under the threshold at the median, Speed Auto's ballistics.
- **VocalSet tune** (18 events in 1431 s of belted and sung vowels): Wide −0.10 / −0.06, Split −0.41 /
  +0.26, Spectral −0.28 / +0.15 dB (lost − asked / landing medians); Spectral's collateral 0.000.
- **Harsh band** (VCTK 669 episodes, 1037 left out near esses; VocalSet 666, 8 left out): Split and
  Wide (the same bell) lose 0.69 / 0.58 dB less than asked at the median and land +0.51 / +0.42 dB
  over; their skirts move the outside by −0.19 / −0.23 dB at the median. Spectral −0.40 / −0.27 and
  +0.31 / +0.15, collateral 0.000; 60.9 % (VCTK) of the Harsh band filter's energy lies outside its
  1-octave band, so 70.6 % of episodes ask more than an in-band cut can take; within its reach, lost −
  asked −0.06 dB (99.0 % within 0.5 dB).
- **Listen Removed: pass.** out + removed = dry within 2.98·10⁻⁸ (−150.5 dBFS, half an ulp of the
  loudest sample) on 140 M samples per shape (VCTK) and 63 M (VocalSet); 0.2–2.3 % of samples not
  bit-exact (out = dry − removed is rounded once).
- **Committed tier** (40 events, 34.5 s, cold starts): Wide −0.05 / −0.21 dB (100 % / 87.5 %), Split
  −0.95 / +0.72, Spectral −0.32 / +0.11 (within its reach, 19 events: −0.15 / −0.07, 94.7 %); 52.5 %
  ask beyond Spectral's reach. Live (Split, Wide): Wide −0.13 / +0.02 dB, 95 % / 82.5 %. Allround
  (−36 dBFS): Wide −0.04 / −0.31 dB. Null test pass in all three.

### Golden renders (`tests/reduction/GoldenTests.cpp`, `[golden]`)

Two committed excerpts back to back (VCTK p251 251, then p225 033; 6.06 s, 48 kHz mono) through the
chain in every Shape × Detection × band (Ess only, Ess and Harsh): 12 files in `tests/golden`, each
two half-second windows of the output (2.0 s: the Harsh band acting, Spectral's target still
learning; 5.0 s: the second speaker's strongest esses), 192 KB each, 2.3 MB in all, as Mulberry's.
Fixed settings, not the defaults (Voice −17.6 dB, Allround −36 dBFS, Range 8, Harsh −10 dB / −30
dBFS, Range 4, Speed Auto), so M3's defaults do not move them. **Recorded once, 2026-11-14, for the
sound M2 intends** (the matched Split filter, Spectral with its −8 dB/oct neutral curve).
The error is measured against what the golden render removes (the removed part is 20–40 dB under
the voice), gate −60 dB; the whole render runs every time. Each Ess-and-Harsh file differs from its Ess-only sibling
by −46 to −51 dBFS RMS. Changing Spectral's gain smoothing from 2.5 to 3.0 ms, the one mutation
of M2 Spectral no gate caught, fails it at −40 dB. A missing file fails (after the review: it only
warned). The files adapt CC BY 4.0 recordings and say so (`tests/golden/ATTRIBUTION.md`). Bit for bit on this machine; on Windows and macOS
(other compilers, FMA contraction, libm) not verified.

### Family gates (`HaliteTests "[family]"`, through the processor, reducing)

- **Channel layouts: pass.** Mono and stereo main, key off, mono or stereo, every shape: the voice
  is cut in its esses and untouched before them; with Key internal, white noise on the key bus
  changes nothing (bit for bit against a silent key); with Key external an esses-free main bus is
  cut where the key has its esses, and with no key bus it falls back and removes nothing.
- A stereo bus carrying one signal twice gives the mono bus's output on both channels **bit for
  bit**, Linked and Unlinked, every shape (Spectral Linked judges both channels' summed power).
- **Rates: pass.** Latency reported equals measured through the processor, every shape, 44.1–192
  kHz, Normal and Live; accuracy across rates (`HaliteTests_reduction "[rates]"`, 6 dB over, Range 8):

  | Rate (kHz) | Wide: asked / lost / landing | Split | Spectral |
  |---|---|---|---|
  | 44.1 | 6.56 / 6.54 / −0.57 | 6.56 / 5.10 / +0.91 | 6.56 / 6.56 / −0.56 |
  | 48 | 6.55 / 6.52 / −0.56 | 6.55 / 5.09 / +0.93 | 6.55 / 6.52 / −0.57 |
  | 88.2 | 6.66 / 6.63 / −0.64 | 6.66 / 5.15 / +0.83 | 6.66 / 6.60 / −0.59 |
  | 96 | 6.63 / 6.58 / −0.56 | 6.63 / 5.10 / +0.87 | 6.63 / 6.59 / −0.52 |
  | 176.4 | 6.56 / 6.55 / −0.51 | 6.56 / 5.05 / +0.98 | 6.56 / 6.48 / −0.47 |
  | 192 | 6.45 / 6.44 / −0.45 | 6.45 / 4.97 / +1.02 | 6.45 / 6.44 / −0.46 |

  (dB; "asked" is the detector's mean reduction, "lost" what B lost, "landing" S over the threshold
  out against the gate's expectation.) The gate: Wide and Spectral lose the ask within 0.1 dB, every
  shape's lost − asked within 0.1 dB and its landing within 0.15 dB of 48 kHz's.
- **Block sizes: pass.** 1, 2, 3, 64, 333, 4096 and a mixed run give the 512-sample render bit for
  bit through the processor, every shape, Normal and Live, Harsh on, Unlinked, stereo key.
- **State: pass.** A restored state (a non-default setting, every shape, Normal and Live) renders bit
  for bit what the saved instance rendered.
- **Bit-identical offline renders: pass.** A second instance, a second prepareToPlay and a reset()
  each render the first render bit for bit, every shape, Normal and Live, 44.1 and 96 kHz.
- **Every parameter is heard: pass** (new): each of the 25 parameters, with its mode on (Allround for
  the absolute thresholds, a fixed release for the releases, Harsh on for Harsh's), changes the
  output; the test also checks that it names every parameter. Without it, a parameter the processor
  never read passed every test (measured: look-ahead fixed at 5 ms survived the whole of
  `HaliteTests`).
- **Mutations:** 6 single-line mutations against these gates (reset() keeping the detector, the
  processor dropping the key, the latency mode not saved, look-ahead, Harsh release and Key HPF not
  read) are each caught; the look-ahead one only once the parameter gate existed.

### CPU (`taskset -c 2 HaliteTests_reduction "[.perf]"`)

% of one core, stereo, detector included, 512-sample blocks, fastest of 5 passes over 10 s; budget at
48 kHz (PLAN.md, Performance budget), ×2 at 88.2/96 and ×4 at 176.4/192 kHz; the crossfade rows ask
for a new Shape every 30 ms (two shapes running all the time: a lane fading or waiting to); "Worst"
rows run an ess, harshness, or both bands at once (an ess 0.4 s of every 0.5 s over a swelling
resonance: Harsh holds its cut through each ess, both cutting in 96 % of the blocks) all the time.
Re-measured after the review's fixes (load average 3.4–5.2): **all 168 readings within budget**; the
closest, Split with Detect swept every block over an ess that never stops, 84 % at 48 kHz (0.421 %);
both bands at once 0.564 % (Split, budget 0.8) and 0.919 % (Spectral Linked, 1.8).

| Case | Budget | 44.1 kHz | 48.0 kHz | 88.2 kHz | 96.0 kHz | 176.4 kHz | 192.0 kHz |
|---|---|---|---|---|---|---|---|
| Split, Linked | 0.5 | 0.327 | 0.343 | 0.595 | 0.631 | 1.127 | 1.209 |
| Split, Unlinked | 0.5 | 0.329 | 0.346 | 0.592 | 0.641 | 1.152 | 1.237 |
| Split, Mid | 0.5 | 0.188 | 0.197 | 0.341 | 0.367 | 0.659 | 0.705 |
| Split, Linked, Live | 0.5 | 0.325 | 0.340 | 0.582 | 0.627 | 1.110 | 1.187 |
| Wide, Linked | 0.5 | 0.227 | 0.233 | 0.388 | 0.417 | 0.730 | 0.776 |
| Wide, Unlinked | 0.5 | 0.229 | 0.236 | 0.394 | 0.426 | 0.755 | 0.806 |
| Spectral, Linked | 1.5 | 0.603 | 0.638 | 1.083 | 1.171 | 2.086 | 2.262 |
| Spectral, Mid | 1.5 | 0.352 | 0.370 | 0.609 | 0.654 | 1.144 | 1.235 |
| Spectral, Unlinked (two Stfts) | 2.5 | 0.706 | 0.741 | 1.208 | 1.306 | 2.279 | 2.466 |
| Harsh's share: Split, Linked | 0.3 | 0.193 | 0.201 | 0.327 | 0.352 | 0.630 | 0.662 |
| Harsh's share: Wide, Linked | 0.3 | 0.193 | 0.205 | 0.327 | 0.353 | 0.634 | 0.664 |
| Harsh's share: Spectral, Linked | 0.3 | 0.156 | 0.159 | 0.271 | 0.268 | 0.505 | 0.500 |
| Harsh's share: Spectral, Unlinked | 0.3 | 0.155 | 0.159 | 0.252 | 0.274 | 0.500 | 0.488 |
| Split, Linked, Harsh on | 0.8 | 0.521 | 0.544 | 0.914 | 0.986 | 1.758 | 1.868 |
| Spectral, Linked, Harsh on | 1.8 | 0.760 | 0.795 | 1.334 | 1.441 | 2.588 | 2.759 |
| Spectral, Unlinked, Harsh on | 2.8 | 0.862 | 0.901 | 1.461 | 1.575 | 2.775 | 2.968 |
| Crossfade all the time: Split <-> Wide, Harsh on | 2.0 | 0.599 | 0.649 | 1.101 | 1.195 | 2.183 | 2.320 |
| Crossfade all the time: Spectral <-> Split, Linked, Harsh on | 2.0 | 0.831 | 0.903 | 1.542 | 1.650 | 3.045 | 3.245 |
| Crossfade all the time: Spectral <-> Split, Unlinked, Harsh on | 2.5 | 0.910 | 1.000 | 1.659 | 1.774 | 3.220 | 3.436 |
| Worst: Split, an ess all the time, Harsh on | 0.8 | 0.549 | 0.574 | 0.956 | 1.021 | 1.773 | 1.917 |
| Worst: Split, harshness all the time | 0.8 | 0.457 | 0.478 | 0.794 | 0.848 | 1.492 | 1.586 |
| Worst: Split, both bands cutting | 0.8 | 0.539 | 0.564 | 0.933 | 1.007 | 1.767 | 1.884 |
| Worst: Split, Detect swept every block, an ess all the time | 0.5 | 0.401 | 0.421 | 0.715 | 0.771 | 1.291 | 1.364 |
| Worst: Wide, Detect swept every block, an ess all the time | 0.5 | 0.257 | 0.265 | 0.424 | 0.455 | 0.777 | 0.823 |
| Worst: Spectral, Linked, an ess all the time, Harsh on | 1.8 | 0.853 | 0.894 | 1.442 | 1.554 | 2.697 | 2.905 |
| Worst: Spectral, Unlinked, an ess all the time, Harsh on | 2.8 | 1.004 | 1.047 | 1.610 | 1.731 | 2.920 | 3.157 |
| Worst: Spectral, Linked, both bands cutting | 1.8 | 0.872 | 0.919 | 1.584 | 1.713 | 3.094 | 3.349 |
| Worst: Spectral, Unlinked, both bands cutting | 2.8 | 0.944 | 0.994 | 1.684 | 1.817 | 3.261 | 3.539 |

The crossfade rows fell from 1.143 / 1.287 % (48 kHz, before the review) to 0.903 / 1.000 %: a
Spectral lane no longer replays three frames when it starts. The same run reports each case's
costliest 512-sample block, as a share of the block's duration (in wall time, the smallest of the
five passes' largest): at most 1.44 % at 48 kHz, 3.39 % at 96 kHz and 10.06 % at 192 kHz (Spectral
Unlinked, whose every block runs a frame there); as first built, a crossfade row's block that started
Spectral ran 12 frames at once.

## After the review (2026-11-14)

Three reviews of M2; the findings that changed code (`PLAN.md`, *Measured status*):

- **Spectral starts without catching up.** A lane that starts Spectral replayed three frames of
  input in one block, and a transport start or jump did the same for every Spectral lane: 12 frames
  per signal, 132 µs (10 %) of a 64-sample block at 48 kHz and 539 µs (162 %) at 192 kHz, where the
  ordinary blocks took 7 µs (the reviewer's probe, rebuilt and run here before the change). Now a new
  lane is handed the frame of input before it and heard one frame later; a restart keeps the engine
  that ran for the frame in flight, on its old grid and track, and starts a second one on the new
  grid (`spectral/NOTES.md`, *Crossfades*, *The timeline*). The same probe after it: 20.9 µs (1.5 %)
  and 68.6 µs (20 %), a transport start 18.4 and 55.2 µs (against 129 and 508). `"[.perf]"` now has
  a report of every such event in 64-sample blocks (wall time, median of the events; the ordinary
  blocks' mean, p99, largest):

  | Event | 48 kHz: µs, % of the block | 96 kHz | 192 kHz | ordinary blocks, 192 kHz |
  |---|---|---|---|---|
  | Shape: Split to Spectral | 13.6, 1.0 % | 13.4, 2.0 % | 21.3, 6.4 % | 8.8 / 49.1 / 94.7 |
  | Stereo in Spectral: Linked to Unlinked | 18.9, 1.4 % | 16.7, 2.5 % | 32.5, 9.8 % | 11.5 / 94.5 / 165.1 |
  | Transport start (Spectral) | 17.0, 1.3 % | 15.2, 2.3 % | 23.2, 7.0 % | 9.8 / 49.0 / 96.2 |
  | Transport jump (Spectral) | 14.4, 1.1 % | 13.7, 2.1 % | 56.8, 17.1 % | 9.7 / 47.4 / 86.4 |
  | Latency: Live to Normal (Spectral) | 10.0, 0.7 % | 11.0, 1.6 % | 15.0, 4.5 % | 8.2 / 46.8 / 66.0 |

  A deterministic gate holds it in CI: no 64-sample block runs more than one frame per signal and
  engine (`"[spectral][realtime]"`: at most 4 at 48 and 192 kHz through every such event; a replay of
  one frame at the start reads 12 and fails).
- **Stereo changes cross-fade as designed** (*Crossfades*): Normal's worst click is now −74.6 dB
  (Wide, Stereo) instead of −61.1, and the in-band envelope of a Stereo change −45.6 / −45.0 /
  −48.7 dB (Split / Wide / Spectral) instead of −28.2 / −28.0 / −44.8, at the level of the detector's
  own ripple (−47.5 / −46.9 dB with nothing changing). One cell got worse: **Live, Wide, Stereo**
  −64.1 dB (it read at most −69.4 dB before; gate −60): in Live a reduction that is still releasing
  when its mode ends is held, and the hold bends the gain's slope over the spline's two hops right
  at the output (traced: the right channel's depth 0.0114, flat, where it was falling by 1.4·10⁻⁵ a
  sample). Normal has the same corner, −73.5 dB.
- **The shapes' input** is clamped at ±10⁶, not ±16 (*The shapes*).
- **A change of latency taken back before the fade-out ends** fades back in (*Latency*).
- **Gates:** every row that meets a plan's gate is a hard check again; a row that does not is held to
  its measured value (0.1 dB, clicks 0.2 dB, of margin for other compilers' rounding) and checked
  against the gate in a `[!mayfail]` case of its own (accuracy, Harsh accuracy, Split's and the
  Harsh bell's collateral, Live's jumping target). Spectral's collateral gate puts noise in the band
  on the main input, so the cut is real (the band loses 6.2 dB; this NOTES said 7.0 until M2b, which
  re-ran the report: Spectral's code and golden renders are unchanged bit for bit), and reads 15119 and 19000 Hz too:
  at most 0.007 dB outside the band (3572 Hz). A missing golden file fails. Split and Wide join the Warm-up
  gate (`spectral/NOTES.md`, *Measured*). Each new check was run against the code it guards with that
  code broken (lanes reading every mode, the ±16 clamp, the old fade-in, a replay at the start, a
  missing golden): each failed.

| Clicks after the review (dB) | Split N | Wide N | Spectral N | Split L | Wide L |
|---|---|---|---|---|---|
| Shape: the other two in turn | −77.7 | −76.8 | −77.7 | −77.6 | −76.7 |
| Detection Voice ↔ Allround | −78.7 | −76.1 | −78.8 | −78.5 | −69.4 |
| Stereo through all four | −78.6 | −74.6 | −78.9 | −78.1 | −64.1 |
| Bypass on and off | −78.8 | −76.7 | −78.9 | −78.7 | −76.7 |
| Output, every block | −71.1 | −70.8 | −71.1 | −71.1 | −71.2 |
| Threshold, every block | −78.8 | −73.6 | −78.9 | −78.7 | −72.5 |
| Range, every block | −78.8 | −74.9 | −78.9 | −78.8 | −74.5 |
| Detect Low and High, every block | −78.5 | −73.4 | −78.9 | −76.7 | −59.2 |
| Harsh Threshold, every block | −78.9 | −78.9 | −78.8 | −78.9 | −78.9 |
| Look-ahead, every block | −78.8 | −74.6 | −78.9 | −78.7 | −72.9 |
| Harsh Freq, Width and Range, every block | −78.0 | −78.0 | −78.8 | −78.0 | −78.0 |
| Harsh on and off | −78.7 | −78.7 | −78.8 | −77.3 | −77.3 |
| Listen Output ↔ Removed | −75.2 | −76.8 | −75.3 | −75.1 | −74.6 |
| nothing changes | −78.8 | −76.3 | −78.9 | −78.7 | −72.9 |

In the band (the 6 kHz envelope, Normal, Split / Wide / Spectral): Stereo −45.6 / −45.0 / −48.7,
Detect swept every block −42.4 / −40.6 / −47.7, Detection −45.2 / −44.5 / −56.9, nothing changing
−47.5 / −46.9 / −64.5 dB (`"Clicks: report"`).

## Not verified

- Hosts: no DAW run (Reaper, Bitwig, Ardour); pluginval at strictness 10 is the only host-like check.
- Rates other than 44.1–192 kHz were checked only by `frameSize`'s arithmetic.
- The golden renders on Windows and macOS: recorded and compared on Linux (GCC) only.
- The perf budget and the block costs on any other machine: one machine, pinned to one core, at load
  average 3–5.2; the block costs in wall time.
- Whether the Live hold corner (Stereo, −64.1 dB) is audible: no listening.

## M2b (2026-11-14)

Split's steep cut (Settled 38, `split/NOTES.md`) in Split and as the Harsh bell in Split and Wide, and
the gates re-graded as settled. No other shape's sound changed: Spectral's and Wide's Ess-only golden
renders pass byte for byte.

- **Accuracy** (Settled 39; `"[accuracy]"`): Split and Spectral on the band's own power (the bins of
  Detect Low … High weighted by the detector's band filter, against what they lose under the detector's
  reduction applied exactly: Wide's render of the same input), the landing on B reported; Wide on the
  landing. dB, own band lost − asked / landing:

  | Over / Range | Wide | Split | Spectral |
  |---|---|---|---|
  | 3 / 8 | 0.00 / −0.56 | −0.00 / −0.53 | +0.19 / −0.52 |
  | 6 / 8 | 0.00 / −0.56 | −0.01 / −0.51 | **+0.61** / −0.57 |
  | 12 / 8 | 0.00 / +0.00 | −0.01 / +0.08 | −0.01 / +0.74 |
  | 6 / 24 | 0.00 / −0.56 | −0.01 / −0.51 | +0.58 / −0.51 |
  | 12 / 24 | 0.00 / −0.57 | −0.02 / −0.39 | **+4.29** / −0.63 |
  | 18 / 24 | 0.00 / −0.57 | −0.04 / −0.07 | **+5.44** / +3.95 |

  Split passes every row (M2b reads Split as Settled 39 reads Spectral, for the user to confirm;
  `split/NOTES.md`, *Options* 1). **Spectral fails** the own-band gate at 6 dB over (by 0.08–0.11 dB) and
  at the deep cuts: its water-fill makes **B** lose the reduction, the ess's skirts past the edges
  included (it leaves them alone), so the band itself is cut deeper than asked, up to Range per bin
  (`spectral/NOTES.md`, *M2b*). Wide's landing misses by 0.06–0.07 dB as before (Speed, M3).
- **Harsh** (Settled 40, `"[harsh]"`): the Harsh band's power (through its band filter) against the
  detector's reduction, every shape within 0.08 dB (Split/Wide −0.03 … −0.08, Spectral −0.02 … +0.07);
  H reported (Split/Wide −0.14 … +0.05, Spectral −0.51 … +0.01). On real material Spectral **fails** it
  (*Real material* below).
- **Collateral:** Split 0.012 dB at most outside the band at 8 dB, the Harsh bell 0.016 dB at 4 dB:
  **pass**, hard gates now; on the near-Nyquist grids within 0.066 dB to 16 dB of cut at every rate, and
  **fail** at 24 dB for a top-octave band at 44.1 / 48 kHz (−0.146 / −0.190 dB near 0.45 · fs; after the
  review, `split/NOTES.md`, *The edges*).
- **Clicks** (`"[clicks]"`): every case at or under −60 dB in Normal and Live but Live's jumping target
  (Wide, Detect automated every block, −59.2 dB), which Settled 40 accepts: reported, held to its value.

  | Clicks, M2b (dB) | Split N | Wide N | Spectral N | Split L | Wide L |
  |---|---|---|---|---|---|
  | Shape: the other two in turn | −76.5 | −76.7 | −76.4 | −77.5 | −76.6 |
  | Detection Voice ↔ Allround | −78.7 | −76.1 | −78.8 | −78.4 | −69.4 |
  | Stereo through all four | −78.7 | −74.6 | −78.9 | −78.3 | −64.1 |
  | Detect Low and High, every block | −78.4 | −73.4 | −78.9 | −76.6 | −59.2 (accepted) |
  | Harsh Freq, Width and Range, every block | −78.7 | −78.7 | −78.8 | −78.7 | −78.7 |
  | Harsh on and off | −78.7 | −78.7 | −78.8 | −77.1 | −77.1 |
  | Listen Output ↔ Removed | −75.2 | −76.8 | −75.3 | −75.1 | −74.6 |
  | nothing changes | −78.8 | −76.3 | −78.9 | −78.6 | −72.9 |

  The rest as M2's table. In the band (the 6 kHz envelope, Split / Wide / Spectral): nothing changing
  −46.7 / −46.9 / −64.5, Detect swept every block −42.5 / −40.6 / −47.7, Stereo −44.8 / −45.0 / −48.7,
  Harsh on and off −55.2 / −55.2 / −152.8 dB. As first built with the study's straight-line
  coefficients, Live Split read −49.1 dB (Detect every block) and Harsh on and off −58.2 dB (Split,
  Wide): `split/NOTES.md`, *Modulation*.

- **Real material** (`HaliteMeasure --reduction`, m1 tune split, which now reads the own band too):

  | VCTK tune (2409 events) | Own band: lost − asked | Lost − asked (B) | Landing | Below / above the band |
  |---|---|---|---|---|
  | Wide | +0.00 (exact by construction), 100 % | −0.07 [−0.32, +0.01], 97.9 % | −0.26 [−1.12, +0.07], 72.5 % | (Wide cuts everything) |
  | Split | −0.00 [−0.01, +0.02], 100 % | −0.34 [−2.56, −0.05], 64.1 % | +0.01 [−0.66, +2.74], 65.5 % | +0.006 / −0.000 median, worst +0.35 / +0.05; 99.3 / 100 % |
  | Spectral | +0.38 [−0.08, +4.19], 54.3 % | −0.46 [−3.92, −0.02], 52.3 % | +0.18 [−0.55, +4.32], 56.5 % | 0.000 on every event |

  The Harsh band (669 VCTK / 666 VocalSet episodes): own band Split/Wide +0.00 / −0.01 dB median, 100 %
  within; Spectral +1.15 / +0.57 dB (27.5 / 46.8 %): **fail**, its water-fill over-cutting the band, as
  on the Ess band, for the Harsh filter's skirts (61 % of its energy outside the band during VCTK's episodes; `spectral/NOTES.md`,
  *M2b*). Collateral: Split within 0.1 dB on 99.3 % of VCTK's events, the Harsh bell (Split and Wide)
  on 98.7 / 94.3 % of episodes: **fail** as written, the outliers in near-empty third octaves
  (`split/NOTES.md`, *Real material*; Spectral 0.000, pass). Listen Removed's null test passes (−147.0 dBFS at worst).
- **Golden renders:** the six that run Split's filter re-recorded once, for the intended sound change
  (`split/NOTES.md`, *Golden renders*); the other six untouched.

- **CPU** (`taskset -c 2 HaliteTests_reduction "[.perf]"`, load average 2.1 … 3.3): 168 readings
  within budget as M2b shipped, but the table missed Split Unlinked with an ess that never stops, which
  read 0.501 % and, with Detect swept, 0.507 % of 0.5: **over** (the M2b review). The stereo pair now
  runs in the two lanes of one SIMD register; after the review (`taskset -c 5`, load 3.1 … 4.8, 5
  Unlinked rows added) all 198 readings within: at 48 kHz Split Linked 0.368 %, Unlinked 0.401, Unlinked
  with an ess all the time 0.432 and swept 0.438 (88 %: the closest), Mid 0.284, Harsh's share 0.252
  (Wide 0.256), Split with Harsh on 0.619, Unlinked swept with Harsh on 0.700; Spectral and Wide as in
  M2 (0.642, 0.235). 192 kHz Split 1.315 / Unlinked 1.466 %. Table: `split/NOTES.md`, *CPU*.

## M3 (2026-11-14): Spectral's water-fill on the band's own bins

Settled 44: Spectral's water-fill leaves what the band filter's skirts pass from outside the band out
of its sum, for both bands, so the band's own bins lose the reduction (`spectral/NOTES.md`, *M3*).
Spectral's own-band rows now pass the accuracy gate as hard gates (−0.01 … −0.10 dB to 24 dB of cut);
its landing on B is reported, as Split's. Its four golden renders were re-recorded once for that
intended change; the other eight passed untouched and are byte-identical. No other shape changed.

## M3 (2026-11-14): Speed Auto

The detector's Ess Speed Auto now releases over a quarter of the time over threshold, 15 … 30 ms
(M1: half of it, 15 … 80 ms; `../detector/NOTES.md`, *M3: Speed*). No shape changed; what they render
did. **Accuracy passes for every shape and row:** Wide lands −0.42 … −0.43 dB from the threshold
(M2 −0.56 … −0.57) and −0.34 … −0.43 dB at 44.1 … 192 kHz, now gated at every rate; Split's landing
on B (reported) −0.39 … +0.08 dB (−0.53 … +0.08), Spectral's −0.19 … +0.15 dB where the skirts do not
bind. The two `[!mayfail]` cases for Wide's and Split's landing are gone (2 left: the landing on B
across rates, Split's deepest collateral near Nyquist). Clicks: the plan's gate unchanged in kind
(Normal worst −70.9 dB, Live's accepted −59.3, every other Live case ≤ −62.9); in the band 1–3 dB
higher (at rest Split −46.6, Wide −46.8, Spectral −61.7 dB). All twelve golden renders re-recorded
once for this intended change.

## M3 (2026-11-15): Learn and the defaults

No shape changed (`../detector/NOTES.md`, *M3: Learn*). `HaliteMeasure --reduction` now reads what each
labelled /s/ /z/ token's band energy over its label lost through each shape, and runs each speaker at
a threshold from a file (`--thresholds`, what Learn sets). At the new defaults (Threshold −17.0 dB,
Harsh −9.9 dB) and at each speaker's learned threshold the shapes deliver what the detector asks as
before: Split's own band 100 % of events within ±0.5 dB, Spectral's own band 96.7–97.4 %, Wide's
landing 75.8–79.2 % (reported), the Harsh bell 100 %, Listen Removed's null within 5.96·10⁻⁸, tune and
holdout. The median labelled token loses 0.8–1.7 dB of band energy over its label (Spectral the least,
Wide the most); at the thresholds Learn sets, 0.9–1.4 dB against the 2.4–3.2 dB its peak loses: the
rule Learn applies is read at the ess's peak. The golden renders run fixed settings and were not re-recorded.

## Open (for the user)

Settled 41–45 (after M2b, taken on the user's behalf) closed Split's reading, edges and collateral
(*Open* 1 as it stood), Spectral's own band (2, built in M3) and the in-band clicks (4, reported, not
gated); M3's Speed Auto closed Wide's accuracy. Left:

1. **Split's ringing** (`split/NOTES.md`, *Ringing*): for the listening pass.

Settled 40 closed M2's Live jumping target and the stereo hold (*After the review*), both as measured.
