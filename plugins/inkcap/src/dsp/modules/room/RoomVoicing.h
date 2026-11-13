#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace inkcap::dsp::room
{

/**
    Size per type (voiced by measurement in M3). Closet, Room and Hall are measured by their longest
    dimension in metres: the reflections' times and the tank's lengths scale with it, and so does
    the decay (RT60 ∝ size^0.7, between a fixed absorption's V / S law and a furnished room's
    flatter one). Spring's Size is its decay time. The stored 0 .. 100 % position maps
    geometrically onto each range, so every step is the same ratio.
*/
struct SizeRange
{
    double low, high;
    const char* unit;
};

inline constexpr std::array<SizeRange, 4> sizeRanges { {
    { 0.8, 2.5, "m" },      // Closet: a booth, a cupboard
    { 2.5, 10.0, "m" },     // Room: a bedroom to a rehearsal room
    { 0.8, 3.0, "s" },      // Spring: the tank's decay
    { 10.0, 40.0, "m" },    // Hall: a school hall to a church
} };

inline const SizeRange& sizeRange (int type) noexcept { return sizeRanges[(size_t) std::clamp (type, 0, 3)]; }

/** The type's size (metres, or Spring's seconds) at a 0 .. 1 position. */
inline double sizeAt (int type, double position) noexcept
{
    const auto& r = sizeRange (type);
    return r.low * std::pow (r.high / r.low, std::clamp (position, 0.0, 1.0));
}

/** The position (0 .. 1) of a size, clamped to the type's range. */
inline double positionOf (int type, double size) noexcept
{
    const auto& r = sizeRange (type);
    return std::clamp (std::log (std::max (size, 1.0e-9) / r.low) / std::log (r.high / r.low), 0.0, 1.0);
}

/** Mid-band RT60 (s) of a type at a size. Closet has no tank: its "decay" is the reflections' and the smear's. */
inline double rt60Of (int type, double size) noexcept
{
    switch (std::clamp (type, 0, 3))
    {
        case 0:  return 0.06 * size / 1.4;
        case 1:  return 0.45 * std::pow (size / 5.0, 0.7);
        case 2:  return size;
        default: return 1.7 * std::pow (size / 20.0, 0.7);
    }
}

/** How long the type rings after its input stops, to 150 dB down (where the module may stop working on silence). */
inline double ringSeconds (int type, double size) noexcept
{
    return 2.5 * rt60Of (type, size) + (type == 3 ? 0.2 : 0.06);
}

} // namespace inkcap::dsp::room
