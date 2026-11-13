#pragma once

#include <cmath>
#include <cstdint>

namespace inkcap::dsp
{

/**
    Who draws a random number. Every module and purpose has its own stream, so no draw ever
    depends on how many numbers another part of the plugin took. The values are part of every
    render (a changed number changes the glitches of every saved song): append new streams in
    their module's block, never renumber or reuse one. See src/dsp/common/NOTES.md.
*/
enum class Stream : uint32_t
{
    glitchSlots      = 0x0101,      // does a grid slot fire
    glitchType       = 0x0102,      // Any: which failure an event plays
    glitchShape      = 0x0103,      // an event's length, repeat count, depth

    warbleDrift      = 0x0201,      // Drift and the random part of Wow and Flutter (value noise)
    warbleSpread     = 0x0202,      // the right channel's own wander under Spread

    crushDrift       = 0x0301,      // slow wander of bias and drive (value noise)

    bitsJitter       = 0x0401,      // clock jitter (value noise)

    hissNoise        = 0x0501,      // the noise bed, one draw per sample
    hissCrackle      = 0x0502,      // crackle impulses: when
    hissCrackleShape = 0x0503,      // crackle impulses: size and click shape
    hissRumble       = 0x0504,      // rumble wander (value noise)
    hissWander       = 0x0505,      // slow level, pitch and buzz wander of Tape and Hum (value noise)

    roomModulation   = 0x0601       // delay-line modulation in the tank
};

/**
    Counter-based random numbers: every value is a pure function of (seed, stream, index), a
    SplitMix64 finaliser over the three, not a running generator. Index anything that has a place
    on the timeline (a grid slot, a sample, a lattice point) and the same seed gives the same
    numbers at the same place, whatever was drawn before, from wherever playback started.

    The hash is frozen: tests/common/SeedRngTests.cpp holds golden values from an independent
    Python reference (tests/common/validation/seed_reference.py). No JUCE; real-time safe.
*/
class SeedRng
{
public:
    constexpr SeedRng (uint32_t seed, Stream stream) noexcept
        : streamKey (mix64 (((uint64_t) seed << 32 | (uint64_t) stream) ^ keySalt))
    {
    }

    /** A sub-stream for a second value at the same index (an event's length next to its type). */
    constexpr SeedRng lane (uint32_t laneIndex) const noexcept
    {
        return SeedRng (mix64 (streamKey + laneGamma * ((uint64_t) laneIndex + 1)));
    }

    /** 64 random bits at an index. Negative indices (a pre-roll before bar 1) are as valid as any. */
    constexpr uint64_t bits (int64_t index) const noexcept
    {
        return mix64 (streamKey + gamma * ((uint64_t) index + 1));
    }

    /** Uniform in [0, 1), 53 bits. */
    constexpr double uniform (int64_t index) const noexcept
    {
        return (double) (bits (index) >> 11) * 0x1.0p-53;
    }

    /** Uniform in [-1, 1). */
    constexpr double bipolar (int64_t index) const noexcept
    {
        return 2.0 * uniform (index) - 1.0;
    }

    /** True with the given probability; 0 is never, 1 is always. */
    constexpr bool chance (int64_t index, double probability) const noexcept
    {
        return uniform (index) < probability;
    }

    /** 0 .. n - 1 (multiply-shift on the top 32 bits; the bias is below n / 2^32). */
    constexpr int below (int64_t index, int n) const noexcept
    {
        return n <= 1 ? 0 : (int) (((bits (index) >> 32) * (uint64_t) n) >> 32);
    }

    /**
        Value noise: random lattice values bipolar (k) at the integers, joined by a uniform cubic
        B-spline. C2-continuous (no corners, no steps in slope), always within [-1, 1], RMS about
        0.4. Evaluate it at a timeline position times a rate; computed from the position alone, so
        it is the same for any block size and from any start.
    */
    double smooth (double position) const noexcept
    {
        const auto floored = std::floor (position);
        const auto k = (int64_t) floored;
        const auto f = position - floored;
        const auto f2 = f * f;
        const auto f3 = f2 * f;
        const auto w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
        const auto w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
        const auto w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
        const auto w3 = f3 / 6.0;
        return w0 * bipolar (k - 1) + w1 * bipolar (k) + w2 * bipolar (k + 1) + w3 * bipolar (k + 2);
    }

    /** The lattice value under a position: sample and hold, stepping at the integers. */
    double held (double position) const noexcept
    {
        return bipolar ((int64_t) std::floor (position));
    }

    constexpr uint64_t key() const noexcept { return streamKey; }

    /** SplitMix64's output finaliser (Steele, Lea and Flood 2014): a bijection on 64 bits. */
    static constexpr uint64_t mix64 (uint64_t z) noexcept
    {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    static constexpr uint64_t gamma = 0x9E3779B97F4A7C15ull;        // 2^64 / golden ratio, odd
    static constexpr uint64_t laneGamma = 0xD1B54A32D192ED03ull;    // a second odd constant
    static constexpr uint64_t keySalt = 0x696E6B6361702121ull;      // "inkcap!!": seed 0, stream 0 is not key 0

private:
    explicit constexpr SeedRng (uint64_t key) noexcept : streamKey (key) {}

    uint64_t streamKey;
};

/** The plan's rand (seed, stream, index): 64 random bits. */
constexpr uint64_t rand (uint32_t seed, Stream stream, int64_t index) noexcept
{
    return SeedRng (seed, stream).bits (index);
}

} // namespace inkcap::dsp
