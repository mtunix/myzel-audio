#pragma once

#include "common/dsp/Blocks.h"

#include <algorithm>
#include <bit>
#include <cmath>

/**
    Reverb building blocks: per-band decay filters and FDN mixing. The generic float blocks
    (delay lines, allpasses, filters, LFOs, smoothers) are shared with the other plugins
    (common/dsp/Blocks.h) and imported here.
*/
namespace cerulean::dsp
{

using myzel::dsp::twoPi;
using myzel::dsp::msToSamples;
using myzel::dsp::dbToGain;
using myzel::dsp::flushTiny;
using myzel::dsp::LinearRamp;
using myzel::dsp::Rng;
using myzel::dsp::DelayLine;
using myzel::dsp::Allpass;
using myzel::dsp::OnePole;
using myzel::dsp::Svf;
using myzel::dsp::SmoothRandom;
using myzel::dsp::SineLfo;
using myzel::dsp::GlideValue;
using myzel::dsp::Smoother;
using myzel::dsp::EnvelopeFollower;
using myzel::dsp::AllpassChain;

//==============================================================================
/** Coefficients of a Simper TPT state-variable filter at Q = 1/sqrt 2 (shareable by many filters). */
struct ShelfCoefficients
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    static constexpr float sqrt2 = 1.41421356f;

    static ShelfCoefficients at (float hz, double sampleRate) noexcept
    {
        const auto fc = std::clamp (hz, 1.0f, 0.49f * (float) sampleRate);
        const auto g = std::tan (std::numbers::pi_v<float> * fc / (float) sampleRate);
        const auto a1 = 1.0f / (1.0f + g * (g + sqrt2));
        return { a1, g * a1, g * g * a1 };
    }
};

/**
    Three-band decay ("absorption") filter for one delay line, after Jot: a broadband gain for the
    mids and two *second-order* (Butterworth-type) shelves built from TPT SVFs at Q = 1/sqrt 2:

        high shelf  H(s) = (1 + sqrt2 sqrt(G) s + G s^2) / (1 + sqrt2 s + s^2)
                    |H|^2 = (1 + G^2 w^4) / (1 + w^4)          (low shelf: the mirror image)

    Each shelf is monotonic between 1 and G, so the filter never exceeds max(gLow, gMid, gHigh) < 1
    and the loop stays stable for any setting. Second order because the skirt matters: a
    first-order shelf at 4.5 kHz shortened the 1-2 kHz octaves' RT60 by 7-10 % (found by the
    Nimbus/Billow work); below the crossover this one leaks with w^4 instead of w^2.

    Written as  x + (G - 1) hp + sqrt2 (sqrt G - 1) bp  (and the low-shelf mirror), so at G = 1
    (freeze) it is exactly transparent. Every gain ramps per sample.
*/
class DecayFilter
{
public:
    void setCrossovers (float lowHz, float highHz, double sampleRate) noexcept
    {
        lo = ShelfCoefficients::at (lowHz, sampleRate);
        hi = ShelfCoefficients::at (highHz, sampleRate);
    }

    void setCrossovers (const ShelfCoefficients& low, const ShelfCoefficients& high) noexcept
    {
        lo = low;
        hi = high;
    }

    /**
        Per-pass gains of the three bands (each in (0, 1]). They ramp to the new values over the
        next numSamples samples (the first call after reset() jumps).
    */
    void setGains (float gLow, float gMid, float gHigh, int numSamples = 1) noexcept
    {
        const auto m = std::max (gMid, 1.0e-6f);
        const auto gl = gLow / m, gh = gHigh / m;
        mid.set (gMid, numSamples);
        loA.set (gl - 1.0f, numSamples);
        loB.set (ShelfCoefficients::sqrt2 * (std::sqrt (gl) - 1.0f), numSamples);
        hiA.set (gh - 1.0f, numSamples);
        hiB.set (ShelfCoefficients::sqrt2 * (std::sqrt (gh) - 1.0f), numSamples);
    }

    void reset() noexcept
    {
        lo1 = lo2 = hi1 = hi2 = 0.0f;
        mid.reset();
        loA.reset();
        loB.reset();
        hiA.reset();
        hiB.reset();
    }

    float process (float x) noexcept { return process (x, lo, hi); }

    /** With crossover coefficients shared by many filters (e.g. ramped once per sample by an FDN). */
    float process (float x, const ShelfCoefficients& low, const ShelfCoefficients& high) noexcept
    {
        {
            const auto v3 = x - hi2;
            const auto v1 = high.a1 * hi1 + high.a2 * v3;
            const auto v2 = hi2 + high.a2 * hi1 + high.a3 * v3;
            hi1 = flushTiny (2.0f * v1 - hi1);
            hi2 = flushTiny (2.0f * v2 - hi2);
            const auto hp = x - ShelfCoefficients::sqrt2 * v1 - v2;
            x += hiA.next() * hp + hiB.next() * v1;
        }
        {
            const auto v3 = x - lo2;
            const auto v1 = low.a1 * lo1 + low.a2 * v3;
            const auto v2 = lo2 + low.a2 * lo1 + low.a3 * v3;
            lo1 = flushTiny (2.0f * v1 - lo1);
            lo2 = flushTiny (2.0f * v2 - lo2);
            x += loA.next() * v2 + loB.next() * v1;
        }
        return mid.next() * x;
    }

private:
    ShelfCoefficients lo, hi;
    float lo1 = 0.0f, lo2 = 0.0f, hi1 = 0.0f, hi2 = 0.0f;
    LinearRamp mid, loA, loB, hiA, hiB;
};

/**
    Per-pass gains for a line of delaySamples from the shared global controls.
    freeze (0..1) blends every gain towards exactly 1.
*/
struct BandGains
{
    float low, mid, high;
};

inline BandGains computeBandGains (float delaySamples, float rt60Mid, float lowEndRatio,
                                   float highRatio, float freeze, float sampleRate) noexcept
{
    auto g = [&] (float rt)
    {
        return std::pow (10.0f, -3.0f * delaySamples / (std::max (rt, 1.0e-3f) * sampleRate));
    };

    BandGains b { g (rt60Mid * lowEndRatio), g (rt60Mid), g (rt60Mid * highRatio) };

    if (freeze > 0.0f)
    {
        const auto f = std::clamp (freeze, 0.0f, 1.0f);
        b.low  += (1.0f - b.low) * f;
        b.mid  += (1.0f - b.mid) * f;
        b.high += (1.0f - b.high) * f;
    }
    return b;
}

//==============================================================================
/** In-place fast Walsh-Hadamard transform, scaled to be orthonormal. N must be a power of 2. */
template <int N>
inline void hadamardInPlace (float* x) noexcept
{
    static_assert ((N & (N - 1)) == 0, "N must be a power of two");

    for (int h = 1; h < N; h <<= 1)
        for (int i = 0; i < N; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const auto a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }

    const auto scale = 1.0f / std::sqrt ((float) N);
    for (int i = 0; i < N; ++i)
        x[i] *= scale;
}

/** In-place Householder reflection I - (2/N) 1 1^T (orthogonal, cheap: O(N)). */
template <int N>
inline void householderInPlace (float* x) noexcept
{
    float sum = 0.0f;
    for (int i = 0; i < N; ++i)
        sum += x[i];

    const auto k = sum * (2.0f / (float) N);
    for (int i = 0; i < N; ++i)
        x[i] -= k;
}

/** +-1 sign of Hadamard row r, column c (Sylvester construction): orthogonal tap patterns. */
inline float hadamardSign (int r, int c) noexcept
{
    return (std::popcount ((unsigned) (r & c)) & 1) ? -1.0f : 1.0f;
}

} // namespace cerulean::dsp
