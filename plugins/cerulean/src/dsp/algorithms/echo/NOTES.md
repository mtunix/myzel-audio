# Echo

A warm, wobbly multi-head tape echo melting into a plate: every repeat is darker and thinner and
washes into the plate behind it.

## Topology

```
in L/R ─► (L+R)/2 ─► record EQ (HP 40 Hz, LP 9 kHz) ─► (+) ─► soft saturation ─► tape (mono)
                                                        ▲                          │
              × Repeats ◄─ HP 110 Hz ◄─ LP 3.6 kHz·2^(0.75 Tone) ◄── main head (T) ┤
                                                                                   │
   heads: T/3 (left), 2T/3 (right), T (centre), gains 0.30 / 0.42 / 1.0 ◄──────────┘
      ─► playback LP (6.5 kHz·2^(0.6 Tone)) ─► echoes ── × 0.85 · (1 − freeze) ───────────┐
      echoes · (1 − freeze) + 0.6 · in ─► bandwidth LP ─► 4-stage diffuser/side           │
         ─► 16-line Hadamard FDN, 9-45 ms (plate) ─► × 0.3 · amount(Decay) ────────────────┴► out
```

- **Tape**: one mono loop (2.5 s + wow margin, ≤ 2 MB at 192 kHz). Three heads read with cubic
  Lagrange at `ratio · T(t) + wow(t)`; the feedback comes from the main head only, so the repeats
  land on T, 2T, 3T… and the side heads add a lighter pulse on the triplet grid between them
  (tested to ±0.03 ms at 90, 375 and 400 ms).
- **Each repeat darker and thinner**: the loop's head/tape loss (one-pole LP 3.6 kHz at flat Tone,
  1.9–6.1 kHz over the Tone range) and a 110 Hz one-pole low cut act once per pass. Measured with a
  pink-noise burst: the share above 3 kHz goes −9.8, −12.3, −14.5, −16.5, −18.3 dB over the first
  five repeats; below 150 Hz −5.6 … −13.4 dB.
- **Saturation**: Padé tanh `x(27 + x²)/(27 + 9x²)` (clamped at ±3), driven at 0.6 and scaled
  back, so it is linear at playing level (−0.06 dB at −12 dBFS), −0.9 dB at 0 dBFS peaks, and
  softly limits louder build-ups. |y| ≤ |x| and slope ≤ 1, and every loop filter has |H| ≤ 1, so
  with feedback ≤ 0.95 the loop is contractive and **can never run away** (tested: +6 dBFS noise
  into Repeats 100 %, 20 ms, bright → peak 1.9, gone 60 dB 3 s later).
  No oversampling: the record path is low-passed at 9 kHz and the loop at ≤ 6 kHz before the
  nonlinearity, and the saturation is light, so the 3rd harmonic is small and lands mostly above
  the 6.5 kHz playback loss. Umber's Reel model (4× oversampled Jiles–Atherton) would cost
  several times the whole budget.
- **Wow & flutter**: wow = 0.6 sine + 0.4 smooth random at 0.55 Hz × Mod Rate, peak pitch
  deviation 0.8 % × Mod (default Mod 0.35: 0.28 %, ~5 cents); flutter = 0.7 sine + 0.3 random at
  6.3 Hz × √Mod Rate, 0.12 % × (0.25 + 0.75 Mod). Depth in samples = deviation · fs / (2π f), so
  the wobble in pitch is the same at every rate and echo time, as on a real transport. The same
  modulation moves all heads (one tape). Echo arrival times wander by 3.2 ms at Mod 1, 0.01 ms at
  Mod 0.
- **Echo time glide**: the tape time is a transport with inertia: pulled to the target with a
  200 ms time constant, at most 0.5 samples/sample (an octave down when lengthening, +50 % when
  shortening), and that speed itself changes with a 30 ms time constant. So a time change (free or
  synced, incl. tempo changes) is a pitch glide that bends in and out. An abrupt 300 → 450 ms jump
  arrives in 1.0 s with a 3rd-difference crest of −70 dB (a plain slew-limited glide: −41 dB).
- **Plate**: a compact plate-like FDN (not the Plate algorithm): 16 short lines, 9–45 ms (mean
  24 ms), Hadamard mixing, strongly coupled 1.4–5.2 ms input diffusers (coefficient
  `0.72 √diffusion + 0.08`), light modulation (0.6 ms × Mod, 0.9 Hz). Echo density reaches 0.9 in
  21 ms: instantly diffuse and bright, as a plate should be.
- **In series**: the plate hears the echoes (and 0.6 of the note itself, as an echo pedal into a
  reverb pedal would), so each repeat washes into the tail.

## Character controls

| Control | Effect |
|---|---|
| Param A | Unused: the echo time comes resolved in `EngineParams::echoTimeMs` (free `maps::echoTimeMs` or synced, ≤ `maps::echoMaxMs`) |
| Param B, **Repeats** | Feedback = Repeats × min(95 %, 10^(−3T / 24 s)). Measured per-repeat steps at T = 400 ms: −13.4 / −7.4 / −3.9 / −1.4 dB at 25 / 50 / 75 / 100 % (predicted from the loop loss to 0.01 dB); 0 % = one echo (three heads), no repeats |

The 24 s ceiling on the trail: up to 185 ms echoes Repeats reaches 95 % per repeat; longer times
top out lower (500 ms 87 %, 1 s 75 %, 1.5 s 65 %), so the trail never outlasts ~24 s. At 95 % a
1.5 s echo would ring for over three minutes, which is a drone, not an echo (Freeze is for holding
things), and it failed the "tail always decays" gate.

## Other controls

- **Decay**: the plate's RT60 (exact, Fdn), and the plate's **amount**: `0.45 + 0.55 smoothstep`
  over log(Decay) 0.3–5 s, so a short Decay is a little ambience behind the echoes and a long one a
  wash. The echo level is fixed.
- **Tone**: plate damping (`maps::toneToHighDecay`) and bandwidth (11 kHz × 2^(0.8 Tone)), plus the
  tape's loop loss and playback loss: dark Tone means murky, quickly-darkening repeats.
- **Low End, Size, Diffusion, Mod, Mod rate**: plate as usual; Mod/Mod rate also drive wow/flutter.
- **Freeze**: the plate holds (Fdn: lossless, unmodulated); the echoes stop: tape feedback, echo
  output and the plate send all ramp to 0 with freeze (per-sample ramps), so nothing new enters the
  frozen plate and **no nonlinearity is inside a frozen loop** (the tape loop is open at freeze 1).
  On release the tape is empty and fresh echoes start. Frozen 150 ms windows stay within 1.9 dB
  (unfrozen they would pulse with the repeats); the gate's 57 s drift is 0.3 dB.

## Gate options

- `base.paramB = 0` (Repeats off): Repeats is a second decay, deliberately independent of Decay.
  At the default 50 % a 300 ms echo's trail lasts ~3 s and would dominate the measured RT60 at
  Decay 1 s (measured 2.5 s). So the gates measure the plate (plus the three head taps). The
  "tail always decays" corners and the square-wave and click gates still sweep Repeats 0–100 %.
- `cpuBudgetPercent = 5`: PLAN.md's budget for Echo.

## Measured (48 kHz, Repeats 0)

| Decay | RT125 | RT250 | RT mid | RT 4k | RT 8k | Echo density 0.9 | Wet level | L/R ρ |
|---|---|---|---|---|---|---|---|---|
| 1 s | 1.00 | 1.00 | 0.95 | 0.72 | 0.56 | 21 ms | | 0.80 (echoes dominate) |
| 3 s | 2.97 | 2.95 | 2.80 | 2.05 | 1.56 | 22 ms | −3.4 dB (2 s) | 0.36 |

RT60 gate: 0.95 / 2.35 / 5.48 s for 1 / 2.5 / 6 s. Wet level on pink noise: −3.4 dB (Repeats 0),
−3.0 dB (50 %), −0.8 dB (100 %) at Decay 2 s. Click gate −64 dB automated (static −86).
Square-wave gate peak 26 (0 dBFS square, Decay 60 s, freeze toggling; limit 64). CPU 1.0 % (budget 5 %).

## References

- J.-M. Jot, A. Chaigne (AES 1991): FDN with per-band decay (the plate tank).
- J. Dattorro, *Effect Design Part 1* (JAES 1997): plate character (short lines, strong input
  diffusion, bandwidth filter) — values are our own.
- J. Dattorro, *Effect Design Part 2: Delay-Line Modulation and Chorus* (JAES 1997): modulated
  delay reads, pitch deviation vs. depth and rate.
- Multi-head tape echoes (equally spaced playback heads, feedback from one head, loss per pass,
  wow/flutter, motor-speed glide): behaviour reference only.
