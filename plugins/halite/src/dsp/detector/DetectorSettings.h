#pragma once

#include "dsp/Modes.h"

namespace halite::dsp
{

/**
    Which noise cue the detector computes (PLAN.md, Detection 3). Not a parameter: M1 measures
    both on the corpus and keeps one, or the blend (the mean of the two noise-likeness values).
*/
enum class NoiseCueKind
{
    periodicity = 0,    // (a) periodicity of the detrended band envelope
    flatness,           // (b) long-frame spectral flatness on envelope-normalised sub-bands
    blend
};

/**
    Plain values of everything the detectors read, taken once per block. Defaults are the
    parameters' (plugin/Parameters.h), which the DSP library cannot include.
*/
struct DetectorSettings
{
    Detection detection = Detection::voice;     // Voice: S = B − V with the noise cue; Allround: S = B
    float thresholdDb = -17.0f;                 // Relative (Voice), dB over the voice
    float thresholdAbsDb = -16.0f;              // Absolute (Allround), dBFS RMS
    float rangeDb = 8.0f;
    float detectLowHz = 4500.0f, detectHighHz = 12000.0f;   // either order: the detector sorts them
    float focus = 0.7f;                         // 0 … 1
    bool speedAuto = true;
    float releaseMs = 20.0f;                    // with speedAuto off
    float lookaheadMs = 5.0f;                   // Normal latency only
    LatencyMode latency = LatencyMode::normal;

    StereoMode stereo = StereoMode::linked;
    KeySource key = KeySource::internal;
    float keyHpfHz = 0.0f;                      // 0 (or below): Off

    bool harshOn = false;
    float harshThresholdDb = -9.9f;            // Relative, dB over the voice's body
    float harshThresholdAbsDb = -22.4f;         // Absolute, dBFS RMS
    float harshRangeDb = 4.0f;
    float harshFreqHz = 3200.0f;
    float harshWidthOct = 1.0f;
    bool harshSpeedAuto = true;
    float harshReleaseMs = 80.0f;               // with harshSpeedAuto off

    NoiseCueKind noiseCue = NoiseCueKind::periodicity;
};

/**
    The detector's constants that the corpus sets (NOTES.md, Measured (M1) and (M1b)). Not
    parameters: the plugin runs these defaults; tools/measure sweeps them. Times are one-pole time
    constants of the power (4.34 dB per τ).
*/
struct DetectorTuning
{
    double bandAveragingMs = 4.0, bandReleaseMs = 5.0;      // B
    double bodyReleaseMs = 80.0;                            // V
    double bodyCapHz = 2000.0, bodyCapShare = 0.5;          // V: 120 Hz up to min (cap, Detect Low × share)
    double bodyHoldMs = 250.0;                              // the longest V holds for one candidate
    double bodyGapDb = 20.0, bodyGapHoldMs = 1000.0;        // V also holds while the body is this far below it, at most this long
    double bodyFloorDb = -60.0;                             // B − V never divides by a body under this (dBFS; plan value)
    float candidateDb = -20.0f;                             // B − V above which V holds
    float riseDb = 2.0f;                                    // V rising faster than this over 5 ms is an onset
    float jumpDb = 10.0f;                                   // the body's averaged level rising faster than this over 5 ms is a jump (0: off)
    double onsetHoldMs = 30.0;                              // after a rise N counts as harmonic this long, after a jump jumpHoldDb applies (0: off)
    float jumpHoldDb = 10.0f;                               // in a jump's hold, S is lowered by at least this, at Focus 100 % (noiseRangeDb or more: N = 0)
    float noiseRangeDb = 40.0f;                             // the most N takes off S, at Focus 100 % (linear in Focus)

    // The shape cue: S is lowered where the Detect band is weaker than the presence band under it
    // (a falling spectrum), by presenceSlope dB per dB under the knee, at most presenceRangeDb, at
    // Focus 100 % (linear in Focus; slope 0: off). The presence band is the body filter's upper
    // band-pass, an octave either side of the body's top (about 1 … 3.9 kHz at the default);
    // both levels are averaged over B's time.
    float presenceKneeDb = -4.0f, presenceSlope = 6.0f, presenceRangeDb = 40.0f;

    // Noise-likeness N from the raw cue, linear between a noise end (N = 1) and a harmonic end (N = 0).
    double periodicNoise = 0.55, periodicHarmonic = 0.70;
    double flatNoiseDb = -4.5, flatHarmonicDb = -8.0;
    double periodicLongestLagMs = 12.5;                     // the Ess cue's longest lag (the lowest f0 it hears)

    // Persistence: while B stays within persistJumpDb of its lowest over persistMs and ρ read more
    // than persistRho in that time, the cue keeps that reading (0: off).
    double persistMs = 40.0;
    float persistRho = 0.8f, persistJumpDb = 6.0f;

    float harshNoiseHold = 0.5f;                            // Voice: Harsh holds at this N of its own band or more

    // Speed Auto: the release is this share of the time spent over, within these limits (M3, NOTES.md).
    // Ess: no longer than 30 ms, so an ess lands at the threshold and its tail leaves the next vowel.
    float essAutoShare = 0.25f, essAutoMinMs = 15.0f, essAutoMaxMs = 30.0f;
    float harshAutoShare = 0.5f, harshAutoMinMs = 30.0f, harshAutoMaxMs = 150.0f;
};

} // namespace halite::dsp
