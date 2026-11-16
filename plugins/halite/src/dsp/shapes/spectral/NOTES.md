# Spectral: the per-bin shape

M2 Spectral (2026-11-14): the third shape (`PLAN.md`, *Reduction*, Spectral), its `Stft`
(`../../common/Stft.h`), the timeline grids and the transport restarts it needs. It joins the
lanes the chain crossfades (`../NOTES.md`). Numbers marked *measured* come from
`HaliteTests_reduction` at 48 kHz unless stated; the tables come from its `"[.report]"` cases,
the corpus numbers from `"[.corpus][spectral]"` on the m1 tune split.

## What was built

| File | What |
|---|---|
| `../../common/Stft.h` | periodic Hann analysis and synthesis at 75 % overlap, 1 / 1.5, overlap-add of what to remove; an output no frame wrote to reads exactly 0 |
| `../../common/RealFft.h` | gained `inverse()` (the forward path is unchanged) |
| `SpectralShape.{h,cpp}` | `SpectralAnalysis`: level estimate, targets, gains, the learning ring; `SpectralShape`: one or two `Stft`s on the timeline's frame grid, Linked or Unlinked gains, and a second engine for the frame in flight across a restart; `SpectralTuning`: the smoothing and learning constants the tests vary |
| `../ShapeLane.{h,cpp}` | Spectral as the lane's third engine; `fill()` (the frame before its start), `ready()`, the restart, the target carried from lane to lane |
| `../../chain/ReductionTrack.h` | the detector's Ess targets per hop beside its reductions; `ReductionClock`, the two tracks across a transport restart |
| `../../chain/HaliteChain.{h,cpp}` | `Timeline`: the grids anchored at the first block, restarts on a transport start or jump; Spectral's start from the frame in the dry delay, heard once ready |
| `../../detector/Detector.{h,cpp}`, `NoiseCues.h` | `anchor()`: hops and the periodicity cue's decimation on the timeline's grid; no constant or result changed |
| `../../../plugin/PluginProcessor.cpp` | reads the host's play head into `Timeline` |

## Signal flow

The Normal latency is one frame, N (512 at 44.1/48 kHz, `HaliteChain::frameSize`). Before
sample t of the timeline enters, at every t that is a multiple of the hop N/4, the frame of the
last N samples is analysed; then sample t − N, whose frames are all done, leaves the overlap-add.
So the removed part of a sample comes out exactly N later, wherever the grid lies, lined up with
the dry delay. Per frame and per gain computer:

1. **Levels.** Each bin's power (Linked: both channels' summed), a one-pole over 10 ms, then the
   mean over a sixth of an octave: the level estimate `L`.
2. **Depths.** The chain's depth (the detector's reduction on the audio's clock) at the frame's
   centre, t − N/2, for the Ess band and the Harsh band. A frame at depth 0 in both has every gain
   exactly 1: nothing is synthesised and the overlap-add stays exactly 0.
3. **Gains.** The water-fill below, per band, against its target; a one-pole over 2.5 ms on the
   gains; bins outside the band have gain exactly 1.
4. **Removed.** Each bin takes the smaller of its two gains (overlapping bands never cut a bin
   twice); `(1 − g) X` goes to the band whose gain it is; each band's spectrum is inverted,
   windowed and overlap-added on its own (Listen's Ess removed and Harsh removed).

## The Stft

`Stft::analyse` windows the last N samples (periodic Hann) and transforms them; `synthesise`
inverts a spectrum, windows it again and adds it, scaled by 1 / 1.5, the sum of the two windows'
product over the four frames that overlap any sample (`../../detector/NOTES.md`, *STFT gain
processing*). `step` moves on: it hands out the oldest samples of each overlap-add and clears them.
Removing `(1 − c) X` in every frame removes `(1 − c) x` a frame later within 2·10⁻⁶ (*measured*,
grid offsets 0, 37 and 128); a gain of 1 in every frame removes exactly 0, because a spectrum of
zeros is never inverted. No allocation after `prepare()`; `RealFft` is M1's lock-free transform
with an inverse added (round trip within 10⁻⁵ at 16 … 2048 points).

## Timing

- **Frames on the timeline.** A frame ends on every multiple of the hop in timeline samples, so a
  render started mid-clip analyses the same frames (*The timeline*, below).
- **The depth at the frame's centre.** The detector's reduction for input time u is known
  `look-ahead + 31` samples after u (`../NOTES.md`, *Timing*); a frame ending at t needs its
  centre's, t − N/2, at t. With the look-ahead up to N/2 − 32 (4.7 ms at 48 kHz) it is there;
  beyond that the frame reads the newest sample the detector has surely reduced, the last of the
  hop ending at or before t − look-ahead. The block size therefore never changes the result
  (*measured*: 1, 32, 33, 4096 and mixed blocks bit for bit). At the default 5 ms (256 samples)
  that is one sample before the centre; at 10 ms, 255 samples: Spectral's reduction then starts
  5.3 ms later than the time-domain shapes' (and still ramps over the look-ahead).

## The gains: a water-fill against the target

The plan: in an ess, each bin above target + threshold is reduced to it, never by more than Range,
with the detector's reduction as the ceiling. Read with a ceiling per bin, that contradicts the
accuracy gate: if no bin may lose more than the detector's reduction c and the band must lose c
in all (the ess at the threshold), every bin must lose exactly c, which is Wide inside the band.
Read with no ceiling, cutting every bin above target + threshold to it overshoots whenever the ess
is flatter than the target (it is: the voice's top end falls by about 8 dB an octave) and
undershoots once Range binds. **Built:** the detector's reduction is what the band loses in all,
as in every shape (the key decides when and how much); Range is the ceiling of each bin; the
target decides which bins give it up:

- Each in-band bin's power gain is `r_k = clamp (λ T_k / L_k, lower_k, 1)`: bins that stick out
  of the target's shape the most are cut first and furthest, those under the line not at all.
- λ is found so that this frame's band, weighed by the detector's band filter (its bilinear
  4th-order response at each bin, `Levels.h`), keeps `(1 − depth)²` of its own power. What the
  filter's skirts pass from outside the band is left alone and not in the sum (M3, Settled 44; M2
  counted it, so the band lost more than asked: *M2b*, *M3*): bisection in log λ, then an
  interpolation in the last bracket, where the kept power is linear in λ. Only the target's form
  matters, not its level; λ never falls below target + threshold while Range does not bind.
- λ is solved on the frame's own power, the shape on the smoothed level: solved on the smoothed
  level, the gains were correlated with the frame's loud bins and the band lost 0.4 dB too much at
  12 dB (*measured*, first build); solved on the frame, each frame loses exactly what is asked and
  only a common factor moves from frame to frame (no musical noise from it).
- `lower_k` is Range (10^(−Range/10)) and, in Voice, the voice's own top end: no bin is cut below
  the target at the current body level (its 10 ms level over the detector's body band). It binds
  only where the band cannot lose what is asked from the bins that stick out (*Measured*, the side
  chain and the floor); without it such a frame cut every bin to Range, the voice's air with it.

Two consequences, *measured* in *M3*: (1) the band's own bins lose what the detector asks within
0.10 dB at every depth to 24 dB, Range permitting, as Split's steep cut does; (2) `B`, which also
hears the skirts, loses less, by their share: the test ess holds 3.55 % of its `B` outside the
band, so `B` loses 5.98 dB where the band loses 6.44, and at most 14.5 dB however deep the cut.

## Musical noise

A raw bin's power spreads by 5.6 dB (std) on noise (M0); gains computed from it jump by several
dB from frame to frame in random bins. Smoothed first, the level estimate holds still; the gains
then get a light one-pole. *Measured* (`"Spectral: musical noise, report"`, the water-fill of M3):
a steady noise ess 4.5 … 12 kHz over a vowel, 6 dB asked; the std over 2 s of each in-band bin's
gain in dB, the mean over the band's bins; the spread of the gains across the band in a frame (the
water-fill's own shaping); what `B` lost (less than the band, by the skirts' share, *The gains*):

| Level estimate, gains | gain std (dB) | spread (dB) | B lost (dB) |
|---|---|---|---|
| raw bins | 2.76 | 3.24 | 5.72 |
| 1/6 octave | 1.03 | 2.52 | 5.59 |
| 1/6 octave + 10 ms | 0.50 | 2.40 | 5.57 |
| **1/6 octave + 10 ms, gains 2.5 ms (built)** | **0.45** | 2.40 | 5.53 |
| 1/6 octave + 10 ms, gains 5 ms | 0.40 | 2.39 | 5.49 |
| 1/3 octave + 10 ms, gains 2.5 ms | 0.33 | 2.38 | 5.55 |
| 1/6 octave + 20 ms, gains 2.5 ms | 0.36 | 2.38 | 5.54 |

The plan's values (1/6 octave, about 10 ms, gains lightly) take 84 % of the jitter off; a third
of an octave or 20 ms would take a little more at the cost of resolution in frequency or time.
(M2's water-fill, which counted the skirts and cut the band deeper: 2.72 raw, 0.41 built, spread
2.15.) Not voiced: these are for the listening pass.

## The target (Voice)

- **What is learned.** Each frame's power spectrum over its body (the detector's body band,
  120 Hz … min (2 kHz, Detect Low / 2), summed), ERB-smoothed (Glasberg and Moore 1990), in dB.
  The target is the mean in dB of the last 2 s of learned frames: a geometric mean, so that a
  frame with an ess in it (30–40 dB over the voice's top end) moves 750 frames by a fraction of a
  dB. As first built, averaged in power, a few ess onsets per second set the target's slope to
  −5.1 dB/oct on a −12 dB/oct vowel; in dB, −11.9 (*measured*).
- **Which frames.** The detector called them non-sibilant: its Ess *target* (before ballistics,
  look-ahead and release) is 0 on every hop the frame spans and for 8 ms after it (the guard: the
  detector decides an onset a few ms late, `B`'s 4 ms averaging), and their body is voiced: within
  20 dB of the loudest body of the last second (1 s memory) and over −60 dBFS. As first built
  with the shaped reductions instead of the targets, the release tails (170–220 ms at Speed
  Auto) left 110 of 1500 frames learned with esses every 0.3 s; with the targets, 764. A frame is
  decided once every hop it spans and the guard are (one frame later plus the guard), so the
  result does not depend on the block size.
- **The ring.** Learned frames join the block of the timeline they end in (32 frames, 85 ms at
  48 kHz, blocks at multiples of 32 hops); a block closes when a later one starts and goes into a
  ring of at most 8 s of blocks; the target is recomputed then, from the newest blocks until they
  hold 2 s of frames, summed afresh: a finite memory, not an exponential.
- **Warm start.** Until the window holds 2 s of frames, the neutral curve stands in for the frames
  not heard yet (in dB, weighted by their number). Its slope and level were *measured* on the m1
  tune split (`"[.corpus][spectral]"`, once each speaker's window is full, every 0.5 s, the median
  per speaker): VCTK, 20 speakers, slope quartiles −11.27 / −7.76 / −6.09 dB/oct over
  4.5 … 12 kHz, the bin at 4.5 kHz −55.8 / −52.5 / −48.8 dB under the body, 35 / 37 / 41 % of
  frames learned; VocalSet sung (9 singers) −10.9 / −4.8 / −2.7 dB/oct, −52.4 / −46.8 / −41.4 dB,
  84–89 %. **Neutral curve: −8 dB/oct, −52.5 dB at 4.5 kHz** (speech, the VCTK medians; the
  holdout check, M3: −8.83 dB/oct, *M3: the constants*). In the dB mean only its slope shapes the
  cut; the level is there for the display. Frozen with the golden files.
- **Resets.** `prepareToPlay` and a transport start or jump (*The timeline*); a crossfade from
  one Spectral lane to another (a new Stereo mode) carries the ring over (*measured*: the target
  moves by 0 dB across a change to Unlinked or Mid).

## Allround

No voice: the target is the signal's own power, a one-pole over 300 ms, averaged over an octave.
The water-fill then trims bins that stick out of their neighbourhood; a smooth spectrum is cut
alike across the band. No floor (there is no voice to keep): *measured* in the floor test, the
vowel's harmonics under an impossible ask lose 25.8 dB in Allround, 1.1 dB in Voice. Its
exponential forgets its start by 60 dB in about 2.1 s.

## Harsh in Spectral

Its own reference, no memory, the same in Voice and Allround: per frame, the straight line in dB
over log frequency through the level estimate at the band's two edge bins; the water-fill cuts
what sticks out above the line until the Harsh band, weighed by the detector's Harsh band filter,
has lost the Harsh reduction (its own bins, the skirts not counted: Settled 44); Range per bin; no
floor. A resonance is cut and the edges are not (*measured*: −9.0 dB at 3.2 kHz, 0 at 2.3 and
4.45 kHz, 6 dB asked; M2 −10.6); white noise, whose line is flat, is cut alike (−5.9, −5.6, −5.7 dB
at 2.5, 3.2 and 4.2 kHz, each bin's mean over 0.5 s, against one frame's scatter of 1.4 dB; M2 cut
it to Range, 12 dB, for the skirts it counted); after
3 s of different histories the same sound gets the same Harsh gains within 10⁻⁶, while the
learned Ess target differs by more than 3 dB/oct.

## Stereo and the side chain

- **Linked:** one gain computer on both channels' powers summed, its gains on both spectra: each
  bin of both channels is cut alike (*measured*: an ess on the left only, Allround, the vowel's
  harmonics on the two channels within 0.006 dB of each other, cut by up to 8 dB). Halving a
  channel halves its output exactly, as in the time-domain shapes.
- **Unlinked:** two gain computers, reduction c on channel c; **Mid**, **Side**: one, on the
  component, decoded as `../NOTES.md` describes. A mono bus has one.
- **Side chain:** the key's detector gives the depths (when and how much); the main signal's own
  spectrum and its own learned target (frames the key's detector called non-sibilant) decide which
  bins. *Measured* (M3): the key's esses ask 10.25 dB; the main has a 9 kHz burst instead; `B`
  loses 9.43 dB, the burst 10.53 dB, the vowel's harmonics beside it at most 0.16 dB (M2, which
  counted the skirts: 10.30, 11.64, 0.17); without the key's esses the output is the aligned input bit
  for bit.

## Crossfades and Live

- A lane that starts Spectral (a Shape or Stereo change) is handed the frame of input before the
  change (the dry delay holds it) as the input its first frame analyses, and runs in real time,
  unheard: the samples from the change on get every frame they need one frame later, and only then
  does the lane fade in (`ShapeLane::ready`; at 48 kHz 10.7 ms). Its level estimate starts from 0 and
  its gains from 1 (two thirds of their way after one frame, 99 % after four), so the first frames
  heard cut a little less; the fade starts at 0 and they matter little. *Measured*: the old shape alone, exactly, for 513 samples after the
  change; right after the fade within 4.0–5.7·10⁻⁴ of a Spectral render from the start; once its
  target has learned 2 s of frames (3.7 s after the change on this signal), within 1.9·10⁻⁹. As first
  built, the lane ran its frames over the three frames of input before the change within the block
  of the change, 12 per signal (`../NOTES.md`, *After the review*).
- A change of latency switches lanes at once while the output is silent (`../NOTES.md`): Spectral's
  removed part would not line up with Live's undelayed audio during a fade. Into Spectral, the
  output stays silent until the lane is ready (*measured*: 1024 samples after the change, against
  512 for Split).
- **Live:** Spectral cannot run without its frame of latency; Split runs and `ChainStatus::fallback`
  says `live` (`../NOTES.md`).

## The timeline (PLAN.md, Warm start; gate Warm-up)

- **Anchoring.** The first block after `prepareToPlay` (or `reset()`) takes the host's position:
  the detector's hops end on its multiples of 32 and its periodicity cue decimates on multiples of
  its factor (7 at 44.1 kHz, 8 at 48 kHz: not a divisor of 32, so it needs its own phase);
  Spectral's frames end on multiples of the hop; the ring's blocks on multiples of 32 hops. The
  detector re-anchors its cues when a path restarts mid-run (another key or stereo routing). Event
  positions are timeline positions. Hosts without a position anchor at 0.
- *Measured*: a detector anchored at 1.0 s + 13 or 1.3 s + 1 samples gives every hop of a run
  from 0 bit for bit after 1.5 s (S and reductions 0 dB apart); not anchored, its reductions differ
  by up to 0.92 dB.
- **Restarts.** A transport that starts playing, or jumps while playing (a position more than 32
  samples from the last block's end: a host whose position strays by a sample must not restart the
  target every block), restarts the analysis there: the detector and its hops on the new grid,
  Spectral from the warm start. The audio in flight (the last frame before the restart) keeps the
  reductions the detector made for it: input times before the restart read the previous track at
  its old positions (`ReductionClock`); its last look-ahead (5 ms) holds the newest one, since the
  key that would have shaped those never came. Each Spectral lane plays the audio in flight from the
  engine that analysed it, which goes on for one frame on its old grid, learning nothing and reading
  no reduction after the restart; a second engine takes over its frame of input, starts on the new
  grid from the warm start, and is heard from the first new sample on (`SpectralShape::restart`). A
  second restart within the frame drops what the first one's old engine had left to add.
  *Measured*: playing from stop at 2 s + 77 (the position moving while stopped, as some hosts do)
  is a render started there bit for bit (Spectral and Split); after a stop that heard input (live
  monitoring) the first 2 s are within −88 dB of a render started there (Spectral; the restart
  resets what the stop taught it); a jump in the middle of an ess, from 1.45 s to 3 s + 5 (Range 24),
  plays the frame in flight bit for bit in Split for its first 256 samples and within −58.1 dB in
  all (the last look-ahead), within −37.8 dB in Spectral (its frames that reach past the jump hear
  the new audio; −34.7 dB when the restart replayed the frame on the new grid), then becomes the
  render from the new position bit for bit: Split after 50 ms, Spectral once its target has refilled
  (3.5 s).

## Constants

| Constant | Value | Why |
|---|---|---|
| frame, hop | 512 at 44.1/48 kHz (the Normal latency), N/4 | PLAN; `../NOTES.md`, *Latency* |
| windows | periodic Hann, analysis and synthesis, 1 / 1.5 | M0 (`../../detector/NOTES.md`) |
| level estimate | 10 ms one-pole on each bin's power, then 1/6 octave | PLAN; musical-noise table |
| gains | 2.5 ms one-pole | "lightly" (PLAN); table; confirmed in M3 |
| band | the bins from Detect Low to Detect High as the detector measures them (`Detector::bandsFor`) | PLAN |
| band weights | the detector's band filter's power response per bin | the gate reads `B` |
| Ess target, Voice | 2 s of learned frames, mean in dB, ERB-smoothed; blocks of 32 frames, ring ≤ 8 s | PLAN; Settled 30; this NOTES |
| learning | Ess target 0 over the frame and 8 ms after; body within 20 dB of the last second's loudest, over −60 dBFS | this NOTES; confirmed in M3 (*M3: the constants*) |
| neutral curve | −8 dB/oct, −52.5 dB at 4.5 kHz | m1 tune VCTK medians; holdout −8.83 dB/oct, −53.95 dB (M3) |
| floor (Voice) | the target at the current body level | the voice's air |
| Allround target | 300 ms one-pole, 1 octave | PLAN |
| Harsh reference | line through the level at the band's edge bins | PLAN |
| start | the frame of input before it, heard one frame later | the overlap-add: one frame |

## Measured (M2 Spectral, 2026-11-14)

`HaliteTests_reduction`, 48 kHz unless stated.

- **Identity: pass.** Every idle signal of `../NOTES.md`'s identity gate, now through the real
  Spectral, every stereo mode and block size: the aligned input bit for bit. Outside events,
  exactly unchanged: with one ess at 1.0 … 1.15 s (fixed 40 ms release) the output differs from the
  aligned input only between 0.992 and 1.715 s of input (the look-ahead's ramp less half a frame,
  the release's tail to its −120 dB snap and half a frame), bit for bit elsewhere, Harsh off or on.
- **Latency:** reported = measured at 44.1, 48, 88.2, 96, 176.4 and 192 kHz (one frame).
- **Blocks:** 1, 32, 33, 4096 and mixed sizes give the 512-sample render bit for bit (44.1 kHz,
  Harsh on, Unlinked).
- **The band loses what is asked** (noise inside the band, so its skirts hold nothing): 2.97, 7.96
  and 19.99 dB of `B` for 3, 8 and 20 dB; sines at 2 and 15 kHz move by under 0.004 dB.
- **Accuracy** (the gate: the ess at the threshold ± 0.5 dB, Range permitting; as the detector
  reads the output), and what `B` lost against the detector's mean reduction:

  | Over (Range) | Spectral out | B lost / reduction | Wide out | Split out |
  |---|---|---|---|---|
  | 3 (8) | −0.52 | 3.48 / 3.55 | −0.56 | +0.19 |
  | 6 (8) | −0.57 | 6.52 / 6.55 | −0.56 | +0.93 |
  | 12 (8) | +4.74 (gate +4) | 7.26 / 8.00 | +4.00 | +5.82 |
  | 6 (24) | −0.51 | 6.48 / 6.55 | −0.56 | +0.93 |
  | 12 (24) | −0.63 | 12.61 / 12.56 | −0.57 | +2.63 |
  | 18 (24) | +3.95 | 14.01 / 18.55 | −0.57 | +4.76 |

  Up to 12 dB the band loses the detector's reduction within 0.07 dB (gate, `"[accuracy]"`), and
  the ess ends where Wide's does: under the threshold by the detector's ballistics at Speed Auto
  (0.54 dB, `../NOTES.md`), **fail by 0.01–0.13 dB**, for M3. Range 8 at 12 over and 18 over
  **fail**: 3.55 % of the test ess's `B` lies in bins outside the band (its own skirts past the
  edges, through the detector's 12 dB/oct filter), so no in-band cut takes more than 14.5 dB off it
  (14.1 dB at Range 24).
- **Harsh accuracy** (the detector's `H`; in brackets what the band's power lost against the
  reduction): +0.13 → −0.36 dB at 3 over (3.21 / 3.17 dB), +0.01 at 6 over (Range 4), −0.51 at 9 over
  (9.25 / 9.17), +0.01 at 15 over (Range 12): the power is within 0.08 dB of the ask, but `H`, fast
  to rise and slow to fall, reads 0.15–0.25 dB more loss once the resonance is cut more than the
  band's edges (its crest changes). The gate as read **fails at 9 over by 0.01 dB**; Split and Wide
  +0.77.
- **Collateral: pass.** Ess at 8 dB: at most 0.002 dB anywhere outside the band (250 Hz … 3572 Hz,
  13 … 19 kHz; −6.05 dB at both edge bins, −8.00 at 7348 Hz); Harsh at 4 dB: at most 0.001 dB outside
  (−0.82 dB at its low edge, −4.00 at 3.2 kHz). Split: −0.99 dB at 2.25 kHz. Those sines sit outside
  the band, which held nothing on the main input to cut; the gate now puts noise in the band too
  (after the review: the band loses 6.2 dB of it as the detector's band filter weighs it; 7.0 as
  first written here, corrected when M2b re-ran the report) and reads
  15119 and 19000 Hz: at most 0.007 dB outside either band (3572 Hz; the Harsh band 0.006). The
  report, with its noise: at most 0.007 dB at 3572 Hz and below and at 13 kHz and above (−0.003 dB at 13 kHz, +0.001 at 15.1 kHz);
  −0.05 and −0.06 dB at 4350 and 12150 Hz, a twentieth of an octave and less out, where the window
  lobe of the sine reaches the first cut bin; the Harsh band at most 0.006 dB outside.
- **Clicks: pass**, Normal, every scenario of `../NOTES.md`'s gate with Spectral (and Shape changes
  into and out of it): worst −75.3 dB (Listen), the sines' own −78.8. On the gate's 330 Hz sines
  Spectral's band is untouched, so this checks the mechanisms. In the band, the envelope of a
  6 kHz sine (`"Clicks: report"`, 3rd-difference crest of the demodulated envelope, not the
  plan's gate): Spectral −64.4 dB with nothing changing (Split −47.5, Wide −46.9: the detector's
  hop-to-hop ripple on their per-sample gain), −47.2 at Shape changes, −48.7 at Stereo changes
  (Split −45.6, Wide −45.0; −44.8 and −28 before each lane read its own mode's reductions,
  `../NOTES.md`, *After the review*), −47.7 with Detect swept every block (Split −42.4, Wide
  −40.6), −48.3 with the Harsh band swept across 6 kHz (Split −65.5).
- **Warm-up: pass.** A singer with an ess every 0.55 s, Harsh on, Linked; renders started at
  2.0 s + 7, 2.6 s + 129 and 3.1 s + 300 samples: once the late render has learned 2 s of frames
  and a block (2.66–2.71 s after its start), its output is the full render's within −94.8 dB,
  −121.8 dB and bit for bit (gate −60); before that −32 to −35 dB. **Split and Wide** (after the
  review, `"[warmup]"`), Harsh on, on this singer and on the two committed VCTK excerpts of the golden
  renders (8 starts from 0.5 to 4.7 s): they learn nothing, but the detector they follow rebuilds its
  body level, holds and cues from silence. No 50 ms window differs from the full render by more
  than −60 dB of what it removes after 0.50 / 0.55 s (singer, Split / Wide) and 0.85 / 0.90 s (VCTK);
  bit for bit after 0.51 s (singer) and 1.11 s (VCTK); from 1 s on every start is within −159 dB or
  bit for bit (gate −60).
- **Timeline:** anchored, a late detector decides every hop as the full one (0 dB apart, 0.92 dB
  unanchored); playing from stop equals a render started there bit for bit; after a jump inside an
  ess the frame in flight is within −37.8 dB of the uninterrupted render (−34.7 dB as first built;
  Split −58.1 dB, bit for bit but its last look-ahead) and the render from the new position follows
  bit for bit (Spectral after 3.5 s).
- **Target:** learns −5.87 and −11.88 dB/oct on −6 and −12 dB/oct vowels (−8 before: the neutral
  curve), within 0.4 dB of a straight line on a steady vowel's harmonic comb; −11.89 dB/oct with
  an ess every 0.3 s (764 frames learned in 4 s); −11.19 dB/oct with a pause every other half
  second over a hiss 60 dB down (950 frames: the pauses are not learned). A detector that turns
  sibilant at a sample leaves exactly the frames ending 8 ms or more before it learned (297 of 297).
- **Side chain, floor, Linked, carry-over:** *Stereo and the side chain*, *The gains*, *The target*.
  Linked with an ess on the right only: the right's ess loses 8.6 dB, the left's vowel 12.7 dB at
  the ess's bins and 0.23 at 5.55 kHz (one set of gains, judged on both). Where the bands overlap,
  a bin with Harsh's smaller gain loses to the Harsh band only (Ess removes 0.004 of it).
- **Allocation:** none in the chain with every shape, Listen, cue and a transport that starts,
  stops and jumps (`HaliteTests`, `"[realtime]"`).
- **CPU** (`taskset -c 2 HaliteTests_reduction "[.perf]"`, % of one core, stereo, detector
  included, 512-sample blocks, load average 3.2–4.2): within budget.

  | | 48 kHz | 96 kHz | budget at 48 kHz |
  |---|---|---|---|
  | Spectral Linked | 0.639 | 1.175 | 1.5 |
  | the same, Harsh on | 0.801 | 1.442 | Harsh +0.3 |
  | Spectral Unlinked (two Stfts), Harsh off / on | 0.751 / 0.912 | 1.323 / 1.593 | 2.5 |
  | Spectral Mid | 0.370 | 0.652 | |
  | an ess all the time (every frame synthesised), Linked / Unlinked, Harsh on | 0.885 / 1.049 | 1.540 / 1.731 | |
  | Spectral ↔ Split, a crossfade all the time (each start replays 3 frames), Linked / Unlinked | 1.086 / 1.193 | 2.035 / 2.183 | 2 |

  (Split Linked in the same run: 0.340 %, 0.630 % at 96 kHz.) Re-measured after the review, every
  rate: `../NOTES.md`, *CPU*; the crossfade rows, a start now costing no replay, 0.903 / 1.000 % at
  48 kHz (Harsh on).
- **Mutations:** 26 single-line mutations of the new code (the `Stft`, the inverse FFT, the
  water-fill, the depth's clamp, the learning's guard, delay, voiced rule and ERB smoothing, the
  floor, Linked and Unlinked, the Harsh line, the overlap's attribution, the replay, the carry-over,
  the anchoring of hops and of the cue's decimation, the clock, the restart, Live's fallback, the
  targets on the clock): 14 were caught by the first tests; the 12 that survived each showed a gap,
  and with the tests added for them 25 are caught. The one left is the gains' 2.5 ms one-pole, a
  constant the musical-noise report measures (0.41 against 0.47 dB) and no gate pins. Among the
  gaps: Catch2 runs a section once per run, so the transport test's sections had never run for
  Split; rewritten without sections, Split's frame in flight measured −58.1 dB, not exact (the last
  look-ahead), which *The timeline* now says.

## Measured (M2 gates, 2026-11-14)

`../NOTES.md`, *Measured (M2 gates)*, has the tables; for Spectral:

- **Across rates:** at 44.1–192 kHz the band loses the detector's reduction within 0.08 dB (6 dB
  over, Range 8), and the ess lands within 0.11 dB of where it lands at 48 kHz (`"[rates]"`).
- **Real material** (`HaliteMeasure --reduction`, m1 tune split, 2409 VCTK events): collateral
  0.000 dB on every event and every Harsh episode. Fidelity falls short where the detector's band
  holds energy outside the band: during VCTK's esses a median 20 % of the band filter's energy lies
  in bins outside it (p90 74 %), and 46 % of the events ask more than an in-band cut can take (*Open*,
  the deepest cuts). Within its reach the band loses what is asked within 0.5 dB on 83 % of events
  (median −0.22 dB); the ess lands at −0.05 dB median.
- **Golden renders:** Spectral in Voice and Allround, with and without Harsh, frozen with the neutral
  curve (`tests/golden`); they catch the 2.5 ms gain smoothing (3.0 ms fails at −40 dB against −60),
  which no gate pinned before.

## M2b (2026-11-14): gated on the band's own power

Settled 39 gates Spectral on the band's own loss: the band's power loses the reduction within ±0.5 dB,
Range permitting, and the landing on B (which also hears the ess's skirts past the edges) is reported.
M2b reads "the band's power" as the power in the band's own bins (Detect Low … High, each weighted by
the detector's band filter, as the water-fill weighs them), against what those bins lose under the
detector's reduction applied exactly (Wide's render of the same input), the reading Split is gated on
too. No Spectral code changed; its four golden renders pass byte for byte.

**Spectral as built fails it**, because its water-fill makes B lose the reduction: λ is solved so that
the band *and the skirts outside it, left alone*, keep (1 − depth)² of their power together (*The
gains*). The skirts keep all of theirs, so the band's own bins give up more than the reduction, up to
Range per bin. *Measured* (`"[accuracy]"`, the tests' ess with 3.55 % of B past its edges), own band
lost − asked: +0.19, **+0.61**, −0.01, **+0.58**, **+4.29**, **+5.44** dB at 3/8, 6/8, 12/8, 6/24,
12/24, 18/24 dB over/Range (where Range binds, at 12 over Range 8, it lands on Range); across rates at
6 dB over +0.59 … +1.09 dB. On real material (`HaliteMeasure --reduction`, m1 tune split): VCTK
+0.38 dB median [−0.08, +4.19], 54.3 % of 2409 events within ±0.5 dB (within its reach +0.25, 67.9 %);
VocalSet +0.74; the Harsh band +1.15 / +0.57 dB (its line-and-water-fill takes the same sum over the
band filter's skirts). The landing on B is as in M2 (−0.52 … −0.63 dB where Range does not bind).

The remedy is one term: leave `outside` out of the water-fill's kept power, so the band's own power
(weighted) loses exactly the reduction, as Split's does. That changes Spectral's sound (deeper cuts
become shallower on B, by the skirts' share) and its golden renders, which M2b's brief kept fixed.
Settled 44 took it; built and measured in M3 (below).

## M3 (2026-11-14): the band's own power (Settled 44)

`waterFill` keeps `(1 − depth)²` of the band's own weighted power; the skirts' power is no longer in
the sum (*The gains*), for the Ess band and the Harsh band alike. Gain exactly 1 outside the band, the
warm start, Live's fallback and the frame count per block are untouched (their tests pass unchanged).

- **Accuracy** (`"[accuracy]"`, the tests' ess, 3.55 % of its `B` outside the band), own band lost −
  asked / landing on `B`, dB, at 3/8, 6/8, 12/8, 6/24, 12/24, 18/24 dB over/Range:
  −0.03 / −0.32, −0.01 / +0.01, −0.01 / +0.74, −0.03 / +0.04, −0.03 / +1.54, −0.07 / +4.91; at 24/24
  −0.10 / +9.96 (M2b: +0.19, +0.61, −0.01, +0.58, +4.29, +5.44 own band). Every own-band row is a hard
  gate now; the landing is reported. `B` loses the band's loss diluted by the skirts' share within
  0.04 dB at every row (the gate: 0.1 dB, `"Accuracy: Spectral cuts the band's own bins*"`, which
  replaces M2's check that `B` loses the reduction up to 12 dB over). The landing passes ±0.5 dB at 3
  and 6 dB over, where Wide's and Split's miss by Speed Auto's overshoot: the skirts' share left in
  `B` offsets it.
- **Across rates** (6 dB over, Range 8): own band −0.01 … −0.07 dB at 44.1–192 kHz, within 0.06 dB of
  48 kHz's. The landing on `B` moves with the skirts' share of the test ess (−0.02, +0.01, +0.23, +0.28,
  +0.38, +0.37 dB at 44.1 … 192 kHz), as Split's does: M2's 0.15 dB check on the landing, and its 0.1 dB
  check on what `B` lost, now sit with Split's under `[!mayfail]` (`"Accuracy: Split's and Spectral's
  landing on B across rates*"`, Settled 41).
- **Harsh** (Settled 40, the band's power through its filter): +0.01, −0.02, +0.02, −0.02 dB at 3, 6, 9,
  15 dB over (own bins +0.04, 0.00, +0.07, 0.00 against the detector's mean); `H` −0.33, +0.01, −0.45,
  +0.01 (M2b −0.36, +0.01, −0.51, +0.01).
- **Real material** (`HaliteMeasure --reduction --data vctk,vocalset --stream --jobs 2`, m1 tune split,
  94 s, 0.75 GB), own band lost − asked, median [p5, p95], share within ±0.5 dB:

  | | M3 | M2b |
  |---|---|---|
  | VCTK, 2409 events | −0.07 [−0.44, +0.11], worst −1.92; 96.3 % | +0.38 [−0.08, +4.19]; 54.3 % |
  | VocalSet, 18 events | −0.01 [−0.71, +0.10], worst −1.22; 88.9 % | +0.74; 44.4 % |
  | Harsh, VCTK 669 episodes | −0.04 [−0.34, +0.09], worst −1.47; 98.1 % | +1.15; 27.5 % |
  | Harsh, VocalSet 666 episodes | −0.02 [−0.26, +0.05], worst −0.90; 98.9 % | +0.57; 46.8 % |

  `B` lost − asked −0.79 dB (VCTK, M2b −0.46), the landing +0.52 (M2b +0.18): the skirts' share stays in
  `B`. Every outlier loses less than asked, most of the worst at asks of 6–8 dB (Range 8); what holds
  them back (the Voice floor, the depth read once per frame against Wide's per-sample reduction, the
  2.5 ms gain smoothing) was not separated. Allround on the committed tier (no floor, −36 dBFS):
  97.5 % within, worst −0.52. Committed tier (Voice): −0.05 dB, 100 % of 40 events; Harsh −0.09, 100 %
  of 14. Collateral 0.000 dB on every event and episode; Listen Removed's null test passes (2.98·10⁻⁸
  VCTK, 5.89·10⁻⁸ VocalSet).
- **Collateral, synthetic:** unchanged to the printed digit (at most 0.007 dB outside the Ess band,
  0.006 outside the Harsh band, noise in the band losing 6.20 / 3.20 dB of `B`).
- **Clicks** (`"Clicks: report"`): every reading unchanged to the printed digit, Spectral's worst
  −75.3 dB (Listen) on the plan's sines; in the band −64.5 dB at rest.
- **Warm-up: pass.** −123.2 dB, bit for bit and −94.1 dB once 2 s are learned (2.66–2.71 s after the
  late starts), −32.7 to −37.0 dB before.
- **Musical noise:** gain std 0.45 dB (M2 0.41), spread across the band 2.40 dB (2.15): the band's own
  bins now take the whole cut (*Musical noise*). The Harsh resonance is cut −9.0 dB at 3.2 kHz (M2
  −10.6), white noise by 5.6–5.9 dB across its band (M2 12.0 at Range); the side chain's burst 10.53 dB
  (11.64); the floor keeps the vowel's harmonics within 1.12 dB (Voice) under a 24 dB ask; where the
  bands overlap the Ess band removes 0.0016 of a bin the Harsh band holds (M2 0.004).
- **Golden renders:** the four `spectral-*` re-recorded once for this intended change. Against the M2
  files they differed by −21.4, −17.3, −18.3 and −12.3 dB of what each removes (`voice-ess`,
  `voice-ess-harsh`, `allround-ess`, `allround-ess-harsh`; gate −60); each now removes 0.1–9 dB less in
  a half second where it cuts deep. The other eight passed untouched and came out byte-identical.
- **CPU** (`taskset -c 5 HaliteTests_reduction "[.perf]"`, load average 3.2 … 4.4): 0 of 198 readings
  over budget. At 48 kHz Spectral Linked 0.664 %, Unlinked 0.795, Mid 0.408, Harsh on 0.826 / 0.964, an
  ess all the time 0.901 / 1.051, a crossfade all the time 0.976 / 1.111 (M2b review, load 3.1 … 4.8:
  0.644, 0.749, 0.376, 0.804 / 0.911, 0.904 / 1.046, 0.968 / 1.102). Both bands cutting rose from
  0.928 / 1.001 to 1.021 / 1.180 % (of 1.8 / 2.8; 192 kHz Unlinked 3.68 of 11.2): M2's sum met its ask
  with every bin at its lowest gain more often, which skips the bisection, and the band's own sum runs
  it (the code path says so; not profiled). The costliest 512-sample block of a Spectral case at
  48 kHz: 1.74 % of its duration (a crossfade all the time, Unlinked; M2b review 1.68).
- **Tests:** the own-band rows of `"[accuracy]"` pass as hard gates; no Spectral row is left under
  `[!mayfail]` but the rates check on the landing (4 `[!mayfail]` cases, as in M2b). The engine test
  `"Spectral: the band loses the depth asked for*"` reads the band's own bins (`ownBandLostDb`, moved to
  `SplitMeasure.h`), `B` within 0.1 dB to 8 dB; the Harsh white-noise reading takes each bin's mean
  over 0.5 s (one frame's gains scatter by 1.4 dB, which the old reading at Range never showed). With
  M2's sum the four failing own-band rows read +0.19 … +5.44 dB (this run, before the change), which the
  hard gate rejects.

## M3: the constants (2026-11-14)

Voicing by measurement (nobody listened). The learning constants moved into `SpectralTuning` (no
value changed) so the tests can vary them; `"Spectral: the gain smoothing and the learning constants,
report"` prints the tables. Nothing changed in the sound; Speed Auto's change re-recorded the
`spectral-*` golden renders with the others (`../../detector/NOTES.md`, *M3: Speed*).

- **The gains' one-pole (2.5 ms, kept).** The musical-noise signal of *Musical noise* (a steady noise
  ess, 6 dB asked, the neutral curve as the target), and a depth that steps on at 1 s: when the band's
  mean gain comes within 0.5 dB of its steady value, and by how much it falls short over the first
  10 ms (frames every 2.67 ms):

  | Gains | gain std | within 0.5 dB after | short over 10 ms |
  |---|---|---|---|
  | none | 0.50 dB | 2.7 ms | 1.55 dB |
  | 1.25 ms | 0.48 dB | 5.3 ms | 1.86 dB |
  | **2.5 ms** | **0.45 dB** | **8.0 ms** | **2.61 dB** |
  | 5 ms | 0.40 dB | 13.3 ms | 3.75 dB |
  | 10 ms | 0.33 dB | 26.7 ms | 4.78 dB |

  The level estimate takes most of the jitter off (raw bins 2.76 dB); the gains' one-pole adds a
  tenth more at the cost of 5.3 ms on a depth's onset, which the detector's own onset (about 10 ms
  on the test ess, `../../detector/NOTES.md`, *M3: Speed*) already exceeds. A frame at depth 0 has
  every gain exactly 1, so the one-pole never lengthens an ess's end. Accuracy on steady esses does
  not depend on it (the gate rows read the steady part); warm-up neither (it learns nothing).
  Kept, the plan's "lightly": which of the two an ear notices first is the listening pass's.
- **The learning guard (8 ms, kept).** A −12 dB/oct vowel with an ess every 0.3 s, the detector's
  own targets deciding which frames are learned (its decisions, with B's 4 ms averaging):

  | Guard | learned slope | frames learned | 2 s learned after |
  |---|---|---|---|
  | 0 | −11.75 dB/oct | 55 % | 3.62 s |
  | 2 / 4 ms | −11.81 / −11.89 | 55 / 54 % | 3.66 / 3.71 s |
  | **8 ms** | **−11.90** | **53 %** | **3.81 s** |
  | 16 / 32 ms | −11.89 / −11.89 | 50 / 45 % | 4.01 / 4.49 s |

  Without a guard the ess onsets the detector decides late leak in (0.15 dB/oct); from 4 ms on the
  slope does not move, and every millisecond more lengthens the warm start a little. 8 ms keeps
  twice B's averaging for the decisions that come later than this test's clean onsets.
- **The voiced rule (20 dB, 1 s, kept).** The vowel with a pause every other half second and white
  noise in the pauses only (its RMS under the vowel's; its body band about 10 dB under its RMS):
  pauses with noise up to −20 dB are not learned with a 20 dB gap (slope −11.60 … −11.76 dB/oct,
  50–51 % of frames), and are with 30 dB (−5.5 dB/oct, 92–97 %); a 10 dB gap learns slightly fewer
  frames and reads the same. Noise at −10 dB is called sibilant by the detector and never learned.
  The memory (0.5, 1, 2 s) moves nothing within 0.15 dB/oct on half-second pauses. 20 dB is the
  narrowest gap that keeps such pauses out; a narrower one would also drop soft voiced syllables
  (not measured on real speech).
- **The neutral curve (−8 dB/oct, kept): the holdout check, run once** (the first of M3's three
  holdout readings). `HALITE_SPLIT=holdout
  "[.corpus][spectral]"`: VCTK holdout, 20 speakers, slope quartiles −10.11 / **−8.83** / −5.07 dB/oct,
  the bin at 4.5 kHz −58.31 / −53.95 / −46.31 dB, 35 / 37 / 38 % of frames learned (tune −11.27 /
  −7.76 / −6.09, −52.5 dB at the median). VocalSet holdout −10.52 / −8.95 / −5.95 dB/oct. The curve
  lies between the two halves' medians, 0.83 dB/oct from holdout's; nothing was changed by it.

## Decisions (for the user to confirm)

1. **The detector's reduction is what the band loses, not a ceiling per bin** (*The gains*): the
   plan's two readings contradict the accuracy gate; this one meets it to 12 dB over.
2. **No bin below the voice's own top end** in Voice (the floor), so an ask the band cannot meet
   never takes the voice's air.
3. **The target learns from the detector's decisions**, with an 8 ms guard, **and averages in
   dB**: robust to the frames an ess leaks into, which a power average is not.
4. **The neutral curve** from the m1 tune split's VCTK speakers (−8 dB/oct).
5. **A transport jump keeps the audio in flight on its old reductions** (but its last look-ahead),
   played by the engine that analysed it on its old grid, and restarts the analysis on the new grid;
   a play start is a fresh render. A position within 32 samples of the expected one is not a jump.
6. **Stereo changes in Spectral carry the target over**; a change of bus layout or of latency
   does not.
7. **A Spectral lane is heard one frame after it starts** (after the review): it starts from the
   frame of input before it rather than catching up on earlier frames in one block, so a Shape or
   Stereo change into Spectral is heard 10.7 ms later (48 kHz) and a change of latency into it
   stays silent that much longer.

## Not verified

- No listening; no host run (Reaper, Bitwig, Ardour): the play head's start and jump detection is
  tested on the chain with a synthetic timeline only; hosts that move the position while stopped,
  or report it late, were not tried.
- The Warm-up gate on real recordings: measured on a synthetic singer only.
- The neutral curve on LibriSpeech (16 kHz: no band above 8 kHz). Holdout was read for this check
  once (*M3: the constants*), one of M3's three holdout readings (PLAN.md, *Measured status*). It is frozen in the golden renders; changing it is a sound change.
- The learning guard and the voiced rule on real recordings: measured on synthetic signals with
  the detector's own targets (*M3: the constants*); the chain runs the plugin's constants, so
  `HaliteMeasure` cannot vary them.

## Open (for the user)

Settled 39 and 44 decided the reading of the detector's reduction (the band's own loss) and the deepest
cuts (bounded on `B` by the skirts; the landing is reported); Settled 40 the Harsh reading; Settled 45
the in-band clicks (reported); M3 measured and kept the constants (*M3: the constants*). Left for the
listening pass: the gains' one-pole (2.5 ms: musical noise 0.45 dB of gain std, a stepped depth
reached within 0.5 dB after 8.0 ms; none: 0.50 dB, 2.7 ms; 5 ms: 0.40 dB, 13.3 ms). Not separated:
what holds back the 3.7 % of VCTK's events whose own band loses more than 0.5 dB less than asked (*M3*).
