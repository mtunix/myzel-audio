#pragma once

// Test signals for the gates (PLAN.md, Measurement and quality gates; dynamics/NOTES.md,
// Conventions and Ballistics). Shared by tests/GateTests.cpp and tools/measure.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

namespace basalt::measure
{

using Samples = std::vector<float>;
using Channels = std::vector<Samples>;

/** 10·log10(2): the constant-magnitude carrier reads this much higher on an RMS detector than on a peak one. */
inline constexpr double carrierRmsOffsetDb = 3.0102999566398120;

inline int samplesFor (double seconds, double rate) { return (int) std::lround (seconds * rate); }
inline double gainOf (double db) { return std::pow (10.0, db / 20.0); }
inline double dbOf (double gain) { return 20.0 * std::log10 (gain); }

/** The constant-magnitude carrier, ±A alternating every sample from +A: a clean level step for any detector. */
inline Samples carrier (int n, double amplitude)
{
    Samples x ((size_t) std::max (n, 0));
    const auto a = (float) amplitude;
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (i % 2 == 0) ? a : -a;
    return x;
}

/** A sine from `startSample` on, so consecutive segments stay phase-continuous. */
inline Samples sine (double hz, int n, double rate, double amplitude, double phase = 0.0, int64_t startSample = 0)
{
    Samples x ((size_t) std::max (n, 0));
    for (size_t i = 0; i < x.size(); ++i)
    {
        // Whole cycles are removed exactly first, so a long sweep keeps the sample-exact peaks of 1 kHz at 48 kHz.
        const auto cycles = std::fmod (hz * (double) (startSample + (int64_t) i), rate) / rate;
        x[i] = (float) (amplitude * std::sin (2.0 * std::numbers::pi * cycles + phase));
    }
    return x;
}

inline Samples silence (int n) { return Samples ((size_t) std::max (n, 0), 0.0f); }

inline void append (Samples& to, const Samples& from) { to.insert (to.end(), from.begin(), from.end()); }

inline Samples scaled (Samples x, double gain)
{
    for (auto& v : x)
        v = (float) (v * gain);
    return x;
}

/** Uniform noise in [−amplitude, amplitude]. */
inline Samples whiteNoise (int n, double amplitude, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> dist (-amplitude, amplitude);
    Samples x ((size_t) std::max (n, 0));
    for (auto& v : x)
        v = (float) dist (rng);
    return x;
}

/** Gaussian noise of standard deviation `sigma`. */
inline Samples gaussianNoise (int n, double sigma, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> dist (0.0, sigma);
    Samples x ((size_t) std::max (n, 0));
    for (auto& v : x)
        v = (float) dist (rng);
    return x;
}

/** In-place radix-2 FFT (sign −1 forward, +1 inverse, unscaled); the size is a power of two. */
inline void fft (std::vector<std::complex<double>>& a, int sign)
{
    const auto n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar (1.0, sign * 2.0 * std::numbers::pi / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> wk (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * wk;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                wk *= w;
            }
        }
    }
}

/**
    Pink noise (−3.01 dB per octave) band-limited to [lowHz, highHz], made in the frequency
    domain with random phases: deterministic for a seed, the band edges exact. Normalised to
    `rmsDb` dBFS of plain RMS (on the detector scale it reads 3.01 dB higher).
*/
inline Samples pink (int n, double rate, unsigned seed, double rmsDb, double lowHz = 20.0, double highHz = 20000.0)
{
    size_t size = 1;
    while (size < (size_t) std::max (n, 2))
        size <<= 1;

    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> phase (0.0, 2.0 * std::numbers::pi);
    std::vector<std::complex<double>> spectrum (size);
    for (size_t k = 1; k < size / 2; ++k)
    {
        const auto hz = (double) k * rate / (double) size;
        const auto p = phase (rng);     // drawn for every bin, so the band edges do not reshuffle the phases
        if (hz >= lowHz && hz <= std::min (highHz, 0.5 * rate))
        {
            spectrum[k] = std::polar (1.0 / std::sqrt (hz), p);
            spectrum[size - k] = std::conj (spectrum[k]);
        }
    }
    fft (spectrum, +1);

    Samples x ((size_t) std::max (n, 0));
    auto sum = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
        sum += spectrum[i].real() * spectrum[i].real();
    const auto scale = gainOf (rmsDb) / std::sqrt (std::max (sum / (double) std::max<size_t> (x.size(), 1), 1.0e-300));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (spectrum[i].real() * scale);
    return x;
}

} // namespace basalt::measure
