#pragma once

#include "dsp/common/Modulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace mulberry::dsp
{

/** sin (2 pi phase) for a phase in [0, 1): odd polynomial on a quarter cycle, within 1e-9. */
inline double sinCycle (double phase) noexcept
{
    // Fold to [-0.25, 0.25] of a cycle, where sin is odd and its Taylor series converges fast.
    auto x = phase < 0.5 ? phase : phase - 1.0;
    if (x > 0.25)
        x = 0.5 - x;
    else if (x < -0.25)
        x = -0.5 - x;
    const auto r = 2.0 * std::numbers::pi * x;
    const auto r2 = r * r;
    return r * (1.0 + r2 * (-1.0 / 6.0 + r2 * (1.0 / 120.0 + r2 * (-1.0 / 5040.0 + r2 * (1.0 / 362880.0 + r2 * (-1.0 / 39916800.0 + r2 * (1.0 / 6227020800.0)))))));
}

/**
    A tape (or drum) transport: the medium's speed, its inertia and its wobble, and the position
    of the medium under the record head (double, metres).

    Speed follows its target as a second-order mechanism (motor and flywheel: natural frequency
    and damping), so a speed change bends in and out and never steps. Wobble is a sum of physical
    components, after Arnardottir, Abel & Smith's decomposition of a measured delay trajectory:

    - capstan and pinch roller eccentricity, periodic at their rotation rates v / (pi D), which
      follow the speed;
    - the splice (tape loops), a short bump once per pass of the loop;
    - slow random wow (reels, belts, motor hunting): smoothed random at a low rate;
    - scrape flutter: band-limited speed noise in the kHz range, heard as modulation noise.

    Each is a relative speed deviation (peak, as a fraction); Wobble scales them all, Shape trades
    the periodic ones against the random ones, Rate scales every rate. Wobble 0 is exactly steady.
*/
class Transport
{
public:
    struct Mechanics
    {
        double naturalHz = 1.5;         // speed response of motor + flywheel
        double damping = 0.8;
        double minSpeed = 0.005;        // m/s: the transport never stops
    };

    struct Wobble
    {
        double capstanDepth = 0.0, capstanDiameter = 0.01;      // relative peak, metres
        double pinchDepth = 0.0, pinchDiameter = 0.02;
        double spliceDepth = 0.0, loopLength = 0.0;             // 0: no splice
        double driftDepth = 0.0, driftHz = 0.5;
        double scrapeDepth = 0.0, scrapeHz = 2500.0;            // at the nominal speed
        double nominalSpeed = 0.19;                             // scrape frequency reference
    };

    void prepare (double sampleRate, uint32_t seed, Mechanics m) noexcept
    {
        fs = sampleRate;
        mech = m;
        drift.setSeed (seed * 2654435761u + 7u);
        scrapeNoise.setSeed (seed * 2246822519u + 13u);
        startPhases = { 0.13 + 0.37 * (double) (seed % 7) / 7.0, 0.61, 0.0 };
        reset();
    }

    void reset() noexcept
    {
        speed = target;
        acceleration = 0.0;
        position = 0.0;
        instantaneous = speed;
        capstanPhase = startPhases[0];
        pinchPhase = startPhases[1];
        splicePhase = startPhases[2];
        drift.reset();
        scrapeNoise.reset();
        scrapeLow = scrapeBand = 0.0;
    }

    /** Jumps to a speed (no glide): the first block after a reset. */
    void snap (double metresPerSecond) noexcept
    {
        target = speed = instantaneous = std::max (metresPerSecond, mech.minSpeed);
        acceleration = 0.0;
    }

    void setTarget (double metresPerSecond) noexcept { target = std::max (metresPerSecond, mech.minSpeed); }
    double getTarget() const noexcept { return target; }

    /** Wobble components (already scaled), the share of periodic and random, and a rate multiplier. */
    void setWobble (const Wobble& w, ShapeGains shape, double rate) noexcept
    {
        wobble = w;
        periodicGain = shape.periodic;
        randomGain = shape.random;
        rateScale = std::clamp (rate, 0.05, 8.0);
        drift.setRate ((float) (w.driftHz * rateScale), fs);
        scrapeSpeed = -1.0;
        any = (w.capstanDepth + w.pinchDepth + w.spliceDepth) * periodicGain + (w.driftDepth + w.scrapeDepth) * randomGain > 0.0;
    }

    /** Advances one sample: returns the instantaneous speed (m/s) and moves the position. */
    double advance() noexcept
    {
        // Motor and flywheel: a damped second-order follower of the target speed.
        const auto w = 2.0 * std::numbers::pi * mech.naturalHz;
        const auto dt = 1.0 / fs;
        acceleration += (w * w * (target - speed) - 2.0 * mech.damping * w * acceleration) * dt;
        speed = std::max (speed + acceleration * dt, mech.minSpeed);

        auto deviation = 0.0;
        if (any)
        {
            const auto rotation = speed * rateScale * dt / std::numbers::pi;
            capstanPhase = wrap (capstanPhase + rotation / wobble.capstanDiameter);
            pinchPhase = wrap (pinchPhase + rotation / wobble.pinchDiameter);

            auto periodic = wobble.capstanDepth * sinCycle (capstanPhase) + wobble.pinchDepth * sinCycle (pinchPhase);

            if (wobble.loopLength > 0.0)
            {
                // The splice: a 12 mm bump (raised cosine) passing the capstan once per loop.
                splicePhase = wrap (splicePhase + speed * rateScale * dt / wobble.loopLength);
                const auto width = 0.012 / wobble.loopLength;
                if (splicePhase < width)
                    periodic += wobble.spliceDepth * 0.5 * (1.0 - std::cos (2.0 * std::numbers::pi * splicePhase / width));
            }

            auto random = wobble.driftDepth * (double) drift.next();
            if (wobble.scrapeDepth > 0.0)
            {
                // Band-limited noise (2-pole band-pass, Q 1) at a frequency that follows the speed.
                if (std::abs (speed - scrapeSpeed) > 0.01 * scrapeSpeed)
                {
                    scrapeSpeed = speed;
                    const auto f = std::min (wobble.scrapeHz * speed / wobble.nominalSpeed * rateScale, 0.4 * fs);
                    scrapeG = std::tan (std::numbers::pi * f / fs);
                }
                const auto g = scrapeG;
                const auto k = 1.0;
                const auto a1 = 1.0 / (1.0 + g * (g + k));
                const auto v3 = (double) scrapeNoise.next() - scrapeLow;
                const auto v1 = a1 * scrapeBand + g * a1 * v3;
                const auto v2 = scrapeLow + g * v1;
                scrapeBand = 2.0 * v1 - scrapeBand;
                scrapeLow = 2.0 * v2 - scrapeLow;
                random += wobble.scrapeDepth * v1 * 2.0;
            }

            deviation = periodicGain * periodic + randomGain * random;
        }

        instantaneous = speed * (1.0 + deviation);
        position += instantaneous * dt;
        return instantaneous;
    }

    double getPosition() const noexcept { return position; }
    double getSpeed() const noexcept { return instantaneous; }      // with wobble
    double getMechanicalSpeed() const noexcept { return speed; }    // without

    /** Copies another transport's mechanical state (one transport for both lines). */
    void follow (const Transport& other) noexcept
    {
        speed = other.speed;
        acceleration = other.acceleration;
        target = other.target;
    }

private:
    static double wrap (double phase) noexcept { return phase - std::floor (phase); }

    double fs = 48000.0;
    Mechanics mech;
    Wobble wobble;
    double periodicGain = 1.0, randomGain = 1.0, rateScale = 1.0;
    bool any = false;

    double target = 0.19, speed = 0.19, acceleration = 0.0, instantaneous = 0.19, position = 0.0;
    double capstanPhase = 0.0, pinchPhase = 0.0, splicePhase = 0.0;
    std::array<double, 3> startPhases {};
    myzel::dsp::SmoothRandom drift;
    WhiteNoise scrapeNoise;
    double scrapeLow = 0.0, scrapeBand = 0.0, scrapeSpeed = -1.0, scrapeG = 0.0;
};

} // namespace mulberry::dsp
