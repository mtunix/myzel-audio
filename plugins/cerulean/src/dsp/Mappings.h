#pragma once

#include <algorithm>
#include <array>
#include <cmath>

/**
    Mappings from parameter values (as stored, see plugin/Parameters.h) to physical DSP values.

    These are shared by the parameter text functions, the engines and the tests, so a knob, its
    readout and the sound can never disagree. Everything here is constexpr-cheap and
    allocation-free, safe on the audio thread.
*/
namespace cerulean::dsp::maps
{

inline float clamp01 (float x) noexcept { return std::clamp (x, 0.0f, 1.0f); }

//==============================================================================
// Global controls

/**
    Size 0..1 -> delay-length scale. 0.5 is each algorithm's nominal size (1x); 0 is 0.3x and 1 is
    2x, exponential on both sides so equal knob travel feels like equal change.
*/
inline float sizeToScale (float size01) noexcept
{
    const auto s = clamp01 (size01);
    return s < 0.5f ? std::pow (0.3f, (0.5f - s) * 2.0f)
                    : std::pow (2.0f, (s - 0.5f) * 2.0f);
}

/**
    Tone -1..1 -> high-band decay. The high band's RT60 is a fraction of the mid band's, starting
    at a crossover that also moves: dark closes the top down early, bright keeps air ringing.
*/
struct HighDecay
{
    float ratio;        // RT60(high) / RT60(mid), in (0, 1]
    float crossoverHz;  // where the high band starts
};

inline HighDecay toneToHighDecay (float tone) noexcept
{
    const auto t = std::clamp (tone, -1.0f, 1.0f);

    // Flat: a natural hall (air absorption): highs decay at ~45 % of the mids from ~4.5 kHz.
    if (t < 0.0f)
        return { 0.45f * std::pow (0.12f / 0.45f, -t), 4500.0f * std::pow (1500.0f / 4500.0f, -t) };

    return { 0.45f + (0.97f - 0.45f) * t, 4500.0f * std::pow (9000.0f / 4500.0f, t) };
}

/** Low-band crossover used by every algorithm's Low End decay. */
inline constexpr float lowCrossoverHz = 250.0f;

/** Wet tilt applied after the engine, in dB per side at Tone = +-1 (pivot ~1.2 kHz). */
inline constexpr float wetTiltMaxDb = 3.0f;
inline constexpr float wetTiltPivotHz = 1200.0f;

/** RT60 (s) and delay length (samples) -> per-pass loop gain. */
inline float decayGain (float delaySamples, float rt60Seconds, float sampleRate) noexcept
{
    return std::pow (10.0f, -3.0f * delaySamples / (std::max (rt60Seconds, 1.0e-3f) * sampleRate));
}

//==============================================================================
// Character controls (Param A / Param B, each 0..1 here)

// Halo: interval of the in-loop pitch shift.
inline constexpr int numHaloIntervals = 5;
inline constexpr std::array<const char*, numHaloIntervals> haloIntervalNames { "-12", "+5", "+7", "+12", "+12/+19" };

inline int haloIntervalIndex (float a01) noexcept
{
    return std::clamp ((int) std::lround (clamp01 (a01) * (numHaloIntervals - 1)), 0, numHaloIntervals - 1);
}

// Spring: number of springs in the tank.
inline int springCount (float a01) noexcept
{
    return 1 + std::clamp ((int) std::lround (clamp01 (a01) * 2.0f), 0, 2);
}

// Rise: swell (fade-in) time, 50 ms .. 4 s, exponential.
inline float riseTimeMs (float a01) noexcept { return 50.0f * std::pow (80.0f, clamp01 (a01)); }

// Billow: bloom length (time for the bloom to peak), 150 ms .. 4 s, exponential.
inline float bloomLengthMs (float a01) noexcept { return 150.0f * std::pow (4000.0f / 150.0f, clamp01 (a01)); }

// Choir: vowel position 0..4 (A, E, I, O, U), continuous so it morphs.
inline constexpr std::array<const char*, 5> vowelNames { "A", "E", "I", "O", "U" };
inline float vowelPosition (float a01) noexcept { return clamp01 (a01) * 4.0f; }

// Choir: number of voices, 1..4, continuous (fractional voices fade in).
inline float voiceCount (float b01) noexcept { return 1.0f + clamp01 (b01) * 3.0f; }

// Echo: free echo time, 20 ms .. 1.5 s, exponential. (When synced, a note value replaces it.)
inline constexpr float echoMaxMs = 2500.0f;   // buffer limit for synced times at slow tempi
inline float echoTimeMs (float a01) noexcept { return 20.0f * std::pow (75.0f, clamp01 (a01)); }

} // namespace cerulean::dsp::maps
