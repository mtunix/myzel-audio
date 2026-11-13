#pragma once

#include "common/dsp/FloatCompare.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace inkcap::dsp
{

/**
    The fade shape of every crossfade and gain move in Inkcap: h (u) = u - sin (2 pi u) / (2 pi),
    0 -> 1 with zero slope and zero curvature at both ends (C2). A linear ramp has a corner at
    each end that the 3rd-difference click gate sees at -35 dB on a full-scale sine; this shape
    keeps a 2 ms fade below -70 dB. Exact 0 and 1 outside [0, 1].
*/
inline double fadeShape (double u) noexcept
{
    if (u <= 0.0)
        return 0.0;
    if (u >= 1.0)
        return 1.0;
    return u - std::sin (2.0 * std::numbers::pi * u) / (2.0 * std::numbers::pi);
}

struct FadeGains
{
    float out = 1.0f;   // the source being left
    float in = 0.0f;    // the source being entered
};

/** Equal gain (out + in == 1): for correlated sources, a signal against a coloured copy of itself. */
inline FadeGains equalGain (double u) noexcept
{
    const auto h = fadeShape (u);
    return { (float) (1.0 - h), (float) h };
}

/** Equal power (out^2 + in^2 == 1): for unrelated sources, a jump in time. Exact at both ends. */
inline FadeGains equalPower (double u) noexcept
{
    if (u <= 0.0)
        return { 1.0f, 0.0f };
    if (u >= 1.0)
        return { 0.0f, 1.0f };
    const auto a = 0.5 * std::numbers::pi * fadeShape (u);
    return { (float) std::cos (a), (float) std::sin (a) };
}

/**
    A value that moves to a new target over a fixed time along fadeShape, and arrives exactly (so
    a gain of 1 or a fade of 0 stays bit-exact). A new target while moving starts a new move from
    where it is. Per sample, so a render is the same for any block size.
*/
class Fade
{
public:
    void setDuration (double sampleRate, double seconds) noexcept
    {
        duration = std::max (1, (int) std::lround (sampleRate * seconds));
    }

    void jumpTo (float target) noexcept
    {
        value = start = goal = target;
        position = duration;
    }

    void moveTo (float target) noexcept
    {
        if (myzel::dsp::isExactly (target, goal))
            return;

        start = value;
        goal = target;
        position = 0;
    }

    float next() noexcept
    {
        if (position < duration)
        {
            ++position;
            value = position == duration ? goal
                                         : start + (goal - start) * (float) fadeShape ((double) position / (double) duration);
        }
        return value;
    }

    float current() const noexcept { return value; }
    float target() const noexcept { return goal; }
    bool isMoving() const noexcept { return position < duration; }

    /** At rest on exactly this value. */
    bool isAt (float v) const noexcept { return ! isMoving() && myzel::dsp::isExactly (value, v); }

private:
    float value = 0.0f, start = 0.0f, goal = 0.0f;
    int position = 1, duration = 1;
};

} // namespace inkcap::dsp
