# Crush

Module 3: saturation, the only part of the chain at 2x (`Oversampler2x`, polyphase IIR, minimum
phase). **Status: voiced (M3)** by measurement (*Voicing*); nothing has been judged by ear yet.

| File | Contents |
|---|---|
| `CrushModule.{h,cpp}` | the 2x pair, the clean path, the On/Off and type fades, the glides, Tone, rest on silence |
| `CrushStages.h` | the band split, `TapeStage` (coating + `HeadLosses`), `ValveStage`, `FuzzStage` |
| `CrushVoicing.h` | every type's numbers in one place: drive ranges, levels, splits, tapes, the valve's values |
| `CrushControls.{h,cpp}` | Drive's read-out in dB per type, typed entry |
| `tests/modules/crush/` | `InkcapTests_crush`: every gate below, golden renders (`golden/<type>.f32`) |

## Signal flow

Per channel:

```
in ─► clean ring (3 samples) ───────────────────────────────────────────────┐
in ─► ↑2x ─► split ─► low band × drive ─► the type's stage ÷ (drive × its small-signal gain) ─► +
                   └─ high band × highShare ──────────────────────────────────────────────────┘
      ─► the type's colour (Tape, Cassette: the head) ─► ↓2x ─► Tone tilt ─► fade vs the clean ring ─► out
```

- **Clean path.** Off and Damage 0 take an integer ring of `pairLatencySamples()` (3, the pair's
  low-frequency group delay of 3.07 samples, rounded): exact, so the chain's Damage-0 null holds
  to the bit. The pair is minimum phase and never compared with the dry at −100 dB (PLAN.md
  *Settled* 18). The module fades between the clean ring and the Crush path over 20 ms (C2), on
  the On key and when the intensity reaches or leaves 0.
- **Latency.** The ring carries it, so it never changes. At rest on the clean path (Off, Damage
  0, or asleep) nothing else runs, the pair included: it starts from rest under the 20 ms fade,
  where its few samples of settling are 60 dB down (*Settled* 42).
- **Normalisation.** Every stage divides its small-signal gain out (the coating's initial
  susceptibility, the triode's gain at 1 kHz, the clipper's slope at rest), so small signals pass
  at unity (within 0.3 dB at 300 Hz at full Damage) and Damage changes colour, not level. The
  loudness of the saturation itself is Match's (M3 calibration).
- **Damage** (intensity i = Amount × Damage^0.6, Crush's curve) scales everything: the drive in dB
  (i × the type's Drive in dB), the head's wear (Tape, Cassette), the band above the split
  (Fuzz), the Tone's tilt. Glides: two one-poles in series, about 20 ms, per sample.
- **Type changes** crossfade the two stages over 20 ms (equal gain: the same input); the new
  stage starts from rest. A third type mid-fade drops the quieter of the two.
- **Rest on silence.** A tape keeps its remanence and a valve settles towards its operating
  point for ever, so neither reaches exact zeros on its own. After 4096 samples of exact silence
  in and below −140 dB out, the module rests on the clean path (exact zeros) and starts its stages
  from rest when sound returns (a fresh render's start, so the next note sounds as the first).

## Types

| Type | Stage (2x) | Colour | Drive (0 … 100 %) | Split |
|---|---|---|---|---|
| **Tape** | Jiles–Atherton coating (`common/dsp/TapeCore`), α 0.01, k 0.5, c 0.97; 0.5 h/a per full scale | 7.5 ips head: gap 2.5 µm, spacing 0.3 µm, thickness 1 µm, bump at 35 Hz | +4 … +22 dB | 4 kHz |
| **Cassette** | the same core, k 0.6, c 0.97 (a wider loop); 0.7 per full scale (less headroom) | 1⅞ ips: gap 1.5 µm, spacing 0.5 µm, thickness 0.6 µm (−7 dB at 7 kHz) | +6 … +24 dB | 3.5 kHz |
| **Valve** | one 12AX7 common-cathode stage at circuit level (`common/dsp/TriodeStage`, Umber's Valve values); 0.15 V per full scale | the circuit's own (coupling, Miller) | +6 … +30 dB | 4 kHz |
| **Fuzz** | our transistor clipper tanh (x + 0.35) − tanh (0.35), first-order ADAA; 0.2 per full scale | collector low-pass 6.5 kHz, coupling high-pass 12 Hz | +12 … +36 dB | 2 kHz, highs to 25 % |

Drive reads out as the dB into the stage at full intensity ("+13 dB"); typed entry takes
"+13 dB", "13" or "40 %". Tone is a first-order tilt around 1 kHz, ±6 dB per side at full
intensity (at Damage 0.4 the half of it).

### The split (*Settled* 39)

Each stage saturates only the band below its split frequency (a 2-pole low-pass, Q 0.5, so the
two bands sum to the input with a gentle +1 dB around the split); the band above passes beside
it (Fuzz: down to 25 % at full intensity, a fuzz eats the highs). At 2x a harmonic folds back
into the audio band once it passes 64 kHz, the 10th of a 7 kHz tone: what reaches a
nonlinearity at 7 kHz must stay small enough that its 10th to 15th harmonics sit below −80 dBc.
The split keeps the heavy drive where the energy and the character are (lows and mids) and the
top end clean, as many mix saturators do on purpose; with the drive ranges above it holds the
family's aliasing gate at every type. Tape's physical HF saturation (pre-emphasis) is not
modelled: at 2x it cannot be, within the gate.

### Tape and Cassette

- **The coating** is the shared Jiles–Atherton equations, solved by Mulberry's `TapeCore`
  (tabulated Langevin, RK2 sub-steps of at most 0.2 in field), moved to `common/dsp/` for this
  (*Settled* 40). The bias-linearised loop is tighter than Mulberry's (c 0.9): the irreversible
  part kinks the loop at each reversal, and at 2x those kinks' harmonics fold back. Measured on
  the gate's 7 kHz tone at the top of Drive: c 0.9 −78 dBc, 0.93 −81, 0.95 −85, 0.97 −90, 0.999
  −97. **The kinks fold back at every Drive, and not monotonically:** the alias of the 13th
  harmonic (2.8 kHz) passes through peaks as the drive moves. With M2's coating (Tape c 0.95,
  split 4.5 kHz; Cassette c 0.94, 4 kHz) the top of Drive met the gate but the middle did not:
  Tape −79.9 dBc at Drive 94 % and −78.5 at 61 %, Cassette −75.7 at 43 %, and Chaos's wander
  took the top of Drive through those (*Settled* 55). Since then Tape is c 0.97 with its split at
  4 kHz and Cassette c 0.97 at 3.5 kHz: the whole Drive range stays under −82 dBc (Tape −82.8 at
  43 %, Cassette −82.3 at 55 %, swept in 3 % steps). The distortion they make at a programme's
  peaks moved by less than a dB (*Voicing*).
- **The head** (`HeadLosses`) is TapeLosses' fitted prototypes (Bertram's gap, spacing and
  thickness losses and the contour bump, `common/dsp/TapeLosses.h`, moved from Mulberry) realised
  as TPT sections, each corner prewarped (blended into TapeLosses' 16 kHz bilinear scale above
  0.2 fs, as TapeLosses blends). **Damage wears the tape in:** the head's lengths are the
  voicing's times the intensity and the bump's wavelength over it, so at a light touch the losses
  sit far above the band and the bump near 0 Hz; nothing sweeps through the band as a different
  filter. The head is redesigned on an 8-sample grid of the timeline when the intensity has
  moved. TapeLosses' own direct-form sections clicked under that movement (−30 dB 3rd-difference
  crest, a 2 % coefficient step per update moves their states' mix of signal and coefficients);
  TPT sections keep −65 dB and better (*Settled* 41).
- Measured at 88.2 kHz against Bertram's closed forms (where the fits hold, down to −12 dB):
  within 0.52 dB at every wear (TapeLosses' own sections at the same rate: 0.65), within 0.47 dB
  of TapeLosses' response to 8 kHz. At the host rate (44.1 kHz) the top octave would be off by
  several dB, as TapeLosses' own: the head stays at 2x.

### Valve

One stage of Umber's validated triode circuit: grid conduction, the Miller capacitance and the
asymmetric clip come out of the circuit, the coupling capacitors take its DC. Inverts; the sign
is divided out with the gain.

### Fuzz

`tanh (x + b) − tanh (b)`, b 0.35 (a biased transistor: even harmonics), antialiased to first
order: the mean of f over the straight line between two samples, `(F (x1) − F (x0)) / (x1 − x0)`
with F (x) = log cosh (x + b) − tanh (b) x, written overflow-free; below a step of 1e-6 the
midpoint value. Then the collector's slew (one-pole 6.5 kHz) and the coupling cap (12 Hz high-pass:
the bias's DC).

## Measured (`InkcapTests_crush`)

| Gate | Tape | Cassette | Valve | Fuzz |
|---|---|---|---|---|
| Aliasing, 7 kHz −1 dBFS, 44.1 kHz, Drive/Amount/Damage max (gate −80 dBc) | −84.7 | −86.9 | −105.8 | −152 ¹ |
| The same over the whole Drive range, and at Chaos 100 % for six seeds (gate −80 dBc) | ≤ −82.8 | ≤ −82.3 | below −80 | — |
| Aliasing report, 1 kHz / 3 kHz, the same (`"[.quality]"`) | −121 / −98 | −124 / −111 | −104 / −85 | −104 / −122 |
| Harmonics 2–9 of 200 Hz at −6 dBFS, Damage 1: Drive 0 / 50 / 100 % | −51 / −34 / −20 dB | −42 / −26 / −15 | −40 / −28 / −14 | −24 / −15 / −9 |
| 3rd-difference crest on a 330 Hz sine (gate −60 dB, or the steady crest of the settings a change moves between when that is higher): On/Off, type, Drive, Tone, Damage → 0 | −76 / −72 / −72 / −75 / −70 | −74 / −72 / −67 / −72 / −65 | −78 / −72 / −76 / −76 / −78 | −71 / −72 / −57 ² / −70 / −72 |
| Exact zeros after the input stops | 0.18 s | 0.40 s | 1.50 s | 0.25 s |

¹ The 7 kHz tone reaches Fuzz's clipper 22 dB down (its split at 2 kHz) and comes out at
−13.8 dBFS; the 1 kHz row is the meaningful one for Fuzz.

² Fuzz at Drive 100 % clips the sine into edges of its own: steady, they read −55 dB at Damage
60 %, and the Drive move between 0 and 100 % stays below that (−57.4 dB). The gate compares a change
with the steady settings it moves between (M3, when Crush's curve put more intensity at Damage
60 % and the M2 gate's fixed −60 dB read the clipping itself).

Also checked: Damage 0 and Off are the input delayed by 3 samples to the bit (every type, every
setting, block sizes 1, 64, 512), and Damage → 0 lands back on it to the bit; every block size
(1, 7, 64, 333, 1024 against 4096) gives the same render to the bit, also across a type change;
no subnormal output; no allocation in `process()` (a counting `operator new`); Drive's
read-outs round-trip; golden renders per type (`"[golden]"`, error energy below −90 dB).
Fuzz's Drive move is the closest click (−61 dB): its drive glides over 20 ms through 24 dB.

**CPU** (`"[.perf]"`, 48 kHz stereo, programme material, Drive 50 %, full Damage, 10 s, the
harness included), on a machine shared with other builds (load average 4–8): Tape 1.20–1.23 %,
Cassette 1.22 %, Valve 1.13 %, Fuzz 0.55 %. The budget for Tape is 1.2 %: it sits on the line.
Instruction counts (callgrind): Tape about 1300 instructions per stereo host sample, of which
the pair's up and down filters about 400 and each 2x stage call (split, coating, head) about 190.
The head at the host rate would save about a tenth, but its top octave would be off by dB
(*The head*). Every module at full Damage through the chain (`InkcapMeasure`): 2.5 % (budget 3 %).

## Voicing (M3)

**Target:** what engineers expect of each medium at a programme's peaks (−6 dBFS), Drive 50 %:
Tape about 1 % THD at the middle of the pad and 2–3 % at full (a machine at a hot level: MOL is
3 %), Cassette hotter (less headroom), Valve warm second harmonic from the first touch, Fuzz
clipped. Drive in dB is already linear in loudness of distortion, so the curve only bends the
warmth in early: Damage^0.6.

| THD, 200 Hz at −6 dBFS, harmonics 2–9 | Amount 100 %: Damage 25 / 50 / 100 % | Amount 50 % (default) |
|---|---|---|
| Tape, Drive 50 % / 100 % | −49 / −43 / −34 · −41 / −31 / −20 dB | −55 / −52 / −47 · −51 / −45 / −38 |
| Cassette | −41 / −35 / −26 · −34 / −25 / −15 | −48 / −45 / −39 · −44 / −38 / −31 |
| Valve | −39 / −35 / −28 · −33 / −26 / −14 | −43 / −41 / −37 · −40 / −37 / −31 |
| Fuzz | −26 / −21 / −15 · −21 / −15 / −9 | −31 / −28 / −24 · −28 / −24 / −19 |

(`InkcapCalibrate --voicing`, after the coating change of *Settled* 55.) The head's wear and the Tone's tilt follow the same intensity.

**Chaos (M3): the drive wanders.** Stream `crushDrift` (reserved since M0): value noise of the
song time on a 0.7 Hz lattice moves the drive by up to ±4 dB × Chaos × intensity, a machine whose
level will not hold, so the saturation breathes (THD spreads by 5.1 dB over 7 s at Chaos 100 %,
none at 0). It is evaluated on the 8-sample update grid of the timeline (a step of a thousandth of
a dB at most) and a new seed's wander fades in over 250 ms; every block size renders the same to
the bit and a seed change does not click (`InkcapTests_crush`, Chaos).

**Match:** Crush's table runs over type, Drive, Tone and intensity. Without it, at full Damage
and Drive 50 %: Tape −1.3, Cassette −3.2, Valve −0.2, Fuzz −5.4 dB on pink noise; with it within
±0.1 dB, also between the table's points.

The wander never takes the drive above the type's top (`drivenDb`): the aliasing gate is met
there, and Chaos at full Drive only ever breathes downwards.

**Golden renders** per type (`golden/<type>.f32`) at Chaos 50 %, so the wander is locked too
(re-recorded for the coating change of *Settled* 55).

## Mix

The chain's Mix blends the wet with a dry that has been through what Crush's clean parts do to
the phase (`chain/MatchedDry.h`, *Settled* 53): the module records per sample how much of its
output came through the pair (`pairShares()`, -1 on the clean ring) and, for Tape and Cassette,
the head's group delay at 2 kHz in host samples (`colourDelays()`, from `HeadLosses::groupDelay`
on the 8-sample update grid). The dry takes an identical pair with nothing between its halves
(its phase is the pair's exactly, its level flat to 0.003 dB below 0.48 fs) and the head's delay
as a fractional delay. A partial Mix then stays within 0.4 dB of the in-phase sum of wet and dry
up to 0.4 fs at every type and a light or a middle Damage (it combed 18 dB at 17 kHz before); a
worn head's own dull top end is the sound and stays in the wet.

## Not done / open

- Nothing has been listened to: drive ranges, splits, levels and the Tone's tilt are voiced by
  measurement against the targets above.
- CPU (callgrind, 48 kHz stereo, Tape at full Damage, Drive 50 %, Chaos 50 %): about 2,600
  instructions per host sample (M3 counted 2,100 in another configuration), the wander 15 of
  them on its grid. The budget for Tape alone is 1.5 % (*Settled* 56).
- The ADAA's half-sample delay at 2x (a quarter host sample) puts Fuzz's low band 4° behind its
  high band at the 2 kHz split; inaudible, not compensated.
