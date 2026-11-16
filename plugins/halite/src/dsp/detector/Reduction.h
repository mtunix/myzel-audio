#pragma once

#include "dsp/common/EventRing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace halite::dsp
{

inline constexpr float kneeDb = 3.0f;

/**
    dB of reduction for a level `overDb` above the threshold: an infinite ratio through a soft
    knee (Giannoulis, Massberg, Reiss 2012, with the ratio taken to infinity), never more than
    range. Above knee/2 the level is brought down to the threshold exactly.
*/
inline float reductionDb (float overDb, float rangeDb) noexcept
{
    constexpr auto half = 0.5f * kneeDb;
    float r;
    if (overDb <= -half)
        r = 0.0f;
    else if (overDb >= half)
        r = overDb;
    else
        r = (overDb + half) * (overDb + half) / (2.0f * kneeDb);
    return std::clamp (r, 0.0f, std::max (rangeDb, 0.0f));
}

/** How a reduction lets go: a fixed time constant, or Auto, which follows how long it was over. */
struct ReleaseRule
{
    bool automatic = true;
    float fixedMs = 20.0f;
    float autoMinMs = 15.0f, autoMaxMs = 30.0f;
    float autoShare = 0.25f;    // Auto: the release is this share of the time spent over, clamped
};

/**
    Turns per-hop reduction targets into the reduction to apply, in dB, one hop at a time.

    Look-ahead (Normal latency): the output belongs to the hop `lookaheadHops` before the newest,
    and is the largest target in the window ahead of it, each weighted down linearly with its
    distance, so the reduction ramps in over the look-ahead and is complete when the target
    arrives. Without look-ahead (Live) the target applies at once: the level detectors' own attack
    is the attack. Release: a one-pole in dB.
*/
class ReductionShaper
{
public:
    /** Allocates. */
    void prepare (int maxLookaheadHops)
    {
        int size = 1;
        while (size < maxLookaheadHops + 1)
            size <<= 1;
        targets.assign ((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset() noexcept
    {
        std::fill (targets.begin(), targets.end(), 0.0f);
        newest = 0;
        applied = 0.0f;
        overHops = 0;
        releaseMs = 20.0f;
    }

    float push (float targetDb, int lookaheadHops, const ReleaseRule& rule, double hopSeconds) noexcept
    {
        lookaheadHops = std::clamp (lookaheadHops, 0, mask);
        newest = (newest + 1) & mask;
        targets[(size_t) newest] = targetDb;

        // Auto latches the release from the last time over; a fixed release applies at once, also
        // to a reduction that is already letting go.
        if (targetDb > 0.0f)
        {
            ++overHops;
            if (rule.automatic)
                releaseMs = std::clamp (rule.autoShare * (float) (overHops * hopSeconds * 1000.0), rule.autoMinMs, rule.autoMaxMs);
        }
        else
        {
            overHops = 0;
        }
        if (! rule.automatic)
            releaseMs = rule.fixedMs;

        auto ramped = 0.0f;
        for (int j = 0; j <= lookaheadHops; ++j)
        {
            const auto weight = 1.0f - (float) j / (float) (lookaheadHops + 1);
            ramped = std::max (ramped, weight * targets[(size_t) ((newest - lookaheadHops + j) & mask)]);
        }

        if (ramped >= applied)
            applied = ramped;
        else
            applied += (ramped - applied) * (float) (1.0 - std::exp (-hopSeconds * 1000.0 / std::max (releaseMs, 0.1f)));

        return applied;
    }

    float get() const noexcept { return applied; }

private:
    std::vector<float> targets;
    int mask = 0, newest = 0;
    float applied = 0.0f;
    std::int64_t overHops = 0;
    float releaseMs = 20.0f;
};

/** A fixed delay of per-hop values, so detector-time values line up with shaped reductions. */
class HopDelay
{
public:
    /** Allocates. Before anything is pushed it delivers `initial`. */
    void prepare (int maxDelayHops, float initialValue)
    {
        int size = 1;
        while (size < maxDelayHops + 1)
            size <<= 1;
        initial = initialValue;
        values.assign ((size_t) size, initial);
        mask = size - 1;
        newest = 0;
    }

    void reset() noexcept
    {
        std::fill (values.begin(), values.end(), initial);
        newest = 0;
    }

    float push (float value, int delayHops) noexcept
    {
        newest = (newest + 1) & mask;
        values[(size_t) newest] = value;
        return values[(size_t) ((newest - std::clamp (delayHops, 0, mask)) & mask)];
    }

private:
    std::vector<float> values;
    float initial = 0.0f;
    int mask = 0, newest = 0;
};

/**
    Events (PLAN.md, Detection 6): an ess starts when the reduction exceeds 0.5 dB and ends once
    it has stayed at or below that for the hangover; a return within the hangover continues it.
*/
class EventTracker
{
public:
    static constexpr float startDb = 0.5f;

    void reset() noexcept
    {
        isActive = false;
        quietHops = 0;
    }

    /**
        One hop of `hopSize` samples ending at `hopEnd`. Returns true when an event ended on this
        hop, with it in `finished`.
    */
    bool push (float reductionDb, float sibilanceDb, std::int64_t hopEnd, int hopSize, int hangoverHops, EssEvent& finished) noexcept
    {
        const auto over = reductionDb > startDb;

        if (! isActive)
        {
            if (! over)
                return false;

            isActive = true;
            quietHops = 0;
            current = {};
            current.start = hopEnd - hopSize;
            current.peakReductionDb = reductionDb;
            current.peakSibilanceDb = sibilanceDb;
            return false;
        }

        current.peakReductionDb = std::max (current.peakReductionDb, reductionDb);
        current.peakSibilanceDb = std::max (current.peakSibilanceDb, sibilanceDb);

        if (over)
        {
            quietHops = 0;
            return false;
        }

        if (++quietHops < std::max (hangoverHops, 1))
            return false;

        // The hangover counts from the start of the first quiet hop, so it ends with this one.
        current.end = hopEnd;
        isActive = false;
        quietHops = 0;
        finished = current;
        return true;
    }

    bool active() const noexcept { return isActive; }

private:
    EssEvent current;
    bool isActive = false;
    int quietHops = 0;
};

} // namespace halite::dsp
