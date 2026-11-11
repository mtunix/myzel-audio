#pragma once

#include "dsp/Algorithms.h"
#include "dsp/Mappings.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cerulean::ui
{

/**
    The decay envelope the tail display draws, computed from the parameters alone (no audio).

    It is a picture of the settings, not a measurement: pre-delay gap, an early-reflection cluster
    spaced by Size, then straight-line (in dB) decays of the low, mid and high bands from Decay,
    Low End and Tone, with a few per-algorithm shapes (Rise swells, Billow blooms, Echo repeats,
    Halo climbs). Frozen, every band is a flat line. Everything is cheap and allocation-free.
*/
struct TailShape
{
    Algorithm algorithm = Algorithm::hall;
    float decaySeconds = 3.0f;
    float preDelayMs = 20.0f;
    float sizeScale = 1.0f;
    float lowEnd = 1.0f;
    float tone = 0.0f;          // -1 .. +1
    float paramA = 0.5f, paramB = 0.5f;
    float echoTimeMs = 300.0f;
    float diffusion = 0.75f;
    bool freeze = false;
    bool layering = false;      // frozen with Freeze Layer on: the playing adds onto the pad
    bool bypassed = false;      // Bypass is on: the displays dim and say so
    bool trailsRinging = false; // ... and Trails lets the old tail ring out

    bool operator== (const TailShape&) const = default;
};

namespace tail
{
    inline constexpr float floorDb = -66.0f;     // bottom of the display
    inline constexpr float tailStartDb = -6.0f;  // where the dense tail starts
    inline constexpr int numEarly = 7;

    enum class Band { low, mid, high };

    inline float bandRt60 (const TailShape& s, Band b) noexcept
    {
        const auto mid = std::max (0.05f, s.decaySeconds);

        switch (b)
        {
            case Band::low:  return mid * std::clamp (s.lowEnd, 0.25f, 4.0f);
            case Band::high: return mid * dsp::maps::toneToHighDecay (s.tone).ratio;
            case Band::mid:  break;
        }

        return mid;
    }

    /** Time (s) the dense tail takes to build up after the pre-delay: bigger and less diffuse is slower. */
    inline float buildSeconds (const TailShape& s) noexcept
    {
        auto base = 0.018f + 0.05f * s.sizeScale * (1.25f - 0.5f * s.diffusion);

        if (s.algorithm == Algorithm::plate)
            base *= 0.45f;
        else if (s.algorithm == Algorithm::room)
            base *= 0.7f;
        else if (s.algorithm == Algorithm::nimbus)
            base *= 2.2f;

        return base;
    }

    /** Envelope of one band at time t (s) after the dry hit, in dB (floorDb = silence). */
    inline float levelDb (const TailShape& s, Band band, float t) noexcept
    {
        const auto t0 = s.preDelayMs * 0.001f;

        if (t < t0)
            return floorDb;

        const auto dt = t - t0;

        if (s.freeze)
            return tailStartDb;

        const auto rt = bandRt60 (s, band);
        auto db = tailStartDb - 60.0f * dt / rt;

        // Build-up of echo density: a smooth rise from about -30 dB.
        const auto build = buildSeconds (s);
        const auto b = std::clamp (dt / build, 0.0f, 1.0f);
        const auto smooth = b * b * (3.0f - 2.0f * b);
        db += -24.0f * (1.0f - smooth);

        switch (s.algorithm)
        {
            case Algorithm::rise:
            {
                // Volume swell: the tail fades in over the rise time.
                const auto riseS = dsp::maps::riseTimeMs (s.paramA) * 0.001f;
                const auto x = std::clamp (dt / riseS, 0.0f, 1.0f);
                db += 20.0f * std::log10 (std::max (1.0e-3f, x * x * (3.0f - 2.0f * x)));
                break;
            }

            case Algorithm::billow:
            {
                // Bloom: energy keeps building after the note, peaking at the bloom length.
                const auto bloomS = dsp::maps::bloomLengthMs (s.paramA) * 0.001f;
                const auto x = dt / std::max (0.05f, bloomS);
                const auto bloom = 1.0f - std::exp (-3.0f * x);
                db += 20.0f * std::log10 (std::max (1.0e-3f, 0.25f + 0.75f * bloom)) + 60.0f * dt / rt * std::exp (-x) * 0.35f * s.paramB;
                break;
            }

            case Algorithm::halo:
            {
                // Shimmer: each pass climbs, which holds the tail up before it falls away.
                const auto lift = 20.0f * s.paramB * (1.0f - std::exp (-dt / (0.2f * rt)));
                db += lift * std::exp (-dt / (0.9f * rt));
                break;
            }

            case Algorithm::room:
            case Algorithm::hall:
            case Algorithm::plate:
            case Algorithm::spring:
            case Algorithm::nimbus:
            case Algorithm::choir:
            case Algorithm::echo:
                break;
        }

        return std::max (floorDb, db);
    }

    /** One early reflection (or echo repeat): time after the hit (s) and its level (dB). */
    struct Spike
    {
        float timeS = 0.0f, levelDb = floorDb;
    };

    /** Early reflections spaced by Size; Echo shows its repeats instead (up to 12). */
    template <size_t N>
    inline int spikes (const TailShape& s, std::array<Spike, N>& out) noexcept
    {
        const auto t0 = s.preDelayMs * 0.001f;
        int count = 0;

        if (s.algorithm == Algorithm::echo)
        {
            const auto period = std::max (0.01f, s.echoTimeMs * 0.001f);
            const auto feedback = 0.15f + 0.6f * s.paramB;   // illustrative repeat decay
            auto level = -2.0f;

            for (size_t i = 0; i < N && level > floorDb + 6.0f; ++i)
            {
                out[i] = { t0 + period * (float) (i + 1), level };
                level += 20.0f * std::log10 (feedback);
                ++count;
            }

            return count;
        }

        // Early-reflection pattern (ms at nominal size) and levels.
        constexpr std::array<float, numEarly> ms { 6.0f, 11.0f, 17.0f, 24.0f, 33.0f, 43.0f, 56.0f };
        constexpr std::array<float, numEarly> db { -2.0f, -5.0f, -3.5f, -7.0f, -8.0f, -11.0f, -13.0f };

        // Room: Early/Late (Param A) moves the balance; Spring drips instead of a cluster.
        const auto earlyGain = s.algorithm == Algorithm::room ? 6.0f * (0.5f - s.paramA)
                             : s.algorithm == Algorithm::plate ? -6.0f
                             : s.algorithm == Algorithm::nimbus ? -9.0f : 0.0f;

        for (size_t i = 0; i < std::min (N, (size_t) numEarly); ++i)
        {
            auto t = ms[i] * 0.001f * s.sizeScale;

            if (s.algorithm == Algorithm::spring)
                t = 0.028f * s.sizeScale * (float) (i + 1);

            out[i] = { t0 + t, std::min (0.0f, db[i] + earlyGain) };
            ++count;
        }

        return count;
    }

    /** Display span (s): long enough for the mid band to reach the floor at about 85 % of the width. */
    inline float spanSeconds (const TailShape& s) noexcept
    {
        constexpr std::array<float, 15> nice { 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 15.0f, 20.0f, 30.0f, 45.0f, 60.0f };
        auto needed = (s.preDelayMs * 0.001f + std::max (0.05f, s.decaySeconds) * (-floorDb + tailStartDb) / 60.0f) / 0.85f;

        if (s.algorithm == Algorithm::rise)
            needed += dsp::maps::riseTimeMs (s.paramA) * 0.001f * 0.5f;

        for (const auto v : nice)
            if (v >= needed)
                return v;

        return nice.back();
    }
}

} // namespace cerulean::ui
