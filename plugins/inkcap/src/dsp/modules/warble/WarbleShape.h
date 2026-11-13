#pragma once

#include "dsp/ModuleInfo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace inkcap::dsp::warble
{

/**
    Each Warble type's own numbers (voiced in M3): its rate at Rate 50 %,
    its pitch deviation at full intensity (peak, as a fraction of the speed) and its window of
    synced note values. See NOTES.md.
*/
struct TypeShape
{
    double nominalHz;
    double maxDeviation;
    int syncSlowest, syncFastest;   // indices into noteValues; the middle one is the nominal
};

struct NoteValue
{
    const char* name;
    double beats;
};

/** Synced LFO periods, slow to fast. */
inline constexpr std::array<NoteValue, 18> noteValues { {
    { "16/1", 64.0 }, { "8/1", 32.0 }, { "4/1", 16.0 }, { "2/1", 8.0 }, { "1/1", 4.0 }, { "1/2D", 3.0 },
    { "1/2", 2.0 }, { "1/4D", 1.5 }, { "1/2T", 4.0 / 3.0 }, { "1/4", 1.0 }, { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 },
    { "1/8", 0.5 }, { "1/16D", 0.375 }, { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 },
} };

inline constexpr std::array<TypeShape, 3> typeShapes { {
    { 0.55, 0.020, 1, 7 },      // Wow: a warped record at 33 1/3 rpm, seasick at full (34 cents); 8/1 .. 1/1 .. 1/4D
    { 7.0, 0.006, 13, 17 },     // Flutter: a capstan past saving at full (10 cents); 1/16D .. 1/16 .. 1/32
    { 0.12, 0.020, 0, 4 },      // Drift: a motor that cannot hold its speed (the 15 ms swing caps it); 16/1 .. 4/1 .. 1/1
} };

/**
    The share of the type's full deviation at an intensity: i^1.3, so equal steps of Damage are
    about equal steps of heard wobble (Wow: 6, 14 and 34 cents at a quarter, half and full).
*/
inline double depthShare (double intensity) noexcept
{
    return intensity > 0.0 ? std::pow (std::min (intensity, 1.0), 1.3) : 0.0;
}

inline const TypeShape& shapeOf (WarbleType type) noexcept
{
    return typeShapes[(size_t) std::clamp ((int) type, 0, (int) typeShapes.size() - 1)];
}

/** Free Rate: two octaves either side of the type's own rate. */
inline double freeHz (WarbleType type, float rate) noexcept
{
    return shapeOf (type).nominalHz * std::exp2 (4.0 * ((double) std::clamp (rate, 0.0f, 1.0f) - 0.5));
}

inline float freePosition (WarbleType type, double hz) noexcept
{
    return (float) std::clamp (0.5 + std::log2 (hz / shapeOf (type).nominalHz) / 4.0, 0.0, 1.0);
}

/** Synced Rate: the note value a position picks in the type's window. */
inline int syncIndex (WarbleType type, float rate) noexcept
{
    const auto& s = shapeOf (type);
    return s.syncSlowest + (int) std::lround ((double) std::clamp (rate, 0.0f, 1.0f) * (s.syncFastest - s.syncSlowest));
}

inline float syncPosition (WarbleType type, int index) noexcept
{
    const auto& s = shapeOf (type);
    return (float) (std::clamp (index, s.syncSlowest, s.syncFastest) - s.syncSlowest) / (float) (s.syncFastest - s.syncSlowest);
}

/**
    sin (2 pi phase) for the LFO, from a polynomial (within 1e-9): twice cheaper than the library's
    per sample, and the same on every platform's libm.
*/
inline double lfoSine (double phase) noexcept
{
    auto u = phase - std::floor (phase);
    u = u < 0.5 ? u : u - 1.0;                              // -0.5 .. 0.5
    u = u > 0.25 ? 0.5 - u : (u < -0.25 ? -0.5 - u : u);    // the quarter around 0, by symmetry
    const auto z = 2.0 * std::numbers::pi * u;
    const auto z2 = z * z;
    return z * (1.0 + z2 * (-1.0 / 6.0 + z2 * (1.0 / 120.0 + z2 * (-1.0 / 5040.0 + z2 * (1.0 / 362880.0
                                                                                          + z2 * (-1.0 / 39916800.0 + z2 * (1.0 / 6227020800.0)))))));
}

/** How much of the wander is random: Chaos for Wow and Flutter; Drift is mostly random sooner. */
inline double randomShare (WarbleType type, float chaos) noexcept
{
    const auto c = (double) std::clamp (chaos, 0.0f, 1.0f);
    return type == WarbleType::drift ? std::sqrt (c) : c;
}

} // namespace inkcap::dsp::warble
