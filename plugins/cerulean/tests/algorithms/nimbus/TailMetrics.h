#pragma once

/**
    Extra tail measurements for the long ambient algorithms (Nimbus): things the standard gates
    don't look at, made for 10-60 s tails. Test-only.

    - spectralPeakinessDb: metallic ringing detector. Welch-averaged power spectrum of a tail
      segment (each frame normalised to unit energy, so the decay doesn't weight the average),
      each bin divided by the median of its +-1/12-octave neighbourhood; returns the 99.5th
      percentile of that ratio in dB over 150 Hz - 6 kHz. A noise-like tail sits near +1.5 dB;
      isolated ringing modes stick out by many dB.
    - meanEchoDensity: mean Abel-Huang echo density over a segment (graininess: ~1 = smooth).
    - envelopeRoughnessDb: standard deviation (dB) of 10 ms RMS frames around a straight-line
      (exponential) fit over a segment: flutter / pumping / grain in the envelope.
    - energyArrivalSeconds: time at which `fraction` of the first `windowS` seconds' energy
      has arrived.
    - highShare: share of energy above 3 kHz in a segment (onset darkness).
*/

#include "analysis/ReverbAnalysis.h"

#include <complex>

namespace cerulean::metrics
{

inline void fft (std::vector<std::complex<double>>& a)
{
    const auto n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Welch power spectrum of x[start, end), frames of `frame` samples (power of 2), 50 % overlap. */
inline std::vector<double> welchSpectrum (const std::vector<float>& x, size_t start, size_t end, size_t frame = 8192)
{
    std::vector<double> psd (frame / 2, 0.0);
    std::vector<std::complex<double>> buf (frame);
    for (size_t pos = start; pos + frame <= std::min (end, x.size()); pos += frame / 2)
    {
        double energy = 0.0;
        for (size_t i = 0; i < frame; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) frame);
            buf[i] = w * (double) x[pos + i];
            energy += std::norm (buf[i]);
        }
        fft (buf);
        for (size_t k = 0; k < frame / 2; ++k)
            psd[k] += std::norm (buf[k]) / std::max (energy, 1.0e-30);
    }
    return psd;
}

inline double spectralPeakinessDb (const std::vector<float>& x, double fs, size_t start, size_t end)
{
    constexpr size_t frame = 8192;
    const auto psd = welchSpectrum (x, start, end, frame);
    const auto binHz = fs / (double) frame;

    std::vector<double> ratios;
    std::vector<double> neighbourhood;
    for (auto k = (size_t) (150.0 / binHz); k < (size_t) (6000.0 / binHz); ++k)
    {
        const auto half = std::max<size_t> (4, (size_t) ((double) k * 0.06));   // ~ +-1/12 octave
        neighbourhood.clear();
        for (auto j = k - std::min (k, half); j <= std::min (k + half, psd.size() - 1); ++j)
            neighbourhood.push_back (psd[j]);
        std::nth_element (neighbourhood.begin(), neighbourhood.begin() + (long) neighbourhood.size() / 2, neighbourhood.end());
        ratios.push_back (psd[k] / std::max (neighbourhood[neighbourhood.size() / 2], 1.0e-30));
    }
    std::sort (ratios.begin(), ratios.end());
    return 10.0 * std::log10 (ratios[(size_t) (0.995 * (double) (ratios.size() - 1))]);
}

inline double meanEchoDensity (const std::vector<float>& x, double fs, size_t start, size_t end)
{
    const std::vector<float> seg (x.begin() + (long) start, x.begin() + (long) std::min (end, x.size()));
    const auto profile = analysis::echoDensityProfile (seg, fs);
    double sum = 0.0;
    for (const auto v : profile)
        sum += v;
    return profile.empty() ? 0.0 : sum / (double) profile.size();
}

inline double envelopeRoughnessDb (const std::vector<float>& x, double fs, size_t start, size_t end)
{
    const auto frame = (size_t) (0.010 * fs);
    std::vector<double> t, db;
    for (size_t pos = start; pos + frame <= std::min (end, x.size()); pos += frame)
    {
        t.push_back ((double) pos / fs);
        db.push_back (analysis::toDb (analysis::rms (x, pos, pos + frame)));
    }
    const auto n = (double) t.size();
    double st = 0, sy = 0, stt = 0, sty = 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        st += t[i]; sy += db[i]; stt += t[i] * t[i]; sty += t[i] * db[i];
    }
    const auto slope = (n * sty - st * sy) / std::max (n * stt - st * st, 1.0e-30);
    const auto icept = (sy - slope * st) / n;
    double var = 0.0;
    for (size_t i = 0; i < t.size(); ++i)
        var += std::pow (db[i] - (icept + slope * t[i]), 2.0);
    return std::sqrt (var / std::max (n, 1.0));
}

inline double energyArrivalSeconds (const analysis::Stereo& ir, double fraction, double windowS)
{
    const auto n = std::min (ir.size(), (size_t) (windowS * ir.sampleRate));
    double total = 0.0;
    for (size_t i = 0; i < n; ++i)
        total += (double) ir.left[i] * ir.left[i] + (double) ir.right[i] * ir.right[i];
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        acc += (double) ir.left[i] * ir.left[i] + (double) ir.right[i] * ir.right[i];
        if (acc >= fraction * total)
            return (double) i / ir.sampleRate;
    }
    return windowS;
}

inline double highShare (const std::vector<float>& x, double fs, size_t start, size_t end)
{
    const auto hi = analysis::highBandRms (x, fs, 3000.0, start, end);
    const auto all = analysis::rms (x, start, end);
    return hi * hi / std::max (all * all, 1.0e-30);
}

} // namespace cerulean::metrics
