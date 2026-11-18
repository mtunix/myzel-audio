# Pumping: a recovery shaped to breathe (1.1, specification only)

A compressor built to pump: the gain drops fast on a loud event and then swells back on a shaped
curve, slow at first and quickest in the middle, landing softly at full gain. With Auto release
the swell follows the beat. There is no reference unit: Pumping is **Basalt's design** on the
shared core (`../../dynamics/NOTES.md`), for the "sidechain-pumping" effect producers build with
any compressor and a long release.

Status: M0 specification for 1.1 (`PLAN.md`, Settled 5, *M6*). Nothing is built in v1; the model's
place in the frozen `model` list holds it (Settled 9). Marks as in the core: **source**,
**measured (simulation)** (Python/NumPy, double precision, 48 kHz, set-ups given with the numbers,
scripts in the M0 session's scratchpad, not kept), **Basalt's design**, **inference**,
**not verified**.

## Why a shaped recovery

An exponential release (every other model) recovers fastest at its start: most of the gain is back
within the first time constant, and the rest creeps. A pump is heard as the opposite: the level
stays down after the hit, then rises audibly toward the next one. The recovery therefore follows a
curve with zero slope at both ends, a slow start (the "duck") and a soft landing (no click when the
gain reaches unity), with its steepest rise in the middle. It also ends: the gain is exactly unity
after the release time, which makes the release time a musical length (a beat) rather than a
time constant.

## 1. Detector

- **Slow RMS, feed-forward** (`PLAN.md`): the core's RMS level `L_R = 10·log10(2m)` with
  **τ_RMS = 20 ms** (**Basalt's design**: long enough to react to a kick's body rather than its
  click, short enough to fall below threshold between beats; the detector's fall is 217 dB/s),
  through the core's look-ahead window and Link.

## 2. Gain computer

The core's eq. 4 at Threshold, Ratio, Knee and Range, unchanged: `r_t = min(x − G(x), Range)`.
`PLAN.md`'s "hard knee, high ratio" is a matter of presets (Knee 0, Ratio 10 and up), since
switching model never moves a knob.

## 3. Ballistics

Per channel (linked as the core), y the applied reduction (dB), r_t the static target:

```
k(u) = (1 − u²)²                                   the recovery shape, k(0) = 1, k(1) = 0,
                                                   k'(0) = k'(1) = 0
if r_t > y:      y = α_A·y + (1 − α_A)·r_t; attacking            (one-pole, τ = Attack)
else:
  on leaving attack:  y0 = y, u = 0                               (latch the depth)
  u = min(1, u + 1/(T_R·fs))
  y = y0·k(u)
  if y ≤ r_t:  y = r_t, y0 = r_t, u = 0                           (the target holds the gain:
                                                                   restart the curve from there)
```

- **Release T_R = Release**, Auto off: the **time to full recovery**, not a time constant. The
  reduction is half recovered at u = √(1 − √0.5) = 0.5412, i.e. **t₅₀ = 0.541 × Release**, and
  exactly 0 at Release, so the gain is exactly 1.0 there without the core's snap.
- **Attack** is the core's one-pole on the reduction. With the 20 ms RMS in front, the attack has a
  floor: from silence to 20 dB over Threshold at Ratio 10, 63 % of the reduction in 4.15 ms at
  Attack 0.1 ms, 5.25 at 1 ms, 10.9 at 5 ms, 27.9 at 20 ms (measured, simulation). Look-ahead
  moves it before the hit.
- **Restart rule:** when the curve falls to the target (the signal is still over Threshold), the
  gain follows the target and the next fall restarts the full curve from there; so steady
  compression is exact (static gate) and every new drop pumps with the full shape. A new hit
  during a release returns to attacking from wherever the curve is (no step).
- **Changing T_R during a release** (Auto) changes only the speed of u, never y: no step.

### Auto: the release follows the beat (Basalt's design)

`autoRelease` On sets T_R from the spacing of the loud events, with Release as the base:

- **Onsets:** an onset is counted when the static target r_t reaches 3 dB while armed; it is armed
  again when r_t falls to 1 dB or below (2 dB of hysteresis on the detector's view, independent of
  the curve). A slow RMS returns to 0 dB between beats, so each beat counts once.
- **Interval:** the time between successive onsets; intervals longer than 2.5 s are not a beat and
  are dropped. The last five intervals are kept (a fixed array).
- **T_R = clamp(0.9 × median(intervals), max(40 ms, Release/4), min(4 × Release, 4 s))**: the gain
  is back just before the next beat (90 %), whatever the tempo, and Release keeps the range within
  a factor of four of where the user set it.
- **No onset for 4 s:** the intervals are cleared and T_R returns to Release.
- **Linked:** onsets on the linked target, one T_R for both channels (core, *Stereo*).
- Measured (simulation, 40 ms bursts 15 dB over Threshold, Ratio 10, Attack 1 ms, Release 300 ms):
  after 6 s at 120 bpm, T_R = **0.450 s** (0.9 × 0.5 s); at 160 bpm, **0.338 s** (0.9 × 0.375 s).

## 4. Make-up and gain law

- Gain 10^(−y/20), the core's ideal cell.
- **Auto Gain** (core): M = r_static(−18 dB), the static curve's reduction at the reference. It
  does not include the pump's average dip, which depends on the material and the release; a pump
  is meant to be heard.
- **Learn** (core): the solve runs these ballistics on the captured envelope (mean square per
  millisecond, `../vca/NOTES.md`, *Proposed changes*, 5).

## Gates (for 1.1)

The core's rows apply, with the release definition replaced:

| Gate | Pumping check |
|---|---|
| Static curve | the core's grid on 1 kHz sines and the carrier (its level on the RMS reading, 3.01 dB above its amplitude; core *Conventions*), ±0.25 dB of eq. 4 with Range (measured, simulation: 7.511 dB for 7.5 dB at 10 dB over, Ratio 4) |
| Recovery shape | carrier held at 10 dB of reduction, then silence, Release 0.1, 0.4, 2 s: reduction at u = 0.25, 0.5, 0.75 of Release equals 0.8789, 0.5625, 0.1914 × the held depth ±0.25 dB (simulation at 0.4 s: 8.789, 5.624, 1.913 dB for 8.789, 5.625, 1.914), exactly 0 (gain 1.0) from Release + 2 samples on, t₅₀ = 0.541 × Release ±5 % |
| Attack | the core's attack definition against the double-precision reference of these equations (detector included), ±10 % |
| Auto | Release 300 ms (as simulated; the clamp's top, 4 × Release = 1.2 s, then sits above every beat tested): bursts at 90, 120, 160 bpm for 6 s: T_R = 0.9 × the beat period ±5 %; 4 s after the last burst T_R = Release. **Clamp:** at Release 100 ms the same bursts give T_R = min(0.9 × period, 0.4 s) ±5 % (0.4 s at 90 and 120 bpm, the clamp; 0.338 s at 160 bpm) |
| Clicks | release onset and landing on a 100 Hz sine at 12 dB reduction: no step artefact above −60 dB (the curve's zero slope at both ends) |

## Proposed changes to PLAN.md

1. ***The reference behaviour*, Pumping row:** Detector "Slow RMS · feed-forward" → "RMS 20 ms ·
   feed-forward"; Release "A long release with a recovery curve shaped to pump" → "recovery on
   (1 − u²)², zero slope at both ends, full recovery exactly at Release; Auto sets Release to 0.9 of
   the beat period from the last five onsets".

## Not verified

- The voicing: the curve's exponents (2 and 2), the 0.9 factor and the 20 ms detector are chosen
  here and need the M6 listening pass. Every "measured (simulation)" figure is a simulation of
  these equations.
