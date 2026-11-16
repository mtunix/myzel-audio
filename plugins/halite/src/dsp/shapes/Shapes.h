#pragma once

#include "dsp/shapes/split/SplitFilter.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace halite::dsp
{

// Each band's cut is designed down to the top of its Range (src/plugin/Parameters.h; splitDesign::ess (), harsh ()).
inline constexpr double essDeepestCutDb = 24.0, harshDeepestCutDb = 12.0;

// The largest input sample the shapes act on as it is (+120 dBFS); the float frames would overflow only near 10³⁰.
inline constexpr float inputLimit = 1.0e6f;

/** Where the two bands act, in Hz, as the detectors measure them (Detector::bandsFor). */
struct ShapeBands
{
    SplitFilter::Kind essKind = SplitFilter::Kind::bell;
    double essLow = 4500.0, essHigh = 12000.0;
    double harshLow = 2262.7, harshHigh = 4525.5;
};

/**
    Where Split's design segments lie for one call (split/NOTES.md, Modulation): on the hop grid of
    the reductions each sample reads. A transport restart moves the grid: the audio still in flight
    keeps the previous analysis's grid and its last segment ends where the restarted one begins.
*/
struct DesignGrid
{
    int phase = 0;                              // the first sample's place on the grid
    int restartAt = SplitFilter::noRestart;     // the first sample on the restarted grid, counted from the first (maybe past the call)
    int phaseAfter = 0;                         // its place there
};

/**
    The detector's reductions by input time, as depths (1 − linear gain), for a shape that needs
    them away from the samples it is handed (Spectral reads them at its frames' centres). Tracks:
    Ess for reductions 0 and 1, then Harsh for 0 and 1 (ReductionTrack).
*/
struct ReductionReader
{
    virtual ~ReductionReader() = default;

    /** The depth the chain renders for one sample of input time. */
    virtual float depthAt (std::int64_t time, int track) const noexcept = 0;

    /** The detector's largest Ess target (dB, before its ballistics) for reduction r over the hops holding input times [from, to). */
    virtual float largestTarget (std::int64_t from, std::int64_t to, int reduction) const noexcept = 0;
};

/**
    One signal (a channel, the mid or the side) or a stereo pair through the shapes that act in
    the time domain (PLAN.md, Reduction). Each takes its input, the Ess and Harsh depths per sample
    (1 − gain, lined up with the input, and SplitFilter::lookahead more of them) and returns what
    each band removes: the Ess band first, the Harsh bell on what it leaves, so removed =
    x − harsh (ess (x)) and both parts are exactly 0 where their depth is.
*/
class TimeDomainShape
{
public:
    void prepare (double sampleRate) noexcept
    {
        essFilter.prepare (sampleRate);
        harshFilter.prepare (sampleRate);
        harshRunning = false;
    }

    void reset() noexcept
    {
        essFilter.reset();
        harshFilter.reset();
        harshRunning = false;
    }

    void setBands (const ShapeBands& b) noexcept
    {
        essFilter.setBand (b.essKind, b.essLow, b.essHigh);
        harshFilter.setBand (SplitFilter::Kind::bell, b.harshLow, b.harshHigh);
    }

    /**
        `channels` signals (1 or 2) with `paths` sets of depths: one for both (Linked) or one each
        (Unlinked). `wide`: the Ess band is the whole signal (removed = d x), else the Split filter.
        `harsh`: whether the Harsh bell runs; it starts from rest whenever it starts again.
    */
    void process (const float* const* x, const float* const* essDepth, const float* const* harshDepth, float* const* essRemoved,
                  float* const* harshRemoved, int channels, int paths, int n, const DesignGrid& grid, bool wide, bool harsh) noexcept
    {
        if (wide)
        {
            for (int c = 0; c < channels; ++c)
            {
                const auto* d = essDepth[std::min (c, paths - 1)];
                for (int i = 0; i < n; ++i)
                    essRemoved[c][i] = d[i] * x[c][i];
            }
        }
        else
        {
            run (essFilter, channels, paths, n, grid, essDepth, essRemoved, [&] (int c, int i) { return (double) x[c][i]; });
        }

        if (harsh && ! harshRunning)
            harshFilter.reset();
        harshRunning = harsh;

        if (harsh)
            run (harshFilter, channels, paths, n, grid, harshDepth, harshRemoved,
                 [&] (int c, int i) { return (double) x[c][i] - (double) essRemoved[c][i]; });
        else
            for (int c = 0; c < channels; ++c)
                std::fill (harshRemoved[c], harshRemoved[c] + n, 0.0f);
    }

private:
    static constexpr int piece = 64;

    /** One filter over the call in pieces, the grid restarting where `grid` says. */
    template <typename Input>
    static void run (SplitFilter& filter, int channels, int paths, int n, const DesignGrid& grid, const float* const* depth, float* const* removed,
                     Input input) noexcept
    {
        std::array<std::array<double, piece>, SplitFilter::maxChannels> in, out;
        const std::array<const double*, SplitFilter::maxChannels> inputs { in[0].data(), in[1].data() };
        const std::array<double*, SplitFilter::maxChannels> outputs { out[0].data(), out[1].data() };

        const auto part = [&] (int from, int to, int phase, int limit)
        {
            for (auto done = from; done < to; done += piece)
            {
                const auto count = std::min (piece, to - done);
                for (int c = 0; c < channels; ++c)
                    for (int i = 0; i < count; ++i)
                        in[(size_t) c][(size_t) i] = input (c, done + i);
                const std::array<const float*, SplitFilter::maxChannels> depths { depth[0] + done, depth[paths - 1] + done };
                filter.process (inputs.data(), outputs.data(), channels, depths.data(), paths, count, phase + (done - from), limit - (done - from));
                for (int c = 0; c < channels; ++c)
                    for (int i = 0; i < count; ++i)
                        removed[c][done + i] = (float) out[(size_t) c][(size_t) i];
            }
        };

        if (grid.restartAt < n)
        {
            part (0, grid.restartAt, grid.phase, grid.restartAt);
            part (grid.restartAt, n, grid.phaseAfter, SplitFilter::noRestart);
        }
        else
        {
            part (0, n, grid.phase, grid.restartAt);
        }
    }

    SplitFilter essFilter { splitDesign::ess() }, harshFilter { splitDesign::harsh() };
    bool harshRunning = false;
};

} // namespace halite::dsp
