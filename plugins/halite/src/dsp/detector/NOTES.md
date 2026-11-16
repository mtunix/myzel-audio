# Detector: sibilance and harshness detection

Research notes for Halite's detectors and the parts of the reduction they feed (`PLAN.md`,
*Detection*, *The Harsh band*, *Reduction*). Product names appear only here and in `PLAN.md`
(root `NOTICE`).

Status: M0 literature notes, then the M1 implementation (*Implementation (M1)*: what was built,
tested on synthetic signals), the corpus measurement (*Measured (M1)*: the noise cue kept, the
constants, the threshold ranges and the gates on held-out speakers) and the M1b round (*M1b*, at
the end: the shape cue, the onset and persistence rules, the revised gates on both halves).
*Implementation (M1)*'s constants table describes the detector as it now is. Each number says
where it comes from:

- **source**: read in the cited paper, manual, page or source file;
- **measured (synthetic)**: computed in this run with NumPy/SciPy on synthetic signals, by the
  method given next to the numbers. These are not corpus results. M1 repeats the relevant ones
  through the real chain in `tools/measure`;
- **inference**: reasoning from sources, not measured;
- **plan value**: `PLAN.md`'s number, with no source found;
- **not verified**: looked for and not found, or not read.

The proposals that would change `PLAN.md` are collected under *Proposed changes to PLAN.md*.

## Sources

What was actually read, and how far each can be trusted. The full bibliography is in `PLAN.md`,
*References*.

| Source | What it gives | Trust |
|---|---|---|
| Jongman, Wayland, Wong, *Acoustic characteristics of English fricatives: I. Static cues*, Cornell working paper 1998 (the full text, read) | peak locations, durations, noise-to-vowel levels for 20 speakers (10 F, 10 M) | primary; American English, careful CVC syllables, sampled at 22 kHz (nothing above 11 kHz) |
| Jongman, Wayland, Wong, JASA 2000 (the journal version) | the same study with dynamic cues | bibliographic record and abstract only |
| Koenig, Shadle, Preston, Mooshammer, JSLHR 2013 (full text on PMC) | /s/ mid-band peaks of adolescent girls and boys | primary; 12 speakers, 22 kHz |
| Monson, Hunter, Lotto, Story, Frontiers in Psychology 2014 (full text) | high-frequency energy (HFE, above ≈ 5 kHz) of speech and singing | review |
| Monson, Lotto, Story, JASA 2012 | HFE by sex, level, mode, fricative | abstract only |
| Pincas, Jackson, JASA 2006 | amplitude modulation of frication by voicing | abstract only |
| Howell, Rosen, JASA 1983 | rise times of voiceless affricates vs fricatives | abstract only |
| Gray, Markel 1974; Johnston 1988; Dubnov 2004; Madhu 2009 | spectral flatness: definition, uses, limits | bibliographic records and abstracts; Johnston's paper was available only as a scan, not read |
| Griffin, Lim 1988 (multiband excitation vocoder) | per-band voiced/unvoiced decisions | bibliographic record and abstract |
| Vicanek, *Matched Second Order Digital Filters*, 2016 (full text) | closed-form magnitude-matched biquads | primary; the peaking-EQ formulas were implemented and measured here |
| Simper, *Linear Trap Integrated SVF*, Cytomic 2013/2016 (full text) | bell and shelf responses from the TPT SVF's outputs (mixing coefficients) | primary |
| Giannoulis, Massberg, Reiss, JAES 2012 | compressor gain computer, knee, detectors | bibliographic record |
| Cappé 1994; Ephraim, Malah 1984; Breithaupt, Gerkmann, Martin 2008; Esch, Vary 2009 | musical noise and its suppression | bibliographic records and abstracts |
| J. O. Smith, *Spectral Audio Signal Processing* (online book), WOLA page | analysis and synthesis windows | primary |
| Open-source de-essers: LSP Deesser manual and parameter source; Calf manual and `metadata.cpp`; TAP DeEsser manual; mda DeEss source (`mdaDeEss.cpp`); Airwindows DeBess source; project READMEs | what each does | primary for what was read; READMEs of new projects are self-descriptions, not tested |
| Two Ardour forum threads on open-source de-essers (2022, 2024) | which tools Linux users name, user reports | anecdotal |
| Dolby, US 11,322,170 B2 (Google Patents page) | a granted patent on level-independent de-essing | bibliographic data and claim 1 as shown there; not legal advice |
| Fox, Nissen 2005; Boll 1979; Regalia, Mitra 1987; Zavalishin 2018 | background only: sex differences in voiceless fricatives; spectral subtraction; tunable EQ sections; TPT filter theory. Nothing in these notes rests on them | bibliographic records |

## Sibilant acoustics

### Spectral peaks

| Sound | Main spectral peak | Source |
|---|---|---|
| /s/, /z/ | mean **6882 Hz** (all speakers, vowels, voicing) | source: Jongman 1998, Table 1 |
| /ʃ/, /ʒ/ | mean **3712 Hz**; the earlier literature puts it at about 2.5–3 kHz | source: Jongman 1998, Table 1 and §1.1 |
| all fricatives, by sex | female 6809 Hz, male 6066 Hz | source: Jongman 1998, §3.1 |
| /s/, adolescent girls | 6320 Hz (non-labial context), 5742 Hz (labial) | source: Koenig 2013, Table 4 |
| /s/, adolescent boys | 4843 Hz (non-labial), 4401 Hz (labial) | source: Koenig 2013, Table 4 |
| /s/, adults | lowest main peak about 3.5–5 kHz, most energy above 4 kHz | source: Koenig 2013 (literature review) |
| /tʃ/, /dʒ/ | voiceless affricates rise faster than the fricatives, but no single rise time separates them | source: Howell, Rosen 1983 (abstract; voiceless only). That affricate frication resembles /ʃ/, /ʒ/ after a closure and burst is general phonetics, not from a source read here. Peak values **not verified** |

Perception agrees: synthetic noise with its peak below 3 kHz is heard as /ʃ/, between about 4.5
and 6.5 kHz as /s/ (Heinz and Stevens 1961, as summarised by Jongman 1998; the 1961 paper itself
not read). Some girls' /s/ resonances lie above 7 kHz (Koenig 2013), and women's HFE exceeds
men's significantly only above 11 kHz (Monson 2012). Both measured sets stop at 11 kHz, so
nothing here describes sibilant energy above 11 kHz.

For Halite: the default Detect band (4.5–12 kHz) covers the /s/, /z/ peak of adults of both sexes
and of girls; a band starting at 4.5 kHz misses the /ʃ/, /ʒ/ peak (2.5–3.7 kHz) and with it the
main energy of /tʃ/ and /dʒ/. *Relative detection* below shows that the plan's body band makes
this worse.

### Levels against the vowel

Jongman 1998 measured the RMS level of the whole frication noise against the RMS of the
following vowel over three pitch periods at its loudest point (Table 3):

| | Noise − vowel |
|---|---|
| /s/, /z/ | **−10 dB** |
| /ʃ/, /ʒ/ | −9.1 dB |
| voiceless fricatives (all places) | −11.1 dB |
| voiced fricatives (all places) | −15.9 dB |
| before /i/ … before /o/ (all fricatives) | −12.7 … −14 dB |

No main effect of sex. These are broadband levels. In the sibilance band the picture flips: in the
typical (long-term average) speech spectrum, energy above 5 kHz is 20–40 dB below the
low-frequency part (Monson 2014, citing Moore et al. 2008 and Monson et al. 2012), so in
4.5–12 kHz an /s/ stands well above the vowel's own top end (inference).

For Halite: `B − V` compares the band level of the ess with the body (broadband below Detect Low)
of the voice. Because nearly all /s/ energy is in the band and nearly all vowel energy is in the
body, a *typical* /s/ reads `B − V ≈ −10 dB` (inference from Table 3). The plan's Threshold range
(0 … +24 dB, default +6) only starts above that, so at the defaults a typical ess is not reduced
and only esses at least 16 dB hotter than the average are. The gate "≥ 95 % of /s/ produce an
event" needs a threshold well below the plan's range. See *Proposed changes*, item 5.

### Durations

| | Frication duration | Source |
|---|---|---|
| /s/, /z/ | 148 ms | source: Jongman 1998, Table 2 |
| /ʃ/, /ʒ/ | 150 ms | source: Jongman 1998, Table 2 |
| voiceless / voiced (all places) | 171 ms / 102 ms | source: Jongman 1998, §3.2 |
| female / male | 139 ms / 135 ms | source: Jongman 1998, §3.2 |

These are syllable-initial fricatives in a carrier phrase; durations in connected speech were not
found in the sources read (**not verified**). Listeners already identify place from the first
20 ms (Jongman 1989, cited there).

For Halite: events of roughly 50–200 ms. The 20 ms event hangover, the 0.5 dB start criterion and
Auto release 15–80 ms fit. The body envelope's 80 ms release does not (*Relative detection*).

### Voicing in /z/, /ʒ/

Voiced fricatives carry voicing in the body band and their frication noise is amplitude-modulated
at f0. The modulation index grows with voicing strength and saturates at about 0.5; /z/ is the
most modulated (Pincas, Jackson 2006, abstract). This matters for the noise cue below: an envelope
cue sees /z/ as partly periodic, flatness does not.

## Spectral flatness

### Definition and use

Spectral flatness is the geometric mean of the power spectrum over its arithmetic mean, over the
bins of interest: 1 (0 dB) for a perfectly flat spectrum, towards 0 for a peaky one. Gray and
Markel (1974) introduced it as a whiteness measure for linear prediction; Johnston (1988) used it
per critical band to tell tone-like from noise-like maskers in a perceptual coder (his mapping from
flatness to a tonality coefficient was not read here, the only copy found being a scan). Madhu
(2009) analyses its shortcomings and proposes an entropy-based alternative; Dubnov (2004)
generalises it to non-Gaussian processes.

### Behaviour on noise and on harmonics

Measured (synthetic). 48 kHz, 4 s of each signal, periodic Hann frames, flatness of the bin power
inside 4.5–12 kHz, median over frames in dB (p10, p90 where noted). Harmonic signals: equal
amplitudes up to 16 kHz, random phases; vibrato 5.5 Hz; jitter a 5 ms-smoothed random walk of the
stated relative depth; the coloured noise is white noise through a +15 dB peaking filter at 7 kHz,
Q 2 (a crude ess spectrum).

| Signal | N = 128 (2.7 ms) | 256 (5.3 ms) | 512 (10.7 ms) | 1024 (21.3 ms) | 2048 (42.7 ms) |
|---|---|---|---|---|---|
| white noise | −2.2 | −2.3 | −2.4 | −2.5 | −2.5 |
| noise with a +15 dB peak | −3.9 | −4.2 | −4.4 | −4.5 | −4.6 |
| harmonic, f0 110 Hz | −2.2 | −2.1 | −1.6 | −0.7 | **−8.3** |
| harmonic, f0 220 Hz | −2.2 | −1.6 | −0.8 | **−8.4** | **−21.9** |
| harmonic, f0 440 Hz | −1.8 | −0.7 | **−8.2** | **−21.9** | **−36.7** |
| harmonic, f0 880 Hz | −0.8 | **−8.6** | **−22.3** | **−37.0** | **−52.6** |
| 220 Hz, vibrato ±50 cents | −1.6 | −1.4 | −0.8 | −3.9 | −3.2 (p10 −12.9) |
| 440 Hz, vibrato ±50 cents | −1.7 | −0.8 | −7.2 | −14.5 | −15.0 (p90 −9.8) |
| 220 Hz, 0.5 % jitter | −2.0 | −1.8 | −0.8 | −3.6 | −4.3 |

What this shows:

- **Noise does not read 0 dB.** The bins of a single periodogram of Gaussian noise are
  exponentially distributed, so the expected log-mean sits γ (Euler's constant) below the log of
  the mean: expected flatness e^−γ = 0.561, **−2.51 dB** (inference; the measured −2.2 … −2.5 dB
  agrees). Averaging the bin power over K frames before taking flatness moves noise towards 0 dB
  (measured, N = 256: −1.1, −0.5, −0.3 dB for K = 2, 4, 8). The "noise range" in the plan's
  mapping must be defined against this, not against 0 dB.
- **Flatness also measures the spectral envelope.** A resonant ess spectrum reads 2 dB less flat
  than white noise without any tonality. Flatness over sub-bands of about 1/3 octave, or over a
  spectrum divided by its smoothed envelope, isolates the fine structure (inference).
- **Harmonics read as noise until they are resolved.** A Hann main lobe is 4 bins wide, so
  harmonics separate only when f0 ≳ 4·fs/N: at 48 kHz, 1.5 kHz for N = 128, 750 Hz for 256,
  375 Hz for 512, 190 Hz for 1024, 94 Hz for 2048 (inference, matching the table's bold cells).
  Unresolved harmonics even read *flatter* than noise, because their bins fluctuate less.
- **Vibrato and jitter defeat long frames.** At 10 kHz a ±50-cent vibrato moves a harmonic by
  ±300 Hz; within a 43 ms frame the comb smears and the 220 Hz voice reads −3.2 dB, next to
  noise.
- Harmonic plus noise (f0 220 Hz, in-band harmonic-to-noise ratio): N = 256 reads −1.6 … −2.4 dB
  whatever the ratio; N = 2048 reads −18.9 dB at 30 dB HNR, −8.7 at 10 dB, −3.9 at 0 dB.

For Halite: the plan's **256-point frame (Normal) cannot tell a harmonic from noise for any f0
below about 750 Hz** at 48 kHz, which is every speaking voice and nearly every sung note; the
**128-point causal frame (Live) nothing below 1.5 kHz**. As specified, the flatness cue would leave
the "Bright isn't sibilant" gate to the band detector alone. A 2048-point frame (scaled with the
rate) works for steady voices from about 100 Hz but not on vibrato or jitter, and in Live it is a
43 ms causal window.

### A candidate cue that does not need resolved harmonics

A voiced sound's high band beats at f0 even when its harmonics are not resolved; noise has an
aperiodic envelope. The multiband excitation vocoder decides voiced/unvoiced per band on the same
principle, by comparing each band with a periodic model (Griffin, Lim 1988).

Measured (synthetic): band-pass 4.5–12 kHz (4th-order Butterworth), square, low-pass at 1.2 kHz,
remove the frame mean, take the largest normalised autocorrelation over lags 1–12.5 ms
(f0 80 Hz … 1 kHz). Median (p10, p90):

| Signal | N = 1024 (21.3 ms) | N = 2048 (42.7 ms) |
|---|---|---|
| white noise | 0.30 (0.24, 0.42) | 0.19 (0.15, 0.25) |
| harmonic, f0 110 or 220 Hz | 1.00 | 1.00 |
| 220 or 440 Hz, vibrato ±50 cents | 0.99 | 0.97–0.98 |
| harmonic 220 Hz + noise, HNR 10 dB | 0.86 | 0.85 |
| harmonic 220 Hz + noise, HNR 0 dB | 0.39 | 0.33 |
| noise amplitude-modulated at 120 Hz, index 0.25 | 0.43 | |
| noise amplitude-modulated at 120 Hz, index 0.5 | 0.59 (0.46, 0.69) | |

(Values slightly above 1 occur through the lag-bias correction.) On *stationary* signals this
separates harmonic from noise at any f0 from 80 Hz, with vibrato, in 21 ms. Its weakness there is
the voiced fricative: noise modulated like a /z/ (index 0.5, the saturation value of Pincas and
Jackson) reads 0.59, between noise and voice, where flatness reads it as plain noise (−2.4 to
−2.5 dB at N = 1024 and 2048 for every index). The mapping from this cue to the 12 dB of Focus
attenuation would need its noise range to include about 0.6.

#### On onsets, rises and decays

Real esses are not stationary: they start after a vowel or a pause, rise, and decay. Removing only
the frame mean leaves any level change inside the window in the envelope, and its autocorrelation
reads it as periodic. Measured (synthetic), the same cue (lags in steps of two samples), N = 1024,
median of 200 trials; the detrending variants replace the mean removal with a linear or quadratic
fit over the frame, or high-pass the envelope at 60 Hz (2nd-order Butterworth) before framing:

| Signal | mean only | linear fit | quadratic fit | 60 Hz high-pass |
|---|---|---|---|---|
| stationary noise | 0.31 | 0.31 | 0.33 | 0.32 |
| noise rising linearly across the window | **0.63** | 0.30 | 0.28 | 0.29 |
| noise starting at 25 / 50 / 75 % of the window | **0.45 / 0.62 / 0.64** | 0.30 / 0.28 / **0.47** | 0.34 / 0.27 / 0.24 | 0.31 / 0.26 / 0.26 |
| noise decaying 12 dB across the window | **0.55** | 0.28 | 0.28 | 0.28 |
| vowel (in-band 20 dB under the ess), the ess from 50 % | **0.61** | 0.28 | 0.28 | 0.27 |
| noise AM at 120 Hz, index 0.5 (/z/-like) | 0.59 | 0.59 | 0.57 | 0.59 |
| harmonic, f0 110 / 220 Hz | 0.99 / 1.00 | 0.99 / 1.00 | 0.98 / 1.00 | 0.99 / 1.00 |
| harmonic 220 Hz rising across the window | 0.63 | 0.60 | 0.68 | 0.68 |
| harmonic 220 Hz starting at 50 % | 0.65 | **0.40** | **0.39** | **0.47** |

Over time, on the 1024-sample window ending *d* ms after a vowel-to-ess change (median of 150):
mean only reads 0.53–0.64 from 3 to 12 ms after the onset, as periodic as a /z/, and would hold
`S` down through the first part of the ess, the audible part; the quadratic fit and the high-pass
read 0.25–0.33 from 2 ms on. After an ess-to-vowel change all three read the vowel as periodic
only once it fills most of the window: 0.37–0.39 at 12 ms, 0.62–0.63 at 16 ms, 0.95–0.96 at
21 ms.

At N = 512 (10.7 ms) the cue is too noisy to use: stationary noise reads 0.66–0.69 with any
detrending. A normalised cross-correlation of the newest window against the one a lag earlier
(as pitch trackers compute it, on the high-passed envelope) keeps a rising harmonic periodic
(0.98) but reads a noise ramp at 0.45 and a vowel starting mid-window at 0.45; no variant tried
handles every case.

So the cue needs **detrending as part of its definition**: the 60 Hz high-pass is the cheapest,
since it runs once per sample before the envelope is decimated (below), and its numbers match the
quadratic fit. With it the cue's errors point the safe way for a de-esser: an ess after a vowel
reads as noise within about 2 ms, while a vowel after an ess (or a pause) reads as noise for up to
about 16 ms, so the Focus attenuation engages late on vowel onsets, not on esses. The bright-vowel
gate therefore needs vowel *onsets* (sung and spoken) as well as steady vowels. Untested on real
voices; M1 decides on the corpus, with ess onsets after vowels and vowel onsets after esses in the
gates.

**Where the window sits.** The window ends at the newest sample the detector has seen. In Normal
latency (512 samples, 10.7 ms at 48 kHz) that sample is Look-ahead ahead of the output, so at the
default 5 ms look-ahead an ess onset is called noise about 3 ms before it reaches the output
(2 ms to decide, 5 ms early), and at 0 ms look-ahead about 2 ms after. In Live there is no
look-ahead: Focus holds `S` down for the first ≈ 2 ms of an ess after a vowel (with the mean only,
for 12–16 ms). M1 measures the onset delay in Live and Normal separately.

**Cost.** Computed directly as measured here, every lag of 1–12.5 ms over a 1024-sample frame at
every 32-sample hop is ≈ 580 M multiply-adds per second per channel at 48 kHz; compiled with -O3
it took 21 % of one core of this machine (i9-12900K, scalar loop, measured), against a budget of
0.5 % for Split or Wide in stereo with the whole detector. The envelope is low-passed at 1.2 kHz,
so it can be decimated by 8 (to 6 kHz, after a steeper anti-alias low-pass at 2.4 kHz) and the
autocorrelation kept as running sums per lag (70 lags, two multiply-adds per lag per decimated
sample): 0.84 M multiply-adds per second per channel, 0.005 % of the same core (measured).
Decimated this way (a 128-sample frame, lags 6–75), the high-passed cue moved by at most 0.09 on
every signal of the table above (harmonic 110 Hz: 1.00 to 0.91; measured). Running sums in float
drift, so they need double precision or a periodic recompute; M1's `[.perf]` test checks the
result against the budget. A flatness frame of 2048 points every 32-sample hop is 1500 FFTs per
second per channel (not measured; by the usual 2.5·N·log2 N estimate ≈ 85 M flops), so if
flatness stays it needs a coarser hop (256 samples: 188 FFTs per second).

### Frame-length trade-off

| | Short frame (128–256) | Long frame (1024–2048) |
|---|---|---|
| Time resolution | an ess onset to a few ms | onset blurred by 21–43 ms (esses last 100–170 ms) |
| Harmonic resolution | none below 750–1500 Hz f0 | from ≈ 100–190 Hz f0, steady pitch only |
| Estimator spread (noise, p10–p90) | 1.4–2 dB | 0.6–0.7 dB |
| Live (causal) | cheap, useless as a tonality cue | a 21–43 ms look-back: with the detrended periodicity cue an ess onset reads as noise within ≈ 2 ms, a vowel onset as periodic only after ≈ 16–21 ms (above) |

## Relative (level-independent) detection

The idea is to compare the sibilance band with a reference taken from the same voice, so a gain
change before the plugin changes neither. With linear envelope followers (homogeneous of degree
one in the signal) and dB thresholds, `B − V` is exactly gain-invariant while both are above the
body floor (inference); the plan's −60 dBFS floor breaks invariance below it on purpose.

Capability references:

- **Lisp** (Sleepy-Time DSP, freeware): per secondary descriptions of its manual, a "voice band"
  and a second band derived from the detected pitch, run through the vendor's transient detector;
  no threshold or frequency setting. Not verified from the vendor.
- **Softube Deesser:** its product page calls its relative threshold an automatic level-independent
  algorithm. Source: vendor page.
- **Pro-DS** (FabFilter): a "Single Vocal" detection mode and an "Allround" mode; the algorithm is
  not published. Source: vendor help.
- **Waves Sibilance:** `PLAN.md` lists it for "level-free operation". **Not verified**: the vendor
  page names its spectral technology but says nothing about level; the vendor's tutorial describes
  a Threshold that sets the level at which it works.

**Patent.** Dolby International AB and Dolby Laboratories Licensing Corp hold US 11,322,170 B2,
*Audio de-esser independent of absolute signal level* (inventors Cengarle, Mateos Sole, Crockett;
priority 2017-10-02, granted 2022-05-03, shown as active with an anticipated expiry in 2039).
Family: EP 3692530 B1, CN 111164683 B, WO 2019/070725, the continuation US 12,051,435 B2. As shown
on Google Patents, claim 1 combines frequency-domain buffers; per buffer an energy envelope, a
sibilance-band energy and a zero-crossing rate; and gain reduction in the sibilance band only when
the sibilance energy relative to the previous buffer's energy envelope passes one threshold **and**
the zero-crossing rate passes another. Halite's cues differ on their face (a body band, not the
previous buffer's broadband envelope; flatness or envelope periodicity, not a zero-crossing rate),
but the continuation's claims were not read and no systematic patent search was done. This is
not legal advice; it belongs on the list for the trademark lawyer's appointment (`PLAN.md`,
Open 1).

### Problems with the plan's body measurement

1. **The body band holds the /ʃ/ peak.** `V` covers 120 Hz up to Detect Low (4.5 kHz), which
   contains the /ʃ/, /ʒ/ peak (2.5–3.7 kHz) and the low skirt of /s/. With a 5 ms attack, `V`
   rises with the fricative itself, so `B − V` of an /ʃ/ is small and /ʃ/, /tʃ/ are seldom caught
   (inference). The gate wants ≥ 95 % of them. Vowel energy is concentrated below about 2 kHz,
   where sibilants have little; a body band of 120 Hz to about 2 kHz (or Detect Low / 2,
   whichever is lower) keeps the fricative out of its own reference (inference).
2. **The body decays during the ess.** An exponential release of time constant τ falls by
   8.69·t/τ dB; with τ = 80 ms that is 10.9 dB after 100 ms and 18.5 dB after 170 ms (the voiceless
   mean). Those figures assume the body band empties during the ess. In the plan's band (120 Hz to
   Detect Low) it does not: the fricative's own energy below 4.5 kHz (item 1; adult /s/ has its
   lowest peaks at 3.5–5 kHz, Koenig 2013) sets a floor under `V`, so 11 dB over 100 ms is an
   *upper bound* for the plan's band, reached only once the band is narrowed. Either way `S` rises
   through the ess, and long esses are reduced more than short ones of the same loudness
   (inference). If "80 ms release" means a time to fall a fixed number of dB the numbers change,
   not the conclusion. `V` should hold while a sibilance candidate is active and release only
   after a hold time longer than the esses (≥ 250 ms suggested; durations above).
   **Items 1 and 2 go together:** narrowing the band without the hold lets the decay act in full;
   the hold without the narrowing keeps the fricative in its own reference. And a body capped at
   2 kHz leaves out F2 and F3 of front vowels such as /i/, so `V`, and with it `S`, comes to depend
   on the vowel. Jongman 1998 already finds noise minus vowel varying with the vowel in broadband
   terms (−12.7 dB before /i/, −14 dB before /o/); for the narrowed band this is inference, not
   measured.
3. **Detector types.** Measured (synthetic): a peak follower (0.5 ms attack) on band noise reads
   +5.2 dB over RMS with 20 ms release and +6.4 dB with 50 ms; on a 7 kHz sine +2.5 and +2.7 dB.
   If `B` is a peak follower and `V` something else, "dB of sibilance over the voice" depends on
   crest factors. Both should use the same detector, and the plan should say which.
4. **Ripple.** The 0.5 ms attack is fine (the look-ahead covers attack in Normal; esses rise over
   tens of ms), but `B` needs a release: measured (synthetic) on 4.5–12 kHz band noise, a peak
   follower with 0.5 ms attack has 0.69 dB standard deviation (3.3 dB p1–p99 span) at 5 ms release,
   0.48 dB (2.2 dB) at 20 ms, 0.39 dB (1.8 dB) at 50 ms. A one-pole mean-square follower: 0.99 dB
   at 1 ms, 0.70 dB at 2 ms, 0.44 dB at 5 ms.
5. **The scale.** As above, a typical /s/ sits near `B − V ≈ −10 dB`; the range and default of
   Threshold have to come from the corpus histogram of `S` (M1).

## Harshness in the 2–6 kHz region

- The singer's formant, a clustering of the third to fifth formants near 2.5–3 kHz in trained
  voices, was explained articulatorily by Sundberg (1974; bibliographic record, the paper not
  read; frequency from a tutorial of the National Center for Voice and Speech, ≈ 3 kHz).
- The ear is most sensitive in roughly the same region (equal-loudness contours, ISO 226:2023;
  the standard's tables were not read, the shape is general knowledge and **not verified** here).
- Trained tenors' singing spectra show a second, fairly consistent peak near 10 kHz (Titze and Sung
  2003, as reported by Monson 2014; the 2003 paper not read).
- No peer-reviewed definition of vocal "harshness" as an upper-mid excess was found in this run.
  The Harsh band rests on these facts and on product practice (resonance suppressors such as
  oeksound's soothe line, whose current version is soothe3).

For Halite:

- The Harsh default band (3.2 kHz, 1 octave: about 2.26–4.53 kHz) contains the /ʃ/ peak. The
  plan's hold ("during an ess the Harsh detector holds") works only when the Ess detector caught
  the ess; with Detect Low at 4.5 kHz and the body problem above, many /ʃ/ will reach the Harsh
  detector as harshness (inference). The gate "Harsh adds ≤ 0.5 dB during sibilant phonemes" will
  show it. Holding Harsh also on noise-like energy in its own band (the noise cue above, applied to
  the Harsh band) avoids depending on the Ess detector.
- Harshness is harmonic and sustained, so the plan's RMS envelope (2/40 ms) and no flatness cue are
  reasonable; no source was found for the constants (plan value).

## De-esser and dynamic-EQ design

The open-source survey below shows the usual topologies: a compressor keyed by a filtered side
chain acting on the whole signal (TAP; Calf in wide mode), split-band processing (Calf split; LSP
with IIR, shelving or linear-phase crossovers; mda DeEss with a one-pole crossover), dynamic EQs
used as de-essers (ZamDynamicEQ, ZL Equalizer), and spectral or many-band designs (two new AGPL
projects; commercial resonance suppressors).

- **Gain computer.** "Pulled down to the threshold, never further than Range" is an
  infinite-ratio limiter with a depth clamp; the soft-knee gain computer and log-domain smoothing
  of Giannoulis, Massberg and Reiss (2012) apply with the ratio taken to infinity.
- **Subtractive form.** `out = dry − removed` with `removed` exactly zero at rest gives the
  bit-identity gate by construction; two of the new projects claim the same property.
- **Split realisation.** Simper's trapezoidal SVF forms bells and shelves by mixing the input,
  band-pass and low-pass outputs with coefficients solved from the target numerator. Any stable
  digital biquad maps to an SVF: its poles through the inverse bilinear transform to the SVF's
  cutoff and damping, its numerator to the three mixing coefficients (inference from the
  structure). A magnitude-matched section can therefore keep the SVF's behaviour under
  modulation.

## STFT gain processing and musical noise

- **WOLA.** A synthesis window after the inverse FFT tapers the artefacts of spectral
  modification at frame edges (Smith, SASP). With periodic Hann analysis and synthesis windows at
  75 % overlap the window product sums to a constant 1.5 (Hann² = 0.375 − 0.5 cos θ + 0.125 cos 2θ;
  the cosine terms cancel over four shifts of N/4). The plan's "Hann, 75 %" works if it means
  both windows; the normalisation is 1/1.5.
- **Exact zero.** `removed = ISTFT((1 − g)·X)` is exactly zero when every `g = 1`, so the plan's
  identity claim holds for the Spectral shape too.
- **Latency.** With frame N, the last frame covering a sample ends up to N − 1 samples later, so a
  constant latency of N − 1 (or N) is the minimum; 512 samples at 48 kHz (10.7 ms) is consistent,
  and a 10 ms look-ahead fits inside it.
- **Per-bin gains are filters.** Multiplying bins applies a circular convolution; sharp gain
  changes across frequency alias in time (Allen 1977; Griffin, Lim 1984). Smoothing the gains over
  frequency shortens the implied impulse response. At N = 512 and 48 kHz, 1/6 octave around 7 kHz
  is 9 bins (measured count); the ERB is about 510 Hz at 4.5 kHz and 1100 Hz at 10 kHz
  (Glasberg, Moore 1990 formula), 5–12 bins of 93.75 Hz.
- **Musical noise.** Measured (synthetic, white noise, N = 512, hop 128): a single bin's power has
  a standard deviation of **5.56 dB** (theory for an exponential variable: 5.57 dB). Gains computed
  from raw bins on a noise-like ess would jump by several dB from frame to frame in random bins, the
  mechanism of musical noise (Cappé 1994 on the Ephraim–Malah suppressor; Breithaupt et al. 2008
  and Esch, Vary 2009 on smoothing). Smoothing the *level estimate* before computing gains:

  | Level estimate at 7 kHz | Std (dB) |
  |---|---|
  | one bin | 5.56 |
  | 1/6-octave mean (9 bins) | 1.96 |
  | + one-pole over 2 / 4 / 8 hops (5.3 / 10.7 / 21.3 ms) | 1.15 / 0.86 / 0.63 |
  | 1/3-octave mean (17 bins) | 1.41 |
  | + one-pole over 2 / 4 / 8 hops | 0.85 / 0.63 / 0.45 |

  The plan smooths gains; since the gain is a clipped function of level, smoothing the level first
  and the gain lightly after is the better order (inference).
- **Frame size.** 512 points at 48 kHz (10.7 ms, 93.75 Hz bins, 2.67 ms hop) resolve the ess
  spectra (peaks with bandwidths of a kHz or more) and the ERB-smoothed target; the plan's value
  is confirmed for the Spectral shape. It is not enough for flatness (above).
- **Warm-up.** The plan's target adapts with a ≈ 2 s time constant and its gate wants a mid-clip
  render to match within −60 dB once 2 s have passed. An exponential average keeps e^−1 = 37 % of
  its starting error after one time constant; reducing it by 60 dB takes 6.9 time constants,
  ≈ 14 s (inference). The gate and the time constant contradict each other. A finite-memory
  target (a ring of block averages over the last 2 s of non-sibilant frames, the sum recomputed from
  the ring each block) forgets its start once the ring has refilled (inference). The Allround
  target (≈ 300 ms) reaches −60 dB in about 2.1 s as an exponential, which fits a 2 s warm-up.
  **Neither remedy is enough on its own.** A render started mid-clip (the plan's "bar 17 alone")
  begins at an arbitrary sample, so its STFT frames, its 32-sample detector hops and its ring
  blocks fall on a different grid from the full render's: it analyses different frames, decides
  sibilance on different hops and gets different per-bin gains during events, whatever the time
  constant. The two renders can agree to within a small error only if every grid is anchored to
  the host timeline (frames and hops at multiples of their length in absolute sample position,
  ring blocks likewise), or for start offsets that are multiples of the longest grid. Whether that
  error then meets −60 dB is not analysed here (inference; nothing measured).
- A 2 s average is a phrase-level reference, not a long-term average spectrum: secondary sources
  give 10–40 s for a stable LTAS (**not verified** from a primary source).

**Built in M2** (`../shapes/spectral/NOTES.md`): Hann analysis and synthesis at 75 %, 1 / 1.5, an
exact zero path; the level estimate smoothed over 1/6 octave and 10 ms, the gains over 2.5 ms (on
noise the gains' frame-to-frame std falls from 2.7 to 0.4 dB); a finite 2 s ring of timeline-
anchored blocks with every grid anchored, which meets the Warm-up gate.

## Magnitude-matched biquads (Split near Nyquist)

The bilinear transform compresses the frequency axis towards Nyquist, so bells narrow and shelves
bend there ("cramping"; Välimäki, Reiss 2016; Orfanidis 1997; Massberg 2011; Vicanek 2016). A TPT
SVF is a bilinear design and cramps the same way.

Measured (synthetic) at 44.1 kHz, maximum |error| against the analog prototype from 20 Hz to
0.45·fs (19.85 kHz). The bell is the usual peaking prototype with its extremum G at f0,
f0 = √(fL·fH) and Q = √r/(r − 1) with r = fH/fL (one reading of "a bell over the Detect band";
the plan does not define the edges of a variable-depth bell). "Bilinear" is prewarped at f0, which
is what a TPT SVF bell does. "Matched" is Vicanek's closed-form peaking EQ (poles by impulse
invariance, numerator matched at DC and at f0 with an extremum there). "Best biquad" is a
numerical fit of the squared magnitude with free poles and zeros (Sanathanan–Koerner iterations,
then a near-minimax refinement), a bound on what any second-order section can do.

| Band | Cut | Bilinear (TPT SVF) | Matched (Vicanek) | Best biquad |
|---|---|---|---|---|
| 4.5–12 kHz (default) | −3 dB | +0.57 | −0.17 | |
| | −6 dB | | | 0.07 |
| | −12 dB | +2.27 | **−0.83** | 0.14 |
| | −24 dB | +5.44 | **−2.12** | 0.24 |
| 6–17.5 kHz | −12 dB | +4.41 | **−1.58** | 0.16 |
| | −24 dB | +8.70 | **−3.16** | 0.25 |
| 8–16 kHz | −24 dB | +7.25 | **−2.88** | 0.25 |
| 10–17 kHz | −24 dB | +7.57 | **−3.05** | 0.25 |
| 3–6 kHz | −24 dB | +1.47 | **−0.55** | 0.14 |

The matched design's error sits at the top of the range (it imposes nothing near Nyquist). Adding
a Nyquist match to the impulse-invariant poles (the Orfanidis idea applied to Vicanek's poles) made
deep cuts worse here (up to 3 dB) or unrealisable at −24 dB.

High shelf (the plan's choice when Detect High ≥ 0.4·fs), second order, Q 0.707, half the gain in
dB at the corner, bilinear prewarped at the corner, same range:

| Corner | −6 dB | −12 dB | −24 dB |
|---|---|---|---|
| 2 kHz | −0.03 | −0.07 | −0.18 |
| 4.5 kHz | −0.15 | −0.33 | **−0.89** |
| 8 kHz | −0.47 | **−1.02** | **−2.64** |
| 12 kHz | **−1.03** | **−2.20** | **−5.27** |
| 16 kHz | **−1.75** | **−3.63** | **−7.88** |

For Halite: the gate (0.5 dB to 0.45·fs at 44.1 kHz) is achievable with one biquad (≤ 0.25 dB in
every case tried) but **not with the closed forms** for cuts deeper than about 6 dB, and the
bilinear shelf fails it too, even at the default corner at full Range. The Split shape modulates
gain per sample, and a numerical fit per sample is too costly. Options for M2: fit sections for
the current band on the message thread over a grid of depths, map each to SVF parameters and
interpolate per sample; or relax the gate (for example to cuts up to 12 dB, or to 0.4·fs). This is
a decision for the user.

**Built in M2** (neither option): the bell's and shelf's shapes do not depend on the depth, so one
fit per band holds every depth and each depth follows in closed form on the SVF; the gate passes,
worst 0.29 dB at any rate (`../shapes/split/NOTES.md`).

## Open-source de-esser survey (Settled 25)

As of 2026-11-13, from project pages, manuals, source files and repository metadata. "Absolute"
means a threshold in dB of the detector signal.

### Established projects

| Project | Linux formats | Licence | Maintenance | Reduction | Detection | Threshold | Spectral | UI |
|---|---|---|---|---|---|---|---|---|
| **LSP Deesser** (mono, stereo, side-chain variants) | LV2, VST3, CLAP, VST2, LADSPA | LGPL-3.0 | active; added in 1.2.34 (2026-08-15), revised in 1.2.37 (2026-11-01) | compressor (ratio 1–100, knee, attack, release, hold) on a split band (crossover Off, Classic IIR + all-pass, Modern IIR shelving, linear-phase FIR), split 500 Hz–4 kHz, LF link | side chain with HPF, LPF and two peak filters; Peak, RMS, LPF or SMA detector; look-ahead 0–20 ms | absolute, −60 … 0 dB (default −12) | no | custom: frequency graph with FFT of input, side chain and output; meters; side-chain listen |
| **Calf Deesser** | LV2 | LGPL-2.1 | end-of-life by its README, maintenance only; last tag 0.90.9 (2025-12-30), last commit 2026-10-13; successor calfNXT (GPL-3.0, VST3 on Linux only) | compressor (ratio 1–20, laxity) on the whole signal or above the split (default 6 kHz) | filtered side chain with a peak bell; "exact" or RMS-like | absolute, −60 … 0 dBFS (default −18) | no | custom (GTK): meters, graph, S/C listen; users report GTK problems |
| **TAP DeEsser** | LADSPA; LV2 port by MOD Audio | GPL-2.0 (the LV2 port's repository) | LV2 port last pushed 2024-02-18 | 1:2 hard-knee compressor, 10 ms attack and release, on the whole signal | side chain high-pass or band-pass, 2–16 kHz | absolute, −50 … +10 dB | no | host's generic controls; side-chain monitor |
| **mda DeEss** (mda-lv2) | LV2 | GPL-3.0-or-later (source header) | maintained; v1.2.12 (2026-06-11) | a crossover of two cascaded one-pole sections; the high band limited to the threshold (infinite ratio); left and right summed to mono, output mono | positive-peak envelope of the high band, fixed per-sample attack and release coefficients | absolute, −60 … 0 dB; Freq 1–12 kHz; HF drive ±20 dB | no | host's generic controls |
| **Airwindows DeEss, DeBess** (in Airwindows Consolidated) | CLAP, VST3, LV2 | MIT | active (pushed 2026-11-08) | DeBess: divides the part above a one-pole low-pass (a first-order split) by a smoothed ratio | DeBess: products of successive sample differences over a window of up to 40 samples ("Sharp"); sensitivity "Intense"; level-dependent | none (sensitivity control) | no | generic (Consolidated); monitor plays the removed part |
| Dynamic EQs used for de-essing: **ZamDynamicEQ** (zam-plugins), **ZL Equalizer** | LV2 and others | GPL-2.0; AGPL-3.0 | both active | dynamic bands | per band | not checked | no | ZL: custom display |

DeEss (the older Airwindows plugin) was not read.

**x42-plugins** (the 25 bundles of its `plugin.list`, checked 2026-11-13) has no de-esser. Its
compressor, darc, has mono and stereo audio ports only (no side-chain input) and no filter in its
detector, so it cannot serve as one either.

### New projects (2026), self-described, not tested

| Project | Linux formats | Licence | Created, stars | Claims |
|---|---|---|---|---|
| **Nebula De-Esser** (Rust, nih-plug) | VST3; CLAP per the build notes, but the 3.3 notes say CLAP is discontinued (**not verified**) | AGPL-3.0 | 2026-03, 43 | a "Relative mode" that learns the voice; spectral reduction by subspace projection; flatness, flux and a Teager–Kaiser operator among its detection features; split/wide; custom UI with a spectrum |
| **Axell Velvet** (JUCE) | VST3 | AGPL-3.0 | 2026-10, 1 | a neural sibilant detector trained on CMU ARCTIC and VCTK; no threshold, level-independent; ten linear-phase bands cut where each ess is harsh; zero-latency mode; a display that shows every ess |
| Reckless DeEsser (JUCE) | none (macOS, Windows) | AGPL-3.0 | 2026-10, 0 | a threshold that floats with the programme's RMS; linear-phase band split |
| magnetophon LookaheadDeEsser | not examined | GPL-2.0 | 2014, 1 | "a look ahead de-esser" (README only) |

Closed-source Linux de-essers, for context only: Harrison XT-DS (LV2; its GUI needs a licence),
linuxDSP DSR500 (freeware, 2012; current availability **not verified**).

### Does `PLAN.md`'s claim hold?

The claim is "few open de-essers, none with relative detection, a spectral shape and a modern
display". **It does not hold as written.**

- Among the **established** open-source de-essers on Linux (LSP, Calf, TAP, mda, Airwindows) it
  holds: every one detects with a filtered band level against an absolute threshold (or, in
  Airwindows, a level-dependent sensitivity), none is spectral, and only LSP has a modern
  analyser-style display. Calf, the one most users know, is end-of-life.
- Two **new AGPL projects** describe exactly the combination: Nebula De-Esser (a relative mode,
  spectral reduction, a custom UI) and Axell Velvet (level-independent neural detection,
  many-band cuts, a display of every ess). Both are months old, have one developer and almost no
  users, and neither ships LV2. Their claims are untested here.
- So README or release text must not say "none". A defensible wording: the established
  open-source de-essers on Linux detect with a band level against a fixed threshold; Halite
  measures each ess against the voice around it, checks that it is noise-like, offers a spectral
  shape, and ships VST3, CLAP, LV2 and Standalone.

## Corpora (for *Measurement and quality gates*)

| Corpus | Licence | Rate | Notes |
|---|---|---|---|
| VCTK 0.92 (Yamagishi, Veaux, MacDonald 2019) | CC BY 4.0 (the archive's `license_text.txt`, read) | 48 kHz (recorded at 96 kHz) | 110 speakers; transcripts for 109; `tools/corpus` uses the MKH 800 channel |
| LibriSpeech (Panayotov et al. 2015) | CC BY 4.0 (OpenSLR page) | **16 kHz** | nothing above 8 kHz: cannot exercise the Detect band above 8 kHz |
| VocalSet 1.0 (Wilkins et al. 2018) with the corrected Annotated-VocalSet, as deposited by S. Donaher (Zenodo 10200775, 2023) | CC BY 4.0 (the Zenodo records of the deposit, VocalSet 1.0 and 1.2 and Annotated-VocalSet) | 44.1 kHz (the committed excerpts' WAV headers) | 20 singers, 10.1 h, vowels and techniques; mostly vowels, good for the bright-vowel gate |
| Hi-Fi TTS (Bakhturina et al. 2021) | CC BY 4.0 (OpenSLR page) | 44.1 kHz | 10 speakers, ≈ 292 h, from LibriVox audiobooks; recordings selected for at least 13 kHz bandwidth and 32 dB SNR (paper abstract), so above 13 kHz nothing is guaranteed; per-file bandwidth **not verified** |
| EARS (Richter et al. 2024) | CC BY-NC 4.0 (paper) | 48 kHz, anechoic | non-commercial: outside the plan's CC BY rule |
| Montreal Forced Aligner (McAuliffe et al. 2017) | MIT (repository) | | not run in M0; its LibriSpeech output is used, as published by Lugosch (Zenodo 2619474, CC BY 4.0); pretrained model licences not checked |
| pocketsphinx 5.0.4 with its CMU en-us model and CMUdict | BSD-style (Carnegie Mellon University; the model Alpha Cephei), from the wheel's and model's licence files | | aligns VCTK and the committed excerpts (`tools/corpus/README.md`) |

## Proposed changes to PLAN.md

Each item names the plan text, the change and the reason. None is applied; the user decides.

1. **Flatness frame** (*Detection* 3: 256 points Normal, 128 Live). 256 points at 48 kHz resolve
   no harmonics below 750 Hz f0, 128 none below 1.5 kHz (measured), so the cue does nothing for
   speaking or most singing voices. Replace or complement it with the band-envelope periodicity
   cue (21 ms, causal-capable), defined with a **detrended envelope** (high-passed at about 60 Hz):
   with only the frame mean removed, ess onsets, rises and decays read as periodic as a /z/
   (measured). Compute it on the envelope decimated to about 6 kHz with running sums (≈ 0.005 % of
   a core, measured; directly at full rate ≈ 21 %). If flatness stays, compute it over ≥ 2048
   points (scaled with the rate) on envelope-normalised sub-bands and a coarser hop. Choose in M1 on
   the corpus, with /z/, ess onsets after vowels and vowel onsets after esses in the gates.
2. **Body band** (*Detection* 2: 120 Hz to Detect Low). Use 120 Hz to the lower of about 2 kHz and
   Detect Low / 2, so /ʃ/ and the skirt of /s/ do not raise their own reference. Adopt together
   with item 3. Sweep the cap in M1 against the vowel: a body without F2 and F3 of front vowels
   makes `S` depend on the vowel.
3. **Body envelope** (5 ms attack, 80 ms release). Hold `V` while a sibilance candidate is active
   (hold ≥ 250 ms, release after); with a free 80 ms release `S` rises by up to about 11 dB over a
   100 ms ess (the bound for an emptied body band; less in the plan's band, which item 2 empties).
4. **Detector types** (*Detection* 1, 2). State that `B` and `V` use the same detector (both RMS,
   or both peak), and give `B` a release (≥ 20 ms for ≤ 0.5 dB ripple). The 0.5 ms attack stays.
5. **Relative threshold scale** (*Controls*: 0 … +24 dB, default +6). Typical esses are about
   10 dB below the vowel in broadband level, so `B − V` of a typical /s/ is negative. Set range and
   default from the corpus `S` histogram in M1; expect a range that reaches well below 0 (for
   example −24 … +24 dB).
6. **Detect band default** (4.5–12 kHz). Right for /s/, /z/ (adult and adolescent peaks 4.4–6.9 kHz
   means, female energy above 11 kHz); misses the /ʃ/, /ʒ/ peak (2.5–3.7 kHz). Keep 4.5 kHz for
   now, sweep Detect Low over 3–5 kHz in M1, and split the detection gate per phoneme (/s/, /z/
   gated at the plan's 95 %; /ʃ/, /tʃ/, /ʒ/, /dʒ/ reported, gated once M1 has numbers).
7. **Harsh hold** (*The Harsh band*). Hold Harsh also on noise-like energy in its own band, not only
   during Ess events; the default Harsh band contains the /ʃ/ peak.
8. **Spectral smoothing** (*Reduction*, Spectral). Smooth the per-bin *level estimate* across
   frequency and time before computing gains, then the gains lightly; a raw bin fluctuates by
   5.6 dB (std) on noise, 1/6 octave plus ≈ 10 ms brings it to 0.86 dB (measured).
9. **Warm start** (≈ 2 s time constant; gate *Warm-up* −60 dB after 2 s). Inconsistent: an
   exponential needs ≈ 14 s for 60 dB. Use a finite 2 s window of non-sibilant frames (ring of
   block averages) and count the gate in non-sibilant time, or keep the exponential and set the
   warm-up to ≈ 14 s. Under either remedy the gate needs the STFT frames, detector hops and ring
   blocks anchored to the host timeline (or holds only for start offsets that are multiples of
   the grid); specify that in M2.
10. **Split near Nyquist** (*Reduction*, Split; gate *Split near Nyquist*). Closed-form matched
    bells miss 0.5 dB at 0.45·fs for cuts of 12 dB and more (−0.83 to −3.16 dB measured), the TPT
    SVF (bilinear) bell by up to 8.7 dB, the bilinear high shelf by up to 7.9 dB (0.89 dB at the
    default corner, full Range). A fitted biquad stays within 0.25 dB. Either fit sections per band
    and depth and run them on the SVF structure, or relax the gate. Also define the bell's edges.
11. **Corpora** (*Measurement and quality gates*). LibriSpeech is 16 kHz and cannot test the band
    above 8 kHz. Add a CC BY corpus with more bandwidth (Hi-Fi TTS: 44.1 kHz, at least 13 kHz
    bandwidth by selection, so it covers the default Detect band up to 13 kHz, not above) and keep
    LibriSpeech for timing and the lower band; EARS is excluded by its NC licence.
12. **Product references** (intro, *References*). Waves Sibilance's "level-free operation" is not
    verified; Softube Deesser documents a relative threshold and could replace or join it. soothe2
    has been succeeded by soothe3.
13. **"What's missing" bullet** (*Few open de-essers, none like this*). Reword per the survey
    verdict; add the Dolby patent family to the lawyer's list (Open 1).

Confirmed as planned: 0.5 ms band attack; 512-point STFT at 44.1/48 kHz scaled with the rate;
75 % overlap (with Hann analysis and synthesis windows, normalised by 1/1.5); Normal latency of one
frame with look-ahead inside it; the event rule (0.5 dB start, 20 ms hangover); the exact-zero
`removed` path in every shape. Not analysed: the detector's 32-sample hop (the plan's value, used
here only in the cost estimates).

## Not verified

- Affricate (/tʃ/, /dʒ/) frication durations and peak values.
- Children's sibilant peaks (only adolescents were found with numbers).
- Sibilant energy above 11 kHz (every measured source sampled at 22 kHz).
- Johnston's flatness-to-tonality mapping; the ISO 226:2023 tables; Sundberg's paper text.
- LTAS stabilisation time (secondary sources only).
- Hi-Fi TTS bandwidth per file; MFA pretrained-model licences.
- Fricative durations in connected speech.
- The new projects' claims; the continuation patent's claims; any patent beyond the Dolby family.

## Implementation (M1)

2026-11-13, M1 part 1: the detectors in C++, wired into the chain, tested on synthetic signals.
The corpus measurement (*Measured (M1)*, below) then chose the noise cue and changed some
constants and two rules; this section describes the detector as it now is and marks what the
corpus decided. Numbers marked *measured* come from the tests named after them
(`HaliteTests_detector`), at 48 kHz unless stated.

### What was built

| File | What |
|---|---|
| `Detector.{h,cpp}` | Ess and Harsh detectors on the key, 32-sample hops at any block size, per path; stereo modes, key routing, look-ahead, events |
| `Levels.h` | the band filter, the level detector every band uses, the body's rise detector |
| `NoiseCues.{h,cpp}` | the two noise-cue candidates and their mapping to a noise-likeness 0 … 1 |
| `Reduction.h` | the soft knee, the release (Speed), the look-ahead ramp, the event rule |
| `DetectorSettings.h` | the settings snapshot, the noise-cue switch (not a parameter) and `DetectorTuning`, the constants the corpus set (the plugin runs its defaults, `tools/measure` sweeps them) |
| `DetectorProbe.{h,cpp}` | the offline probe for `tools/measure`: the plugin's own `Detector` over a buffer, every hop and event |
| `Learn.{h,cpp}` | Learn (M3, *M3: Learn*): ess candidates from every hop, the live 60 s histogram, the 8 s capture, the rule; `LearnTuning` |
| `../common/EventRing.h` | the lock-free ring of the last 256 events for the UI (one writer, one reader, a seqlock per slot) |
| `../common/RealFft.h` | a radix-2 real FFT for the flatness cue (no lock, no stack scratch) |
| `../chain/HaliteChain` | runs the detectors every block, publishes meters (atomics) and events (the ring); the audio passes bit for bit |

Per path, per sample (double precision): the key's high-pass (Key HPF, off at its lowest position),
then four 4th-order bands (Detect, body, Harsh, Harsh body; a 2nd-order Butterworth high-pass into a
2nd-order low-pass, TPT SVFs), each into a level detector, and the noise cues on the Detect and
Harsh band signals. Per hop: levels in dB, `S`, the targets, the holds, the look-ahead ramp and
release, events. Stereo: Linked runs two paths and follows the louder (by `B` for Ess, by `H` for
Harsh); Unlinked two paths and two reductions; Mid and Side one path on (L ± R) / 2; a mono main bus
with a stereo key is Linked. External key without a side chain falls back to the input.

### Constants

"Corpus" marks a value chosen on the m1 tune split (*Measured (M1)*), "M1b" one chosen in the M1b
round (*M1b*); `DetectorTuning` holds those.

| Constant | Value | Source |
|---|---|---|
| hop | 32 samples | plan value |
| level detector | power averaged by a one-pole over τa, then a 0.5 ms attack and a release; time constants act on the power (4.34 dB per τ) | the plan's "same detector type" for every level (below) |
| `B` | τa 4 ms, release 5 ms | corpus, within the ripple gate: 0.45–0.46 dB at 44.1–96 kHz (gate 0.5) |
| `V` | 120 Hz … min (2 kHz, Detect Low / 2), τa 5 ms, release 80 ms | plan values; the cap and the release confirmed on the corpus |
| body hold | while `B − V` > −20 dB, at most 250 ms; and while the body's averaged power is more than 20 dB under `V`, at most 1 s | the candidate hold: plan values, confirmed on the corpus; the gap hold: corpus (replaces the plan's absolute floor hold, decision 9) |
| body floor | `B − V` never divides by less than −60 dBFS | plan value (a `DetectorTuning` constant, so the level gate's options can be measured; *Measured (M1)*, Level) |
| body rise | `V` up by more than 2 dB over 5 ms: `B − V` reads the smaller of `B` and `B` with the body's averaging; the Harsh band reads `H` against its body by the same rule | M1, decision 2 |
| onset hold | for 30 ms after a body rise, `S` counts the band as harmonic (N = 0) | corpus (decision 10) |
| body jump | the body's 5 ms average up by more than 10 dB over 5 ms, whether `V` is held or not: an onset too (`V` is held through closures and pauses, so its own rise misses a voice coming back) | M1b |
| jump hold | for 30 ms after a jump, `S` is lowered by at least 10 dB × Focus (7 dB at the default 70 %) | M1b review (*M1b*, *After the review*); M1b first counted the band as harmonic there too |
| noise range | `S` lowered by up to 40 dB × Focus × (1 − N), linear in Focus (28 dB at the default 70 %) | M1b (M1: 30 dB) |
| shape cue | `S` lowered by Focus × min (40 dB, 6 × max (0, −4 dB − R)); R = B's 4 ms average over the presence band's, the body filter's upper band-pass (centred on the body's top, Q 0.707: about 1 … 3.9 kHz at the default) | M1b |
| persistence | while `B` stays within 6 dB of its lowest over 40 ms and ρ read over 0.8 in that time, the cue keeps that reading | M1b |
| knee | 3 dB, infinite ratio, Range clamp | plan values (the corpus sees a knee only as a threshold offset) |
| Speed Auto | Ess: release = a quarter of the time over threshold, clamped to 15 … 30 ms; Harsh: half of it, 30 … 150 ms; latched from the last time over; a fixed release applies at once, also to a reduction already letting go | M3 (*M3: Speed*): Ess chosen on tune against the release-tail gate (Settled 47) and the landing; Harsh the plan's, confirmed. M1's Ess rule (half, 15 … 80 ms) decided the vowel gate (*Measured (M1)*) |
| fixed releases | Ess 20 ms, Harsh 80 ms (with Speed Auto off) | M3 (*M3: Speed*) |
| look-ahead | 5 ms (Normal), the reduction ramping in linearly over it | plan value, confirmed in M3 (*M3: Speed*, the onset) |
| Focus | both cues' terms (and the jump's hold) scale linearly with Focus; 0 is `B − V` exactly | M1b; the curve and the 70 % default confirmed in M3 (*M3: Learn*, Focus) |
| Learn | an ess: `S` over −24 dB (Voice) for 25 ms or more, ended 20 ms under it (Allround: `B` over −60 dBFS, also ended 6 dB under its peak); Threshold = max (median − 4 dB, p25) of 3 or more; Harsh: the loudest 7 % of voiced frames, 1 s or more | M3 (*M3: Learn*), `LearnTuning` |
| `H` | τa 2 ms, release 40 ms; Harsh body 150 Hz … half an octave under the band, as `V` | plan values |
| Harsh holds | during an ess (target > 0, plus the 20 ms hangover); in Voice while its N ≥ 0.5; 50 ms after it is switched on | M1; the gate passes on the corpus |
| events | start over 0.5 dB, end 20 ms after; the largest reduction of all channels | plan values |
| key | clamped to ±16 (+24 dBFS); NaN and Inf read 0 | M1, decision 11 |
| noise cue | periodicity (a) | corpus |
| periodicity mapping | N = 1 at ρ ≤ 0.55, 0 at ρ ≥ 0.70, linear between; lags 1 … 12.5 ms | corpus; the lag range confirmed in M1b |
| flatness mapping | N = 1 at ≥ −4.5 dB, 0 at ≤ −8 dB | measured (synthetic), below; not kept |

### Decisions taken in M1 (for the user to confirm)

1. **Every level is the same detector** (power, i.e. RMS-type: the plan's "both RMS or both
   peak"). The plan's attack times of `V` (5 ms) and `H` (2 ms) are the averaging times; one 0.5 ms
   attack follows for all. **`B` is averaged over 4 ms before its 0.5 ms attack and releases over
   5 ms**, where the plan has the attack alone and a release of at least 20 ms: the corpus wants
   `B` to fall fast after an ess (with 2 ms / 20 ms, 9.7 % of vowel frames trigger on their own S at
   the 95 % recall point, with 4 ms / 5 ms 5.1 %), and the averaging keeps the ripple under the
   plan's 0.5 dB (*Measured (M1)*, B). The cost is `B`'s rise: a tone at the band's centre switched
   on hard reads within 3 dB after 4.0 ms and within 1 dB after 7.3 ms at 48 kHz (3.6 / 7.3 ms at
   44.1 kHz, 3.7 / 7.0 ms at 96 kHz; `HaliteMeasure --ripple`), so within 3 dB inside the default
   5 ms look-ahead. In Live this rise is the detector's attack: about 4 ms to within 3 dB and 7 ms
   to within 1 dB, not the plan's 0.5 ms. A sine at the band's centre reads within 0.2 dB of its RMS (measured), so the
   difference of two levels barely depends on crest factors.
2. **The body's rise rule.** `B` (2 ms in part 1, now 4 ms) rises faster than `V` (5 ms), so a
   voice onset reads as sibilance for a few ms. The numbers below are part 1's, with the 2 ms `B`. On vowels (−6 and −12 dB per octave) fading in over 5–20 ms, after
   silence or after a vowel 30 dB quieter, B − V overshot its steady value by 6.1–11.0 dB, and the
   first syllable after a reset was caught as an ess at a threshold 15 dB over the vowel's steady
   `S`. While `V` rises by more than 2 dB over 5 ms, `B − V` uses the smaller of `B` and `B`
   measured with the body's averaging (a second envelope on the same band signal), so both rise
   alike: the overshoot is then 4.7–7.9 dB on the same signals (6.3 and 6.4 dB in the test case;
   all measured). **The Harsh band uses the same rule** (its own rise detector on the Harsh body and
   a second `H` with the body's averaging): on the test case's vowel `H − body` overshoots its
   steady value by 6.8–6.9 dB with it and 10.1–10.2 dB without (measured, `DetectorTests`, "a voice
   starting"); on the corpus streams it moves the Harsh means by 0.01 dB at most (measured with and
   without, tune and holdout). What remains sits
   in the first ms of the onset, about 40 dB under the vowel's final level, where the body filter's
   group delay (its 120 Hz high-pass) lets `V` lag; a 2 ms fade adds its own spectral splatter
   (13.4 dB on the dull vowel). Esses after vowels are unaffected (`V` is held then).
3. **`B − V` never divides by less than the body floor**, so before any voice `V` (0) does not turn
   the first breath into a huge ratio. Level independence is exact while `V` is above the floor
   (gate, *Measured (M1)*). This is the floor's only job now (decision 9).
4. **The Harsh body holds with `V`**, or the end of each ess (the body empty, then the vowel back)
   would read as harshness.
5. **Detect Low above Detect High swaps them**, an M1 decision (the plan left the order open); a
   band narrower than a third of an octave widens around its centre (downwards against the top);
   nothing reaches past 0.45 fs.
6. **Look-ahead is a whole number of hops** (0.67 ms at 48 kHz): 5 ms is 8 hops, 5.33 ms. The
   reduction ramps in linearly over the look-ahead (the largest target ahead, weighted down by its
   distance), so it is complete when the ess arrives.
7. **Flatness**, against the M0 sketch: each bin is divided by the mean power of the bins within
   ±375 Hz (not third-octave blocks: those read the skirts of a band as tonal, 2–5 kHz noise in the
   Detect band −6.4 dB instead of −2.9 dB, measured); one frame every quarter frame (10.7 ms at
   48 kHz, the "coarser hop"); its own FFT, because JUCE's fallback engine (Linux, Windows) locks a
   spin lock and copies the frame into a full-length complex transform on the stack, and took
   436 k instructions per 2048-point frame (callgrind). With JUCE's FFT and a frame every eighth of
   a frame flatness cost 0.94 % of a core in stereo; with its own FFT and the quarter-frame hop,
   0.30 % (measured, as *CPU* below).
8. **Periodicity** follows the M0 sketch: band squared, 4th-order low-pass at 1.2 kHz, decimated to
   ≈ 6 kHz, 60 Hz 2nd-order high-pass, normalised correlation of the 21.3 ms window with the window
   one lag earlier over 1 … 12.5 ms, running sums per lag in double; one lag (or the energy) is
   summed again in full every hop, so after a loud passage every sum is exact again within ≈ 50 ms.
9. **`V` holds relative to itself, not below an absolute floor** (corpus). The plan holds `V` while
   the body is under −60 dBFS. At −30 dB input gain VCTK's body dips under that in every closure
   and ess, at 0 dB it does not, so `V`'s history and every later `S` depended on the gain (up to
   1.4 dB on an excerpt, 2022 event-list changes on the tune half; *Measured (M1)*, Level). `V` now
   holds while the body's 5 ms average is more than 20 dB under `V`, at most 1 s: a ratio, so the
   same at any gain, and room tone and breaths in pauses never become the reference (the open
   problem part 1 found, for pauses up to 1 s). After 1 s `V` follows, so a voice that drops by more
   than 20 dB is followed within about 1.3 s. The absolute floor remains only as the ratio's
   minimum.
10. **Onset hold** (corpus): a rising body is a voice starting, which an ess is not, and the
    periodicity cue needs about 20 ms to hear a new vowel's pitch. For 30 ms after a body rise `S`
    counts the band as harmonic. `N` in the frame stays the cue's own reading; the flag
    `voiceOnset` says when the rule applied.
11. **The key is made sane before the filters** (review): one NaN or Inf sample left every filter,
    envelope and running sum NaN until the next reset, and a burst at float max held `V` hundreds
    of dB up through the gap hold. Each key sample is clamped to ±16 (+24 dBFS, above any real
    float-host level) and NaN or Inf reads 0, per channel before Mid or Side is formed. Measured
    (`DetectorChangeTests`, a vowel with esses at −20 dBFS): with NaN or Inf in the key every hop
    stays finite and the events are the clean run's; one sample at float max lifts `V` for 0.40 s,
    64 samples for 1.76 s (*Open problems*, loud transients). Cost: 5.9 M of 144 M instructions per
    4 s of stereo (*CPU*).
12. **Changes while running** (review): a path fed another signal (Stereo between Mid, Side and the
    two-path modes, or the key between internal and external) starts clean, with the Harsh warm-up,
    so the holds never keep the old signal's `V` (before, a switch from Mid to Side kept the mid's
    `V` through the gap hold); a reduction that comes back starts clean as before. A fixed release changed
    while a reduction lets go applies from the next hop.

### The noise cues, measured (synthetic)

Raw values, median (p10, p90) over 2.9 s, defaults (`HaliteTests_detector "[.report]"`):

| Signal | Periodicity | Flatness (dB) |
|---|---|---|
| band noise 4.5–12 kHz | 0.28 (0.20, 0.36) | −2.4 (−2.7, −2.1) |
| band noise 2–5 kHz (in the Detect band) | 0.29 (0.20, 0.39) | −2.9 (−3.2, −2.6) |
| noise AM at 120 Hz, index 0.5 (/z/-like) | 0.58 (0.47, 0.68) | −2.4 (−2.6, −2.1) |
| harmonic 110 Hz: steady, ±20, ±50 cents | 0.93, 0.99, 0.96 (p10 ≥ 0.91) | −8.3, **−2.4**, **−1.4** |
| harmonic 220 Hz: steady, ±20, ±50 cents | 0.98, 0.99, 0.99 (p10 ≥ 0.96) | −22.0, −11.0, **−3.1** |
| harmonic 440 Hz: steady, ±20, ±50 cents | 1.00, 1.00, 0.99 (p10 ≥ 0.97) | −36.5, −27.2, −14.8 |
| harmonic 220 Hz + noise, HNR 10 / 0 dB | 0.76 / 0.30 | −8.1 / −3.5 |

- **Periodicity separates** every harmonic signal from noise, with vibrato, at 110–440 Hz; the
  /z/-like noise reads 0.58 (p90 0.68), which the corpus mapping (0.55 … 0.70) counts as mostly
  noise (N = 0.8 at the median, 0.13 at p90); on the corpus /z/ recall did not change with the
  mapping (*Measured (M1)*).
- **Flatness fails with vibrato at low f0** (bold: noise-like values for harmonic sounds), as M0
  predicted: a 43 ms frame smears harmonics that move by a bin within it. It separates steady
  voices at 110–440 Hz and vibrato at 440 Hz, and ±20 cents at 220 Hz. It reads the skirt of 4.5–12
  kHz noise in the Harsh band as −4.3 dB (the mapping's noise end is −4.5).
- **Onsets** (a bright vowel whose top end lies 20 dB under the ess): an ess after the vowel reads as
  noise within the first hop with periodicity (0.67 ms) and after 11.3 ms with flatness (its hop
  and the window's taper); a vowel after the ess reads harmonic from 38.7 ms (periodicity) and
  43.3 ms (flatness). Periodicity first reads the vowel harmonic at ≈ 22 ms, then dips to 0.52 at
  34–42 ms: the detrending high-pass ringing after the 20 dB drop of the envelope. Noise rising
  linearly across a window or decaying 12 dB across one reads as noise with both (p10 of N ≥ 0.9).
- **The /ʃ/ hold:** 2.5–3.7 kHz noise in place of a harsh vowel holds the Harsh band on 100 % of its
  hops with periodicity, 94 % with flatness (Voice); Allround never holds on noise.

### CPU (measured)

`taskset -c 2 HaliteTests_detector "[.perf]"`, i9-12900K, the detector alone, stereo, 512-sample
blocks, process CPU time, the fastest of five passes over 20 s. With the final constants
(2026-11-14, load average 7–12 from other work on the machine, so possibly a little high):

| Cue | Linked, Harsh off | Harsh on | Unlinked, Harsh off | Harsh on | Mid |
|---|---|---|---|---|---|
| **periodicity (kept), 48 kHz** | **0.164 %** | **0.315 %** | 0.166 % | 0.318 % | 0.086 % |
| flatness, 48 kHz | 0.303 % | 0.570 % | 0.306 % | 0.573 % | |
| blend, 48 kHz | 0.382 % | 0.716 % | 0.385 % | 0.726 % | |
| periodicity, 96 kHz | 0.286 % | 0.545 % | 0.293 % | 0.553 % | |

Part 1's run (load average ≈ 2, before the corpus constants) read 0.161 / 0.310 % for periodicity
at 48 kHz. These are process-time figures on a loaded machine and were **not re-measured** after
the review's fixes (load average 42–50 throughout); measure again on an idle machine before M2
spends the remaining budget. Load-independent, callgrind on the detector alone (4 s of the perf
signal, 48 kHz stereo, Linked, periodicity, 512-sample blocks, -O3; a driver outside the
repository): **144.4 M instructions with Harsh off, 277.3 M with Harsh on**, of which the key's
clamp (decision 11) is 5.9 M. The budget (Split or Wide ≤ 0.5 % with the detector, Harsh ≤ +0.3 %) leaves the
reduction 0.34 % with the kept cue; Harsh adds 0.15 %. Making the periodicity
cue's lag loop contiguous and spreading its exact recompute over a hop took the detector from 210 M
to 130 M instructions per 4 s of stereo (callgrind; that loop had been 56 % of it).

### Open problems found

- **Noise in pauses** (part 1: white noise at −50 dBFS read `S` = +1.2 dB a second into a pause).
  The gap hold (decision 9) keeps `V` at the voice for pauses up to 1 s whatever the room tone; a
  longer pause with hiss more than 20 dB under the voice still lets `V` fall to the hiss after 1 s.
  Not measured again on synthetic noise; M3.
- **The release floor.** `B` now releases over 5 ms (4.34 dB per 5 ms), so it no longer holds a
  reduction up. Speed Auto's own release (M1: 15 … 80 ms) is what lengthens reductions into the next
  vowel, and it decides the vowel-frame gate (*Measured (M1)*). Settled in M3 (*M3: Speed*: 15 … 30 ms).
- **Vowel onsets** read noise-like for their first ≈ 20 ms after silence and up to ≈ 40 ms after an
  ess. The onset hold (decision 10) covers onsets that raise the body; a vowel after a voiceless
  stop's aspiration (the body rising slowly) and the first note of a belted scale remain the main
  failures (*Measured (M1)*).
- **Hops are anchored to the host timeline** since M2 Spectral (`Detector::anchor`, PLAN.md,
  Warm-up): they end on the timeline's multiples of 32 and the periodicity cue decimates on its
  multiples of its factor; the probe still counts from 0 (`../shapes/spectral/NOTES.md`).
- **Loud transients deafen the detector for up to the gap hold.** Anything in the body band more
  than 20 dB over the voice (a click, a mic bump, perhaps a hard plosive) lifts `V`; the voice after
  it then reads more than 20 dB under `V`, so the gap hold keeps `V` up for its 1 s before it
  falls. Measured on synthetic bursts only (64 samples at +24 dBFS over a −20 dBFS vowel: `V` more
  than 1 dB off for 1.76 s); real transients not measured. M3.

### The probe

`DetectorProbe.h`: `probe::run (input, channels, samples, rate, settings, key, keyChannels, blocks,
tuning)` runs a fresh `Detector` (the class the chain runs) over the buffer under flush-to-zero, as
`processBlock` does, and returns per hop every level, cue, flag, target, hold and reduction
(`DetectorFrame`), plus the events. The reductions are moved back by the look-ahead, so every
field of `hops[i]` describes input samples `[i · 32, (i + 1) · 32)`; the look-ahead and the end are
completed with silence. `NoiseCueKind::blend` computes both cues, so one run reports both raw values.

### Tests

`HaliteTests_detector` (this folder's sources only): B ripple and RMS reading at 44.1/48/96 kHz;
B's rise; level independence of `B − V`, N, `S`, targets, reductions, H − body and the events over
−30 … 0 dB (both cues, within 10⁻³ dB, after the first second); the body hold through a 150 ms ess
and its release after 250 ms of noise; voice onsets; knee and Range; Relative against Absolute;
Allround ignoring N; the event rule hop by hop, in Normal and Live; the look-ahead ramp; the Harsh
holds; Linked, Unlinked, Mid, Side and a mono bus with a stereo key; key routing and Key HPF;
Detect Low above High and the third-octave widening (also against 0.45 fs at 44.1 kHz); any block
size (1, 32, 33, 4096, mixed) giving identical hops at 44.1 kHz; both cues on noise, harmonics,
vibrato, onsets, rises and decays; the periodicity sums exact again after a +80 dB passage; the
event ring (order, overrun, a second thread); the FFT against a direct DFT; the gap hold through a
pause and after a 30 dB drop; the plan's literal values the corpus does not see (the 3 dB knee, the
linear look-ahead ramp, Speed Auto's limits (M3: 15 … 30 ms), the 30 ms onset hold, the body cap); Harsh from
the louder band when Linked, `H − body` in Voice and `H` in Allround, its body holding with `V`,
its 50 ms warm-up; an Unlinked ess on either channel; NaN, Inf and float max in the key; settings
that change while it runs (`DetectorChangeTests.cpp`: a fixed release, the stereo routing and the
key, Harsh switched on); the corpus (`CorpusTests.cpp`, *Measured (M1)*); `[.perf]` and
`[.report]`. Each of these was checked to fail when the rule it pins is broken (37 single-line
mutations of the detector, all caught). `HaliteTests`: the chain passes audio bit for bit with the
detectors running (every cue, stereo mode and key), its events are the probe's, the processor keys
the detectors from the side chain, its defaults are `DetectorSettings{}` (what `tools/measure`
measures), NaN and Inf leave its meters finite, and nothing allocates (processor with every
parameter automated, chain with every cue at 44.1 and 96 kHz).

## Measured (M1)

2026-11-14, M1 part 3: the detector on labelled speech and singing through `tools/measure`
(`HaliteMeasure`), which runs the plugin's own `Detector` through `dsp::probe` and the gate code in
`tests/support/Gates.cpp` (shared with the corpus tests). Every number here was printed by it,
on the m1 subset (`tools/corpus/README.md`, *Subsets*) unless marked: 48 kHz VCTK (40 speakers,
1597 recordings), 44.1 kHz VocalSet sung /i/ and /e/ (18 singers, 302 exercises), 16 kHz
LibriSpeech test-clean (40 speakers, 2620 recordings), the committed tier and synthetic vowels.
The episode and token counts under *Failure cases* were counted from its `--dump` output by a
script outside the repository (the dumps are per hop, so a token there is a run of equal labels).
**Constants were chosen on the `tune` split alone; `holdout` was run once the constants were
fixed and changed none of them.**

### Definitions

- **Detection:** a phoneme token is detected when an event overlaps its labelled span. Gated on
  VCTK (the 48 kHz corpus) and the committed tier; LibriSpeech (nothing above 8 kHz, so its band
  is 4.5–7.2 kHz) is reported.
- **Vowel frames:** 32-sample hops whose centre lies in a vowel label more than 30 ms from every
  sibilant label; *reduced* means more than 0.5 dB of Ess reduction (release tails included, as
  the plan words it); *own S* means the hop's own `S` is over the threshold (the detector's
  decision, without the release tail).
- **Onset cases:** recall of sibilants whose previous label is a vowel; reduced frames in vowels
  whose previous label is a sibilant (after the 30 ms guard).
- **Bright:** vowel frames of sung material (VocalSet, synthetic, the committed VocalSet
  excerpts) at Focus 70; the gate is "at most 0.5 dB" on every frame. The synthetic vowels come in
  two sets of 42: the *tune* set (f0 110 … 880 Hz, ±50 cents at 5.5 Hz) was seen while constants
  were chosen; the *held-out* set (f0 98 … 784 Hz, ±35 cents at 6.2 Hz, other phases) was built
  after the review and only reports.
- **Harsh:** mean Harsh reduction over the hops of sibilant tokens (all six sibilants), Harsh on at
  its defaults. Reported beside it: the share of tokens whose mean (or peak) Harsh reduction is
  over 0.5 dB, the worst speaker, and the mean split into *held* (hops where Harsh keeps the
  target it had before: during an ess, on noise in its band, the warm-up) and *own* (hops where it
  follows the sibilant itself).
- **Level independence:** renders at −10, −20 and −30 dB compared with 0 dB; an event counts when
  `V` is above the body floor (−60 dBFS) in the −30 dB render on every hop from 300 ms before it
  to its end. That is M1's reading of the plan's "above the body floor": the plan names no window,
  and the 300 ms lookback (the longest Auto release, an earlier event's tail) is M1's own (PLAN.md,
  Open 2). Gate: no event appears or disappears, no peak reduction moves by more than 0.5 dB.
  Every event, the floored ones too, is reported beside it (*Gates*).
- **Stream and per file.** VCTK recordings are single sentences, each a cold start for the
  detector, which a vocal track in a session is not. The gates are reported both ways: *stream*
  plays each speaker's recordings back to back (one cold start per speaker), *per file* starts
  every recording cold. Constants were chosen on streams.

### The noise cue: periodicity

At the threshold where /s/ and /z/ both reach 95 % on VCTK tune (T95), with the final constants
but the onset hold (added last; `--stream --set onsetHoldMs=0 --set cue=… --set threshold=…`,
synthetic: the tune set):

| Cue | T95 | Vowel frames reduced | Own S over threshold | VocalSet frames reduced | Synthetic frames reduced |
|---|---|---|---|---|---|
| **(a) periodicity** | −24.9 dB | **23.4 %** | **5.2 %** | **1.9 %** | 52 % |
| (b) flatness | −24.2 dB | 40.0 % | 17.7 % | 50.9 % | 62 % |
| blend | −24.8 dB | 24.3 % | 5.5 % | 13.8 % | 61 % |
| blend, flatness mapping retuned to −3 … −8 dB (best of −3, −4.5, −6 × −8, −12, −16) | −24.8 dB (/z/ 94.9 %) | 23.3 % | 4.9 % | 7.6 % | 61 % |

With flatness's mapping retuned the blend matches periodicity on spoken vowel frames (23.3
against 23.4 %, own `S` 4.9 against 5.2 %). **The choice of periodicity rests on the sung
material:** 1.9 % of VocalSet frames reduced against 7.6 % for the best blend, and 52 against 61 %
of the synthetic frames.

The same order held with the provisional constants (an earlier run, not repeated: 12 dB noise range, 0.6 … 0.85 mapping:
periodicity 26.2 % / 5.8 %, flatness 38.3 % / 15.9 %, blend 29.3 % / 8.2 %, VocalSet 36 %, 65 %,
51 %) and with flatness's ends moved to −4 … −10 dB (37.8 %, VocalSet 49.5 %). /z/ needs the
same threshold with every cue (T95 within 0.7 dB), so /z/ does not choose. **Periodicity is
kept**; flatness and the blend stay in the code behind `DetectorSettings::noiseCue` for
measurement only. On sung vowels the periodicity cue reads harmonic throughout (mean N 0.01–0.03
on five VocalSet exercises); flatness fails on vibrato as M0 predicted.

### Sweeps (tune, streams)

Each line changes one thing from the configuration of its time; "@T95" is at the /s/+/z/ 95 %
threshold, "@default" at −17.5 dB.

| Constant | Values (vowel frames reduced) | Chosen |
|---|---|---|
| `B` averaging / release | 2/20 ms: 28.8 %, own 9.7 %; 3/8: 25.3 %, 5.9 %; **4/5: 24.5 %, 5.1 %** (@T95). Ripple ≤ 0.5 dB holds for 4/5 (0.451–0.459 dB), 4/3 (0.48), 3/5 (0.49–0.50), 1/20 (0.47–0.48) | 4 / 5 ms |
| noise range (Focus 70 %) | 12 dB: 26.2 %, VocalSet 36 %; 18: 25.2 %, 15 %; 24: 24.8 %, 5.5 %; **30: 24.5 %, 2.0 %**; 36: 24.3 %, 1.6 % (@T95) | 30 dB |
| periodicity mapping | 0.60…0.85: 24.5 %, VocalSet 2.0 %; **0.55…0.70: 22.5 %, 1.3 %**; 0.50…0.80: 23.5 %, 1.7 %; 0.60…0.75: 24.0 %; 0.65…0.80: 25.2 % (@T95) | 0.55 … 0.70 |
| body cap | 1 kHz: 7.1 %; 1.5: 6.7 %; **2: 6.6 %**; 2.5: 6.5 %; 3 kHz: 6.4 % (@default, share 0.67); recall within 2 points | min (2 kHz, Detect Low / 2), as planned |
| `V` release | **80 ms: 26.2 %**; 160: 27.9 %; 320: 30.1 % (@T95) | 80 ms |
| candidate level, hold | −30 dB: 27.4 %; −25: 25.4 %; **−20: 26.2 %**; −15: 27.1 %; 500 ms hold: 26.2 % (@T95) | −20 dB, 250 ms |
| rise rule | 1, 2, 4 dB, off: 26.2–26.3 % (@T95) | 2 dB (it matters on synthetic onsets, not on streams) |
| gap hold | none: 25.5 %; 12 dB/2 s: 23.4 %; 15/1: 23.6 %; **20/1: 23.4 %**; 20/2: 22.2 %; 30/1: 23.6 % (@T95) | 20 dB, 1 s (2 s is 1.2 points better; 1 s keeps `V` following a quieter voice) |
| onset hold | 0: 7.35 %, own 0.68 %, /z/ 68.7 %; 20 ms: 6.68 %; **30: 6.60 %, 0.50 %, 68.6 %**; 50: 6.50 %, /z/ 68.4 %; 80: 6.29 %, /z/ 66.9 % (@default); synthetic tune set: recordings reduced 25 → 14, largest 8.0 → 4.8 dB; the held-out set (built after the choice): 26 → 14, 8.0 → 5.6 dB | 30 ms |
| Detect Low | 3 kHz: 10.3 %, /ʃ ʒ tʃ dʒ/ 84.9 %; 3.5: 8.6 %, 80.9 %; 4: 7.5 %, 74.7 %; **4.5: 6.6 %, 69.3 %**; 5: 6.0 %, 64.7 % (@default; VocalSet frames 0.74 % at 3 kHz, 0.12 % at 4.5) | 4.5 kHz, as planned |
| look-ahead, Live | 0, 5, 10 ms and Live: 24.5–24.8 % (@T95) | no effect on these gates |
| Speed | Auto: 24.5 %; fixed 5 ms: 10.8 %; fixed 15 ms: 18.2 %; Auto share 0.25 with 5 … 40 ms: 13.6 % (@T95). @default: Auto 6.6 %, **fixed 5 ms 1.2 % (holdout 1.2 %)**, fixed 15 ms 3.6 % | unchanged (M3); see *Gates* |

The knee (3 dB) is not swept: on these gates it acts as a threshold offset (an event needs
`S` > T + 0.23 dB with a 3 dB knee).

### Threshold ranges and defaults

From the tune histograms (streams), by the plan's own Learn rules (*Learn*): the default
Threshold reduces the median ess by 4 dB and the quietest quarter by none; the default Harsh
Threshold reduces the loudest tenth of voiced frames.

| Largest `S` per token (dB) | p1 | p5 | p25 | p50 | p75 | p90 | p99 |
|---|---|---|---|---|---|---|---|
| VCTK /s/ + /z/ | −29.2 | −23.8 | −17.6 | −13.5 | −9.3 | −4.7 | 8.5 |
| VCTK /s/ | −26.9 | −22.5 | −16.8 | −12.7 | −8.2 | −3.1 | 12.8 |
| LibriSpeech /s/ + /z/ | −23.1 | −18.2 | −11.8 | −7.3 | −2.6 | 2.0 | 11.7 |
| VCTK /s/, per file (cold starts) | −26.9 | −22.5 | −16.8 | −12.6 | −8.2 | −3.0 | 18.7 |

| Vowel frames (dB) | p1 | p10 | p50 | p90 | p99 | p99.5 |
|---|---|---|---|---|---|---|
| VCTK `H − body` | −32.4 | −23.1 | −16.3 | −10.1 | −5.2 | −3.6 |
| VocalSet `H − body` | −29.1 | −16.8 | −8.0 | −0.7 | 4.9 | 5.8 |
| LibriSpeech `H − body` | −34.6 | −21.6 | −13.3 | −5.8 | 0.6 | 2.2 |
| VCTK `S` | −64.4 | −55.0 | −46.3 | −29.1 | −19.6 | −17.3 |

- **Threshold:** max (median − 4, p25) = max (−17.5, −17.6) → **default −17.5 dB**. Range **−36 …
  +18 dB**: 7 dB under VCTK's p1; 5 dB over the streams' p99 of VCTK /s/ (+12.8; /s/ + /z/ +8.5)
  and 6 dB over LibriSpeech's (+11.7). It is **0.7 dB under the cold-start p99 of VCTK /s/
  (+18.7)**: just over 1 % of the /s/ peaks of recordings started cold read above the top. Those
  are the first syllables before `V` knows the voice, not a voice in a session; M0's top (+24 dB)
  would clear them. The user moved the top to +24 dB (Settled 35); M1b set the default to −17.6 dB
  (*M1b*).
- **Harsh Threshold:** VCTK p90 → **default −10 dB**. Range **−36 … +12 dB**: under every corpus's
  p1, 6 dB over belted VocalSet's p99.5.
- A typical ess reads −13.5 dB (Settled 31's "below 0 dB").

### Gates

Defaults (Threshold −17.5 dB, Range 8, Focus 70 %, Detect 4.5–12 kHz, look-ahead 5 ms, Normal,
Speed Auto, Harsh on at −10 dB). **Bold** fails the plan's gate.

| | VCTK tune, stream | **VCTK holdout, stream** | VCTK holdout, per file | Committed tier | Gate |
|---|---|---|---|---|---|
| /s/ detected | **80.4 %** (1125) | **75.7 %** (1067) | **75.9 %** | **64.5 %** (31) | ≥ 95 % |
| /z/ detected | **68.6 %** (910) | **61.5 %** (844) | **61.5 %** | **72.7 %** (11) | ≥ 95 % |
| /ʃ/, /ʒ/, /tʃ/, /dʒ/ | 75.8, 57.1, 76.7, 68.7 % | 76.5, 50.0, 78.4, 53.0 % | 76.5, 50.0, 78.4, 52.0 % | 85.7, –, 33.3, 66.7 % | reported |
| sibilants after a vowel (/s/, /z/) | 79.8, 67.0 % | 75.3, 58.0 % | 75.3, 58.0 % | 61.5, 72.7 % | reported |
| vowel frames reduced | **6.60 %** | **5.79 %** | **6.10 %** | **5.14 %** | ≤ 2 % |
| … by their own `S` | 0.50 % | 0.50 % | 0.49 % | 0.60 % | (diagnostic) |
| … in vowels right after an ess | 24.7 % | 25.2 % | 25.5 % | 10.2 % | reported |
| … in vowels right after a pause | 8.3 % | 6.7 % | 11.5 % | 6.3 % | reported |
| bright, VocalSet / committed sung: frames reduced, largest | **0.118 %, 8.0 dB** | **0.264 %, 8.0 dB** | **0.275 %, 8.0 dB** | **0.34 %, 1.8 dB** | ≤ 0.5 dB |
| bright, synthetic | **14 of 42 reduced, 4.8 dB** (tune set) | **14 of 42, 5.6 dB** (held-out set) | | | ≤ 0.5 dB |
| Harsh during sibilants | 0.145 dB | 0.143 dB | 0.244 dB | 0.184 dB | ≤ 0.5 dB: ok |
| … held / own | 0.122 / 0.022 dB | 0.120 / 0.023 dB | 0.212 / 0.032 dB | 0.176 / 0.008 dB | (diagnostic) |
| … tokens with a mean (peak) over 0.5 dB | 8.2 % (12.3 %) | 7.7 % (11.8 %) | 12.0 % (17.2 %) | 5.5 % (5.5 %) | reported |
| … worst speaker: mean, its tokens over 0.5 dB | p334: 0.639 dB, 32.8 % | p317: 0.550 dB, 32.5 % | p317: 0.634 dB, 35.0 % | p311: 0.710 dB, 20.0 % | reported |
| level independence, events above the floor (excluded): list changes, largest change | 1408 (1529): 0, 0.000 dB | 1163 (1356): 0, 0.000 dB | 1113 (2181): 0, 0.000 dB | 31 (10): 0, 0.000 dB | ok, on these events |
| … every event: list changes, largest change | 1121 of 8859, 7.48 dB | 922 of 7580, 7.45 dB | 3251 of 9977, 7.49 dB | 3 of 124, 6.61 dB | reported (Open 2) |
| /s/, /z/ detected at −10, −20, −30 dB | 80.4, 79.9, 73.3; 68.6, 68.6, 62.0 % | 75.6, 75.2, 71.1; 61.5, 61.5, 55.9 % | | | reported |

LibriSpeech (reported): tune /s/ 96.3 %, /z/ 89.6 %, vowel frames 10.3 %; holdout 93.1 %, 84.8 %,
11.2 %; level independence 0–2 changes per half at file starts (0.07–0.64 s in), largest
5.2 dB. Per file, VCTK tune: /s/ 80.4 %, /z/ 68.5 %, vowel frames 6.96 % (after a pause 17.4 %
against 8.3 % in streams: the cold starts).

**Level independence passes only on the events its rule keeps.** In the −30 dB render VCTK's body
sits at a median of −53.6 dBFS in vowels (holdout streams; −61.1 … −48.7 dBFS per speaker), within
a few dB of the −60 dBFS floor, so the rule leaves out
52–54 % of the streams' events (66 % per file). Over every event the gain changes the event list in
about one comparison in eight (one in three per file) and moves a peak reduction by up to 7.5 dB, and recall at −30 dB
falls by 4.6–7.1 points (/s/, /z/; table). The gap hold is relative and does not depend on the
gain; the absolute floor does, wherever `V` is under it. Options, measured on the streams (holdout;
tune in brackets), for the user (PLAN.md, Open 2):

- *Start every recording at the same level* (median `V` in vowels at −20 dBFS) before the 0 …
  −30 dB sweep: 904 events excluded (965); every event 761 of 7778 changes, 7.46 dB (856 of 9052,
  7.47 dB).
- *Narrow the claim to 0 … −20 dB*: 569 excluded (737); every event 368 of 5050, 7.43 dB (454 of
  5908, 7.48 dB).
- *Lower the floor*: at −80 dBFS 450 excluded (528), every event 397 of 7956 (364 of 9139), at
  −90 dBFS 148 of 7949 (132 of 9116); detection does not move and streamed vowel frames rise 5.79 →
  6.06 % (6.60 → 6.76 %), but per file, where the floor guards cold starts, they rise 6.10 → 8.52 %
  (6.96 → 8.67 %) at −80 dBFS. At −100 dBFS an eligible event moves by up to 0.002 dB (5.5 dB),
  per file by up to 2.9 dB (4.3 dB).

None leaves every event unchanged: the largest change over every event stays 7.4–7.5 dB in each.

**The Harsh gate passes as a pooled mean** (0.14 dB over every sibilant hop) but not evenly: 7.7–8.2 %
of sibilant tokens get more than 0.5 dB on average, and one speaker in each half averages more
than the gate (tune p334 0.64 dB, holdout p317 0.55 dB, a third of their tokens over 0.5 dB). Most
of it is held (0.120 of 0.143 dB on holdout): the reduction Harsh had on the vowel before, kept
through the ess as the plan's hold asks, not the ess's own energy (0.023 dB). Whether the gate is
the pooled mean, a bound per token or per speaker, and whether held reduction counts, is for the
user (PLAN.md, Open 2).

**Trade-off** (VCTK, streams, Speed Auto; the threshold is the only change):

| Threshold | −30 | −27 | −24 | −21 | **−17.5** | −15 | −12 | −9 | −6 |
|---|---|---|---|---|---|---|---|---|---|
| tune /s/, /z/ | 99.7, 99.2 | 99.0, 97.3 | 96.9, 93.8 | 92.4, 85.3 | 80.4, 68.6 | 64.3, 51.3 | 45.2, 31.2 | 29.1, 17.0 | 16.3, 7.8 |
| tune vowel frames | 39.2 % | 28.6 % | 19.0 % | 11.8 % | 6.6 % | 4.2 % | 2.3 % | 1.1 % | 0.6 % |
| holdout /s/, /z/ | 100, 99.3 | 99.9, 98.3 | 98.3, 92.9 | 92.8, 82.6 | 75.7, 61.5 | 59.6, 44.8 | 38.9, 26.4 | 23.8, 12.6 | 13.2, 5.6 |
| holdout vowel frames | 36.3 % | 25.8 % | 17.4 % | 11.1 % | 5.8 % | 3.5 % | 1.9 % | 0.9 % | 0.6 % |

**No threshold passes both the detection and the vowel-frame gate on VCTK.** /s/ and /z/ at 95 %
need about −24.5 dB, where 17–19 % of vowel frames are reduced; 2 % of vowel frames allows about
−12 dB, where /s/ is at 39–45 % and /z/ at 26–31 %. With a fixed 5 ms release instead of Speed
Auto the vowel-frame gate passes at the default (1.22 % tune, 1.15 % holdout) and detection barely
moves (79.3 / 67.8 % tune, 74.8 / 60.9 % holdout): the reductions' release tails, not the
detector's decisions (0.50 %), make the vowel-frame number.

**Per speaker** (VCTK streams, /s/, /z/, vowel frames reduced; min / median / max): tune 25.0 /
90.4 / 100 %, 12.5 / 70.4 / 100 %, 1.2 / 6.1 / 13.0 %; holdout 42.9 / 78.5 / 100 %, 23.5 / 55.2 /
100 %, 1.8 / 4.2 / 16.1 %. 6 of 20 tune and 4 of 20 holdout speakers reach 95 % on /s/. The
speakers whose esses read low against their own voice (tune p263, p304, p323, p364; holdout p248,
p261, p347, p376) are the ones a fixed relative threshold leaves alone; the speakers with the
most reduced vowel frames (p238, p249, p297; p336, p340) have bright esses and are reduced most.
Learn (M3) sets the threshold per voice for this reason.

### Failure cases

- **Vowel frames after voiceless and alveolar stops.** Of the vowel frames whose own `S` is over
  the threshold (holdout), 81 % follow a consonant (/t/ 29 %, /d/ 16 %, /n/ 8 %, /k/ 5 %), 69 % lie
  in the vowel's first 50 ms and 100 % read noise-like: the stop's burst and aspiration, labelled
  as vowel by the aligner and physically noise in the band. Of the events that overlap no
  sibilant (tune streams: 1154 of 2937), the label they overlap most is a pause (`sil`, 256), then
  /t/ (181), /ɪ/ (119), /ʌ/ (100).
- **Release tails.** Vowels right after an ess have 25 % of their frames reduced past the 30 ms
  guard (above: Speed Auto). Recordings with the most reduced vowel frames: p238, p297, p249
  (tune; largest 8 dB in p238_004 at 3.47 s, p297_005 at 2.30 s), p336, p265, p340 (holdout;
  p336_001 at 0.98 s, p265_182 at 3.68 s).
- **Missed esses** are mostly quiet ones. Of the 577 missed /s/ /z/ tokens in the holdout streams'
  dumps (the gate counts 584), 555 have frication in the band and a label longer than the
  aligner's 30 ms minimum; their largest `S` has a median of −19.8 dB, 2.3 dB under the threshold
  (detected tokens −12.2 dB), the cue reads them as noise (median of the token's mean `N` 0.87),
  and they are a little shorter (median 90 against 100 ms). The highest of them read −17.3 dB,
  inside the knee, where the reduction stays under the event's 0.5 dB; the lowest are /z/ of p376,
  p302 and p345 at −34 … −28 dB. The lowest-`S` misses in the tool's list are label errors: p274_372
  at 2.50 s /z/ is a 30 ms label 35 dB under `V` with no frication near it; p265_055 at 5.24 s /s/
  lies after the frication of *terrorists* (5.10–5.22 s, inside the label /t/, where `S` reaches
  −17.5 dB). 22 of the 577 look like that (a 30 ms label, or `B − V` never over −30 dB).
- **Sung vowels**: the periodicity cue drops for a few tens of ms inside sustained and gliding
  notes, not at note onsets. The 18 VocalSet streams (both halves) hold 67 episodes of 2 dB or more
  in vowel labels: 61 start more than 500 ms after the last body rise; against the
  Annotated-VocalSet note files, 21 start inside a transition between notes, 43 inside a sounding
  note and 3 outside both; only 8 lie within 50 ms of a note's first sound, 8 within 150 ms of the
  vowel label's start, and 4 on a body more than 20 dB under the singer's median `V` (breaths and
  gaps). At the start of 66 of them the cue reads noise (`N` ≥ 0.5). Example, m11 (holdout) at
  27.27 s into its stream, a steady sung /i/ with `B − V` −8 … −10 dB: ρ falls from 0.95 to about
  0.4 for ≈ 35 ms, `N` goes to 1, `S` to −8 dB and the reduction to 8 dB. The labels give one
  vowel span per phrase (the note files' notes and transitions are merged), so a gate per note
  needs the note files. The committed f2 scale: 1.8 dB within its first 0.25 s (a cold start, the
  excerpt cut mid-scale), nothing after.
- **Synthetic**: every flat-spectrum vowel (harmonics of equal amplitude to 12 kHz) is reduced
  through its steady state (`S` about −15 dB, 2.5 dB over the threshold) and most at its onset (up
  to 4.8 dB at 0.30–0.31 s; held-out set 5.6 dB): `B − V` reads +4.4 … +6.0 dB, 21 dB of noise
  range takes it to −15 … −16 dB. Belted /i/ in VocalSet reads `B − V` −10 … −13 dB. Spectra
  falling 3 or 6 dB per octave pass in both sets, onsets included.

### Proposed gate revisions (for the user to decide)

None is applied; the gates in `PLAN.md` stand and fail as above.

1. **Vowel frames:** count the detector's decisions (own `S`, 0.50 % on both halves) and give the
   reduction's release its own gate in M2/M3 with Speed; or keep the gate and let M3 choose Speed
   Auto so that it passes (a fixed 5 ms release passes at 1.2 %). Either way, consider a 50 ms
   guard after voiceless stops' releases, as the 30 ms guard round sibilants: the frames there are
   aspiration.
2. **Detection:** 95 % of /s/ and /z/ at one default threshold contradicts ≤ 2 % of vowel frames on
   VCTK (trade-off above) and the plan's own Learn rule (which leaves the quietest quarter
   alone). Gate recall at the threshold Learn sets per speaker (M3), or gate a lower recall at the
   default (measured: 75.7 / 61.5 % on holdout).
3. **Bright** (revised after the review): the earlier proposal, to gate sung frames past a note's
   first 50 ms, is **withdrawn**. Only 8 of the 67 VocalSet episodes lie there, and the labels
   cannot measure it. The failure is the periodicity cue dropping inside sustained and gliding
   notes, a detector problem for M3 (for example keeping `N` harmonic while the body is steady
   and the cue read harmonic just before), not a gate to move. What remains for the user: the
   synthetic flat spectrum (0 dB per octave to 12 kHz) fails in both sets on its own, and no voice
   has that spectrum; whether it belongs in the gate (−3 and −6 dB per octave pass).
4. **Level independence:** the gate holds only on the events M1's eligibility rule keeps (46–48 %
   of the streams' events). Choose what the gate means: keep the rule and report the share;
   normalise each recording first; narrow it to 0 … −20 dB; or lower the floor (it trades against
   cold-start vowel frames). Numbers for each: *Gates*, Level independence.
5. **Harsh:** choose the reading: the pooled mean (passes, 0.14 dB), a bound per token (7.7–8.2 %
   of tokens over 0.5 dB) or per speaker (one per half over 0.5 dB), with or without the held
   reduction (most of it).
6. **Threshold top:** +18 dB is frozen but lies 0.7 dB under the cold-start p99 of VCTK /s/
   (*Threshold ranges*); confirm it, or move it to +24 dB before anything ships. (Decided: +24 dB,
   Settled 35; the user took 1, 2, 4 and 6 in Settled 34–35, *M1b*.)

### Not verified

- Sung sibilants (no labelled source, `tools/corpus/README.md`).
- Live latency on the corpus beyond one run (no effect on these gates, above); look-ahead was not
  swept for timing against the labels.
- The flatness cue on its own with a re-tuned frame or noise range: it lost by 17 points of vowel
  frames and 49 points of VocalSet frames; only its mapping was retuned, inside the blend (*The
  noise cue*).
- Rounding across compilers: the corpus tests' bounds leave two tokens and about 1.5 points of
  margin; Windows and macOS were not run here.

## M1b

2026-11-14: one more measured detector round before M2 (PLAN.md, Settled 34–37), under the
revised gates. Three offline studies on the `tune` split proposed changes; the proposals were
ported to C++ behind `DetectorTuning` constants, measured with `HaliteMeasure` on `tune` and chosen
there. `holdout` ran three times, and no holdout result chose anything:

- **Holdout (1)** ran before implementation was finished. Callgrind had already shown the
  detector over a ~0.35 % line of M1b's own (PLAN's budget has no such line, *CPU*), and the
  change made for it (the presence band from the body filter) came after the run, chosen on `tune`.
- **Holdout (2)** is the detector as built and reviewed.
- **Holdout (3)** is the final detector. The review found that the jump onset cost the start of
  affricates and of esses after stops, which token recall cannot see; the fix was chosen on `tune`
  (*After the review*).

Every run is reported. Every number in this section was printed by `HaliteMeasure` (or a test
named beside it) from the plugin's own `Detector`, unless it is marked *study* (Python on the C++
dumps, tune only, by the method its report gives) or *callgrind* / *timed*.

### The gates as measured

`tools/measure` and `tests/support/Gates.cpp` implement Settled 34:

- **Separability (gated on VCTK):** T95 is the highest Relative threshold, to 0.02 dB, at which at
  least 95 % of /s/ tokens produce an event (an event overlaps the label, as in M1). A *decision* is
  a vowel frame (more than 30 ms from every sibilant label) whose own target exceeds the event
  level, 0.5 dB: `S` over T95 + 0.23 dB with the 3 dB knee, the same rule that makes an /s/ count.
  Gate: decisions ≤ 2 % of vowel frames at T95. PLAN's wording, "own `S` over threshold", read
  literally (`S` over T95) counts a few more: `HaliteMeasure` prints it beside the gate (*S over
  T95*). Reported there: /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/; the decisions per speaker (min / median / max)
  and by kind; LibriSpeech and the committed tier at their own T95; the sung sets at VCTK's T95.
- **Inside the labels** (added after the review): a token counts as detected when any event
  overlaps it, so recall cannot see an ess caught late or let partly through. Per sibilant, at the
  threshold set and at T95: the label's frames with a decision, its frames reduced by more than
  0.5 dB, the mean reduction over them, and the first decision's time after the label's start
  (median, p90, share over 20 ms). A /tʃ/ or /dʒ/ label includes the closure, so its first decision
  comes 55–65 ms in even in M1: the times compare configurations, not the absolute onset.
- **Release tails** (vowel frames reduced by more than 0.5 dB that are not decisions) are reported
  at the threshold set, apart from the decisions; their gate comes with Speed in M3.
- **Level independence:** an event is in the working range when `V` stays at least 20 dB over the
  absolute body floor on every hop from 300 ms before it to its end, in the quietest render; the
  share of events this covers is reported beside the result. Each quieter gain is also reported
  against 0 dB in its own render's range, and every event without the range.
- **Harsh**, on VCTK and LibriSpeech, in every reading: the pooled mean over sibilant hops (the
  gate column), held and own, *added* (each token's mean Harsh reduction over what the hop before
  it carried), tokens over 0.5 dB by mean, peak and added, the worst speaker and the number of
  speakers over 0.5 dB.
- **Bright** per set: VocalSet; the synthetic vowels by tilt (0, −3, −6, −12 dB per octave; the
  flat spectrum reported apart, Open 2), at the default threshold (the gate) and at T95 (frames
  with a decision, largest `S` − T95).

M1 under these definitions (VCTK tune streams, T95 −22.28 dB): 2.03 % of vowel frames are
decisions (per speaker 0.23 / 1.74 / 7.22 %), /z/ 88.8, /ʃ/ 95.2, /ʒ/ 87.1, /tʃ/ 95.3, /dʒ/ 92.8 %.
73 % of the decisions fall in a vowel's first 50 ms, 83 % after a consonant (/t/ 29 %, /d/ 14 %).
Read literally (`S` over T95), M1 is at 2.17 % (holdout 1.93 %). The studies' own scoring (`S`
over T on any hop of the token, no knee) put M1 at 2.33–2.34 %.

### What the studies proposed (study, tune)

| Study | Candidate | Their tune result (M1 by their scoring: 2.33–2.34 %) | Their recommendation |
|---|---|---|---|
| Shape | **A:** `S` − 4 · max (0, −4 − R), R = B's 4 ms average over a 1–3 kHz band's | 0.82 %, worst speaker 1.76 %; /ʃ/ class within 1.6 points; VocalSet frames over T95 0.29 → 0.12 %; +21.5 M instructions | take |
| Shape | A2 (knee −2, slope 2); spectral slope over 1–11 kHz; same-instant body; a voiced-/z/ bonus | 0.75 % with /ʒ/ −5 points; slope no better than A at 7× the cost; the same-instant body loses /z/; the bonus adds 3 points of /z/ but worsens bright synthetic harmonics | not, or M3 (the bonus) |
| Reference | **C7:** a floor 30 dB under the body's loudest of 3 s (−100 dBFS minimum) and `S` − 0.64 · (500–2000 Hz band − V), clipped | 0.61 % (5-fold CV 0.48 %), every synthetic tilt passes, VocalSet 0.003 %; /ʒ/ −7 points; the threshold scale moves up by 14 dB | take |
| Reference | C2/C4 (`V` release 40 ms, the relative floor, persistence of the decision) | 1.48 / 1.23 %, worst fold 3.5 / 2.9 % | fallback |
| Noise cue | **D:** lags to 25 ms, an onset from the body's averaged level (10 dB over 5 ms), persistence of a harmonic reading (40 ms, ρ 0.8, B within 6 dB), noise range 40 dB | 0.86–0.88 %, worst speaker 2.4 %, sibilants within 0.6 points, VocalSet over T95 0.286 → 0.022 % | take |
| Noise cue | E (10.7 ms window); a fine-structure cue for /z/; Harsh hold rules | E 0.62 % with higher sung excess; /z/ is bounded by B − V, not the cue; LibriSpeech's Harsh number is the vowel's cut carried through the ess (added 0.09 dB) | M3 / no change |

### Measured in C++ (tune, streams)

Each candidate behind `DetectorTuning` constants, the same build, VCTK tune streams for
separability, VocalSet tune and the synthetic tune set for bright. A uses the study's band here
(1–3 kHz, 4th order, scaled with Detect Low) and slope 4 at Focus 70 %.

| Configuration | T95 | Decisions (worst speaker) | /z/ | /ʃ/ | /ʒ/ | /tʃ/ | /dʒ/ | VocalSet at T95: frames, largest S − T95 | Flat synthetic at T95 |
|---|---|---|---|---|---|---|---|---|---|
| M1 | −22.28 | 2.03 % (7.22) | 88.8 | 95.2 | 87.1 | 95.3 | 92.8 | 0.234 %, +17.4 dB | 14 of 14, +9.6 dB |
| relative floor alone (C1) | −22.28 | 2.00 % (7.16) | 88.8 | 95.2 | 87.1 | 95.3 | 92.8 | 0.235 %, +17.4 | 14 of 14, +13.2 |
| A | −22.28 | 0.75 % (1.57) | 88.4 | 94.1 | 85.7 | 95.3 | 94.0 | 0.101 %, +10.3 | 14 of 14, +9.6 |
| D, noise range 30 dB | −22.32 | 0.78 % (2.18) | 88.2 | 95.2 | 87.1 | 95.3 | 91.6 | 0.118 %, +10.5 | 14 of 14, +9.6 |
| D, noise range 40 dB | −22.32 | 0.76 % (2.18) | 88.2 | 95.2 | 87.1 | 95.3 | 91.6 | 0.018 %, +10.2 | 14 of 14, +2.6 |
| C7 | −8.10 | 0.61 % (1.23) | 88.7 | 93.5 | 82.3 | 95.3 | 94.0 | 0.003 %, +6.0 | none, −5.0 |
| A + D (40 dB) | −22.32 | 0.40 % (1.09) | 87.9 | 94.1 | 85.7 | 95.3 | 91.6 | 0.002 %, +8.5 | 14 of 14, +2.6 |
| A + D + relative floor | −22.32 | 0.39 % (1.09) | 87.9 | 94.1 | 85.7 | 95.3 | 91.6 | 0.002 %, +8.5 | 14 of 14, +6.2 |
| C7 + D | −8.10 | 0.33 % (0.92) | 88.1 | 93.5 | 83.0 | 94.2 | 91.6 | 0.001 %, +2.3 | none, −5.0 |

One constant at a time around A + D (40 dB), the same table's columns. These were measured with
the study's 4th-order 1–3 kHz presence band and the jump's first hold (N = 0), before either was
replaced; the final detector's sweep is under *After the review*:

| Constant | Values (decisions, worst speaker; what else moved) | Chosen |
|---|---|---|
| shape slope at Focus 100 % | 2.9: 0.43 % (1.25); 4.3: 0.41; 5.7: 0.40 (1.09); 8.6: 0.39 (1.08), /dʒ/ −1.2 | 6 (4.2 at Focus 70 %): flat, a round value |
| shape knee | −2 dB: 0.33 % but /ʃ/ 90.9, /ʒ/ 81.0, /dʒ/ 85.5; **−4: 0.40 %**; −6: 0.46 % | −4 dB |
| noise range | 30 dB: 0.41 %, VocalSet over T95 0.099 %, tilt −3 fails; **40: 0.40 %, 0.002 %**; 50: 0.40 %, the flat spectrum passes at T95 (−4.4 dB), /dʒ/ −1 token | 40 dB (28 at Focus 70 %); 50 is the option if the flat spectrum stays in the gate (Open 2) |
| longest lag | 12.5 ms: 0.43 % (1.18), /dʒ/ 92.8; 20: 0.41; 25: 0.40; bright identical | 12.5 ms: with A on, 25 ms adds 0.03 points and nothing on bright, at the most CPU of D's parts |
| jump onset | off: 0.67 % (1.41), VocalSet 5.8 dB; 8 dB: 0.36 %; **10: 0.40 %**; 12: 0.43 %. Its hold 20 ms: 0.40 %; **30 ms: 0.34 % (1.02)**, /tʃ/ +1.2 | 10 dB over 5 ms, sharing M1's 30 ms onset hold |
| persistence | off: 0.42 %, VocalSet default 5.5 dB, synthetic 1.8 dB; **40 ms: 0.40 %, 4.4 dB, none**; 60 ms: 0.41 % | 40 ms, ρ 0.8, B within 6 dB |
| presence averaging | 2 ms: 0.41 %; **4 ms (B's): 0.40 %**; 8 ms: 0.39 %, /ʃ/ −0.6 | B's 4 ms, so R is B's own ratio |
| shape term's bound | 24, 40, 200 dB: identical gates | 40 dB × Focus: `S` stays readable on vowels (it reached −150 dB unbounded) |

**The floor** (A + D, tune; per file means every recording starts cold): separability does not
depend on it; what it trades is level coverage against cold starts.

| Absolute floor | Relative floor (30 dB under the 3 s loudest body) | Decisions, streams / per file | VocalSet reduced, largest: streams; per file | VCTK events per file | Every event at −10 … −30 dB: list changes |
|---|---|---|---|---|---|
| **−60 dBFS** | off | 0.35 / 0.48 % | 0.016 %, 4.4 dB; 0.027 %, 5.7 dB | 2958 | 771 of 6989 |
| −60 dBFS | on | 0.34 / 0.48 % | 0.015 %, 4.4 dB; 0.027 %, 5.7 dB | 2957 | 628 of 6840 |
| −70 dBFS | on | 0.34 / 0.75 % | 0.055 %, 7.4 dB; 0.224 %, 8.0 dB | 3568 | 310 of 6918 |
| −80 dBFS | on | 0.34 / 0.76 % | 0.064 %, 7.4 dB; 0.677 %, 8.0 dB | 3641 | 126 of 6919 |
| −80 dBFS | off | 0.41 / 0.76 % | 0.084 %, 7.4 dB; 0.685 %, 8.0 dB | 3654 | 309 of 7258 |
| −100 dBFS | on | | | | 5 of 6906 |

The relative and background floors (and C7) were measured with code that was removed when they
were not taken, so `HaliteMeasure` can no longer reproduce those rows.

A lower absolute floor makes the score level-independent for nearly every event (5 list changes
with −100 dBFS and the relative floor), and costs every cold start: before a voice has been heard
the floor is the only reference, and the room's hiss (VocalSet f4: band −98, body −84 dBFS, `S`
−14 dB, noise-like) and the breath before the first note (m7: the body rising 20–25 dB over the hiss)
read as esses; the release of those events runs into the first note (8 dB). A third floor, 15 dB
over the body's quietest of 3 s (held over 50 ms), took VocalSet streams from 0.093 to 0.028 % at a
−120 dBFS absolute floor, fixed the hiss case, and not the breath (per file 0.816 → 0.435 %). The
−60 dBFS floor shows the other side: quieter input reads *fewer* esses (A + D, VocalSet streams at
−20 / −30 dB gain: 0.002 % / 0 % against 0.019 % at 0 dB), the floor binding more.
**Kept: the −60 dBFS absolute floor alone.** The relative and background floors are not built:
their gain inside the chosen cue is small, and the trade is the user's (*Proposals*).

**The presence band.** The study's 4th-order 1–3 kHz band, with its averager, cost 25 M
instructions per 4 s of stereo in the C++ port (callgrind, *CPU*; the study estimated 21.5 M). A 2nd-order band-pass on the same edges measured the same (0.35 %, /ʃ/
94.6 %) at half the cost. The body filter's own low-pass stage gives a band-pass output for free:
centred on the body's top (2 kHz, or Detect Low / 2), Q 0.707, −3 dB at about 1.0 and 3.9 kHz.
On tune it measured as well or better than both (0.34 %, worst speaker 0.96 %, /z/ 89.2, /ʒ/ 86.4 %,
VocalSet largest 4.5 dB) and costs only its averager. **Kept**, after the first holdout
run, for the CPU gate; holdout was run again (*Gates*).

**Robustness (tune, the final detector):** Live (no look-ahead) 0.42 %, as Normal, /z/ 89.1 %;
Focus 100 % 0.36 %; Focus 0 is a pure relative detector (`S` = `B − V` exactly, a test). Detect
Low 3 kHz: T95 −21.81 dB, 0.68 % (worst speaker 1.74 %; M1 at the same band 4.76 %), /ʃ ʒ tʃ dʒ/
97.3, 93.9, 98.8, 96.4 %; Detect Low 6 kHz: −23.99 dB, 0.32 % (0.92 %; M1 1.38 %), but /ʃ/ 80.1,
/ʒ/ 74.1, /tʃ/ 86.0, /dʒ/ 77.1 % against M1's 89.8, 82.3, 95.3, 89.2 %: a band starting at 6 kHz
leaves /ʃ/'s energy to the presence band, and the shape cue reads it as falling. (Before the
review these read, with the separate 1.3–4 kHz band: Live 0.37 %, Detect Low 6 kHz 0.26 % and /ʃ
ʒ tʃ dʒ/ 76.3, 70.7, 80.2, 72.3 %.)

### What was built

`S` (Voice) = `B − V`, lowered by up to 40 dB × Focus as `N` falls (28 dB at the default 70 %;
M1: 30 dB; at least 10 dB × Focus for 30 ms after a jump, below), and by the **shape cue**: Focus ×
min (40 dB, 6 × max (0, −4 dB − R)), R = B's 4 ms power average over the presence band's (the body
filter's upper band-pass). Neither term raises `S`; for an ess (R well over −4 dB, N near 1, no
jump just before) `S` is `B − V`, so the threshold line, the event list and Learn keep their
meaning: dB of sibilance over the voice, lowered where the band is harmonic or the spectrum falls
toward it. Allround is unchanged (`S` = `B`).

- **Onset:** the 30 ms onset hold (N counts as 0) starts when `V` rises by more than 2 dB over 5 ms
  (M1). A **jump**, the body's averaged level rising by more than 10 dB over 5 ms, starts a 30 ms
  hold of its own (M1b), whether `V` is held or not (59 % of jump hops on tune have `V` not held;
  requiring it moved the decisions by 0.02 points with the first hold). `V` is held through closures and pauses, so its
  own rise misses a vowel coming back. A jump is a vowel's onset and a stop's burst before an ess
  alike, so its hold only asks for more: `S` lowered by at least 10 dB × Focus (*After the
  review*). As first built it counted the band as harmonic, as a rise does.
- **Persistence:** while `B` stays within 6 dB of its lowest over the last 40 ms and ρ read more
  than 0.8 in that time, the cue keeps that reading (the larger of the two): the periodicity of a
  sustained or gliding note dips for a few tens of ms; an ess raises `B`. A monotone queue per
  quantity (`SlidingExtreme`), so the cost does not grow with the window.
- **Unchanged from M1:** `B`, `V`, the holds, the −60 dBFS floor, the rise rule, the periodicity cue
  (lags 1–12.5 ms; `periodicLongestLagMs` lets `tools/measure` sweep it), the events, Harsh.
- **Threshold** −36 … +24 dB (Settled 35), default **−17.6 dB** by the plan's Learn rule on the tune
  streams: max (median − 4, p25) of the largest `S` per /s/ and /z/ token = max (−13.6 − 4, −17.7);
  provisional until M3. Top: the cold-start p99 of /s/ is +18.7 dB (tune per file) and +20.2 dB
  (holdout per file, with the separate presence band and again with the final detector); +24
  clears both. Bottom: the token p1 is −32.2 dB as reviewed and −30.7 dB with the final detector
  (tune streams, /s/ + /z/); the median and p25, and so the default, did not move.

**Not built:** C7's mid-body dip (it moves the threshold scale by 14 dB, costs /ʒ/ 5–7 points and
measures no better than A + D on separability); lags to 25 ms; the relative and background floors;
the voiced-/z/ bonus; flatness changes.

### Gates

The detector as built and reviewed; the final detector's gates are under *After the review*.
VCTK, each speaker's recordings back to back unless *per file*; M1 → M1b. Holdout (1) was the run
before the presence band moved to the body filter, holdout (2) the run after it.

| | Tune | Holdout (1) | **Holdout (2)** | Holdout (2), per file | Gate |
|---|---|---|---|---|---|
| T95 | −22.28 → −22.70 dB | −21.99 → −22.18 dB | −21.99 → **−22.18 dB** | −21.93 → −22.18 dB | |
| vowel decisions at T95 | 2.03 → **0.34 %** | 1.82 → 0.31 % | 1.82 → **0.28 %** | 1.77 → 0.34 % | ≤ 2 %: pass |
| per speaker min / median / max | 0.23 / 1.74 / 7.22 → 0.00 / 0.30 / 0.96 % | 0.11 / 1.13 / 7.34 → 0.05 / 0.20 / 1.08 % | → 0.04 / 0.19 / 0.86 % | 0.12 / 1.08 / 7.33 → 0.02 / 0.29 / 0.94 % | reported |
| /z/ at T95 | 88.8 → 89.2 % | 86.5 → 87.4 % | 86.5 → 87.4 % | 86.4 → 87.4 % | reported |
| /ʃ/, /ʒ/, /tʃ/, /dʒ/ at T95 | 95.2, 87.1, 95.3, 92.8 → 94.6, 86.4, 95.3, 92.8 % | 93.5, 83.6, 94.6, 79.0 → 91.8, 80.8, 91.9, 76.0 % | → **89.4, 76.7, 89.2, 74.0 %** | 93.5, 82.9, 94.6, 79.0 → 88.8, 76.7, 89.2, 72.0 % | reported |
| at the default (−17.5 → −17.6 dB): /s/, /z/ | 80.4, 68.6 → 79.7, 68.2 % | | 75.7, 61.5 → 75.2, 61.7 % | 75.9, 61.5 → 75.2, 62.0 % | reported |
| … /ʃ/, /ʒ/, /tʃ/, /dʒ/ | 75.8, 57.1, 76.7, 68.7 → 75.8, 55.1, 76.7, 62.7 % | | 76.5, 50.0, 78.4, 53.0 → 71.2, 46.6, 64.9, 45.0 % | | reported |
| … vowel frames: decisions, tails | 0.50, 6.10 → 0.13, 4.40 % | | 0.50, 5.29 → 0.11, 3.85 % | 0.49, 5.61 → 0.13, 4.06 % | tails: M3 |
| decisions at T95 by kind | M1: 73 % in a vowel's first 50 ms, 83 % after a consonant (/t/ 29 %); M1b: 96 % in the first 50 ms, 88 % after a consonant (/t/ 59 %, /d/ 16 %) | | M1b: 90 % in the first 50 ms, 89 % after a consonant (/t/ 53 %, /d/ 15 %) | | |

**The separability gate passes on both halves**, with a margin of 6–7× and the worst speaker
under 1.1 %. What remains is the first 50 ms of vowels after /t/ and /d/: the stop's aspiration,
noise in the band and labelled as vowel (M1, *Failure cases*); NOTES M1's 50 ms guard after
voiceless stops would remove most of it, a gate decision for the user. **The /ʃ/ class pays for
it:** in token recall on holdout 4–7 points at T95 against M1 (2–4 more with the body filter's band
than with the separate band of holdout (1)), on tune 0–1 point. Token recall understates it: in
label frames with a decision tune loses 7–8 points too (*After the review*). /ʃ/'s peak
(2.5–3.7 kHz) lies in the presence band, so a weak /ʃ/ reads as a falling spectrum. The gate reports these classes; whether the cost
is acceptable is the user's (*Proposals*). Holdout's /ʃ/ numbers chose nothing: the band was kept
as chosen on tune.

LibriSpeech (16 kHz, Detect band 4.5–7.2 kHz; reported), M1 → M1b at its own T95: tune 0.71 → 0.19 %
(worst speaker 2.61 → 0.60 %), /ʃ/, /tʃ/, /dʒ/ 94.0, 93.6, 82.8 → 88.7, 86.7, 71.6 %; holdout (2)
2.20 → 0.39 % (13.16 → 2.55 %), /ʃ/, /ʒ/, /tʃ/, /dʒ/ 97.1, 84.9, 93.9, 86.5 → 86.9, 64.2, 86.0,
70.2 %. With the band ending under 8 kHz the /ʃ/ class costs more: 5–20 points.

Committed tier (CI): T95 −24.08 dB both; decisions 3.93 → 0.43 %; at the default /s/ 20 → 22 of
31, /z/ 8 of 11, decisions 0.60 → 0.30 %, tails 4.54 → 3.82 %.

**Bright** (the gate: at most 0.5 dB at the default threshold; M1 → M1b):

| | Tune | Holdout (2) | Gate |
|---|---|---|---|
| VocalSet: frames reduced, largest, recordings | 0.118 %, 8.0 dB, 7 of 9 → 0.013 %, 4.5 dB, 2 of 9 | 0.264 %, 8.0 dB, 9 of 9 → 0.052 %, 8.0 dB, 3 of 9 | **fail** |
| VocalSet at T95: frames with a decision, largest S − T95 | 0.234 %, +17.4 → 0.001 %, +3.9 dB | 0.288 %, +18.3 → 0.039 %, +14.7 dB | reported |
| synthetic −3, −6, −12 dB/oct, both sets | pass → pass (largest S − T95 −4.1 dB) | pass → pass | pass |
| synthetic flat (0 dB/oct), at the default | every frame, 4.8 dB → none | every frame, 5.65 dB → 0.01 dB | pass (Open 2) |
| synthetic flat at T95 | 14 of 14, +9.6 → 14 of 14, +3.0 dB, decisions on 23.9 % of its frames | +10.1 → +3.3 dB, 0.36 % of its frames | reported |
| committed sung excerpts | 1.82 → 1.86 dB in the first 0.25 s (a cold start) | | **fail** |

The bright gate still fails on sung material. Holdout's worst recordings: m9's messa di voce at
5.15 s and m6's scale at 0.53 s (a cold start), 8 dB each; tune's: m5's messa di voce at 13.70 s
(4.5 dB). Per file, holdout: 0.068 %, 15 of 149 exercises.

**Harsh** (Harsh on at its defaults; pooled mean in sibilants is the gate column; M1 → M1b):

| | VCTK tune | VCTK holdout (2) | LibriSpeech tune | LibriSpeech holdout (2) |
|---|---|---|---|---|
| pooled mean (held, own) | 0.145 (0.122, 0.022) → 0.181 (0.154, 0.027) dB | 0.143 (0.120, 0.023) → 0.187 (0.159, 0.028) dB | 0.669 (0.623, 0.047) → 0.746 (0.687, 0.058) dB | 0.686 (0.639, 0.047) → 0.761 (0.701, 0.061) dB |
| added over the hop before | 0.052 → 0.076 dB | 0.030 → 0.061 dB | 0.090 → 0.134 dB | 0.090 → 0.135 dB |
| tokens over 0.5 dB: mean, peak, added | 8.2, 12.3, 2.2 → 9.6, 14.2, 3.0 % | 7.7, 11.8, 1.3 → 9.3, 14.2, 2.5 % | 29.9, 37.0, 4.1 → 32.4, 39.6, 5.8 % | 28.6, 34.5, 4.1 → 31.3, 37.4, 6.3 % |
| worst speaker; speakers over 0.5 dB | p334 0.639 → 0.715 dB; 1 → 1 of 20 | p317 0.550 → 0.562 dB; 1 → 2 of 20 | 5142 2.165 → 2.204 dB; 8 → 9 of 20 | 6930 3.609 → 3.665 dB; 9 → 10 of 20 |
| gate (pooled ≤ 0.5 dB) | pass | pass | reported | reported |

Harsh rose because the Ess band fires less: each false Ess trigger at a vowel onset after a stop
froze Harsh at its earlier target; without them Harsh cuts those vowels as designed (mean in
vowels 0.30 → 0.34 dB on tune) and carries the cut into the next ess. Measured on tune, with the
study's presence band: the jump onset alone moves the pooled mean 0.15 → 0.17 dB; the noise range,
the shape cue and persistence leave it within 0.01 dB. LibriSpeech's number stays what the study found it to be: the cut on the
vowel before the ess, carried through it (added 0.13 dB, half of its speakers over 0.5 dB
pooled). Which reading the Harsh gate uses stays the user's (Open 2).

**Level independence** (gains 0, −10, −20, −30 dB): Settled 34's gate, the working range of the
−30 dB render, holds **no event** of these corpora, so it is **not measured**: VCTK's body sits
6 dB over the −60 dBFS floor at −30 dB (M1). (The tool and the corpus tests printed and checked
"ok" over that empty set until the review; they now say *not measured*.) What can be measured is
each quieter render in its own range: no list change and no change over 0.5 dB at −10 and −20 dB,
on any corpus. That range covers (VCTK holdout (2); tune in brackets): −10 dB 48.6 % (49.3 %) of
events, −20 dB 2.7 % (1.4 %); LibriSpeech holdout 68.9, 3.9 %; committed 74.4, 7.7 %. Over
every event: 768 of 6986 comparisons change (tune; M1 1121 of 8859), 535 of 5906 on holdout (M1
922 of 7580), largest 7.5 dB.

### CPU

The detector alone, 48 kHz stereo, Linked, periodicity, 512-sample blocks.

| | Harsh off | Harsh on | Conditions |
|---|---|---|---|
| callgrind, 4 s of the perf signal: M1's constants → M1b | 146.0 → **156.9 M** instructions | 278.9 → **290.5 M** | -O3, a driver outside the repository, `Detector::process` only (M1's own code read 144.4 / 277.3 M) |
| … with the study's 4th-order presence band, persistence by scanning | 180.9 M | 314.2 M | before the two optimisations below |
| timed, `[.perf]`, fastest of 5 passes over 20 s, `taskset -c 2` | **0.203 %** | **0.362 %** | load average 2.0 (1 min), 2.3 (5 min) |
| timed back to back, M1's constants → M1b's, the same code (a driver outside the repository) | 0.190 → 0.201 % | 0.343 → 0.357 % | load average 1.6–2.0 |
| M1 (Measured (M1), for reference) | 0.164 % | 0.315 % | load average 7–12, another day |

Unlinked 0.204 / 0.364 %, Mid 0.110 %; 96 kHz Linked 0.353 / 0.624 %. M1b adds 7 % of the
detector's instructions (4 % with Harsh): the jump rule's level per hop, persistence's two queues,
the shape cue's averager and ratio. Against PLAN's budget (Split or Wide ≤ 0.5 % with the detector
included, Harsh ≤ +0.3 %) the detector takes 0.203 % of Split's 0.5 %, leaving 0.297 % for the
reduction, and Harsh adds 0.159 % of its 0.3 %. M1b had held itself to a ~0.35 % line for detector
and Harsh together that PLAN never set; that line is withdrawn. Timed runs on this machine vary by
±0.02 % between minutes. Two changes took the cost from 314.2 to 290.5 M: the body filter's
band-pass in place of the presence band's own filters (*The presence band*) and the monotone
queues in place of scanning 40 ms per hop. The jump's hold after the review: *After the review*.

### Tests

`HaliteTests_detector` adds: the shape cue (rising and falling noise spectra, a flat and a −12 dB
per octave harmonic spectrum, an ess over a vowel: the term is zero for a rising spectrum, never
raises `S`, never exceeds its bound, and `S` is composed exactly as above), the shape cue's two
levels sharing `B`'s averaging (broadband noise stepped 30 dB every 30 ms: `presenceDb` ripples
0.68 dB; with `B`'s ballistic level 4.1 dB, with a 1 ms presence average 7.3 dB), Focus 0 as a pure
relative detector, a vowel after a 60 ms closure (V held, no rise, the jump starts its hold; off
without the jump rule), the jump's span (the body back 30 dB up over 4 ms jumps, over 30 ms does
not), an ess after a stop's burst (*After the review*), the onset holds after a rise or a jump, a
voiced /z/ (frication amplitude-modulated at f0 over a vowel: caught, persistence leaves it alone,
the shape cue sees it rise), gliding notes a fifth apart with 30 ms glides (persistence calls 242
hops more harmonic, never fewer; the largest N falls from 1 to 0), the sliding queue against a
brute-force window, the periodicity cue's longest lag (55 Hz reads ρ 0.32 with 12.5 ms and 1.00
with 25 ms; the exact recompute after a loud passage holds for both), `presenceDb` in the
level-independence and block-size checks, a reset before `prepare()` and a reset in the middle of
a hold against a fresh detector. The corpus cases: separability on the committed tier (the gate,
and a regression bound of 66 decisions of 11400 frames, measured 58), the default's regression
bounds in frames (34 decisions and 470 reduced of 11400, bounds 40 and 520), level independence per
gain (−10 dB carries it; the −30 dB reading is checked only when its range holds an event), bright on
the sung excerpts after 300 ms and on every synthetic tilt (the flat spectrum included), the bright
gate on the sung excerpts' cold start under `[!mayfail]`, and `[m1]`: the plan's gates on both
halves, regression bounds on `tune` only.

**Mutations.** As built, M1b claimed that 11 of 12 single-line mutations of the new code were
caught. The review compiled 8 more and found 4 survivors: `presenceDb` from `B`'s ballistic level,
the jump's span of one hop, a 1 ms presence average (each took the committed tier from 0.43 to
0.67–0.91 % decisions, under its 1 % bound), and persistence comparing with the lowest `B` of the
previous 40 ms only. After the review, 26 single-line mutations (the review's 16, 7 of the jump's
hold and the sliding queue, 3 more constants), each compiled into a copy of the tests outside the
repository and run on `[detector],[committed]`: **24 are caught; 2 are equivalent.** Persistence
against the previous 40 ms gives `B − min (B, lowest) < 6 dB` exactly when `B − lowest < 6 dB`.
`reset()` leaving the jump's hold set is cleared by the next `process()`, which sees a new route
after every reset. The three survivors are now caught by the new synthetic checks and by the
committed tier's bound in frames.

### After the review

Three independent reviews of M1b reported 17 findings. Most are wording, bounds or reporting,
resolved in the docs, `HaliteMeasure` and the tests above. One changed the detector: **the jump's
hold cost the start of affricates and of esses after stops.** Everything here is `tune` unless
marked, chosen there; holdout (3) ran once, after.

**What the jump cost.** M1b's jump counted the band as harmonic for 30 ms, which takes 28 dB off `S`
at Focus 70 %. It fires on a stop's burst as readily as on a vowel coming back: of the jump holds
that change a decision at T95, 417 remove vowel decisions (3301 frames; the jump in a /t/, /d/ or
/ð/ label before a vowel, or in /t/ after /s/) and 361 remove sibilant decisions (6497 frames; in
/k/, /t/ or the sibilant itself before /s/, /z/, /tʃ/). An overlap of any event still counts the
token, so recall moved by one token. Inside the labels, at one threshold (−22.70 dB, M1b's T95):

| Label frames decided (reduced), first decision median / p90 | M1 | M1b, no jump | **M1b as reviewed** | M1b final |
|---|---|---|---|---|
| /s/ | 66.9 (76.6) %, 21.2 / 40.5 ms | 65.9 (76.3) %, 21.5 / 40.7 | 64.1 (75.0) %, 23.0 / 41.8 | 64.9 (75.5) %, 22.3 / 41.1 |
| /z/ | 50.8 (66.5) % | 49.4 (65.9) % | 48.4 (65.2) % | 48.7 (65.5) % |
| /ʃ/ | 61.4 (71.5) % | 54.1 (68.0) % | 53.3 (67.1) % | 53.8 (67.9) % |
| /ʒ/ | 40.9 (56.8) % | 33.8 (53.7) % | 33.8 (53.2) % | 33.8 (53.3) % |
| /tʃ/ | 43.1 (56.8) %, 57.0 / 100.0 ms | 39.0 (54.5) %, 57.0 / 99.4 | 33.2 (48.1) %, 60.9 / 107.8 | 36.3 (50.8) %, 59.1 / 105.7 |
| /dʒ/ | 37.2 (50.7) %, 55.8 / 77.2 ms | 31.3 (47.9) %, 56.4 / 77.3 | 27.4 (44.2) %, 57.7 / 97.6 | 29.9 (46.6) %, 56.7 / 81.4 |
| vowel decisions | 2.28 % | 0.69 % | 0.34 % | 0.42 % |

Token by token against M1b without the jump (each token's first decision in its label, tokens
decided in both): as reviewed, /tʃ/ 32 of 82 later by more than 5 ms and 17 by more than 20 ms,
/dʒ/ 14 and 9 of 74, /s/ 98 and 18 of 1042, /z/ 29 and 12 of 805, none earlier; final, /tʃ/ 24
and 3, /dʒ/ 7 and 1 of 75, /s/ 71 and 4, /z/ 22 and 5, none earlier (a script outside the
repository on `--dump` output). The /ʃ/ class's loss against M1 is mostly the shape cue's: without
it /ʃ/ and /ʒ/ read 59.0 and 39.6 % of frames (and the decisions 1.20 %); the jump moves them by
under a point.

**No rule at the jump separates the two.** A burst before a vowel and a burst before an ess look
alike at the jump: the body's rise (median 12.8 against 11.9 dB), the band's rise, their difference,
`B − V`, the presence ratio and the body under `V` all overlap. Each remedy trades the same way,
measured at −22.70 dB (vowel decisions; /tʃ/, /dʒ/ frames decided):

| Remedy | Decisions | /tʃ/, /dʒ/ |
|---|---|---|
| none (as reviewed) | 0.34 % | 33.2, 27.4 % |
| the jump only while `V` is held (as PLAN worded it) | 0.36 % | 33.6, 27.5 % |
| no jump while `presenceDb` is over the knee | 0.55 % | 35.7, 29.5 % |
| no jump unless the body rose 0 / 10 / 20 dB more than the band (at each own T95) | 0.45 / 0.56 / 0.63 % | 36.2, 29.5 / 38.1, 30.8 / 38.1, 30.4 % |
| a shorter hold after a jump: 10 / 15 / 20 ms | 0.56 / 0.48 / 0.42 % | 38.1, 30.5 / 37.1, 29.8 / 36.1, 29.0 % |
| **`S` lowered by at least J × Focus in the hold, J = 6 / 10 / 14 dB** | 0.51 / **0.42** / 0.38 % | 37.2, 30.9 / **36.3, 29.9** / 35.3, 28.8 % |
| no jump | 0.69 % | 39.0, 31.3 % |

Every line lies near the straight trade between no jump and the reviewed one; the floor in `S`
does a little better than that line (at 0.42 % it keeps 0.2 points more of /tʃ/ and 0.9 more of
/dʒ/ than the 20 ms hold). It is also the rule that fits what the hold is for: after a burst, ask an ess for 7 dB
more evidence, but let a clear one through at once. **Kept: J = 10 dB** (`jumpHoldDb`), the
smallest that keeps VocalSet where the reviewed jump left it (largest 4.5 dB, 2 of 9 recordings
reduced; with 6 and 8 dB a third one is) with decisions within 0.1 point of it. The synthetic affricate test pins
it: a frication 11 dB over the threshold after a burst is decided as early as without any hold
(4.7–6.9 ms after it starts at 44.1–96 kHz), one 5 dB over waits for the hold's end (26.5 ms), and
with N = 0 in the hold the clear one waits too (26.5 ms, the review's 26.0–26.8).

Whether the de-esser should keep any jump is the user's: without it decisions rise from 0.42 to
0.65 % at its own T95 (a third of the gate) and VocalSet reads 5.8 dB on 4 of 9 recordings; the
vowel decisions it removes are mostly a stop's aspiration labelled as vowel (*Proposals*).

**The final detector, one constant at a time** (tune streams, VCTK at each own T95; frames: /ʃ/, /ʒ/,
/tʃ/, /dʒ/ label frames decided; VocalSet at the default):

| Constant | Values: T95, decisions (worst speaker); frames; what else moved | Chosen |
|---|---|---|
| jump hold J | 0 dB: −22.32, 0.65 % (1.25); 52.9, 32.7, 38.1, 30.5; VocalSet 5.8 dB (4). 4: 0.55 %. 6: 0.51 %. 8: 0.46 %, VocalSet 4.5 dB (3). **10: −22.70, 0.42 % (1.07); 53.8, 33.8, 36.3, 29.9; 4.5 dB (2).** 14: 0.38 %. 20: 0.35 %. 40 (as reviewed): 0.34 % (0.96); 53.3, 33.8, 33.2, 27.4 | 10 dB |
| shape knee × slope | −2: slope 3 0.38 %, 6 0.33 %, 9 0.32 %, /ʃ/ tokens 91.4, 88.2, 84.9 %. **−4**: 0.45, **0.42**, 0.41 %; /ʃ/ 94.6 % at every slope; frames 55.8/35.2, **53.8/33.8**, 52.3/33.1. −6: T95 −22.35, 0.51, 0.47, 0.46 %; /ʃ/ 94.6 %; frames 57.2/37.4, 56.8/37.0, 56.5/36.7; VocalSet 5.4 dB (4) | −4 dB, 6 dB per dB |
| noise range | 30 dB: 0.43 %, flat synthetic +10.0 dB over T95; **40: 0.42 %, +3.0 dB**; 50: 0.42 %, flat −4.0 dB (passes), /dʒ/ −1 token | 40 dB |
| persistence | off: 0.44 % (1.14), VocalSet 3 recordings; **40 ms: 0.42 %**; 60 ms: 0.43 % | 40 ms |
| longest lag | **12.5 ms: 0.42 %**; 25 ms: 0.40 %, frames 1.6–2.4 points lower in every class | 12.5 ms |
| onset hold | 20 ms: −22.35, 0.46 %; **30: 0.42 %**; 40: −22.75, 0.39 %, /tʃ/ −2 tokens | 30 ms |

With the final band, tune's /ʃ/ class does not answer the shape cue in token recall (94.6 % at
knee −4 and −6, every slope), which is why tune could not see the 4–7 points holdout loses. Frames
answer: knee −6 gives back 3.0 (/ʃ/) and 3.2 (/ʒ/) points of label frames for 0.05 points of
decisions, a T95 0.35 dB higher and VocalSet 5.4 dB on 4 recordings. Not taken; the trade is in
*Proposals* for the user. (*M1b*'s earlier claim that a −6 dB knee gives back 0.5–1.4 points of
/ʃ/ class recall came from the study band.)

**Gates, final detector** (VCTK streams unless marked; as reviewed in brackets):

| | Tune | **Holdout (3)** | Holdout (3), per file | Gate |
|---|---|---|---|---|
| T95 | −22.70 dB (−22.70) | **−22.15 dB** (−22.18) | −22.13 dB | |
| vowel decisions at T95 | **0.42 %** (0.34) | **0.33 %** (0.28) | 0.38 % | ≤ 2 %: **pass** |
| … read literally, `S` over T95 | 0.44 % (0.36) | 0.34 % (0.29) | 0.40 % | |
| per speaker min / median / max | 0.00 / 0.43 / 1.07 % (0.00 / 0.30 / 0.96) | 0.04 / 0.19 / 1.13 % (0.04 / 0.19 / 0.86) | 0.02 / 0.30 / 1.17 % | reported |
| /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/ at T95 | 89.2, 94.6, 86.4, 95.3, 92.8 % (same) | 87.2, 89.4, 76.7, 89.2, 76.0 % (87.4, 89.4, 76.7, 89.2, 74.0) | 87.3, 88.8, 76.7, 89.2, 73.0 % | reported |
| label frames decided at T95: /s/, /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/ | 64.9, 48.7, 53.8, 33.8, 36.3, 29.9 % (64.1, 48.4, 53.3, 33.8, 33.2, 27.4) | 62.2, 45.2, 52.1, 29.2, 30.2, 21.7 % (61.8, 44.9, 51.7, 29.3, 27.8, 19.8) | | reported |
| at the default (−17.6 dB): /s/, /z/ | 79.7, 68.2 % (same) | 75.4, 61.7 % (75.2, 61.7) | 75.4, 62.0 % | reported |
| … /ʃ/, /ʒ/, /tʃ/, /dʒ/ | 75.8, 55.1, 76.7, 62.7 % (same) | 71.2, 46.6, 64.9, 45.0 % (same) | 70.6, 45.9, 64.9, 42.0 % | reported |
| … vowel frames: decisions, tails | 0.15, 4.55 % (0.13, 4.40) | 0.12, 3.95 % (0.11, 3.85) | 0.14, 4.19 % | tails: M3 |
| decisions at T95 by kind | 95 % in a vowel's first 50 ms, 89 % after a consonant (/t/ 55 %, /d/ 20 %) | 91 % in the first 50 ms, 89 % after a consonant (/t/ 50 %, /d/ 17 %) | | |
| bright: VocalSet at the default | 0.013 %, 4.5 dB, 2 of 9 (same) | 0.052 %, 8.0 dB, 3 of 9 (same) | | **fail** |
| bright: synthetic, every tilt but flat, at the default | pass | pass | | pass |
| … flat spectrum at the default; at T95 | none; +3.0 dB, 23.9 % of its frames (same) | 0.01 dB; +3.3 dB, 0.33 % (0.36) | | reported (Open 2) |
| Harsh pooled (held, own) | 0.170 (0.144, 0.025) dB (0.181) | 0.181 (0.154, 0.027) dB (0.187) | | ≤ 0.5 dB: pass |
| … added; tokens over 0.5 dB: mean, peak, added; speakers over | 0.070 dB; 9.3, 13.9, 2.8 %; 1 of 20 | 0.056 dB; 9.1, 13.8, 2.3 %; 1 of 20 (2) | | reported |
| level: −30 dB working range (the gate) | no event: **not measured** | no event: **not measured** | | (Open 2) |
| … each gain in its own range: −10, −20 dB | 48.7, 1.3 % of events; no change | 47.7, 2.6 %; no change | | reported |
| … every event: list changes | 877 of 7263, 7.47 dB | 621 of 6103, 7.48 dB | | reported |

LibriSpeech (16 kHz, reported), at its own T95, final (as reviewed): tune −16.63 dB, 0.21 % (0.19),
worst speaker 0.71 % (0.60); holdout (3) −18.57 dB, **0.47 %** (0.39), worst speaker **3.21 %** (2.55);
/ʃ ʒ tʃ dʒ/ 86.9, 64.2, 86.1, 71.2 % (86.9, 64.2, 86.0, 70.2). The fix costs LibriSpeech's
holdout 0.08 points of decisions and its worst speaker 0.66, more than VCTK; it chose nothing.
Harsh pooled 0.731 / 0.748 dB (0.746 / 0.761). Committed tier: T95 −24.08 dB, 58 of 11400 vowel
frames (0.51 %; as reviewed 49, 0.43 %); at the default /s/ 22 of 31, /z/ 8 of 11, decisions 34
(0.30 %), tails 3.82 %; Harsh 0.206 dB; level 29, 3 and 0 of 39 events in range at −10, −20, −30 dB.

**CPU.** Callgrind (the M1b driver outside the repository, 4 s of the perf signal): 156.9 → 157.1 M
instructions per 4 s with Harsh off, 290.5 → 290.7 M with it. Timed (`[.perf]`, `taskset -c 2`,
load average 6–8): 0.201 % and 0.362 % at 48 kHz Linked, Unlinked 0.203 / 0.365 %, Mid 0.109 %,
96 kHz 0.350 / 0.622 %: the detector takes 0.201 % of Split's 0.5 %, Harsh adds 0.161 % of its
0.3 %.

### Proposals (for the user)

1. **The floor and the working range.** Settled 34's range at −30 dB covers no event with the
   −60 dBFS floor, so that clause of the level gate is untested. Lowering the floor makes the score
   level-independent for nearly every event (5 changes with −100 dBFS and a floor 30 dB under the
   voice's loudest), and lets hiss and breaths before the first note read as esses (VocalSet per
   file 0.03 → 0.7–0.8 %). A rule that no ess exists until a voice has been heard (a harmonic body),
   with the relative floor after it, would remove that trade; it is not built or measured.
2. **A 50 ms guard after /t/ and /d/** in the vowel-frame count (M1's proposal): 95 % of the
   remaining decisions lie in a vowel's first 50 ms, 75 % after /t/ or /d/ (holdout 91 and 67 %).
3. **The /ʃ/ class** loses 4–7 points of token recall at T95 on holdout VCTK and more on 16 kHz
   material, and 7–8 points of label frames on tune (M1 → final at −22.70 dB: /ʃ/ 61.4 → 53.8 %, /ʒ/
   40.9 → 33.8 %). With the final band a −6 dB knee gives back about 3 points of /ʃ/ and /ʒ/ frames
   on tune for 0.05 points of decisions, and VocalSet reads 5.4 dB (4 recordings, against 4.5 dB
   and 2); a presence band that stops under the /ʃ/ peak was not measured.
4. **The flat synthetic spectrum** passes at the default; at T95 it is 3.0–3.3 dB over, with
   decisions on 23.9 % of the tune set's flat frames (holdout set 0.33 %). A 50 dB noise range passes
   it there too, for one /dʒ/ token on tune.
5. **The jump** (new after the review): keep the floor of 10 dB × Focus in its hold (0.42 % of
   vowel frames at T95), the reviewed N = 0 (0.34 %, the start of 17 of 82 /tʃ/ more than 20 ms
   late), or no jump (0.65 %, VocalSet 5.8 dB). Most of what it removes is a stop's aspiration
   labelled as vowel, which proposal 2 would take out of the count.

### Not verified

- Sung sibilants (no labelled source).
- 44.1 and 96 kHz speech (VCTK is 48 kHz; VocalSet is 44.1 kHz singing); the 96 kHz cost is
  timed only.
- The studies' cross-validation was repeated in C++ only as the per-speaker spread, not by folds.
- The audible onset in Live, and whether the jump's hold, which lets a marginal ess after a burst
  wait up to 25 ms, is audible (measured only as frames and decision times).
- The cause of LibriSpeech's high `H − body` (recording chain or voices), as in the study.
- Whether the background floor's hiss fix survives other recording chains: measured on VocalSet only.

## M3: Speed

2026-11-14, M3 voicing part 1: Speed Auto for both bands, the fixed releases and the look-ahead,
chosen on the `tune` split by measurement (nobody listened). `holdout` was read once at the end of
this part, at the −17.6 dB default then in force, and chose nothing; part 2 moved the defaults after
it and read holdout again (*M3: Learn*, the milestone's final reading), so this part's holdout
numbers are superseded. Every number was printed by `HaliteMeasure` (VCTK streams, defaults,
Harsh on) or by the `[.report]` cases named beside it. `HaliteMeasure` now prints the release-tail gate
(*Over 1 dB*), and inside each sibilant label the share of each token's frames reduced, the first
reduced frame and whether the label's first frame is already reduced (`tests/support/Gates`).

### What changed

- **Ess Speed Auto:** the release is a quarter of the time over threshold, clamped to **15 … 30 ms**
  (M1: half of it, 15 … 80 ms). `DetectorTuning::essAutoShare`, `essAutoMinMs`, `essAutoMaxMs`.
- **Ess fixed release:** default **20 ms** (provisional 40 ms).
- **Harsh Speed Auto:** unchanged (half the time over, 30 … 150 ms), its constants now in
  `DetectorTuning` beside the Ess ones so `HaliteMeasure` can sweep them; fixed default 80 ms kept.
- **Look-ahead:** 5 ms kept.

### The release-tail gate (Settled 47)

Vowel frames more than 30 ms from any sibilant label that carry more than 1 dB of Ess reduction,
decisions and tails together; the gate is 2 % at the defaults on holdout. Tune; *label frames
reduced* is the share of /s/ label frames reduced by more than 0.5 dB at the default (what "cutting
an ess short" would lower):

| Ess release | Over 1 dB | /s/ label frames reduced |
|---|---|---|
| Auto, half, 15 … 80 ms (M1b) | **2.86 %** | 55.3 % |
| Auto, half, 15 … 60 / 40 / 30 ms | 2.79 / 2.32 / 1.80 % | 55.1 / 54.9 / 54.7 % |
| Auto, 0.35, 15 … 80 / 40 / 30 ms | 1.91 / 1.81 / 1.59 % | 54.6 / 54.4 / 54.3 % |
| **Auto, a quarter, 15 … 30 ms (chosen)** | **1.33 %** | 54.0 % |
| Auto, a quarter, 15 … 80 / 40 ms | 1.39 / 1.38 % | 54.1 / 54.0 % |
| Auto, 0.15, 15 … 30 … 80 ms (the floor binds) | 1.02 % | 53.6 % |
| Auto, 0.15 / 0.2, floor 10 ms | 0.68 / 0.88 % | 52.5 / 52.9 % |
| Auto, 0.15 / 0.2, floor 20 ms | 1.56 / 1.64 % | 54.6 / 54.7 % |
| fixed 5 / 10 / 15 / **20** / 30 / 40 ms | 0.29 / 0.55 / 0.96 / **1.54** / 3.14 / 5.07 % | 50.0 / 52.0 / 53.5 / 54.6 / 56.1 / 57.1 % |
| fixed 60 / 80 ms | 9.08 / 13.00 % | 58.8 / 60.3 % |

The decisions (0.15 %) do not move with Speed; everything else in the column is release tails. The
largest share alone does little: with the 80 ms limit a quarter already reads 1.39 %, because most
esses are over threshold for under 120 ms. What the limit decides is the landing (below).

**The esses' own frames.** The frames the detector decides inside each label do not change (44.3,
30.8, 36.0, 16.2, 22.5, 16.2 % for /s/, /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/, tune, default). Reduced by more than
0.5 dB, before → after: /s/ 55.3 → 54.0 %, /z/ 44.8 → 43.1, /ʃ/ 48.2 → 47.5, /ʒ/ 27.5 → 22.4, /tʃ/
33.8 → 33.2, /dʒ/ 29.2 → 26.6 %; per token, the median /s/ keeps 73 % of its frames reduced (p10 34 →
32 %); mean reduction over /s/ frames 2.77 → 2.69 dB. What goes is the old release's reduction on
label frames the detector did not decide (its own target under 0.5 dB, so `S` at most 0.23 dB over
the threshold): in dips inside a label and after its last decision. /ʒ/, the most broken-up, loses
most. Whether a reduction that holds through those
frames sounds smoother is for the listening pass (PLAN.md, Open).

### The landing (accuracy)

Wide and Split landed 0.51 … 0.57 dB under the threshold at 3 … 18 dB over (M2, M2b): the shaped
reduction follows the peaks of `S`'s ripple, since the attack is instant and the release slower, so
its mean runs over the mean target. A longer release holds the peaks longer.
`"Accuracy: the landing against Speed, report"` (Range 8; dB from where the gate wants it; the std of
the shaped reduction over the steady ess, the gain modulation a faster release costs):

| Release | Wide, 3 / 6 dB over | Split, 3 / 6 dB over | reduction std |
|---|---|---|---|
| Auto, M1b (the test's 0.4 s ess reaches 80 ms) | −0.56 / −0.56 | −0.53 / −0.51 | 0.28 dB |
| fixed 5 / 10 / 15 ms | −0.11 / −0.23 / −0.30 | −0.06 / −0.17 / −0.24 (6 over) | 0.43 / 0.39 / 0.38 dB |
| fixed 20 / 25 / 30 ms | −0.36 / −0.40 / −0.43 | −0.30 / −0.35 / −0.37 (6 over) | 0.36 / 0.34 / 0.33 dB |
| fixed 40 / 60 / 80 ms | −0.49 / −0.56 / −0.60 | −0.43 / −0.50 / −0.54 (6 over) | 0.31 / 0.28 / 0.27 dB |

A long ess must release in about 30 ms or less for Wide to land inside ±0.5 dB with a margin, hence
the 30 ms limit. With it every Wide row lands at −0.42 … −0.43 dB (and 0.00 where Range binds), at
44.1 … 192 kHz −0.34 … −0.43 dB, and Split's landing on `B` (reported, Settled 41) −0.39 … +0.08 dB at
48 kHz, −0.18 … −0.37 dB across rates. **The accuracy gate now passes for every shape and every
row**; the two `[!mayfail]` cases that held Wide's and Split's misses are gone, and Wide's landing is
gated at every rate. On real material (`HaliteMeasure --reduction`, tune, reported; esses are not
steady, so the landing spreads either way): Wide lands at a median −0.25 dB, 75.8 % of VCTK's 2451
events within ±0.5 dB (M2 −0.26, 72.5 % of 2409); Split's landing +0.02 dB, 67.0 % (M2b +0.01, 65.5 %);
Spectral's own band 96.4 % within (96.3 %); Listen Removed's null test passes (≤ 5.89·10⁻⁸).

### Gates at the final constants

VCTK streams; tune → **holdout** (this part's reading at −17.6 dB, superseded by *M3: Learn*'s);
M1b in brackets:

| | Tune | Holdout | Gate |
|---|---|---|---|
| vowel frames over 1 dB at the default (Settled 47) | **1.33 %** (2.86) | **1.02 %** (M1b not read on holdout) | ≤ 2 %: **pass** |
| … reduced over 0.5 dB, decisions, tails | 2.30, 0.15, 2.15 % (4.70, 0.15, 4.55) | 1.80, 0.12, 1.68 % (M1b 4.07, 0.12, 3.95) | reported |
| … in vowels right after an ess | 10.6 % (22.7) | 9.7 % | reported |
| separability: T95, decisions (worst speaker) | −22.81 dB, 0.43 % (1.08) (−22.70, 0.42, 1.07) | −22.46 dB, **0.35 %** (1.20) (−22.15, 0.33, 1.13) | ≤ 2 %: **pass** |
| /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/ at T95 | 89.7, 94.6, 86.4, 95.3, 91.6 % | 87.9, 89.4, 74.7, 90.5, 73.0 % (87.2, 89.4, 76.7, 89.2, 76.0) | reported |
| /s/, /z/ detected at the default | 79.0, 67.8 % (79.7, 68.2) | 74.8, 61.4 % (75.4, 61.7) | reported |
| bright, VocalSet at the default | 0.006 %, 3.8 dB, 2 of 9 (0.013 %, 4.5 dB) | 0.038 %, 8.0 dB, 3 of 9 (0.052 %, 8.0 dB) | **fail** (as M1b) |
| Harsh pooled in sibilants | 0.170 dB (unchanged) | 0.181 dB (unchanged) | ≤ 0.5 dB: pass |

The detector's decisions do not change with Speed, so what moves recall (and with it T95) is how long
events stay open: a token counts as detected when any event overlaps it, and the old release kept
events open further into neighbouring tokens (the reasoning, not counted token by token). The
committed tier (CI): vowel frames over 1 dB 2.58 → 1.14 % (130 of 11400; regression bound 150),
reduced 4.12 → 2.32 %, /s/ 22 and /z/ 8 detected as before, T95 and its decisions unchanged
(−24.08 dB, 58 frames).

### The attack side: is the onset caught?

The detector decides an ess late, not the reduction: once a hop is decided, the look-ahead has the
full reduction in place. Inside the labels (tune, default) the first decision comes a median 28.4 ms
after an /s/ label's start (p90 60.3 ms), 38.0 ms for /z/, 43.0 for /ʃ/, 65.8 for /tʃ/ (whose label
holds its closure); 73 % of decided /s/ tokens are decided more than 20 ms in, and only 8.3 % have the
label's first frame reduced. At T95 /s/ is decided at a median 22.2 ms. Part of it is the aligner
(its boundaries differ by about 10 ms) and part the cues: the periodicity cue's 21 ms window and
persistence hold a harmonic reading until the frication dominates the band.

`"Accuracy: the ess's onset against the look-ahead, report"`: the test ess (2 ms fade-in, 6 dB over,
Wide, so the gain is the reduction): how much less it loses than in its steady part, in 1 ms windows
from its onset, and what the vowel loses in the 10 ms before it:

| Look-ahead | first 1 ms | 4–5 ms | 5–10 ms | mean over 5 ms | mean over 10 ms | vowel before: mean, largest |
|---|---|---|---|---|---|---|
| Live | 6.41 | 5.76 | 3.13 | 6.26 | 4.70 | 0, 0 dB |
| 0 | 6.41 | 5.31 | 2.58 | 6.11 | 4.34 | 0, 0 dB |
| 2.5 ms | 6.36 | 4.71 | 2.28 | 5.78 | 4.03 | 0, 0 dB |
| **5 ms** | 6.02 | 3.91 | 2.14 | 5.08 | 3.61 | 0.02, 0.15 dB |
| 7.5 ms | 5.38 | 3.52 | 2.05 | 4.53 | 3.29 | 0.09, 0.46 dB |
| 10 ms | 4.73 | 3.14 | 1.95 | 3.99 | 2.97 | 0.33, 1.18 dB |

On this ess `S` takes about 10 ms to reach its steady value, more than any look-ahead, so its first
milliseconds pass nearly whole. On real material (tune, default, Speed Auto as chosen), look-ahead 0 /
2.5 / 5 / 7.5 / 10 ms: /s/ label frames reduced 52.1 / 52.9 / 54.0 / 54.8 / 56.0 %, the first reduced
frame a median 28.4 / 27.0 / 25.4 / 24.3 / 22.7 ms in, the label's first frame reduced in 6.6 / 7.0 /
8.3 / 8.9 / 10.1 % of tokens; vowel frames over 1 dB 1.31 … 1.34 %; detection and decisions do not
move (the look-ahead acts after them). Live reads as 0.

**Kept: 5 ms.** Each step of look-ahead takes about a sixth of the onset's shortfall and costs the
vowel before the ess more (0.15 dB at its last millisecond at 5 ms, 1.18 dB at 10 ms; Wide ducks the
whole signal, Split the band). It costs no latency (the frame holds 10 ms), and 5 ms is about the
longest Spectral uses in full: a frame reads its centre's reduction, which the detector has made
only with a look-ahead up to N/2 − 32 samples (4.7 ms at 48 kHz, `../shapes/spectral/NOTES.md`,
*Timing*); beyond that Spectral's cut no longer moves earlier. The onset itself is the cues' (a
detector change, not a voicing constant) and the trade above is for the listening pass (PLAN.md, Open).

### Harsh Speed

Swept on tune (VCTK and VocalSet streams, Harsh on at its defaults). The gate is Settled 46's pooled
mean during sibilants on VCTK; pumping, reported, is read on sustained vowels (vowel labels of at
least 150 ms in which Harsh reduces by more than 0.5 dB): how often the Harsh reduction dips by more
than 1 dB and comes back, per second of those vowels (the depth of the swing, p95 − p5 per vowel,
sits at 2.0 dB on VCTK and 3.3 dB on VocalSet in every row, its p90 at the 4 dB Range: the vowels'
onsets and ends are in it):

| Harsh release | VCTK pooled in sibilants (tokens over 0.5 dB) | VCTK mean in vowels | dips per second: VCTK, VocalSet |
|---|---|---|---|
| Auto, a quarter, 15 / 30 / 60 … 150 ms | 0.13 (5.8 %) / 0.14 / 0.19 dB | 0.27 / 0.30 / 0.36 dB | 0.25, 0.68 / 0.20, 0.67 / 0.09, 0.62 |
| **Auto, half, 30 … 150 ms (the plan's)** | **0.17 dB (9.3 %)** | 0.33 dB | **0.19, 0.60** |
| Auto, half, 15 / 60 … 150 ms | 0.16 / 0.20 dB | 0.31 / 0.37 dB | 0.21, 0.62 / 0.09, 0.58 |
| Auto, all of it, 15 / 30 / 60 … 150 ms | 0.23 / 0.23 / 0.25 dB | 0.38 / 0.39 / 0.41 dB | 0.15, 0.57 / 0.08, 0.56 / 0.07, 0.54 |
| fixed 20 / 40 / **80** ms | 0.12 / 0.15 / **0.22** dB | 0.27 / 0.32 / 0.40 dB | 0.22, 1.27 / 0.18, 1.04 / **0.07, 0.77** |
| fixed 150 / 300 ms | 0.35 / **0.57** dB | 0.51 / 0.71 dB | 0.03, 0.51 / 0.00, 0.30 |

**Kept: the plan's Auto** (half the time over, 30 … 150 ms): it passes the stacking gate with room
(0.17 dB) and dips less on sung vowels than any fixed release that stacks as little (fixed 80 ms:
0.22 dB, 0.77 dips per second). More of the time over (all of it) dips 7 % less on VocalSet and stacks
35 % more; less of it the reverse. **Fixed default kept at 80 ms:** the stacking gate passes (0.22 dB)
and its dips lie between 40 ms and 150 ms, which stacks 0.35 dB. Holdout (part 1's reading): pooled
0.181 dB, dips 0.11 (VCTK) and 0.51 (VocalSet) per second. Harsh accuracy (synthetic, the band's power)
does not move: the steady resonance never releases. Which pumping rate is audible is the listening
pass's.

### Ess fixed release

20 ms: the release-tail gate passes on tune (1.54 %) and the landing too (Wide −0.36, Split −0.30 dB
at 6 dB over), and it lies in the middle of Auto's range (15 … 30 ms, about √(15 · 30)). 15 ms (Auto's
floor) reads 0.96 % and −0.30 dB; the plan's provisional 40 ms read 5.07 % and −0.49 dB.

### Clicks and modulation

`"Clicks: report"` at the final constants: the plan's gate on sines unchanged in kind, Normal worst
−70.9 dB (Wide, Output every block; M2b −70.8), Live's accepted case −59.3 dB (Wide, Detect every block; M2b −59.2),
every other Live case ≤ −62.9 dB (Wide, Stereo; M2b −64.1). In the band (reported, Settled 45), at
rest Split −46.6, Wide −46.8, Spectral −61.7 dB (M2b −47.5, −46.9, −64.5); Stereo −43.6 / −43.8 / −48.7
(−45.6 / −45.0 / −48.7). The faster release moves the in-band envelope 1–3 dB, as the reduction's
std above (0.33 against 0.28 dB at a 30 against a 60 ms release).

### Golden renders

All twelve re-recorded once for this intended change (every configuration runs Speed Auto). Against
the M3 water-fill files they differed by −45.4 / −47.3 / −21.6 / −23.9 dB (`split-` voice-ess,
voice-ess-harsh, allround-ess, allround-ess-harsh), −48.6 / −49.1 / −32.7 / −33.2 dB (`spectral-`),
−29.0 / −30.6 / −10.5 / −10.9 dB (`wide-`), of what each removes (gate −60 dB). No other constant
changed after the re-recording.

### Tests

`HaliteTests_detector`: Speed Auto's test reads the tuning's limits (15 … 30 ms at bursts of 10, 80
and 500 ms: the floor, the middle and the limit), and since the M3 review a case pins the six
constants (0.25, 15, 30 ms; Harsh 0.5, 30, 150 ms) and a Harsh burst case checks its one-hop release
at the floor, the middle and the limit (over for 39, 181 and 531 ms: 30, 90 and 150 ms); the committed tier is held to the release-tail gate
and a bound of 150 frames over 1 dB (measured 130) and 300 reduced (265); `[m1]` holds both halves to
the gate and tune to 1.5 %. `HaliteTests_reduction`: every accuracy row is a hard gate, Wide's landing
also at every rate; `"Accuracy: the landing against Speed, report"` and `"Accuracy: the ess's onset
against the look-ahead, report"` print the tables above. `HaliteTests`: the fixed release's default.
Three single-constant mutations (share 0.35, limit 40 ms, floor 20 ms), each compiled and run on
`[committed]` and `[accuracy],[golden]`: each is caught (`[accuracy],[golden]` fails on all three, 8–12
checks; the committed tier's bound also on the share and the floor).

### Not verified

- Listening: whether 15 … 30 ms sounds smooth on long esses, whether the dips the old release bridged
  are heard, the onset's first milliseconds, Harsh's pumping.
- Speech at 44.1 and 96 kHz (VCTK is 48 kHz; the synthetic landing is gated at every rate).

## M3: Learn

2026-11-15, M3 voicing part 2: Learn built and tuned, the defaults from it, the Focus curve
measured. Chosen on the `tune` split by measurement (nobody listened); `holdout` was read at the final
defaults and chose nothing. It is M3's third holdout reading, not its only one (the neutral curve's
check, *M3: Speed*'s gates at −17.6 dB, this one; PLAN.md, *Measured status*): this part moved four
defaults after part 1 had read holdout, every choice on tune numbers. This reading is the milestone's
final one. Every number was printed by `HaliteMeasure` (`--learn`, the
detection gates, `--reduction`) from the plugin's own `Detector`, `Learn` and chain, or by the tests
named. VCTK and VocalSet streams unless marked.

### What was built

- **`Learn.{h,cpp}`** (audio thread, no allocation after `prepare`): `EssCandidates` cuts the hops into
  ess candidates; `LevelHistogram` counts levels in 0.1 dB bins, each bin an atomic, so the UI reads
  it lock-free; `Learn` feeds every candidate into the **live histogram** of the last 60 s (a ring of
  4096 candidates) and, on `request()`, captures the next 8 s of candidates and, with Harsh on, of
  voiced frames' Harsh levels; the captured histograms stay until the next request. A capture counts
  the candidates that begin inside it (M3 review, PLAN.md Settled 59): a run open at the press, its
  peak reached before it, is left out; one open when the capture stops counts with its peak so far,
  and a capture pressed at once does not count it again. A host's `prepare` at the same rate changes
  nothing; at a new rate a running capture starts over for the same request and a finished one stays,
  with the hop length it was counted in (`capturedHopSeconds()`; Settled 61). `learnThresholds` is
  the rule. The chain's publisher feeds it every hop (`HaliteChain::learn()`).
- **`src/plugin/LearnControl`** (message thread): `start()` requests a capture and polls at 20 Hz;
  when it is done it computes the rule and sets Threshold (and Harsh Threshold if Harsh was on when it
  ended), in the unit of the Detection it ran in: every parameter's gesture begins before any value
  moves and ends after, and `beforeApplying` lets the editor open its undo transaction (one step). It
  reports `idle`, `listening`, `learned`, `tooFew` and the counts for the M4 display: the state follows
  Threshold (`tooFew` whenever it stays, also when Harsh Threshold was set from the same capture), and
  `result().harshSet` says whether Harsh moved (Settled 60); `dismiss()` is the display's click. Learn is **not a parameter** (PLAN.md, Settled 55).
- **`HaliteMeasure --learn`** (`tools/measure/LearnMeasure`): per recording or stream, 8 s captures
  every 2 s, each through a fresh copy of the plugin's `Learn` warm for 0.5 s before its request; Learn over the whole
  recording; the plan's rule read from the labels (max (median − 4, p25) of the largest `S` of each
  /s/ /z/ token); the reduction each capture's threshold gives the labelled tokens (the detector's
  target at their peaks); candidates against the labels; Harsh against the labelled vowel frames.
  `--thresholds-out` writes Learn's per-speaker thresholds, `--thresholds` runs the detection gates and
  `--reduction` at them, `--normalise` scales recordings to an RMS over their speech (Allround),
  `--data cymbals` adds synthetic hats and ride. `--reduction` also prints what the band filter's
  energy over each labelled /s/ /z/ token lost through each shape.

### What an ess is

The rule needs every ess, the quiet ones too (its quietest quarter), and Learn has no labels. The
detector's events exist only over the threshold, so a capture of them would depend on where the
threshold already is. **An ess candidate is a run of `S` over a floor**, ended once `S` has stayed
under it for 20 ms (the event rule's hangover), at least a minimum length; its value is its peak.
`S` already carries the cues (a harmonic band or a falling spectrum lowers it), so the floor needs no
other test, and none of it depends on the threshold: pressing Learn twice learns the same, and a
threshold automated during a capture changes nothing (a test).

The reading: the labels' rule per speaker gives the plan's own outcome. At it the median token is
cut by 3.2 dB at its peak and the quietest quarter not at all (tune, medians over speakers): for most
voices the quietest quarter binds, their esses spreading less than 4 dB from p25 to the median.
Candidates other than esses (stops' aspiration, /f/ /θ/, breaths) sit low and pull the rule down; a
high floor loses quiet esses and pushes it up. Tune, 8 s captures every 2 s (1393), *− labels* the
capture's threshold less the labels' rule for that speaker, *cut* the median token's reduction and
the share of the quietest quarter reduced over 0.5 dB at the capture's threshold:

| Floor, min length | Captures set (≥ 3) | Candidates p10/p50/p90 | − labels: median [p10, p90] | p90 to whole | Cut: median token, quietest quarter | On a sibilant label | /s/ /z/ covered | VocalSet captures set |
|---|---|---|---|---|---|---|---|---|
| −36 dB, 0 ms | 100 % | 12 / 18 / 25 | −8.39 [−15.4, −3.3] | 5.6 dB | 8.0 dB, 95 % | 36.0 % | 99.5 % | 19 % |
| −30 dB, 10 ms | 100 % | 8 / 13 / 18 | −3.65 [−8.5, −0.1] | 4.5 dB | 6.7 dB, 68 % | 50.7 % | 98.4 % | 4 % |
| −27 dB, 25 ms | 98.3 % | 5 / 9 / 13 | −0.92 [−4.5, +2.4] | 3.9 dB | 4.1 dB, 19 % | 69.2 % | 94.6 % | 0.6 % |
| −25.5 dB, 25 ms | 96.8 % | 4 / 8 / 13 | −0.40 [−3.9, +2.8] | 3.8 dB | 3.4 dB, 4 % | 72.7 % | 92.9 % | 0 % |
| −24 dB, 15 ms | 97.4 % | 4 / 9 / 14 | −0.35 [−4.1, +2.7] | 3.7 dB | 3.3 dB, 4 % | 69.2 % | 92.3 % | 1.2 % |
| **−24 dB, 25 ms** | **95.0 %** | **3 / 8 / 12** | **+0.14 [−3.2, +3.6]** | **3.4 dB** | **2.7 dB, 0 %** | **76.9 %** | **89.9 %** | **0 %** |
| −24 dB, 40 ms | 90.2 % | 3 / 6 / 10 | +0.75 [−2.5, +4.2] | 3.7 dB | 2.1 dB, 0 % | 84.0 % | 83.4 % | 0 % |
| −22.5 dB, 15 ms | 95.4 % | 4 / 8 / 13 | +0.15 [−3.3, +3.4] | 3.4 dB | 2.7 dB, 0 % | 72.6 % | 89.1 % | 0.4 % |
| −21 dB, 25 ms | 88.9 % | 2 / 6 / 11 | +1.11 [−2.2, +4.2] | 3.3 dB | 1.8 dB, 0 % | 81.8 % | 81.3 % | 0 % |

The table was measured before the M3 review's capture fix (a capture counted a run open at its
press; *What was built*). With the fix the chosen row reads 94.7 % set, +0.12 [−3.2, +3.5], p90 to
whole 3.5 dB, every other column unchanged; whole recordings do not change.

**Kept: −24 dB, 25 ms.** Centred on the labels' rule (+0.14 dB for captures, +0.12 after the fix,
−0.06 dB for whole recordings), no capture of the sung vowels sets a threshold, and 25 ms is shorter than any sibilant
label (the aligner's 30 ms) and longer than a stop's burst. −22.5 dB with 15 ms measures the same
with a 0.4 % false set on VocalSet. Of the candidates, 76.9 % lie on a sibilant label, 17.4 % on a
consonant, 5.1 % in a pause, 0.5 % in a vowel.

**How many esses a capture needs** (−24 dB, 25 ms; share of captures set, and the median / p90
distance to Learn over the whole recording; after the fix): 1: 99 % (1.4 / 3.6 dB); **3: 95 % (1.3 /
3.5)**; 4: 89 % (1.3 / 3.4); 5: 82 % (1.3 / 3.4); 6: 74 % (1.3 / 3.3); 8: 52 % (1.4 / 3.3); 10: 31 %
(1.4 / 3.4). The
spread does not grow down to 3, and VocalSet sets none at 3 or more (5 % of its captures hold one or
two candidates). **Kept: 3**; with fewer Threshold stays and the display says *too few esses*.

**Capture length** (the plan's 8 s, Settled 15, kept): 4 s sets 73 % of captures (p90 distance
4.5 dB), 8 s 95 % (3.4 dB), 16 s 98.9 % (2.6 dB). A hangover of 40 ms instead of 20 measures the same
(95.1 %, 3.6 dB).

**The rule's own reading.** At each capture's threshold the median token is cut 2.7 dB at its peak
(p90 over captures 6.4 dB), the quietest quarter 0 %, 71 % of the tokens over 0.5 dB. Through the
shapes (`--reduction` at each speaker's learned threshold, tune), the band filter's energy over a
token's label loses less than its peak: the median /s/ /z/ token 1.18 dB in Split, 1.36 in Wide, 0.91
in Spectral (p75 3.31 / 3.60 / 2.75 dB; 62 % of tokens lose more than 0.5 dB). The detector decides an
/s/ a median 28 ms into its label (*M3: Speed*), and an ess's peak is brief.

### Harsh

**Voiced frames.** M1 set the Harsh default from labelled vowel frames; Learn has no labels. A frame
is voiced when both bands read harmonic (N under 0.5), the body is over its floor and, in Voice, `S` is
under the ess floor. As first built it also excluded the sibilance candidate flag (`B − V` over
−20 dB): a belted vowel's band reads about 10 dB under the voice, so the frames Harsh is for were left
out (VocalSet captures 8.26 dB under its labelled vowels; with the `S` floor instead, whole recordings
+0.06 dB). The `S` floor itself moves the readings by 0.01 dB (the cue already calls nearly every ess
noise-like); it keeps a voiced /z/ out by construction (a test). Voiced frames include the voiced consonants, whose Harsh band is weak: the loudest tenth of
them sits under the vowels' (whole recordings −0.72 dB, captures −0.92 dB, tune). Learn's per-speaker
median against the share (the labels' vowel frames at the tenth: −10.89 dB): 0.05: −10.31, 0.06:
−10.60, **0.07: −10.89**, 0.08: −11.16, 0.10: −11.83 dB. **Kept: the loudest 7 %**, the vowels'
tenth in speech. A capture needs 1 s of voiced frames: 99.9 % of VCTK captures and all of VocalSet's
set it; captures against the labels (at 7 %) −0.98 dB [−2.6, +0.7], p90 distance to the whole
recording 2.2 dB.

**On singing the whole take and the capture differ** (re-measured after the M3 review; VocalSet tune,
medians over singers unless marked). Over a whole take Learn sits on the labelled vowels' 7 % (+0.05
dB) and about 0.7 dB over their tenth (Learn −1.79, the vowels' 7 % −1.84, their tenth −2.50 dB): the
side that reduces less. An 8 s capture reads −2.51 dB under the vowels' 7 % (median over captures;
p10 −7.9, p90 +1.9; p90 distance to the whole take 7.9 dB), so about 1.8 dB under their tenth: the
side that reduces more, since an exercise changes vowel and pitch within 8 s (per singer the captures'
median sits 2.2 to 5.2 dB under the take's). "Reduces less" holds for whole takes only; the button makes
captures (PLAN.md, *Open* 3). The earlier −2.50 dB "tenth" beside −1.79 came from a run with the
labels at the tenth; the labels now use Learn's share, and `--vary harshShare=0.1` prints the tenth.

### Allround

There is no voice: an ess is a run of `B` over −60 dBFS. A floor alone fails where an instrument's
level never falls under it between hits (the ride's wash: one candidate in 32 s), so in Allround a run
also ends where `B` falls 6 dB under its peak, and the next starts once `B` has risen 6 dB from its
lowest since. Synthetic cymbals at −18 dBFS RMS (`--data cymbals`: closed and open hats, a ride with
bell partials): every capture sets a threshold, within ±0.3 dB of the whole recording (a split of 3
or 10 dB sets every capture too; none, and the ride sets nothing). On speech scaled to −18 dBFS
RMS over its labelled speech, Allround cannot tell an ess from a vowel's top end: captures read 7.2 dB
under the labels' rule [−13.6, −2.6] (a −40 dBFS floor gives −1.5 dB, but depends on the level). Voice
is for voices; the manual will say so (M5).

### The defaults

What Learn sets over a whole corpus at once (pooled), beside its per-speaker median and the labels:

| | Learn, pooled | Learn, median of speakers | Labels: pooled, median of speakers | Default |
|---|---|---|---|---|
| Threshold (tune VCTK) | −16.96 dB | −16.69 dB | −17.59 (M1b's −17.6), −16.73 dB | **−17.0 dB** |
| Harsh Threshold (tune VCTK) | −9.89 dB | −10.89 dB | vowel frames pooled: −9.21 at 7 %, −10.09 at the tenth (M1's −10); median of speakers −10.13 at 7 % | **−9.9 dB** |
| Threshold, Absolute (synthetic cymbals at −18 dBFS RMS: an assumption) | −16.02 dBFS | hats −15.7, ride −16.2 | – (speech at −18 dBFS RMS: −34.23 pooled, −33.94 median) | **−16 dBFS** |
| Harsh, Absolute (tune VCTK at −18 dBFS RMS) | −22.43 dBFS | −23.17 dBFS | −22.08 pooled, −22.73 median | **−22.4 dBFS** |

**Kept: Learn pooled**, so a default is what the button would set on the corpus (Learn's own
candidate and voiced-frame histograms over every recording at once). *Restated after the M3 review:*
this is not the reading M1 and M1b called "the rule on the corpus"; that one, the labels' rule pooled
over every /s/ /z/ token, still reads −17.59 dB and would keep M1b's −17.6. The relative defaults move
by 0.6 dB (Threshold, from −17.6) and 0.1 dB (Harsh); the per-speaker median differs from the pooled
value by less than an 8 s capture's spread (PLAN.md, Settled 57, 62). **The absolute default is an
assumption, not a corpus reading:** two soloed cymbals that `HaliteMeasure` generates (`--data
cymbals`: white noise high-passed at 7 kHz for the hats, 3 kHz and low-passed at 12 kHz for the ride,
32 s each, scaled to −18 dBFS RMS), so nearly all their energy lies in the Detect band; on a bus or a
mixed track the band sits far lower and −16 dBFS would do little. Speech at the same level gives
−34 dBFS by the labels' rule (table). Harsh's comes from speech, the only harmonic material here at a
known level; −18 dBFS RMS is the common working level (0 VU) and the assumption behind both. At −17.0 dB the
tune split reads /s/ 74.8 %, /z/ 64.4 % at the default (−17.6: 79.0, 67.8 %), vowel frames over 1 dB
1.19 % (1.33 %), Harsh pooled 0.169 dB (0.170), VocalSet 3.38 dB on 1 recording (3.83 on 2); the
committed tier /s/ 18 of 31, /z/ 7 of 11 (22, 8), 107 frames over 1 dB (130), Harsh 0.202 dB. Harsh at
Learn's per-speaker −11.8 dB would double the stacking (0.33 dB) and the vowel frames it cuts (15.7 →
27.2 %): not taken.

**Kept as they were, measured** (tune): **Range 8 dB**: at the default threshold it binds for the /s/
/z/ tokens whose peak is over −9.0 dB, about a quarter (p75 −9.3, p90 −4.7 dB); deeper cuts are for the
listening pass. **Harsh Range 4 dB**: binds over −5.9 dB, about 1.5 % of vowel frames (p98 −6.6, p99
−5.2 dB). **Detect 4.5 … 12 kHz** (Detect Low chosen in M1; Detect High swept, at each own T95):
10 kHz: 0.45 % decisions, /ʃ ʒ tʃ dʒ/ label frames 56.2, 36.2, 38.0, 31.5 %, VocalSet 3.6 dB on 3;
**12 kHz: 0.43 %, 54.2, 34.1, 36.6, 30.2 %, 3.8 dB on 2**; 14 kHz: 0.43 %, 53.1, 33.0, 35.9, 29.6 %, 4.0
dB on 2; 16 kHz: 0.41 %, 51.4, 31.4, 34.5, 28.3 %, /ʒ/ tokens −5 points, 4.2 dB on 2. No band measures
better on everything; 12 kHz stays.

### Focus

Focus scales the noise cue's term, the shape cue's and the jump's hold linearly (M1b); 0 is `B − V`
exactly (a test). Tune, at M1b's −17.6 dB (decisions and frames at each own T95):

| Focus | Decisions (worst speaker) | /ʃ ʒ tʃ dʒ/ label frames decided | Over 1 dB at the default | VocalSet at the default: frames, largest, recordings | Synthetic at the default (gated tilts) | Flat spectrum at T95 |
|---|---|---|---|---|---|---|
| 0 | **10.89 %** (34.79) | 63.4, 42.7, 44.6, 39.8 % | 4.72 % | 46.1 %, 8.0 dB, 9 | **fail** (42 reduced, 8.0 dB) | +30.6 dB |
| 25 | 0.74 % (1.57) | 60.6, 37.8, 41.2, 34.7 % | 1.91 % | 0.97 %, 8.0 dB, 6 | **fail** (−3 dB/oct: 8.0 dB) | +21.1 dB |
| 50 | 0.51 % (1.13) | 57.3, 35.0, 38.2, 32.5 % | 1.49 % | 0.010 %, 3.8 dB, 4 | pass (flat 5.9 dB, reported) | +11.1 dB |
| **70** | **0.43 % (1.08)** | **54.2, 34.1, 36.6, 30.2 %** | **1.33 %** | **0.006 %, 3.8 dB, 2** | **pass** | **+3.1 dB** |
| 85 | 0.39 % (1.05) | 51.9, 33.6, 35.5, 28.9 % | 1.27 % | 0.006 %, 3.8 dB, 2 | pass | −2.9 dB |
| 100 | 0.37 % (1.01) | 51.1, 33.3, 34.8, 28.0 % | 1.22 % | 0.006 %, 3.8 dB, 2 | pass | −8.9 dB |

**Kept: linear, 70 %.** 70 is the lowest Focus at which every gate that can pass does (separability
from 25, the gated synthetic tilts from 50) and VocalSet reaches its fewest recordings; above it the
decisions gain 0.04–0.06 points while the /ʃ/ class loses up to 3.1 points of label frames. **The
VocalSet bright failure does not answer Focus** (3.8 dB from 50 to 100 %): tune's worst hop
(m5's stream at 78.65 s) is a noise burst in a pause before a note, `V` under the −60 dBFS floor, N
1.00, a rising spectrum (presence +8 dB): noise, which no cue lowers. It is the floor's cold-start case
(*M1b*, *Proposals* 1), not a Focus curve.

### Gates at the final constants

VCTK streams at the defaults (Threshold −17.0 dB, Harsh −9.9 dB, Focus 70 %, Speed Auto); tune →
**holdout** (this part's reading, M3's final); M3 part 1 (at −17.6 / −10 dB) in brackets:

| | Tune | Holdout | Gate |
|---|---|---|---|
| separability: T95, decisions (worst speaker) | −22.81 dB, 0.43 % (1.08) (same) | −22.46 dB, **0.35 %** (1.20) (same) | ≤ 2 %: **pass** |
| /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/ at T95 | 89.7, 94.6, 86.4, 95.3, 91.6 % (same) | 87.9, 89.4, 74.7, 90.5, 73.0 % (same) | reported |
| release tails: vowel frames over 1 dB (Settled 47) | **1.19 %** (1.33) | **0.91 %** (1.02) | ≤ 2 %: **pass** |
| … reduced over 0.5 dB: decisions, tails | 0.13, 1.94 % | 0.11, 1.51 % | reported |
| /s/, /z/ detected at the default | 74.8, 64.4 % (79.0, 67.8) | 71.4, 56.5 % (74.8, 61.4) | reported |
| label frames reduced over 0.5 dB at the default, /s/, /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/ (Settled 47) | 50.7, 40.2, 45.2, 20.0, 30.8, 24.6 % (54.0, 43.1, 47.5, 22.4, 33.2, 26.6) | 47.1, 33.8, 44.7, 19.0, 25.9, 17.7 % (50.3, 36.9, 47.2, 20.9, 27.7, 19.3) | reported |
| bright, VocalSet at the default | 0.005 %, 3.38 dB, 1 of 9 (0.006 %, 3.8 dB, 2) | 0.034 %, **8.0 dB**, 2 of 9 (0.038 %, 8.0 dB, 3) | **fail** |
| bright, synthetic (held-out set on holdout): gated tilts; flat at T95 | pass; +3.1 dB | pass; +3.6 dB | pass (flat reported) |
| Harsh pooled in sibilants (Settled 46) | 0.169 dB (0.170) | 0.175 dB (0.181) | ≤ 0.5 dB: **pass** |
| Harsh pumping: dips per second, VCTK / VocalSet | 0.20 / 0.61 | 0.12 / 0.52 | reported |
| level: −30 dB working range | no event: not measured | no event: not measured | (Open 2) |
| … each gain in its own range, −10 / −20 dB | 48.4 / 1.2 % of events, no change | 48.2 / 2.7 %, no change | reported |
| Learn: captures set; − labels median [p10, p90]; p90 to whole | 95.0 %; +0.14 [−3.2, +3.6]; 3.4 dB; after the review's fix 94.7 %; +0.12 [−3.2, +3.5]; 3.5 dB | 94.6 %; +0.50 [−3.5, +3.7]; 3.3 dB (before the fix; not re-read) | reported |
| Learn: median token cut, quietest quarter cut, at each capture's threshold | 2.7 dB, 0 % | 2.4 dB, 0 % | reported |
| Learn: VocalSet captures that set Threshold | 0 % | 0.3 % | reported |
| Learn: Harsh captures set; − labels (7 %) | 99.9 %; −0.98 dB (VocalSet 100 %; −2.51 dB) | 99.0 %; −0.68 dB | reported |
| Learn pooled over the half: Threshold, Harsh | −16.96, −9.89 dB | −17.63, −9.22 dB | reported |
| at each speaker's learned threshold: /s/, /z/; over 1 dB | 77.6, 64.0 %; 0.83 % | 76.7, 61.0 %; 0.74 % | reported |

Through the shapes (`--reduction`; tune → holdout; at the defaults, then at each speaker's learned
threshold): Split's own band within ±0.5 dB on 100 % of events in every run; Spectral's own band 96.7 →
96.9 % (learned 97.0 → 97.4 %); Wide's landing 75.8 → 79.2 % (learned 76.6 → 79.1 %); Harsh, Split and
Wide 100 %, Spectral 98.0 % (holdout); Listen Removed's null test passes in every run (≤ 5.96·10⁻⁸). The labelled
/s/ /z/ tokens' band energy, median: tune 1.47 / 1.11 / 1.66 dB (Split / Spectral / Wide) at the
default, 1.18 / 0.91 / 1.36 learned; holdout 0.98 / 0.84 / 1.07 at the default, 1.27 / 1.10 / 1.39
learned (Learn's per-speaker median sits 0.4 dB under the default on holdout, 0.3 dB over it on tune).

The label-frame shares fall with the threshold's move (−17.6 → −17.0 dB) on top of the release's
(*M3: Speed*): /s/ 55.3 % at M1b's Speed, 54.0 at part 1's, 50.7 at the final defaults (tune). The
detector did not change, so separability is part 1's to the digit; what moves is the threshold.
Holdout's Learn reads about 0.7 dB lower than tune's (pooled −17.63 against −16.96 dB, per-speaker
median −17.44 against −16.69): so do its speakers' labels (pooled −18.24 against −17.59). Nothing was
chosen on it.

### Tests

`HaliteTests_detector "[learn]"` (11 cases): the histogram's levels, edges and atomic counts; the rule
(median − 4 dB, the quietest quarter binding, too few, Harsh's share and its 1 s); candidates (the
hangover, a dip joining or splitting runs, the minimum length, Allround's split, a capture's open run);
a capture through the detector learning the rule on its own esses' peaks exactly; the same capture at
thresholds −36, −17 and +10 dB (identical histograms); silence, a sung vowel and steady band noise
setting no Threshold; Harsh from the voiced frames, esses never voiced; the live window; a capture
restarted by a request or a change of Detection, not by a reset; a run straddling the press left out,
the run open at a capture's stop counted once across two captures pressed back to back; a prepare at
the same rate keeping a running and a finished capture, one at a new rate restarting a running capture
and keeping a finished one with its hop length. `HaliteTests "[learn]"` (9 cases and `[realtime]`'s,
through the processor): Threshold set with one gesture and one change, undone in one step through the
shared `ParameterUndoRecorder`; both thresholds with Harsh on, both gestures before either value; the
absolute ones in Allround; too few esses (a vowel, silence) keeping Threshold with no gesture; nothing
running without the button and nothing stored (a session saved mid-capture restores none); a
threshold automated every block during a capture learning the same; pressed again, starting over;
`prepareToPlay` during a capture (same rate: it runs on; new rate: it starts over) and right after one,
before `LearnControl` reads it (same and new rate: the reference's result exactly); Harsh on with a
sung vowel, too few esses: `tooFew`, Threshold untouched, Harsh Threshold moved in one gesture. Each
case added in the review fails against the behaviour it replaced (checked by reverting it in place).
`"[realtime]"`: Learn pressed during the processor's allocation test, requests during the chain's, and
a capture that runs to its end, Harsh on: no allocation. The committed tier's regression bounds moved
with the default (measured /s/ 18, /z/ 7, 228 frames reduced, 30 decisions, 107 over 1 dB); `[m1]`
reads the new defaults and passes on both halves. Three single-line mutations of `Learn.cpp` (no
minimum length, the rule without its quietest quarter, voiced frames without the ess floor) are each
caught; the third survived the tests as first written (the noise cue already keeps the synthetic esses
out) and is caught by a frame built for it. A capture's id is published after its phase (`Learn::hop`),
so `LearnControl` never reads the previous capture as the new one; that ordering is reasoned, not
tested with a second thread.

### CPU

`HaliteTests_reduction "[.perf]"` re-run after the review with Learn in the chain (pinned to one core,
load 3.1 … 4.3): 0 of 198 readings over budget. Spectral Linked 0.703 %, Unlinked 0.875 (the M3
Spectral entry, before Learn, 0.664 / 0.795; Learn's share and the load are not separated), both bands
cutting 1.022 / 1.197 (1.021 / 1.180). Closest margins at 44.1/48 kHz: Split Unlinked with Detect swept
and an ess all the time 0.440 of 0.5, Harsh's share in Wide 0.255 of 0.3. Learn's per-hop work is
the candidate test, a histogram bin or two and the live ring's expiry.

### Not verified

- Listening: whether the learned thresholds sound right on real sessions, whether the median ess's
  0.9–1.4 dB of band energy (2.4–2.7 dB at its peak) is the de-essing a user expects from "4 dB".
- Learn on real instruments in Allround (only synthetic cymbals here), and on sung sibilants (no
  labelled source).
- Speech at 44.1 and 96 kHz (VCTK is 48 kHz; Learn's constants are in ms and dB).
