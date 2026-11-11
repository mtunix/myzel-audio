# Rise

Volume swells into a slow, lush cathedral: notes fade in with no attack ("slow gear" into a big
hall), and each new note blooms in on top of the previous note's tail, which keeps ringing.

## Topology

```
in L/R ─┬─► onset detector (undelayed; fast peak env, running baseline, note peak) ──► trigger
        │                                                                              │
        └─► bandwidth LP (Tone) ─► 5 ms wet look-ahead ─► 6-stage allpass diffuser/side│
              ─► routing (2 ms crossfade; a trigger sends the note to the quieter tank) ◄┘
                    ├─► tank A: 16-line Hadamard FDN, 41-163 ms ─► × swell A ─┐
                    └─► tank B: same, lengths × 1.0737          ─► × swell B ─┴─► × 0.5 ─► out

dry gain (getDryGain) = 1 − Blend · (1 − freeze) · (1 − dry swell)
```

### Where the swell sits, and why two tanks

| Option | Impulse / noise gates | A new note over an old tail |
|---|---|---|
| Swell on the tank *input* (physical slow gear → reverb) | An impulse is erased: no measurable tail | Old tail untouched |
| Swell on one tank's *output* | Measurable (tail swells, then decays at RT60) | Old tail ducks to 0 and swells back with every new note: pumping |
| **Swell on the output of two tanks, new note → quieter tank** | Measurable | **Old tail untouched, new note blooms in on top** |

The two-tank form is what Rise uses. A trigger routes the input (behind a 5 ms wet-only look-ahead,
so the switch always happens before the note arrives) to whichever tank is quieter (100 ms energy
of its post-swell output). That tank's swell restarts: whatever older tail it still held fades out
over 8 ms (it is the quieter one, so this is masked) and the S-curve brings the tank up over the
Rise time. The other tank keeps ringing at its own gain. The cost is a second FDN, which fits the
budget (1.8 % total).

**Short notes:** with an output swell, a note shorter than the Rise time leaves a tail that is
already decaying while its gain is still creeping up. Once the note has died away (detector
re-armed), the rest of its swell completes within 600 ms: the wash behind a short note blooms up
gently, then rings out at the set Decay (tested: 10.4 dB/s at Decay 6 s). Without this, Rise 4 s
failed the "tail always decays" gate (the wet was still swelling between 1 and 2 s).

### Onset detector (level-adaptive, no threshold knob)

- `envFast`: peak follower of max(|L|, |R|), 0.3 ms attack, 30 ms release.
- `baseline`: 60 ms one-pole of `envFast`. A **trigger** needs `envFast > 2.8 × baseline` (+9 dB
  jump), `envFast > −60 dBFS` (hum and noise floor never trigger), and the detector **armed**.
- **Re-arm** once the note has died away: `envFast` 12 dB under the note's peak (the peak sags
  slowly, 2.2 dB/s, so a long fade still re-arms) or under −60 dBFS, and at least 50 ms after the
  last trigger (one pick never triggers twice).
- So legato (a synth's tied notes, hammer-ons, tremolo, fast picking over a ringing note) keeps
  one swell, and a pick after the last note has decayed 12 dB (or was muted) swells again. That is
  how slow-gear players work: mute between notes to get a swell on each.

### Swell curve

`g(p) = smoothstep(p)²`, p = time / Rise time: −39 dB at 20 %, −12 dB at 50 %, −1.9 dB at 80 %,
0 dB at 100 %. Roughly linear in dB through the middle (even in loudness, like a volume pedal
with an audio taper) and flat at both ends, so there is no audible start or "arrival".
On a retrigger the held value ducks exponentially while the curve rises
(`held·d + (1 − held·d)·g(p)`), so the gain never jumps.

### The dry (Blend)

The dry swell uses the same curve, triggered by the same detector. When the note has died away
(re-arm), the dry fades out over 250 ms, so the next pick starts from silence and its attack is
hidden (a dry still open when a new pick arrives ducks in 3 ms; the chain's 3 ms smoother is on
top). Blend mixes continuously: dry gain = 1 − Blend·(1 − swell). It is ramped per sample.

## Character controls

| Control | Effect |
|---|---|
| Param A, **Rise time** | Swell time, `maps::riseTimeMs` 50 ms – 4 s. Wet reaches −6 dB at 64 % and −1 dB at ~85 % of it |
| Param B, **Blend** | 0 = only the wet swells (dry gain exactly 1); 1 = the dry swells too; continuous between (0.5 → the dry dips to 0.5) |

## Other controls

- **Decay, Low End, Tone, Size, Mod, Mod rate**: as Hall, applied to both tanks. Mod depth up to
  2.2 ms (Hall 1.6 ms), 70 % smooth random, 0.35 Hz nominal: lusher and slower, a little chorused.
- **Diffusion**: diffuser coefficient `0.7 √diffusion + 0.06` (≤ 0.78).
- **Freeze**: both FDNs lossless (Fdn), and every swell, duck and close stops moving
  (rates × (1 − freeze)), so nothing changes level while frozen (measured drift 0.4 dB over 57 s).
  While frozen the chain cuts the engine's input, so the detector can't hear the player: the dry
  gain is released to 1 (`× (1 − freeze)`) instead of staying stuck closed. On release, the
  returning input triggers a new swell into the quieter tank, over the decaying pad.

## Tuning decisions

- Tank lengths 41–163 ms (mean 93 ms), ~20 % longer than Hall: a slower, bigger room. Tank B is
  the same set × 1.0737, so consecutive notes don't share modes.
- 6 diffuser stages (3–28 ms) instead of Hall's 4: smoother texture (the onset is hidden by the
  swell anyway). Lengths are whole samples at 48/96/192 kHz (n / 48 ms): with fractional lengths
  the Lagrange smear differs per rate and echo density at 96 kHz trailed 48 kHz by 18 ms.
- No early reflections: they are attack, which Rise exists to remove.
- Output gain 0.5 → −3.5 dB wet on steady pink noise at Decay 2 s.

## Gate options

The gates run at Rise time 50 ms (`base.paramA = 0`), so the swell is complete before anything is
measured; the swell has its own tests. At the default Rise (447 ms) the RT60s are the same
(0.96 / 2.32 / 5.55 s), but the steep swell envelope sits right at the echo-density 0.9 crossing
(the first ~150 ms are 30+ dB down, inaudible) and makes that crossing differ by 12 ms between
48 and 96 kHz.

## Measured (48 kHz)

Gates (Rise 50 ms): 

| Decay | RT125 | RT mid | RT 4k | RT 8k | Echo density 0.9 | Wet level | L/R ρ | Mono sum |
|---|---|---|---|---|---|---|---|---|
| 1 s | 0.99 | 0.99 | 0.80 | 0.65 | 74 ms | | | |
| 2.5 s | 2.45 | 2.31 | 1.80 | 1.38 | 73 ms | −3.5 dB (2 s) | 0.02 | −2.9 dB |
| 6 s | 5.98 | 5.54 | 4.15 | 3.10 | 73 ms | | | |

Click gate: −67 dB automated (static −85). Freeze: 0.4 dB drift over 57 s. CPU 1.8 % (budget 3 %).

Swell (held pink noise, Decay 2 s):

| Rise time | −6 dB at | −1 dB at | first 10 % of the rise |
|---|---|---|---|
| 200 ms | 175 ms | 335 ms (tank build-up dominates) | −222 dB |
| 1 s | 605 ms | 790 ms | −80 dB |
| 4 s | 2355 ms | 3395 ms | −71 dB |

- Blend 1, Rise 1 s: dry gain 0 at the pick, 0.25 at 500 ms, 0.999 at 986 ms; closes to 0.0006
  1.9 s after a note is muted; 1.0 while frozen.
- Retriggers: held noise 1, legato melody with ±2 dB tremolo 1, four picks after silence 4, a
  pick over a note 24 dB down 2, eighth notes over a ringing note 1.
- New note over an old tail (Rise 1 s, Decay 4 s): first 100 ms of the new note differ from the
  old tail alone by < 0.1 dB (no attack, no duck); 1.1–1.9 s later the wet is 35 dB above it.

## Notes for the chain

- The engine detects onsets on its own input, which is **after the pre-delay**, while the chain
  applies `getDryGain()` to the undelayed dry. With pre-delay > 0 and Blend up, the dry swell
  starts late by the pre-delay (the dry stays closed until then, so the attack is still hidden).
  A sidechain copy of the undelayed, unfrozen input for engines would fix this and let the dry
  swell work while frozen too (see the report).

## References

- J.-M. Jot, A. Chaigne, *Digital delay networks for designing artificial reverberators* (AES 1991).
- G. Luff, *Let's Write a Reverb* (ADC 2021): diffuser + Hadamard FDN.
- Boss SG-1 Slow Gear / BigSky-style swell reverbs: behaviour reference only (gate-and-swell
  retriggering, wet/both modes); no code or circuit used.
