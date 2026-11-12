#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace mulberry::notes
{

/**
    Tempo-synced note values: 1/64 ... 1/1, each as triplet, straight and dotted, ordered by
    length. The index is stored in state (Note / Right Note): never reorder.
*/
inline constexpr int count = 21;

inline constexpr std::array<const char*, count> names {
    "1/64T", "1/64", "1/64D", "1/32T", "1/32", "1/32D", "1/16T", "1/16", "1/16D",
    "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1T", "1/1", "1/1D"
};

inline constexpr int index1_8D = 11;
inline constexpr int index1_4 = 13;

/** Length in quarter notes (beats). */
inline double beats (int index) noexcept
{
    const auto i = std::clamp (index, 0, count - 1);
    const auto straight = 4.0 / (double) (64 >> (i / 3));       // 1/64 = 1/16 beat ... 1/1 = 4 beats
    constexpr std::array<double, 3> kind { 2.0 / 3.0, 1.0, 1.5 };
    return straight * kind[(size_t) (i % 3)];
}

/** Synced length in ms at a tempo, before folding. */
inline double lengthMs (int index, double bpm) noexcept
{
    return 60000.0 / std::clamp (bpm, 20.0, 999.0) * beats (index);
}

/** Synced lengths above the longest Time fold down by octaves until they fit. */
struct Folded
{
    double ms;
    int octaves;    // how many times it was halved (the read-out says so)
};

inline Folded fold (double ms, double maxMs) noexcept
{
    int octaves = 0;
    while (ms > maxMs && octaves < 16)
    {
        ms *= 0.5;
        ++octaves;
    }
    return { ms, octaves };
}

} // namespace mulberry::notes
