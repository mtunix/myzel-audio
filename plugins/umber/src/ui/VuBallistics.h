#pragma once

#include <algorithm>
#include <cmath>

namespace umber::ui
{

/**
    Moving-coil needle dynamics: a damped second-order system (mass, spring, damping).

    Tuned to the classic VU response: a step reaches 99 % in ~300 ms with ~1.2 % overshoot
    (zeta = 0.79, wn = 13.2 rad/s). Input and output are linear (1.0 = 0 VU).
*/
class VuBallistics
{
public:
    static constexpr float zeta = 0.79f;
    static constexpr float naturalFrequency = 13.2f;   // rad/s

    /** Advances by dt seconds towards target; returns the needle position. */
    float process (float target, float dtSeconds) noexcept
    {
        // Fixed sub-steps keep the integration stable for any frame rate.
        const auto steps = std::clamp ((int) std::ceil (dtSeconds / maxStep), 1, 64);
        const auto h = dtSeconds / (float) steps;
        const auto w2 = naturalFrequency * naturalFrequency;

        for (int i = 0; i < steps; ++i)
        {
            const auto accel = w2 * (target - position) - 2.0f * zeta * naturalFrequency * velocity;
            velocity += accel * h;
            position += velocity * h;
        }

        // The needle stops at the end pins.
        if (position < 0.0f)  { position = 0.0f;  velocity = std::max (0.0f, velocity); }
        if (position > pinMax) { position = pinMax; velocity = std::min (0.0f, velocity); }

        return position;
    }

    void reset (float value = 0.0f) noexcept
    {
        position = value;
        velocity = 0.0f;
    }

    float getPosition() const noexcept { return position; }

    /** Right-hand pin, in linear units (a little past +3 VU). */
    static constexpr float pinMax = 1.55f;

private:
    static constexpr float maxStep = 1.0f / 480.0f;

    float position = 0.0f, velocity = 0.0f;
};

} // namespace umber::ui
