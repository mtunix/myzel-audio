#pragma once

#include <algorithm>
#include <vector>

namespace halite::dsp
{

/** A delay of up to its capacity, read for the samples just written. */
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

    /** The `count` samples written before the newest `skip`, oldest first. count + skip ≤ maxDelay + maxBlock. */
    void copyRecent (float* out, int count, int skip) const noexcept
    {
        const auto start = written - skip - count;
        for (int i = 0; i < count; ++i)
            out[i] = buffer[(size_t) ((start + i) & mask)];
    }

private:
    std::vector<float> buffer;
    long long written = 0;
    int mask = 0;
};

} // namespace halite::dsp
