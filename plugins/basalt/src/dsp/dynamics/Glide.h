#pragma once

#include "common/dsp/FloatCompare.h"

#include <algorithm>
#include <cmath>

// Values that move to a new setting over a set time instead of jumping (NOTES.md, Clicks).

namespace basalt::dynamics
{

/** A value that glides linearly to its target over a set time, restarting from where it is when the target moves. */
class LinearGlide
{
public:
    void prepare (double ms, double sampleRate) noexcept { steps = std::max (1, (int) std::lround (ms * 0.001 * sampleRate)); }

    void jumpTo (double v) noexcept
    {
        value = target = v;
        remaining = 0;
    }

    void setTarget (double v) noexcept
    {
        if (myzel::dsp::isExactly (v, target))
            return;

        target = v;
        remaining = steps;
        delta = (target - value) / steps;
    }

    double next() noexcept
    {
        if (remaining > 0)
            value = --remaining == 0 ? target : value + delta;
        return value;
    }

    double current() const noexcept { return value; }
    bool moving() const noexcept { return remaining > 0; }

private:
    double value = 0.0, target = 0.0, delta = 0.0;
    int steps = 1, remaining = 0;
};

/**
    A value that glides to its target over a set time on a cubic whose slope is 0 where it lands
    and, on a new target mid-way, continues the slope it has: neither the value nor its slope
    steps, however often the target moves. From rest it is the smoothstep 3u² − 2u³. It lands on
    the target exactly.
*/
class SmoothGlide
{
public:
    void prepare (double ms, double sampleRate) noexcept
    {
        const auto settled = ! moving();
        steps = std::max (1, (int) std::lround (ms * 0.001 * sampleRate));
        position = settled ? steps : std::min (position, steps);
    }

    void jumpTo (double v) noexcept
    {
        value = start = target = v;
        startSlope = 0.0;
        position = steps;
    }

    void setTarget (double v) noexcept
    {
        if (myzel::dsp::isExactly (v, target))
            return;

        startSlope = moving() ? slope() : 0.0;
        start = value;
        target = v;
        position = 0;
    }

    /** One sample on; the value for this sample. */
    double next() noexcept
    {
        if (position < steps)
        {
            if (++position == steps)
            {
                value = target;
            }
            else
            {
                const auto u = (double) position / steps;
                value = start + (target - start) * u * u * (3.0 - 2.0 * u) + startSlope * steps * u * (1.0 - u) * (1.0 - u);
            }
        }

        return value;
    }

    double current() const noexcept { return value; }
    double targetValue() const noexcept { return target; }
    bool moving() const noexcept { return position < steps; }

private:
    /** The slope per sample where it is now. */
    double slope() const noexcept
    {
        const auto u = (double) position / steps;
        return (target - start) * 6.0 * u * (1.0 - u) / steps + startSlope * (1.0 - u) * (1.0 - 3.0 * u);
    }

    double value = 0.0, start = 0.0, target = 0.0, startSlope = 0.0;
    int steps = 1, position = 1;
};

} // namespace basalt::dynamics
