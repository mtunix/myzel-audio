#pragma once

/** Small spectral helpers for the Halo tests and measurements (band bank, centroid). */

#include "analysis/ReverbAnalysis.h"

#include <vector>

namespace cerulean::halotest
{

/** RMS of x in a 1/3-octave band around fc over [start, end) (filter run from 0 to settle). */
inline double bandRms (const std::vector<float>& x, double fs, double fc, size_t start, size_t end, double q = 4.32)
{
    auto f1 = analysis::Biquad::bandpass (fc, q, fs);
    auto f2 = f1;
    end = std::min (end, x.size());
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = 0; i < end; ++i)
    {
        const auto y = f2.process (f1.process (x[i]));
        if (i >= start)
        {
            sum += y * y;
            ++count;
        }
    }
    return count > 0 ? std::sqrt (sum / (double) count) : 0.0;
}

/** Third-octave centre frequencies from 100 Hz to ~12.5 kHz. */
inline std::vector<double> thirdOctaves()
{
    std::vector<double> f;
    for (int k = 0; k <= 21; ++k)
        f.push_back (100.0 * std::pow (2.0, k / 3.0));
    return f;
}

/** Energy-weighted mean of log2 (f) over a third-octave bank: the spectral centroid in octaves. */
inline double centroidHz (const std::vector<float>& x, double fs, size_t start, size_t end)
{
    double num = 0.0, den = 0.0;
    for (const auto fc : thirdOctaves())
    {
        const auto e = std::pow (bandRms (x, fs, fc, start, end), 2.0);
        num += e * std::log2 (fc);
        den += e;
    }
    return den > 0.0 ? std::exp2 (num / den) : 0.0;
}

/** Sum of both channels' energy in a band (dB) over a time window in seconds. */
inline double bandDb (const analysis::Stereo& s, double fc, double t0, double t1, double q = 8.0)
{
    const auto a = (size_t) (t0 * s.sampleRate), b = (size_t) (t1 * s.sampleRate);
    const auto l = bandRms (s.left, s.sampleRate, fc, a, b, q);
    const auto r = bandRms (s.right, s.sampleRate, fc, a, b, q);
    return 10.0 * std::log10 (std::max (l * l + r * r, 1.0e-30));
}

} // namespace cerulean::halotest
