#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace viridian::dsp
{

/**
    A model's noise floor for the Noise control: white hiss (near-Gaussian, a sum of four uniforms)
    and mains hum (fundamental and third harmonic), from a seeded generator that reset() restarts,
    so renders are bit-identical. Levels are in the caller's units; the hiss is specified over 20 Hz
    ... 20 kHz and keeps that density at every rate.
*/
class HissAndHum
{
public:
    struct Spec
    {
        double hissRms = 0.0;           // over 20 Hz ... 20 kHz
        double humHz = 50.0;
        double humPeak = 0.0, thirdPeak = 0.0;
    };

    void prepare (double sampleRate, const Spec& s, std::uint64_t seedValue) noexcept
    {
        spec = s;
        seed = seedValue | 1u;
        hissScale = spec.hissRms * std::sqrt (0.5 * sampleRate / 20000.0) * std::sqrt (3.0);
        stepCos = std::cos (2.0 * std::numbers::pi * spec.humHz / sampleRate);
        stepSin = std::sin (2.0 * std::numbers::pi * spec.humHz / sampleRate);
        reset();
    }

    void reset() noexcept
    {
        state = seed;
        humCos = 1.0;
        humSin = 0.0;
    }

    double next() noexcept
    {
        double sum = 0.0;
        for (int k = 0; k < 4; ++k)
            sum += uniform();
        // sin 3x = sin x (3 - 4 sin^2 x); the phasor turns by one step and is pulled back to the
        // unit circle (one Newton step), so its amplitude holds over hours.
        const auto s = humSin;
        const auto hum = s * (spec.humPeak + spec.thirdPeak * (3.0 - 4.0 * s * s));
        const auto c = humCos * stepCos - humSin * stepSin;
        humSin = humCos * stepSin + humSin * stepCos;
        humCos = c;
        const auto norm = 1.5 - 0.5 * (humCos * humCos + humSin * humSin);
        humCos *= norm;
        humSin *= norm;
        return hissScale * sum + hum;
    }

private:
    /** xorshift64*: uniform in [-0.5, 0.5). */
    double uniform() noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        const auto bits = (state * 0x2545F4914F6CDD1DULL) >> 11;
        return (double) bits * (1.0 / 9007199254740992.0) - 0.5;
    }

    Spec spec;
    std::uint64_t seed = 1, state = 1;
    double hissScale = 0.0, stepCos = 1.0, stepSin = 0.0, humCos = 1.0, humSin = 0.0;
};

} // namespace viridian::dsp
