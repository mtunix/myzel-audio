#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace basalt::dynamics
{

/**
    The maximum of the last `length` values pushed: the look-ahead window and the feedback
    pre-hold (NOTES.md, Look-ahead). Lemire's monotonic wedge (2006), under three comparisons per
    sample amortised, in a ring sized once in prepare().

    The window shrinks at once but grows by at most one sample per push. Growing faster would
    have to bring back values the wedge already dropped as expired; at one per push the expiry
    point never moves backwards, so the result is exact for any length sequence.
*/
class RunningMax
{
public:
    /** Not real-time safe. */
    void prepare (int maximumLength)
    {
        maxLength = std::max (maximumLength, 1);
        size_t capacity = 1;
        while (capacity < (size_t) maxLength + 1)
            capacity <<= 1;

        mask = capacity - 1;
        ring.assign (capacity, {});
        reset();
    }

    void reset() noexcept
    {
        head = tail = 0;
        now = 0;
        length = target = 1;
    }

    /** The window in samples, 1 … the prepared maximum; 1 passes each value straight through. */
    void setLength (int samples) noexcept { target = std::clamp (samples, 1, maxLength); }

    int getLength() const noexcept { return length; }

    /** Adds v; the maximum over the window that now ends with it. */
    double push (double v) noexcept
    {
        const auto stamp = ++now;
        length = target < length ? target : std::min (target, length + 1);

        auto* entries = ring.data();
        auto t = tail;
        while (t != head && entries[(t - 1) & mask].value <= v)
            --t;

        entries[t & mask] = { v, stamp };
        tail = t + 1;

        const auto expired = stamp - length;
        auto h = head;
        while (entries[h & mask].stamp <= expired)
            ++h;
        head = h;

        return entries[h & mask].value;
    }

private:
    struct Entry
    {
        double value = 0.0;
        std::int64_t stamp = 0;
    };

    std::vector<Entry> ring;
    std::uint64_t head = 0, tail = 0, mask = 0;
    std::int64_t now = 0;
    int maxLength = 1, length = 1, target = 1;
};

} // namespace basalt::dynamics
