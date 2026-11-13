#pragma once

#include "common/dsp/Interpolators.h"

#include <cmath>
#include <cstdint>

namespace mulberry::dsp
{

using myzel::dsp::MirroredRing;
using myzel::dsp::WindowedSinc;

/**
    A moving medium (tape, drum) for one line: every written sample is stored with the medium
    position at which it was recorded, and heads read where the medium is.

    A head at distance d downstream of the record head finds the sample recorded at position
    P(now) - d: an incremental search (O(1) amortised, the position only grows) and linear
    interpolation of the position between two samples give a fractional sample index, read with
    the windowed sinc. The read index then advances at exactly v(t) / v(t') samples per sample,
    with t' the record instant: the pitch of a head is the ratio of the speeds now and then, to
    the precision of the position, not to first order as a variable read delay would give.

    Positions are stored as a float offset from a double base per 16-sample chunk (sub-nanometre
    over hours of tape, at 4 bytes per sample).
*/
class TapeTrack
{
public:
    static constexpr int chunk = 16;

    /** A head's search state. */
    struct Head
    {
        long long sample = -1;      // last sample found (absolute write count), -1: not yet
        int index = 0;              // its place in the ring
        double p0 = 0.0, p1 = 0.0;  // positions recorded with it and the next sample
    };

    /** Memory: audio (capacity + guard floats), offsets (capacity floats), bases (capacity / chunk). */
    void attach (float* audioMemory, float* offsetMemory, double* baseMemory, int capacity) noexcept
    {
        ring.attach (audioMemory, capacity);
        offsets = offsetMemory;
        bases = baseMemory;
        size = capacity;
        restart();
    }

    void restart() noexcept
    {
        ring.restart();
        written = 0;
        writeIndex = 0;
        newestPosition = -1.0e300;
    }

    long long samplesWritten() const noexcept { return written; }

    void write (float sample, double position) noexcept
    {
        if ((writeIndex & (chunk - 1)) == 0)
            bases[writeIndex / chunk] = position;
        offsets[writeIndex] = (float) (position - bases[writeIndex / chunk]);
        ring.write (sample);
        ++written;
        if (++writeIndex == size)
            writeIndex = 0;

        // The newest position a head may read at (its window needs half a window of newer samples).
        if (written > WindowedSinc::half + 1)
            newestPosition = positionAt (wrap (writeIndex - 1 - WindowedSinc::half));
    }

    /** Position recorded with an absolute sample (must still be in memory). */
    double positionOf (long long sample) const noexcept { return positionAt ((int) (sample % size)); }

    /** The oldest sample a head may still read (the interpolation window needs a margin). */
    long long oldestReadable() const noexcept
    {
        return std::max (0LL, written - (long long) size + MirroredRing::guard + WindowedSinc::taps);
    }

    /**
        Reads at a medium position. Returns 0 where nothing was recorded yet (or it is gone), so a
        head never reads stale memory. fast: nearest tabulated fraction (output-only heads in
        dense patterns).
    */
    /** What a head may read now (the same for every head until the next write). */
    struct Span
    {
        long long newest = 0, oldest = 0;
        double newestPosition = -1.0e300;
    };

    Span span() const noexcept { return { written - 1 - WindowedSinc::half, oldestReadable(), newestPosition }; }

    float read (Head& head, double target, bool fast = false) const noexcept { return read (head, target, fast, span()); }

    float read (Head& head, double target, bool fast, const Span& readable) const noexcept
    {
        if (! (target < readable.newestPosition))
            return 0.0f;

        const auto newest = readable.newest;
        const auto oldest = readable.oldest;
        if (head.sample < oldest || head.sample >= newest)
        {
            head.sample = std::max (oldest, newest - 1);
            head.index = (int) (head.sample % size);
            head.p0 = positionAt (head.index);
            head.p1 = positionAt (next (head.index));
        }

        // The positions of the current pair are kept with the head: a step costs one lookup.
        auto s = head.sample;
        auto i = head.index;
        auto p0 = head.p0, p1 = head.p1;
        while (s + 1 < newest && ! (p1 > target))
        {
            ++s;
            i = next (i);
            p0 = p1;
            p1 = positionAt (next (i));
        }
        while (s > oldest && p0 > target)
        {
            --s;
            i = i == 0 ? size - 1 : i - 1;
            p1 = p0;
            p0 = positionAt (i);
        }
        head.sample = s;
        head.index = i;
        head.p0 = p0;
        head.p1 = p1;

        if (target < p0)
            return 0.0f;
        const auto fraction = p1 > p0 ? std::clamp ((target - p0) / (p1 - p0), 0.0, 1.0) : 0.0;
        return ring.readAt (i, (float) fraction, fast);
    }

    /** The fractional sample index a head would read at (tests). */
    double indexAt (Head& head, double target) const noexcept
    {
        (void) read (head, target);
        const auto p0 = positionOf (head.sample), p1 = positionOf (head.sample + 1);
        return (double) head.sample + (p1 > p0 ? (target - p0) / (p1 - p0) : 0.0);
    }

private:
    double positionAt (int index) const noexcept { return bases[index / chunk] + (double) offsets[index]; }
    int next (int i) const noexcept { return i + 1 == size ? 0 : i + 1; }
    int wrap (int i) const noexcept { return i < 0 ? i + size : (i >= size ? i - size : i); }

    MirroredRing ring;
    float* offsets = nullptr;
    double* bases = nullptr;
    int size = 1;
    int writeIndex = 0;
    long long written = 0;
    double newestPosition = -1.0e300;
};

} // namespace mulberry::dsp
