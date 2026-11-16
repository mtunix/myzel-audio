#pragma once

#include "common/dsp/FloatCompare.h"

#include "dsp/Modes.h"
#include "dsp/detector/Detector.h"
#include "dsp/shapes/Shapes.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace halite::dsp
{

/**
    The detector's reductions on the audio's clock (src/dsp/shapes/NOTES.md, Timing). Each hop's
    shaped reductions are stored by the input hop they belong to, as depths (1 − linear gain), and
    rendered per sample as a uniform quadratic B-spline through the hop values: the gain and its
    slope stay continuous, every sample is a convex mix of three hop values, and three hops at
    exactly 0 give exactly 0.

    Each hop also keeps the stereo mode it was made in: the reductions mean one thing per channel
    in Unlinked and another in Linked, Mid or Side. A lane reads only its own mode's (NOTES.md,
    Crossfades): after its mode's last hop it holds that one, before its first it takes the first,
    and in a track that never had its mode it reads 0.
*/
class ReductionTrack
{
public:
    static constexpr int hopSize = Detector::hopSize;
    static_assert (hopSize == 32, "render() finds the hop by shifting");

    /** Ess for reductions 0 and 1, then Harsh for 0 and 1. */
    static constexpr int numTracks = 4;
    static constexpr int essTrack (int reduction) noexcept { return reduction; }
    static constexpr int harshTrack (int reduction) noexcept { return 2 + reduction; }

    /** A depth this small (−120 dB) counts as none, so the output returns to the input bit for bit after a release. */
    static constexpr double smallestDepth = 1.0e-6;

    /** Allocates room for this many hops (the latency's, a block's and a few more). */
    void prepare (int capacityHops)
    {
        auto size = 1;
        while (size < capacityHops)
            size <<= 1;
        hops.assign ((size_t) size, Depths {});
        targets.assign ((size_t) size, Targets {});
        mask = size - 1;
        reset();
    }

    void reset() noexcept
    {
        std::fill (hops.begin(), hops.end(), Depths {});
        std::fill (targets.begin(), targets.end(), Targets {});
        newest = newestTarget = first = firstTarget = 0;
        any = anyTarget = false;
        runs = {};
        targetRuns = {};
    }

    /** The detector's Ess targets of its newest hop, `hop` (DetectorFrame::position), before the look-ahead moves them. */
    void pushTargets (std::int64_t hop, const DetectorFrame& f, StereoMode mode) noexcept
    {
        if (anyTarget && hop <= newestTarget)
            return;
        Targets t;
        for (int r = 0; r < 2; ++r)
            t[(size_t) r] = f.decisions[(size_t) (r < f.numReductions ? r : 0)].essTargetDb;
        if (anyTarget)
        {
            for (auto h = std::max (newestTarget + 1, hop - mask); h < hop; ++h)
                targets[(size_t) (h & mask)] = targets[(size_t) (newestTarget & mask)];
            targetRuns[(size_t) newestTargetMode].last = hop - 1;
        }
        else
        {
            firstTarget = hop;
        }

        auto& run = targetRuns[(size_t) mode];
        if (! anyTarget || mode != newestTargetMode)
        {
            run.first = hop;
            run.seen = true;
        }
        run.last = hop;
        targets[(size_t) (hop & mask)] = t;
        newestTarget = hop;
        newestTargetMode = mode;
        anyTarget = true;
    }

    /**
        The largest Ess target of reduction r over the hops holding input times [from, to), for a
        lane in `mode`; +∞ if any of them was decided in another mode (nothing of it to learn from).
    */
    float largestTarget (std::int64_t from, std::int64_t to, int reduction, StereoMode mode) const noexcept
    {
        const auto& run = targetRuns[(size_t) mode];
        auto largest = 0.0f;
        for (auto hop = from >> 5; hop <= (to - 1) >> 5; ++hop)
        {
            const auto h = std::min (hop, newestTarget);
            if (anyTarget && h >= firstTarget && ! (run.seen && h >= run.first && h <= run.last))
                return std::numeric_limits<float>::infinity();
            largest = std::max (largest, targets[(size_t) (h & mask)][(size_t) reduction]);
        }
        return largest;
    }

    /**
        One hop of the detector, made in `mode`: its reductions belong to input hop `hop` (samples
        [hop · 32, hop · 32 + 32)). A look-ahead that grows maps several detector hops to one input
        hop (the first one kept); one that shrinks skips input hops, which keep the value before.
    */
    void push (std::int64_t hop, const DetectorFrame& f, StereoMode mode) noexcept
    {
        Depths d;
        for (int r = 0; r < 2; ++r)
        {
            const auto& red = f.reductions[(size_t) (r < f.numReductions ? r : 0)];
            d[(size_t) essTrack (r)] = depthOf (red.essDb);
            d[(size_t) harshTrack (r)] = depthOf (red.harshDb);
        }

        if (any && hop <= newest)
            return;

        if (any)
        {
            const auto held = at (newest);
            for (auto h = std::max (newest + 1, hop - mask); h < hop; ++h)
                slot (h) = held;
            auto& before = runs[(size_t) newestMode];
            before.last = hop - 1;
            before.lastValue = held;
        }
        else
        {
            first = hop;
        }

        auto& run = runs[(size_t) mode];
        if (! any || mode != newestMode)
        {
            run.first = hop;
            run.firstValue = d;
            run.seen = true;
        }
        run.last = hop;
        run.lastValue = d;
        slot (hop) = d;
        newest = hop;
        newestMode = mode;
        any = true;
    }

    /** Depths of one track for samples [start, start + n) of input time (negative before the first hop), as a lane in `mode` reads them. */
    void render (std::int64_t start, int n, int track, float* out, StereoMode mode) const noexcept
    {
        for (int i = 0; i < n;)
        {
            const auto t = start + i;
            const auto hop = t >> 5;
            const auto p0 = valueOf (hop - 2, track, mode), p1 = valueOf (hop - 1, track, mode), p2 = valueOf (hop, track, mode);
            const auto firstSample = (int) (t & (hopSize - 1));
            const auto count = std::min (n - i, hopSize - firstSample);

            if (myzel::dsp::isExactly (p0, 0.0) && myzel::dsp::isExactly (p1, 0.0) && myzel::dsp::isExactly (p2, 0.0))
            {
                std::fill (out + i, out + i + count, 0.0f);
            }
            else
            {
                // ½ (1 − u)² p0 + (½ + u − u²) p1 + ½ u² p2, with u = (j + 1) / 32: the segment ends at
                // (p1 + p2) / 2, where the next one starts.
                const auto c0 = 0.5f * (p0 + p1), c1 = p1 - p0, c2 = 0.5f * (p0 - 2.0f * p1 + p2);
                for (int j = 0; j < count; ++j)
                {
                    const auto u = (float) (firstSample + j + 1) * (1.0f / hopSize);
                    out[i + j] = std::max (0.0f, c0 + u * (c1 + u * c2));
                }
            }
            i += count;
        }
    }

    /** Whether a render from input time `from` on, in `mode`, reads only hops made in it: the detector is in it, and its first hop has passed. */
    bool readsOnly (StereoMode mode, std::int64_t from) const noexcept
    {
        const auto& run = runs[(size_t) mode];
        return any && newestMode == mode && run.seen && (from >> 5) - 2 >= run.first;
    }

    static float depthOf (float reductionDb) noexcept
    {
        const auto d = -std::expm1 (-(double) std::max (reductionDb, 0.0f) * (std::log (10.0) / 20.0));
        return d < smallestDepth ? 0.0f : (float) d;
    }

private:
    using Depths = std::array<float, numTracks>;
    using Targets = std::array<float, 2>;

    /** The newest run of hops made in one stereo mode. */
    struct Run
    {
        std::int64_t first = 0, last = -1;
        Depths firstValue {}, lastValue {};
        bool seen = false;
    };

    struct TargetRun
    {
        std::int64_t first = 0, last = -1;
        bool seen = false;
    };

    Depths& slot (std::int64_t hop) noexcept { return hops[(size_t) (hop & mask)]; }
    // A hop not pushed yet reads as the newest: the chain's latency keeps that from happening.
    const Depths& at (std::int64_t hop) const noexcept { return hops[(size_t) (std::min (hop, newest) & mask)]; }

    float valueOf (std::int64_t hop, int track, StereoMode mode) const noexcept
    {
        hop = std::min (hop, newest);
        if (! any || hop < first)
            return 0.0f;
        const auto& run = runs[(size_t) mode];
        if (! run.seen)
            return 0.0f;
        if (hop > run.last)
            return run.lastValue[(size_t) track];
        if (hop < run.first)
            return run.firstValue[(size_t) track];
        return at (hop)[(size_t) track];
    }

    std::vector<Depths> hops;
    std::vector<Targets> targets;
    std::int64_t mask = 0, newest = 0, newestTarget = 0, first = 0, firstTarget = 0;
    bool any = false, anyTarget = false;
    StereoMode newestMode = StereoMode::linked, newestTargetMode = StereoMode::linked;
    std::array<Run, numStereoModes> runs {};
    std::array<TargetRun, numStereoModes> targetRuns {};
};

/**
    The reductions on the audio's clock across a restart of the analysis (a host transport start or
    jump, PLAN.md, Reduction, Warm start): input times from `boundary` on read the detector that
    started there; earlier ones, the audio still in the delay, the track from before, whose times
    were `shift` later.
*/
class ReductionClock
{
public:
    /** Allocates. */
    void prepare (int capacityHops)
    {
        for (auto& t : tracks)
            t.prepare (capacityHops);
        reset();
    }

    void reset() noexcept
    {
        for (auto& t : tracks)
            t.reset();
        current = 0;
        boundary = std::numeric_limits<std::int64_t>::min();
        shift = 0;
    }

    void restart (std::int64_t newBoundary, std::int64_t newShift) noexcept
    {
        current = 1 - current;
        tracks[(size_t) current].reset();
        boundary = newBoundary;
        shift = newShift;
    }

    /** One hop of the detector, made in `mode`: its targets belong to the hop that ends at `position`, its reductions to the one ending at `reductionPosition`. */
    void push (const DetectorFrame& f, StereoMode mode) noexcept
    {
        auto& t = tracks[(size_t) current];
        t.pushTargets (f.position / Detector::hopSize - 1, f, mode);
        t.push (f.reductionPosition / Detector::hopSize - 1, f, mode);
    }

    void render (std::int64_t start, int n, int track, float* out, StereoMode mode) const noexcept
    {
        const auto before = start >= boundary ? 0 : (int) std::min<std::int64_t> (boundary - start, n);
        if (before > 0)
            previous().render (start + shift, before, track, out, mode);
        if (before < n)
            tracks[(size_t) current].render (start + before, n - before, track, out + before, mode);
    }

    float largestTarget (std::int64_t from, std::int64_t to, int reduction, StereoMode mode) const noexcept
    {
        auto largest = 0.0f;
        if (from < boundary)
            largest = previous().largestTarget (from + shift, std::min (to, boundary) + shift, reduction, mode);
        if (to > boundary)
            largest = std::max (largest, tracks[(size_t) current].largestTarget (std::max (from, boundary), to, reduction, mode));
        return largest;
    }

    /** Whether a render from input time `from` on, in `mode`, reads only hops the detector made in it since it last changed mode. */
    bool readsOnly (StereoMode mode, std::int64_t from) const noexcept
    {
        return from >= boundary && tracks[(size_t) current].readsOnly (mode, from);
    }

    /** Input time `t` on the hop grid of the track it reads: before the restart's boundary, the previous track's. */
    std::int64_t gridTime (std::int64_t t) const noexcept { return t < boundary ? t + shift : t; }

    /** The first input time the current track holds (the lowest int64 if there was no restart). */
    std::int64_t restartBoundary() const noexcept { return boundary; }

private:
    const ReductionTrack& previous() const noexcept { return tracks[(size_t) (1 - current)]; }

    std::array<ReductionTrack, 2> tracks;
    int current = 0;
    std::int64_t boundary = std::numeric_limits<std::int64_t>::min(), shift = 0;
};

/** One lane's view of the clock: the reductions of the stereo mode the lane works in. */
class LaneReductions final : public ReductionReader
{
public:
    void set (const ReductionClock& c, StereoMode m) noexcept
    {
        clock = &c;
        laneMode = m;
    }

    StereoMode mode() const noexcept { return laneMode; }

    void render (std::int64_t start, int n, int track, float* out) const noexcept { clock->render (start, n, track, out, laneMode); }

    float depthAt (std::int64_t time, int track) const noexcept override
    {
        auto d = 0.0f;
        clock->render (time, 1, track, &d, laneMode);
        return d;
    }

    float largestTarget (std::int64_t from, std::int64_t to, int reduction) const noexcept override
    {
        return clock->largestTarget (from, to, reduction, laneMode);
    }

private:
    const ReductionClock* clock = nullptr;
    StereoMode laneMode = StereoMode::linked;
};

} // namespace halite::dsp
