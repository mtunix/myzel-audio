#pragma once

#include "dsp/Algorithms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace cerulean
{

/** Param A as an exponential time: 0..1 -> loMs .. loMs * ratio. */
struct TimeMapping
{
    float loMs;
    float ratio;

    float forward (float a01) const noexcept { return loMs * std::pow (ratio, std::clamp (a01, 0.0f, 1.0f)); }

    /** ms > 0; times outside the range clamp to 0 or 1. */
    float inverse (float ms) const noexcept { return std::clamp (std::log (ms / loMs) / std::log (ratio), 0.0f, 1.0f); }
};

/**
    What an algorithm is, for everything outside its engine: the chain, the processor, parameter
    text, the UI and the tests. Header-only and JUCE-free, so the engines and tools can use it.
*/
struct AlgorithmInfo
{
    Algorithm algorithm = Algorithm::room;
    const char* name = "";      // display name, also the Algorithm choice parameter's strings
    const char* key = "";       // lower case: golden file and CeruleanMeasure names

    /**
        Wet gain = sizeScale^k, so Size changes the space, not the level: a feedback network's
        steady-state energy goes as 1 / loop length at a fixed RT60. Fitted from steady pink noise
        at Decay 2 s (the "Size changes the space" chain test). Room and Spring already scale their
        input by sqrt (Size) inside the engine; their values are small trims on top of that.
    */
    float sizeLevelExponent = 0.5f;

    float extraTailSeconds = 0.0f;  // added to the processor's tail length (echo repeats, blooms)

    int paramASteps = 0;            // discrete positions of Param A / B; 0 = continuous
    int paramBSteps = 0;

    std::optional<TimeMapping> paramATime {};  // Param A is a time (Rise, Billow, Echo)
};

inline constexpr std::array<AlgorithmInfo, numAlgorithms> algorithmInfos { {
    { .algorithm = Algorithm::room,   .name = "Room",   .key = "room",   .sizeLevelExponent = 0.12f },
    { .algorithm = Algorithm::hall,   .name = "Hall",   .key = "hall",   .sizeLevelExponent = 0.50f },
    { .algorithm = Algorithm::plate,  .name = "Plate",  .key = "plate",  .sizeLevelExponent = 0.49f },
    { .algorithm = Algorithm::spring, .name = "Spring", .key = "spring", .sizeLevelExponent = -0.12f,
      .paramASteps = 3 },
    { .algorithm = Algorithm::nimbus, .name = "Nimbus", .key = "nimbus", .sizeLevelExponent = 0.76f },
    { .algorithm = Algorithm::halo,   .name = "Halo",   .key = "halo",   .sizeLevelExponent = 0.57f,
      .paramASteps = 5 },
    { .algorithm = Algorithm::rise,   .name = "Rise",   .key = "rise",   .sizeLevelExponent = 0.61f,
      .paramATime = TimeMapping { 50.0f, 80.0f } },
    { .algorithm = Algorithm::billow, .name = "Billow", .key = "billow", .sizeLevelExponent = 1.16f,
      .extraTailSeconds = 6.0f, .paramATime = TimeMapping { 150.0f, 4000.0f / 150.0f } },
    { .algorithm = Algorithm::choir,  .name = "Choir",  .key = "choir",  .sizeLevelExponent = 0.59f,
      .paramASteps = 5, .paramBSteps = 4 },
    { .algorithm = Algorithm::echo,   .name = "Echo",   .key = "echo",   .sizeLevelExponent = 0.44f,
      .extraTailSeconds = 6.0f, .paramATime = TimeMapping { 20.0f, 75.0f } },
} };

static_assert ([]
{
    for (int i = 0; i < numAlgorithms; ++i)
        if (static_cast<int> (algorithmInfos[(size_t) i].algorithm) != i)
            return false;
    return true;
}(), "algorithmInfos lists every Algorithm once, in enum order");

constexpr const AlgorithmInfo& info (Algorithm a) noexcept
{
    return algorithmInfos[(size_t) std::clamp (static_cast<int> (a), 0, numAlgorithms - 1)];
}

} // namespace cerulean
