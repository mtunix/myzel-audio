#pragma once

#include "common/dsp/FloatCompare.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// The dynamics core's conventions (NOTES.md, Conventions; Idle and the exact 1.0). The side chain
// runs in double at the host rate; levels are in dB on the detector scale, where a sine reads its
// peak on every detector; a reduction is in dB, positive, and Upward's lift is a negative one.

namespace basalt::dynamics
{

/** What a level of nothing reads: far below any Threshold, and never the log of zero. */
inline constexpr double silentDb = -200.0;

/** Reductions closer to 0 than this are exactly 0, so the gain is exactly 1 (NOTES.md, Idle). */
inline constexpr double snapDb = 1.0e-6;

/** Level states (mean squares, crest detectors) below this are flushed to 0, so none goes denormal. */
inline constexpr double flushBelow = 1.0e-30;

/** Auto Gain's reference: a sine peaking at −18 dBFS (NOTES.md, Make-up and Auto Gain). */
inline constexpr double autoGainReferenceDb = -18.0;

/** Linked, Unlinked and Mid/Side detect on two channels; Mid, Side and mono on one. */
inline constexpr int maxDetectorChannels = 2;

/** 10·log10(2): how much higher the constant-magnitude carrier reads on an RMS detector than on a peak one. */
inline constexpr double carrierRmsOffsetDb = 3.0102999566398120;

// log2 is the cheaper libm call; 20·log10(x) = 20·log10(2)·log2(x).
inline constexpr double dbPerOctave = 6.0205999132796239;

/** A peak magnitude |k| in dB. */
inline double peakDb (double magnitude) noexcept
{
    return magnitude > 1.0e-10 ? dbPerOctave * std::log2 (magnitude) : silentDb;
}

/** A mean square in dB on the detector scale, 10·log10(2m), so a sine reads its peak. */
inline double meanSquareDb (double meanSquare) noexcept
{
    return meanSquare > 0.5e-20 ? 0.5 * dbPerOctave * std::log2 (2.0 * meanSquare) : silentDb;
}

/** The magnitude that reads `db` on the peak scale. */
inline double magnitudeFor (double db) noexcept
{
    return std::pow (10.0, db / 20.0);
}

/** A one-pole's pole α = e^(−1/(τ·fs)) for a time constant τ in ms; 0 ms follows at once. */
inline double poleFor (double ms, double sampleRate) noexcept
{
    return ms > 0.0 ? std::exp (-1000.0 / (ms * sampleRate)) : 0.0;
}

inline double snapReduction (double reductionDb) noexcept
{
    return std::abs (reductionDb) < snapDb ? 0.0 : reductionDb;
}

/**
    The snap for a state that follows a target: exactly 0 once both are under 10⁻⁶ dB. Snapping
    the state alone would also catch the first steps of a slow attack towards a small target
    (at Attack 300 ms one step towards 0.01 dB is under 10⁻⁶ dB) and hold it at 0 for good.
*/
inline double snapState (double stateDb, double targetDb) noexcept
{
    return std::abs (stateDb) < snapDb && std::abs (targetDb) < snapDb ? 0.0 : stateDb;
}

/** For states that are never negative (powers). */
inline double flushLevel (double x) noexcept
{
    return x < flushBelow ? 0.0 : x;
}

/** The gain cell's linear gain: exactly 1 at a reduction of exactly 0, never 10^(0/20) computed. */
inline float gainFor (double reductionDb) noexcept
{
    constexpr auto dbToNeper = std::numbers::ln10 / 20.0;
    return myzel::dsp::isExactly (reductionDb, 0.0) ? 1.0f : std::exp ((float) (-reductionDb * dbToNeper));
}

} // namespace basalt::dynamics
