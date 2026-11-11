#pragma once

// Header-only measurement helpers shared by all DSP tests. No JUCE dependency.

#include "dsp/common/CircuitModel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

namespace umber::test
{

inline std::vector<float> sine (double freq, double amplitude, double sampleRate, int numSamples, double phase = 0.0)
{
    std::vector<float> out ((size_t) numSamples);
    const auto w = 2.0 * std::numbers::pi * freq / sampleRate;

    for (int i = 0; i < numSamples; ++i)
        out[(size_t) i] = (float) (amplitude * std::sin (w * i + phase));

    return out;
}

/** Amplitude of a single frequency over a window (Goertzel, Hann-windowed, amplitude-corrected). */
inline double toneAmplitude (const float* x, int n, double freq, double sampleRate)
{
    const auto w = 2.0 * std::numbers::pi * freq / sampleRate;
    double re = 0.0, im = 0.0, windowSum = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto win = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / (n - 1));
        re += win * x[i] * std::cos (w * i);
        im -= win * x[i] * std::sin (w * i);
        windowSum += win;
    }

    return 2.0 * std::sqrt (re * re + im * im) / windowSum;
}

/** Signed gain of the fundamental: correlates output with input to detect inversion. */
inline double correlation (const float* a, const float* b, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += (double) a[i] * b[i];
    return sum;
}

struct HarmonicResult
{
    double fundamental = 0.0;       // output amplitude at f0
    std::vector<double> harmonics;  // amplitudes of 2f0 .. (N+1)f0
    double thd = 0.0;               // ratio, not percent
    double gainDb = 0.0;            // fundamental out / in
    bool inverted = false;

    double evenOddRatio() const
    {
        double even = 0.0, odd = 0.0;
        for (size_t i = 0; i < harmonics.size(); ++i)
            ((i % 2 == 0) ? even : odd) += harmonics[i] * harmonics[i]; // index 0 == H2 (even)
        return odd > 0.0 ? std::sqrt (even / odd) : 1.0e9;
    }
};

/**
    Drives a model with a sine of peak amplitude `amplitude`, lets it settle, and measures
    the harmonic content. Harmonics above Nyquist are ignored.
*/
inline HarmonicResult measureHarmonics (dsp::CircuitModel& model, double sampleRate, double freq,
                                        double amplitude, int numHarmonics = 9)
{
    model.prepare (sampleRate);
    model.reset();

    // Settle for at least 0.25 s and 25 periods: low tones leave decaying DC flux/charge in
    // transformer cores and coupling caps when they start at phase 0.
    const int settle  = (int) (std::max (0.25, 25.0 / freq) * sampleRate);
    const int periods = std::max (20, (int) std::round (0.1 * freq));
    const int window  = (int) std::round (periods * sampleRate / freq);

    auto signal = sine (freq, amplitude, sampleRate, settle + window);
    auto input  = signal;
    model.process (signal.data(), (int) signal.size());

    const float* y = signal.data() + settle;
    const float* x = input.data() + settle;

    // remove DC from the analysis window (the chain DC-blocks after the model)
    std::vector<float> yac (y, y + window);
    double mean = 0.0;
    for (auto v : yac) mean += v;
    mean /= window;
    for (auto& v : yac) v = (float) (v - mean);

    HarmonicResult r;
    r.fundamental = toneAmplitude (yac.data(), window, freq, sampleRate);
    r.gainDb = 20.0 * std::log10 (std::max (r.fundamental, 1.0e-12) / amplitude);
    r.inverted = correlation (x, yac.data(), window) < 0.0;

    double sumSq = 0.0;
    for (int h = 2; h <= numHarmonics + 1; ++h)
    {
        if (h * freq >= 0.45 * sampleRate)
            break;

        const auto a = toneAmplitude (yac.data(), window, h * freq, sampleRate);
        r.harmonics.push_back (a);
        sumSq += a * a;
    }

    r.thd = std::sqrt (sumSq) / std::max (r.fundamental, 1.0e-12);
    return r;
}

struct BoundsResult
{
    bool allFinite = true;
    double peak = 0.0;
};

inline BoundsResult measureBounds (dsp::CircuitModel& model, const std::vector<float>& input)
{
    auto buffer = input;
    model.process (buffer.data(), (int) buffer.size());

    BoundsResult r;
    for (auto v : buffer)
    {
        if (! std::isfinite (v))
            r.allFinite = false;
        else
            r.peak = std::max (r.peak, (double) std::abs (v));
    }
    return r;
}

/** Average processing cost in nanoseconds per sample (one channel). */
inline double nanosecondsPerSample (dsp::CircuitModel& model, double sampleRate, double amplitude, int numSamples = 1 << 18)
{
    model.prepare (sampleRate);
    model.reset();

    auto signal = sine (997.0, amplitude, sampleRate, numSamples);
    constexpr int block = 256;

    // warm-up
    for (int i = 0; i + block <= numSamples / 4; i += block)
        model.process (signal.data() + i, block);

    signal = sine (997.0, amplitude, sampleRate, numSamples);

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i + block <= numSamples; i += block)
        model.process (signal.data() + i, block);
    const auto end = std::chrono::steady_clock::now();

    return std::chrono::duration<double, std::nano> (end - start).count() / numSamples;
}

} // namespace umber::test
