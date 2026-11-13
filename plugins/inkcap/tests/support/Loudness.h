#pragma once

#include "support/Analysis.h"

#include <cmath>
#include <cstdint>
#include <vector>

/**
    Loudness for Match (PLAN.md, Match): the calibration (tools/calibrate), its gate in the chain
    suite and InkcapMeasure. Offline only, JUCE-free.
*/
namespace inkcap::loudness
{

/** The calibration's programme level: a mix at 0 VU. */
constexpr double programmeDb = -18.0;

/** The rate Match is calibrated at (BS.1770's K-weighting coefficients are given for it). */
constexpr double calibrationRate = 48000.0;

/**
    Deterministic pink noise (Paul Kellet's refined filter over a counter-based white source),
    the same in both channels (a centred mix: most of a programme's energy), at programmeDb RMS.
*/
inline analysis::Stereo pinkNoise (double sampleRate, double seconds, uint32_t salt = 20261113)
{
    const auto white = analysis::noise ((size_t) std::lround (seconds * sampleRate), salt, 1.0);
    std::vector<float> pink (white.size());
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, sum = 0;
    for (size_t i = 0; i < white.size(); ++i)
    {
        const double w = white[i];
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        pink[i] = (float) p;
        sum += p * p;
    }

    const auto gain = std::pow (10.0, programmeDb / 20.0) / std::sqrt (sum / (double) std::max<size_t> (pink.size(), 1));
    for (auto& v : pink)
        v = (float) (v * gain);
    return { pink, pink };
}

/** ITU-R BS.1770-4 K-weighted mean square of one channel at 48 kHz, from `from` on (filters run from the start). */
inline double kWeightedMeanSquare (const std::vector<float>& x, size_t from)
{
    struct Biquad
    {
        double b0, b1, b2, a1, a2;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        double process (double x) noexcept
        {
            const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
    Biquad highpass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };

    double sum = 0.0;
    size_t count = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto v = highpass.process (shelf.process ((double) x[i]));
        if (i >= from)
        {
            sum += v * v;
            ++count;
        }
    }
    return sum / (double) std::max<size_t> (count, 1);
}

/** Stereo loudness in dB (BS.1770: the channels' K-weighted mean squares summed), from `from` on. */
inline double loudnessDb (const analysis::Stereo& x, size_t from)
{
    return 10.0 * std::log10 (kWeightedMeanSquare (x.left, from) + kWeightedMeanSquare (x.right, from) + 1.0e-30);
}

} // namespace inkcap::loudness
