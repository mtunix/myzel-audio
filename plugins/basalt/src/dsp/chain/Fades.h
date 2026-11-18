#pragma once

#include "common/dsp/FloatCompare.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// Copied from Halite's src/dsp/chain/Fades.h (PLAN.md, Shared infrastructure: Halite's sources stay as they are);
// Crossfade::remaining() is Basalt's.

namespace basalt::dsp
{

/**
    A crossfade weight that travels between 0 and 1 along a raised cosine, so neither the gain nor
    its slope steps where a fade starts or ends. It can turn round mid-way. At either end it is
    exactly 0 or 1.
*/
class Crossfade
{
public:
    void setLength (int samples) noexcept { step = 1.0 / std::max (samples, 1); }

    /** Sets where it is and where it goes: 0 or 1. */
    void jumpTo (bool one) noexcept
    {
        phase = one ? 1.0 : 0.0;
        target = phase;
    }

    void moveTo (bool one) noexcept { target = one ? 1.0 : 0.0; }

    bool moving() const noexcept { return ! myzel::dsp::isExactly (phase, target); }
    bool atOne() const noexcept { return myzel::dsp::isExactly (phase, 1.0) && myzel::dsp::isExactly (target, 1.0); }
    bool atZero() const noexcept { return myzel::dsp::isExactly (phase, 0.0) && myzel::dsp::isExactly (target, 0.0); }

    /** Samples of next() until it lands, 0 when it is there. */
    int remaining() const noexcept { return moving() ? (int) std::ceil (std::abs (target - phase) / step) : 0; }

    /** The weight for this sample, then one sample on. */
    float next() noexcept
    {
        const auto w = weight();
        if (phase < target)
            phase = std::min (phase + step, target);
        else if (phase > target)
            phase = std::max (phase - step, target);
        return w;
    }

    float weight() const noexcept
    {
        return phase <= 0.0 ? 0.0f : (phase >= 1.0 ? 1.0f : (float) (0.5 - 0.5 * std::cos (std::numbers::pi * phase)));
    }

private:
    double phase = 0.0, target = 0.0, step = 1.0;
};

/**
    A gain that follows its target through two one-poles in series (critically damped), so the
    slope never steps however often the target moves; it lands on the target exactly once within
    10⁻⁶ of it.
*/
class GainSmoother
{
public:
    void setTime (double ms, double sampleRate) noexcept { coefficient = 1.0 - std::exp (-1.0 / (ms * 0.001 * sampleRate)); }

    void jumpTo (float gain) noexcept { first = second = target = gain; }
    void setTarget (float gain) noexcept { target = gain; }

    bool moving() const noexcept { return ! myzel::dsp::isExactly (second, target) || ! myzel::dsp::isExactly (first, target); }
    float current() const noexcept { return (float) second; }

    float next() noexcept
    {
        if (moving())
        {
            first += (target - first) * coefficient;
            second += (first - second) * coefficient;
            const auto close = 1.0e-6 * std::max (1.0, std::abs (target));
            if (std::abs (target - second) <= close && std::abs (target - first) <= close)
                first = second = target;
        }
        return (float) second;
    }

private:
    double first = 1.0, second = 1.0, target = 1.0, coefficient = 1.0;
};

} // namespace basalt::dsp
