#pragma once

#include "common/dsp/Filters.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>

namespace inkcap::dsp::hiss
{

/** Unit-variance, nearly Gaussian noise from one 64-bit draw: four 16-bit uniforms summed (Irwin-Hall, bounded at ±3.46). */
inline float gaussian (uint64_t bits) noexcept
{
    const auto sum = (bits & 0xFFFFu) + ((bits >> 16) & 0xFFFFu) + ((bits >> 32) & 0xFFFFu) + (bits >> 48);
    return (float) (((double) sum - 131070.0) * (1.7320508075688772 / 65536.0));
}

/** floor (a / b) and the matching non-negative remainder, for timeline indices before 0. */
inline int64_t floorDiv (int64_t a, int64_t b) noexcept
{
    const auto q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

inline int64_t floorMod (int64_t a, int64_t b) noexcept { return a - floorDiv (a, b) * b; }

/**
    The RIAA playback curve (time constants 3180, 318 and 75 µs), 0 dB at 1 kHz: what a phono
    stage does to everything the stylus picks up besides the music, so surface noise and clicks
    come out with a record's colour (dark, the bass lifted by up to 19 dB).

        (1 + s 318 µs) / ((1 + s 3180 µs) (1 + s 75 µs)),   first factor = r + (1 - r) / (1 + s 3180 µs), r = 0.1
*/
class RiaaPlayback
{
public:
    void prepare (double sampleRate) noexcept
    {
        bass.setCutoff (bassHz, sampleRate);
        treble.setCutoff (trebleHz, sampleRate);

        // The TPT one-pole is the bilinear transform: at w its response is g / (g + j tan (w / 2)).
        const auto warped = std::tan (std::numbers::pi * 1000.0 / sampleRate);
        const auto onePole = [warped, sampleRate] (double hz)
        {
            const auto g = myzel::dsp::prewarp (hz, sampleRate);
            return std::complex<double> (g, 0.0) / std::complex<double> (g, warped);
        };
        norm = 1.0 / std::abs ((turnover + (1.0 - turnover) * onePole (bassHz)) * onePole (trebleHz));
        reset();
    }

    void reset() noexcept
    {
        bass.reset();
        treble.reset();
    }

    double process (double x) noexcept
    {
        const auto shelved = turnover * x + (1.0 - turnover) * bass.processLowpass (x);
        return norm * treble.processLowpass (shelved);
    }

    static constexpr double bassHz = 50.05;         // 3180 µs
    static constexpr double trebleHz = 2122.07;     // 75 µs
    static constexpr double turnover = 0.1;         // 318 / 3180

private:
    myzel::dsp::TptOnePole bass, treble;
    double norm = 1.0;
};

/**
    Mains hum at 50 Hz as two waveforms of RMS 1, a pure function of the phase:

    - hum:  the transformer's field, odd harmonics strongest (sine phase);
    - buzz: a full-wave rectifier's charging spikes, every harmonic in cosine phase so they add
            up to a spike at each half cycle; the even harmonics (the 100 Hz pulse train) lead.

    Hum and buzz are orthogonal (sine against cosine phase), so any mix of them has a known RMS.
    Harmonics up to 64 (3.2 kHz) or 0.45 fs, summed once per rate into a 4096-point cycle that is
    read with linear interpolation (the 64th harmonic's error: -70 dB).
*/
class HumSource
{
public:
    static constexpr int mainsHz = 50;
    static constexpr int maxHarmonics = 64;
    static constexpr int tableSize = 4096;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        integerRate = std::abs (sampleRate - std::round (sampleRate)) < 1.0e-9 ? (int64_t) std::llround (sampleRate) : 0;
        const auto harmonics = std::clamp ((int) std::floor (0.45 * sampleRate / mainsHz), 1, maxHarmonics);

        constexpr std::array<double, 14> field { 0.0, 1.0, 0.3, 0.45, 0.08, 0.2, 0.04, 0.1, 0.02, 0.05, 0.0, 0.03, 0.0, 0.02 };
        std::array<double, maxHarmonics + 1> humAmp {}, buzzAmp {};
        double humEnergy = 0.0, buzzEnergy = 0.0;
        for (int k = 1; k <= harmonics; ++k)
        {
            const auto i = (size_t) k;
            humAmp[i] = i < field.size() ? field[i] : 0.0;
            buzzAmp[i] = (k % 2 == 0 ? 1.0 : 0.3) / (1.0 + std::pow ((double) k / 28.0, 2.0));
            humEnergy += 0.5 * humAmp[i] * humAmp[i];
            buzzEnergy += 0.5 * buzzAmp[i] * buzzAmp[i];
        }

        for (int n = 0; n <= tableSize; ++n)
        {
            const auto w = 2.0 * std::numbers::pi * (double) n / tableSize;
            double h = 0.0, b = 0.0;
            for (int k = 1; k <= harmonics; ++k)
            {
                h += humAmp[(size_t) k] * std::sin (k * w);
                b += buzzAmp[(size_t) k] * std::cos (k * w);
            }
            humTable[(size_t) n] = (float) (h / std::sqrt (humEnergy));
            buzzTable[(size_t) n] = (float) (b / std::sqrt (buzzEnergy));
        }
    }

    /** The phase (cycles, 0 .. 1) at a timeline index: exact integer arithmetic at an integer rate, so it never drifts. */
    double cyclesAt (int64_t index) const noexcept
    {
        if (integerRate > 0)
            return (double) floorMod (index * mainsHz, integerRate) / (double) integerRate;
        const auto c = (double) index * mainsHz / fs;
        return c - std::floor (c);
    }

    struct Pair
    {
        double hum = 0.0, buzz = 0.0;
    };

    /** Both waveforms at a phase in cycles (any real number). */
    Pair at (double cycles) const noexcept
    {
        const auto x = (cycles - std::floor (cycles)) * tableSize;
        const auto i = std::min ((int) x, tableSize - 1);
        const auto f = x - (double) i;
        const auto n = (size_t) i;
        return { humTable[n] + f * (humTable[n + 1] - humTable[n]), buzzTable[n] + f * (buzzTable[n + 1] - buzzTable[n]) };
    }

private:
    double fs = 48000.0;
    int64_t integerRate = 48000;
    std::array<float, tableSize + 1> humTable {}, buzzTable {};
};

} // namespace inkcap::dsp::hiss
