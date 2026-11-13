# Bits

Module 4: a sampler's converter, sampling at its own clock instants, or a phone line. **Status:
voiced (M3)** by measurement (*Voicing*); nothing has been judged by ear yet.

| File | Contents |
|---|---|
| `BitsModule.{h,cpp}` | the clocked domain per channel, the clock on the timeline, jitter, the quantiser, fades |
| `BitsVoicing.h` | every type's numbers: clock, filters, native depth, mu-law, the phone's low cut; Rate and Depth mappings |
| `BitsControls.{h,cpp}` | Rate in kHz and Depth in bits per type, typed entry |
| `tests/modules/bits/` | `InkcapTests_bits`: every gate below, golden renders (`golden/<type>.f32`) |

## Signal flow

Per channel:

```
in ─► (Phone: 300 Hz low cut) ─► anti-alias filter ─► sample at the converter's clock
   ─► quantiser (linear; Phone: mu-law) ─► zero-order hold ─► reconstruction filter ─► fade vs in ─► out
```

The filters and the sampling are the Holters–Parker clocked domain (`common/dsp/ClockedDomain.h`,
moved from Mulberry, namespace only; validated against an independent implementation and a 64x
brute force in Mulberry's Bucket tests). The converter samples the input filter's *exact*
continuous-time output at its own instants, and each held value drives the output filter as an
exact step, so aliasing and clock images are those of the modelled sampler, never the host
rate's. Both filters are 4th-order Butterworth (two complex modes), at the type's share of the
clock.

## Types

| Type | Clock at Rate 50 % | Filters | Depth at 50 % | Converter |
|---|---|---|---|---|
| **Sampler 12** | 26.04 kHz | 0.42 × clock (a little under its Nyquist) | 12 bits | linear, round to nearest, clips at full scale |
| **Sampler 8** | 16 kHz | 0.5 × clock (at its Nyquist: it aliases) | 8 bits | linear |
| **Phone** | 8 kHz | 0.425 × clock (3.4 kHz), and a 300 Hz 2-pole low cut before | 8 bits | mu-law (mu 255): compress, quantise the code, expand |

- **Rate** (0 … 100 %): the clock, log-linear on each side of the type's own: three octaves down
  at 0 %, one up at 100 % (Sampler 12: 3.26 … 26.0 … 52.1 kHz). Reads "26.0 kHz", "8.00 kHz";
  typed "26 kHz", "26k", "26040 Hz", or a bare number (under 200: kHz).
- **Depth** (0 … 100 %): the native depth −6 … +6 bits, fractional depths included (at least 2).
  Reads "12 bits", "9.5 bits".
- **Jitter** (0 … 100 %): the clock instants wander up to 0.35 clock periods either way at full
  intensity: value noise (stream `bitsJitter`) of the song time on a lattice at the type's clock
  over 16, so the wander is slow against the clock and the same from any start.

## Damage

The intensity i (Amount × Damage^0.75, Bits' curve) takes the converter from where it is first
heard to its setting, every part continuously:

- the clock from the one whose filters just reach their cap (0.4 of the host rate: 45.7 kHz for
  Sampler 12 at 48 kHz) down to Rate, log-linear in i;
- the depth from 16 bits down to Depth, linear in i;
- the filters with the clock, **never above 0.4 of the host rate** (*Settled* 43): the domain
  takes the host's samples as impulses, and filters open past the host's Nyquist pass those
  impulses' images (+5 dB at 1 kHz before the cap, at a light touch);
- Phone's low cut at 300 Hz × i; Jitter's depth × i, and **Chaos widens the jitter**: the
  jitter in use is Jitter + (1 − Jitter) × Chaos / 2 (`bits::jitterWithChaos`), so at Chaos 100 %
  a converter set to no jitter wanders as at Jitter 50 %.

Off and Damage 0 pass the input untouched, to the bit (the module skips all work at rest); the
module fades over 20 ms (C2, equal gain). A converter that comes on starts at its settings, under
that fade. A type change ducks the converter for 10 ms each way and restarts it at the new type.
The click gate excepts Bits by plan: its steps are the sound (the chain's and InkcapMeasure's
click checks switch Bits off, as they do Glitch and Hiss).

## The clock on the timeline

The domain's clock is a phase in clock periods. When the converter starts and after every
transport jump, its phase is set to clock × the song time (`secondsAt`) plus the jitter's
displacement there (`ClockedDomain::setClockPhase`, a small addition to the shared block that
Mulberry never calls), so a render from bar 17 samples at the instants a render from bar 1 did
(−150 dB after 500 ms, every type, Jitter 100 %). The clock follows its glide and the jitter on a
4-sample grid of the timeline: each update sets the rate that lands the phase on the next grid
point's displacement exactly; the filters follow the clock on a 32-sample grid. A Rate or
Damage move after the start leaves the phase where the glide took it: the property holds for
steady settings, as for Warble's free LFO.

**Delay.** A converter delays what it passes: half a clock period (the hold) and its two
filters' group delay. Measured through the module at a light touch (Sampler 12, 48 kHz, clock
41.4 kHz, filters at 17.4 kHz): 2.88 samples at low frequencies, exactly the hold's 0.58 and the
filters' 2.30; the domain's reading of its input at the period's end adds nothing on top (the
impulse convention places it). `latencySamples()` stays 0: the delay moves with Rate and Damage
(from about 2 samples at a light touch to 130 at Phone's lowest Rate, at 48 kHz), and a latency
that moves with a control makes hosts re-align on every move; it is the device's own delay, as
an analogue filter's group delay is never reported (*Settled* 54).

## Mix

The chain's Mix blends the wet with a dry delayed by the converter's delay (`chain/MatchedDry.h`,
*Settled* 53), so a partial Mix does not comb: the module records per sample its share of the
output (`converterShares()`, -1 when it passes the input) and its delay (`converterDelays()`,
`converterDelay()`): half a clock period plus the filters' low-frequency group delay
(2 × 2.613 / ωc for two 4th-order Butterworths) stretched by 16 %, which matches their phase near
the cutoff too (the stretch is the minimax of the comb against the filters' magnitude: within
0.12 dB up to 1.1 fc). The dry reads it through the windowed sinc (flat to 0.35 fs, 1.7 dB down
at 0.4 fs while the converter is on). Measured at Mix 50 % against the in-phase sum of wet and
dry, 100 Hz to 0.4 fs: within 0.1 dB for Sampler 12 and 8 at a light and a middle Damage (−28 dB
at 8 kHz before); Phone from 1 kHz up, since its 300 Hz low cut leads the dry below its band, the
phase of any filter blended with its own input.

## Measured (`InkcapTests_bits`)

| Gate | Result |
|---|---|
| Against a brute-force sampler at 64x (Sampler 8 at 5.66 kHz, 14 bits, a 2 kHz tone): tone, alias (clock − f), image (clock + f), second alias | within 0.0003 dB at all four (−8.4, −22.8, −54.4, −62.9 dB) |
| Depth: the converter's noise against the filters alone, 12 vs 8 bits | −78.5 / −54.5 dB: 24.0 dB for 4 bits |
| Phone band: 120 Hz / 1 kHz / 3.9 kHz | −16.0 / −0.3 / −15.7 dB |
| mu-law: error on quiet values against a linear 8-bit converter; monotonic over ±1 | smaller; yes |
| Damage 0.1 … 1: clock and depth move monotonically to Rate and Depth; at intensity 0.05 the 1 kHz level within 0.1 dB | yes |
| From bar 17 vs from bar 1, after 500 ms, Jitter 100 % (gate −80 dB) | Sampler 12 −151, Sampler 8 −155, Phone −158 dB |
| Jitter 100 % against none (a 1.5 kHz tone) | −45 dB apart, the same level within 0.01 dB |
| Exact zeros after the input stops | 6 / 8 / 23 ms |

Also checked: Damage 0 and Off are the input to the bit (every type, every setting, block sizes 1,
64, 512), and Damage → 0 lands back on it; every block size (1, 7, 64, 333, 1024 against 4096)
gives the same render to the bit with Jitter on; a type change stays finite and lands on the new
clock; a new seed moves the jitter; no subnormal output; no allocation in `process()`; read-outs
round-trip; golden renders per type (`"[golden]"`, error energy below −60 dB: a quantiser turns a
last-bit difference of another libm into a whole step now and then).

**CPU** (`"[.perf]"`, 48 kHz stereo, programme material, full Damage, Rate and Depth 50 %), on a
machine shared with other builds (load average 4–8): Sampler 12 0.40 % (0.56 % with Jitter
100 %), Sampler 8 0.27 % (0.44 %), Phone 0.26 % (0.43 %). Most of it is the shared domain's
per-period complex exponentials (`ClockedDomain::beginSample`, `setClock`); making those
incremental would change Mulberry's renders in the last bits, so it was left alone.

## Voicing (M3)

**Target:** audible from the first quarter of the pad, the type's own setting at full. M2 started
the converter at twice the host rate and 24 bits: with the filters capped at 0.4 of the host rate
nothing was heard until about two thirds of the intensity, and at Amount 50 % Sampler 12 never
went below 19.2 kHz and 18 bits. Now the clock starts where its filters reach the cap and the
depth at 16 bits, CD's, where a converter's noise is first heard.

| Amount 100 %: Damage 25 / 50 / 100 % | top (kHz) | depth (bits) |
|---|---|---|
| Sampler 12 | 15.7 / 13.7 / 10.9 | 14.6 / 13.6 / 12.0 |
| Sampler 8 | 14.1 / 11.4 / 8.0 | 13.2 / 11.2 / 8.0 |
| Phone | 10.4 / 6.9 / 3.4 | 13.2 / 11.2 / 8.0 |

At the default Amount (50 %) and Damage 50 %: Sampler 12 16.2 kHz and 14.8 bits, a gentle sampler
(`InkcapCalibrate --voicing`).

**Match:** Bits' table runs over type, Rate, Depth and intensity. Without it, at full Damage:
Sampler 12 −1.4, Sampler 8 −1.9, Phone −5.5 dB on pink noise; with it within ±0.4 dB at the
defaults. Where Rate and Depth both sit near 0 (2–3 bits at a few kHz) a quiet mix rounds to zero
and the boost that would match it stops at +12 dB (`Match.h`): there Sampler 8 is 2.7 dB louder
than the input at the calibration level, Phone at its lowest Rate 1.4 dB quieter.

**Golden renders** per type at Chaos 40 %, so the widened jitter is locked too.

## Not done / open

- Nothing has been listened to: clocks, filter shares, the jitter's depth and lattice are voiced
  by measurement.
- CPU (callgrind, Sampler 12 at full Damage with Jitter): about 2,500 instructions per host
  sample, most of it the shared domain's per-period complex exponentials
  (`ClockedDomain::beginSample`, `setClock`); making those incremental would change Mulberry's
  renders in the last bits, so it was left alone.
- No gain ranging (Mulberry's Rack converter has it); the samplers here are plain linear
  converters.
