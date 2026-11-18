#pragma once

#include <algorithm>
#include <vector>

// Copied from Halite's src/dsp/chain/DelayLine.h (PLAN.md, Shared infrastructure: Halite's sources stay as they are).

namespace basalt::dsp
{

/** A delay of up to its capacity, read for the samples just written. Samples pass as copies, never through arithmetic. */
class DelayLine
{
public:
    /** Allocates. */
    void prepare (int maxDelay, int maxBlock)
    {
        auto size = 1;
        while (size < maxDelay + maxBlock + 1)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        written = 0;
    }

    /** Writes n samples, then reads the same n samples `delay` earlier into out. */
    void process (const float* in, float* out, int n, int delay) noexcept
    {
        const auto start = written;
        for (int i = 0; i < n; ++i)
            buffer[(size_t) ((start + i) & mask)] = in[i];
        for (int i = 0; i < n; ++i)
            out[i] = buffer[(size_t) ((start + i - delay) & mask)];
        written += n;
    }

private:
    std::vector<float> buffer;
    long long written = 0;
    int mask = 0;
};

} // namespace basalt::dsp
