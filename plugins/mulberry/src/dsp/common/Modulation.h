#pragma once

#include "common/dsp/Blocks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mulberry::dsp
{

/**
    Wobble Shape: how much of a machine's instability is random rather than periodic. 0.5 keeps
    the machine's own mix (nominalRandom), 0 is periodic only, 1 random only.
*/
inline float randomShare (float shape01, float nominalRandom) noexcept
{
    const auto s = std::clamp (shape01, 0.0f, 1.0f);
    return s <= 0.5f ? nominalRandom * (s * 2.0f) : nominalRandom + (1.0f - nominalRandom) * (s - 0.5f) * 2.0f;
}

/** Scales of the periodic and random parts so their sum keeps the machine's nominal depth. */
struct ShapeGains
{
    float periodic = 1.0f;
    float random = 0.0f;
};

inline ShapeGains shapeGains (float shape01, float nominalRandom) noexcept
{
    const auto r = randomShare (shape01, nominalRandom);
    const auto n = std::clamp (nominalRandom, 0.001f, 0.999f);
    return { (1.0f - r) / (1.0f - n), r / n };
}

/** Seeded white noise, unit RMS (sum of two uniforms, triangular, scaled). */
class WhiteNoise
{
public:
    void setSeed (uint32_t s) noexcept { seed = s; rng.setSeed (s); }
    void reset() noexcept { rng.setSeed (seed); }

    float next() noexcept { return (rng.bipolar() + rng.bipolar()) * 1.2247449f; }

private:
    myzel::dsp::Rng rng;
    uint32_t seed = 1;
};

/** Noise knob 0..1 -> gain on the machine's nominal noise floor (0.5 = nominal, 1 = +6 dB). */
inline float noiseGain (float noise01) noexcept { return 2.0f * std::clamp (noise01, 0.0f, 1.0f); }

/**
    A time or position that follows its target like a mechanism with inertia: pulled towards the
    target with time constant tau, at a bounded speed, and that speed changes with its own time
    constant (Cerulean Echo's transport). Double precision, so long glides never stall.
*/
class InertialGlide
{
public:
    void prepare (double sampleRate, double tauMs, double accelMs, double maxSlewPerSample) noexcept
    {
        pull = 1.0 / (tauMs * 0.001 * sampleRate);
        accel = 1.0 - std::exp (-1.0 / (accelMs * 0.001 * sampleRate));
        maxSlew = maxSlewPerSample;
    }

    void snap (double v) noexcept { value = target = v; velocity = 0.0; }
    void setTarget (double v) noexcept { target = v; }
    double getTarget() const noexcept { return target; }
    double get() const noexcept { return value; }
    bool isMoving() const noexcept { return value < target || value > target || velocity < 0.0 || velocity > 0.0; }

    double next() noexcept
    {
        if (! isMoving())
            return value;
        const auto desired = std::clamp ((target - value) * pull, -maxSlew, maxSlew);
        velocity += (desired - velocity) * accel;
        value += velocity;
        // Arrived: snap only below a relative hair, so values in metres (a sliding head) do not
        // jump the last fraction of a sample.
        const auto scale = 1.0 + std::abs (target);
        if (std::abs (target - value) < 1.0e-10 * scale && std::abs (velocity) < 1.0e-13 * scale)
        {
            value = target;
            velocity = 0.0;
        }
        return value;
    }

private:
    double value = 0.0, target = 0.0, velocity = 0.0;
    double pull = 0.0, accel = 1.0, maxSlew = 1.0;
};

} // namespace mulberry::dsp
