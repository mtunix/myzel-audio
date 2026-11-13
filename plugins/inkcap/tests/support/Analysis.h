#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

/**
    Signals and measurements for the JUCE-free suites (common, chain, every module) and
    tools/measure. Plain functions over std::vector<float>; no allocation tricks, test code only.
*/
namespace inkcap::analysis
{

struct Stereo
{
    std::vector<float> left, right;

    size_t size() const noexcept { return left.size(); }
};

inline double toDb (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-30)); }

inline std::vector<float> sine (double hz, double sampleRate, double seconds, double amplitude = 0.5, double phase = 0.0)
{
    std::vector<float> x ((size_t) std::lround (seconds * sampleRate));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (amplitude * std::sin (2.0 * std::numbers::pi * hz * (double) i / sampleRate + phase));
    return x;
}

/** Deterministic white noise (an LCG), uniform in [-amplitude, amplitude). */
inline std::vector<float> noise (size_t n, uint32_t salt, double amplitude = 0.5)
{
    std::vector<float> x (n);
    auto state = 0x9E3779B9u ^ (salt * 2654435761u);
    for (auto& v : x)
    {
        state = state * 1664525u + 1013904223u;
        v = (float) (amplitude * (2.0 * (double) (state >> 8) / 16777216.0 - 1.0));
    }
    return x;
}

/** Music-like test material: two tones, a slow swell and some noise, different per channel. */
inline Stereo programme (double sampleRate, double seconds, uint32_t salt = 1)
{
    const auto n = (size_t) std::lround (seconds * sampleRate);
    const auto a = noise (n, salt, 0.08), b = noise (n, salt + 7, 0.08);
    Stereo s { std::vector<float> (n), std::vector<float> (n) };
    for (size_t i = 0; i < n; ++i)
    {
        const auto t = (double) i / sampleRate;
        const auto swell = 0.6 + 0.4 * std::sin (2.0 * std::numbers::pi * 0.7 * t);
        s.left[i] = (float) (swell * (0.3 * std::sin (2.0 * std::numbers::pi * 220.0 * t) + 0.15 * std::sin (2.0 * std::numbers::pi * 1375.0 * t))) + a[i];
        s.right[i] = (float) (swell * (0.3 * std::sin (2.0 * std::numbers::pi * 330.0 * t) + 0.12 * std::sin (2.0 * std::numbers::pi * 2750.0 * t))) + b[i];
    }
    return s;
}

inline double rms (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, x.size());
    if (to <= from)
        return 0.0;
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) (to - from));
}

inline double peak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, x.size());
    double p = 0.0;
    for (size_t i = from; i < to; ++i)
        p = std::max (p, (double) std::abs (x[i]));
    return p;
}

/** The difference of two signals relative to the reference, in dB (-300 when identical). */
inline double nullDb (const std::vector<float>& x, const std::vector<float>& reference, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min ({ to, x.size(), reference.size() });
    if (to <= from)
        return -300.0;
    double diff = 0.0, ref = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const auto d = (double) x[i] - (double) reference[i];
        diff += d * d;
        ref += (double) reference[i] * reference[i];
    }
    if (diff <= 0.0)
        return -300.0;
    return 10.0 * std::log10 (diff / std::max (ref, 1.0e-30));
}

/** True when the two are the same to the bit over [from, to). */
inline bool identical (const std::vector<float>& x, const std::vector<float>& y, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min ({ to, x.size(), y.size() });
    for (size_t i = from; i < to; ++i)
        if (std::abs (x[i] - y[i]) > 0.0f || std::isnan (x[i]) != std::isnan (y[i]))
            return false;
    return true;
}

/** Peak of the 3rd difference (a broadband discontinuity detector) over [from, to). */
inline double thirdDifferencePeak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, x.size());
    double p = 0.0;
    for (size_t i = std::max<size_t> (from, 3); i < to; ++i)
        p = std::max (p, std::abs ((double) x[i] - 3.0 * x[i - 1] + 3.0 * x[i - 2] - x[i - 3]));
    return p;
}

/** 3rd-difference crest in dB (the click gate, as the siblings'): the 3rd difference's peak over the RMS. */
inline double clickCrestDb (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    return toDb (thirdDifferencePeak (x, from, to) / std::max (rms (x, from, to), 1.0e-12));
}

/** Amplitude of one frequency over [from, from + n) (Hann-windowed DFT bin). */
inline double toneAmplitude (const std::vector<float>& x, double hz, double sampleRate, size_t from, size_t n)
{
    n = std::min (n, x.size() - std::min (from, x.size()));
    const auto w = 2.0 * std::numbers::pi * hz / sampleRate;
    std::complex<double> acc {};
    double windowSum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const auto hann = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) n);
        acc += hann * (double) x[from + i] * std::polar (1.0, -w * (double) i);
        windowSum += hann;
    }
    return 2.0 * std::abs (acc) / std::max (windowSum, 1.0e-12);
}

/**
    The instantaneous frequency of a near-sinusoid, from its upward zero crossings (linearly
    interpolated): one value per period, at the crossing's time in samples.
*/
struct FrequencyTrack
{
    std::vector<double> time, hz;
};

inline FrequencyTrack frequencyTrack (const std::vector<float>& x, double sampleRate, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, x.size());
    FrequencyTrack t;
    double last = -1.0;
    for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const auto crossing = (double) (i - 1) + (double) -x[i - 1] / ((double) x[i] - (double) x[i - 1]);
            if (last >= 0.0)
            {
                t.time.push_back (0.5 * (crossing + last));
                t.hz.push_back (sampleRate / (crossing - last));
            }
            last = crossing;
        }
    return t;
}

/**
    Least-squares fit of a + b sin + c cos at a known frequency to a frequency track: the
    swing (hypot (b, c), Hz) and the RMS of what the sine does not explain (Hz).
*/
inline std::pair<double, double> fitSine (const FrequencyTrack& t, double hz, double sampleRate)
{
    double s00 = 0, s01 = 0, s02 = 0, s11 = 0, s12 = 0, s22 = 0, y0 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < t.hz.size(); ++i)
    {
        const auto w = 2.0 * std::numbers::pi * hz * t.time[i] / sampleRate;
        const double v[] = { 1.0, std::sin (w), std::cos (w) };
        s00 += v[0] * v[0]; s01 += v[0] * v[1]; s02 += v[0] * v[2];
        s11 += v[1] * v[1]; s12 += v[1] * v[2]; s22 += v[2] * v[2];
        y0 += v[0] * t.hz[i]; y1 += v[1] * t.hz[i]; y2 += v[2] * t.hz[i];
    }

    // The 3 x 3 normal equations, by Cramer's rule.
    const auto det = [] (double a, double b, double c, double d, double e, double f, double g, double h, double i)
    { return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g); };
    const auto d = det (s00, s01, s02, s01, s11, s12, s02, s12, s22);
    const auto a = det (y0, s01, s02, y1, s11, s12, y2, s12, s22) / d;
    const auto b = det (s00, y0, s02, s01, y1, s12, s02, y2, s22) / d;
    const auto c = det (s00, s01, y0, s01, s11, y1, s02, s12, y2) / d;

    double residual = 0.0;
    for (size_t i = 0; i < t.hz.size(); ++i)
    {
        const auto w = 2.0 * std::numbers::pi * hz * t.time[i] / sampleRate;
        const auto e = t.hz[i] - (a + b * std::sin (w) + c * std::cos (w));
        residual += e * e;
    }
    return { std::hypot (b, c), std::sqrt (residual / std::max<double> (1.0, (double) t.hz.size())) };
}

} // namespace inkcap::analysis
