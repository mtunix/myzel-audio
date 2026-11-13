#pragma once

#include "common/dsp/Interpolators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace inkcap::dsp
{

/**
    The recent input, for read heads that move through it: Glitch's repeats, stalls and bends,
    Warble's modulated delay. Delay 0 is the sample just written (write a frame, then read it), so
    a head at delay 0 is the input itself, to the bit.

    A fractional read never needs a sample newer than the one just written. The windowed sinc
    (`myzel::dsp::WindowedSinc`, 12 taps) needs 5 newer samples, so it reads delays from 5 on;
    below, a 4-point Hermite (delays 1 to 5) and a linear read (0 to 1) take over, and the sinc
    fades in over delays 5 to 6. Below 5, a read at an integer delay is that sample; the sinc
    stays the sinc there too (its table sits 0.0013 dB below unity, and a jump to the exact
    sample at each integer would tick). A head that starts at the input and falls behind (a
    stall, a bend, a warble that switches on) is click-free from its first sample. at() reads
    whole samples exactly at any delay. Samples older than the last restart() read as 0.

    Up to maxChannels channels. prepare() allocates; everything else is real-time safe.
*/
class CaptureBuffer
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int sincFrom = myzel::dsp::WindowedSinc::half - 1;

    /** Allocates room for reads up to maxDelay samples back. */
    void prepare (int numChannels, int maxDelay)
    {
        channels = std::clamp (numChannels, 1, maxChannels);
        const auto capacity = maxDelay + myzel::dsp::MirroredRing::guard + myzel::dsp::WindowedSinc::taps + 4;
        for (int c = 0; c < maxChannels; ++c)
        {
            memory[(size_t) c].assign ((size_t) (capacity + myzel::dsp::MirroredRing::guard), 0.0f);
            rings[(size_t) c].attach (memory[(size_t) c].data(), capacity);
        }
        longest = maxDelay;

        // The sinc's table is built on first use: here, not in the audio thread's first read.
        myzel::dsp::WindowedSinc::instance();
    }

    /** Forgets the contents: reads give 0 until new samples arrive. */
    void restart() noexcept
    {
        for (auto& r : rings)
            r.restart();
    }

    int numChannels() const noexcept { return channels; }
    int maxDelay() const noexcept { return longest; }

    /** Samples written since the last restart(). */
    long long written() const noexcept { return rings[0].samplesWritten(); }

    /** Writes one frame (one sample per channel); delay 0 is now this frame. */
    void write (const float* frame) noexcept
    {
        for (int c = 0; c < channels; ++c)
            rings[(size_t) c].write (frame[c]);
    }

    void write (int channel, float x) noexcept { rings[(size_t) channel].write (x); }

    /** The sample `delay` frames back (0: the newest). */
    float at (int channel, int delay) const noexcept { return rings[(size_t) channel].at (delay + 1); }

    /** A fractional read, `delay` frames back (0 .. maxDelay). */
    float read (int channel, double delay) const noexcept
    {
        const auto& ring = rings[(size_t) channel];
        delay = std::clamp (delay, 0.0, (double) longest);

        if (delay >= sincFrom + 1)
            return ring.read (delay + 1.0);

        const auto i = (int) delay;
        const auto f = (float) (delay - i);

        if (i == 0)
        {
            const auto a = ring.at (1);
            return a + f * (ring.at (2) - a);
        }

        // 4-point, 3rd-order Hermite (Catmull-Rom) between delays i and i + 1.
        const auto yNewer = ring.at (i);
        const auto y0 = ring.at (i + 1);
        const auto y1 = ring.at (i + 2);
        const auto y2 = ring.at (i + 3);
        const auto c1 = 0.5f * (y1 - yNewer);
        const auto c2 = yNewer - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const auto c3 = 0.5f * (y2 - yNewer) + 1.5f * (y0 - y1);
        const auto hermite = ((c3 * f + c2) * f + c1) * f + y0;

        if (i < sincFrom)
            return hermite;

        return hermite + f * (ring.read (delay + 1.0) - hermite);
    }

private:
    std::array<std::vector<float>, maxChannels> memory;
    std::array<myzel::dsp::MirroredRing, maxChannels> rings;
    int channels = 1, longest = 0;
};

} // namespace inkcap::dsp
