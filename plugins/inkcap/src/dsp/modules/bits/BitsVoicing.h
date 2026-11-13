#pragma once

#include "dsp/ModuleInfo.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace inkcap::dsp::bits
{

/**
    What each Bits type is, in one place (NOTES.md, Types; voiced by measurement in M3). Rate and
    Depth are stored as positions: 50 % is the type's own clock and depth.
*/
struct Voicing
{
    double clockHz;             // the converter's clock at Rate 50 %
    double filterRatio;         // anti-alias and reconstruction cutoff, a share of the clock
    double nativeBits;          // the depth at Depth 50 %
    bool mulaw;                 // a companding (mu-law) converter
    double lowCutHz;            // a band's lower edge before the converter (0: none)
};

constexpr std::array<Voicing, 3> voicings { {
    { 26040.0, 0.42, 12.0, false, 0.0 },        // Sampler 12: steep filters a little under its Nyquist
    { 16000.0, 0.5, 8.0, false, 0.0 },          // Sampler 8: cheap filters at its Nyquist, so it aliases
    { 8000.0, 0.425, 8.0, true, 300.0 },        // Phone: 300 - 3400 Hz, mu-law at 8 kHz
} };

inline const Voicing& voicingOf (BitsType type) noexcept
{
    return voicings[(size_t) std::clamp ((int) type, 0, (int) voicings.size() - 1)];
}

constexpr double octavesDown = 3.0;     // Rate 0 %: the clock three octaves under the type's
constexpr double octavesUp = 1.0;       // Rate 100 %: one octave over
constexpr double bitsEitherSide = 6.0;  // Depth 0 .. 100 %: the native depth -6 .. +6 bits
constexpr double minimumBits = 2.0;
constexpr double cleanBits = 16.0;      // the depth a light touch starts from: where a converter's noise is first heard

/** Rate (0 .. 1 position) as the clock in Hz: log-linear on each side of the type's own clock. */
inline double clockHz (BitsType type, double position) noexcept
{
    const auto p = std::clamp (position, 0.0, 1.0);
    const auto octaves = p < 0.5 ? (p - 0.5) * 2.0 * octavesDown : (p - 0.5) * 2.0 * octavesUp;
    return voicingOf (type).clockHz * std::exp2 (octaves);
}

inline double clockPosition (BitsType type, double hz) noexcept
{
    const auto octaves = std::log2 (std::max (hz, 1.0) / voicingOf (type).clockHz);
    const auto p = octaves < 0.0 ? 0.5 + octaves / (2.0 * octavesDown) : 0.5 + octaves / (2.0 * octavesUp);
    return std::clamp (p, 0.0, 1.0);
}

/** Depth (0 .. 1 position) as bits; fractional depths are as valid as whole ones. */
inline double depthBits (BitsType type, double position) noexcept
{
    const auto bits = voicingOf (type).nativeBits + (std::clamp (position, 0.0, 1.0) - 0.5) * 2.0 * bitsEitherSide;
    return std::max (bits, minimumBits);
}

inline double depthPosition (BitsType type, double bits) noexcept
{
    return std::clamp (0.5 + (bits - voicingOf (type).nativeBits) / (2.0 * bitsEitherSide), 0.0, 1.0);
}

/** Jitter at 100 %: the clock instants wander this many clock periods either way. */
constexpr double jitterPeriods = 0.35;
/** The wander's lattice: value noise at the type's own clock over this (a slow wander of each instant). */
constexpr double jitterLatticeDivider = 16.0;

/** Chaos widens the jitter: at Chaos 100 % half of what Jitter has left to give. */
inline double jitterWithChaos (float jitter, float chaos) noexcept
{
    const auto j = (double) std::clamp (jitter, 0.0f, 1.0f);
    return j + (1.0 - j) * 0.5 * (double) std::clamp (chaos, 0.0f, 1.0f);
}

/** mu-law's mu (the 8-bit telephone law). */
constexpr double mu = 255.0;

} // namespace inkcap::dsp::bits
