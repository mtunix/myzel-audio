#pragma once

#include "dsp/Engine.h"
#include "dsp/common/ReverbBlocks.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <vector>

/**
    Reverb measurements shared by the tests and tools/measure. Not part of the plugin.

    - renderEngine / impulseResponse: drive an Engine directly, in maxSubBlock chunks.
    - octaveBand: 1-octave band filter (two cascaded RBJ band-passes).
    - estimateRt60: Schroeder backward integration, T30 (falls back to T20).
    - echoDensityProfile / echoDensityTime: Abel & Huang's normalised echo density.
*/
namespace cerulean::analysis
{

struct Stereo
{
    std::vector<float> left, right;
    double sampleRate = 48000.0;

    size_t size() const noexcept { return left.size(); }
};

/** Renders `input` (zero-padded to totalSamples) through the engine. paramsAt(sample) may
    change the parameters over time (called once per sub-block); null keeps `params`. */
inline Stereo renderEngine (dsp::Engine& engine, const dsp::EngineParams& params, const Stereo& input,
                            size_t totalSamples,
                            const std::function<dsp::EngineParams (size_t)>& paramsAt = {})
{
    Stereo out;
    out.sampleRate = input.sampleRate;
    out.left.assign (totalSamples, 0.0f);
    out.right.assign (totalSamples, 0.0f);

    std::array<float, dsp::Engine::maxSubBlock> inL {}, inR {};

    for (size_t pos = 0; pos < totalSamples; pos += dsp::Engine::maxSubBlock)
    {
        const auto n = (int) std::min<size_t> (dsp::Engine::maxSubBlock, totalSamples - pos);
        for (int k = 0; k < n; ++k)
        {
            const auto i = pos + (size_t) k;
            inL[(size_t) k] = i < input.left.size() ? input.left[i] : 0.0f;
            inR[(size_t) k] = i < input.right.size() ? input.right[i] : 0.0f;
        }

        const auto p = paramsAt ? paramsAt (pos) : params;
        engine.process (inL.data(), inR.data(), out.left.data() + pos, out.right.data() + pos, n, p);
    }
    return out;
}

/** Impulse into both inputs (a centred source), engine reset first. */
inline Stereo impulseResponse (dsp::Engine& engine, const dsp::EngineParams& params, double sampleRate,
                               double seconds)
{
    engine.reset();
    Stereo in;
    in.sampleRate = sampleRate;
    in.left = { 1.0f };
    in.right = { 1.0f };
    return renderEngine (engine, params, in, (size_t) (seconds * sampleRate));
}

//==============================================================================
/** Deterministic pink noise (Paul Kellet's filter), roughly -12 dBFS RMS. */
inline std::vector<float> pinkNoise (size_t n, uint32_t seed = 12345)
{
    dsp::Rng rng (seed);
    std::vector<float> out (n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

    for (auto& y : out)
    {
        const double white = rng.bipolar();
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        y = (float) ((b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.11);
        b6 = white * 0.115926;
    }
    return out;
}

inline double rms (const std::vector<float>& x, size_t start = 0, size_t end = (size_t) -1)
{
    end = std::min (end, x.size());
    if (start >= end)
        return 0.0;
    double sum = 0.0;
    for (size_t i = start; i < end; ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) (end - start));
}

inline double toDb (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-12)); }

inline bool allFinite (const Stereo& s)
{
    auto finite = [] (const std::vector<float>& v)
    {
        return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); });
    };
    return finite (s.left) && finite (s.right);
}

inline double peak (const Stereo& s)
{
    double p = 0.0;
    for (size_t i = 0; i < s.size(); ++i)
        p = std::max (p, (double) std::max (std::abs (s.left[i]), std::abs (s.right[i])));
    return p;
}

/** Pearson correlation of two channels over [start, end). */
inline double correlation (const std::vector<float>& a, const std::vector<float>& b, size_t start, size_t end)
{
    end = std::min ({ end, a.size(), b.size() });
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = start; i < end; ++i)
    {
        sab += (double) a[i] * b[i];
        saa += (double) a[i] * a[i];
        sbb += (double) b[i] * b[i];
    }
    return sab / std::sqrt (std::max (saa * sbb, 1.0e-30));
}

//==============================================================================
/** RBJ biquad, double precision. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    static Biquad bandpass (double fc, double q, double fs)
    {
        const auto w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto a0 = 1.0 + alpha;
        Biquad b;
        b.b0 = alpha / a0;
        b.b1 = 0.0;
        b.b2 = -alpha / a0;
        b.a1 = -2.0 * std::cos (w0) / a0;
        b.a2 = (1.0 - alpha) / a0;
        return b;
    }

    static Biquad highpass (double fc, double q, double fs)
    {
        const auto w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto cw = std::cos (w0);
        const auto a0 = 1.0 + alpha;
        Biquad b;
        b.b0 = (1.0 + cw) / 2.0 / a0;
        b.b1 = -(1.0 + cw) / a0;
        b.b2 = (1.0 + cw) / 2.0 / a0;
        b.a1 = -2.0 * cw / a0;
        b.a2 = (1.0 - alpha) / a0;
        return b;
    }

    static Biquad lowpass (double fc, double q, double fs)
    {
        const auto w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto cw = std::cos (w0);
        const auto a0 = 1.0 + alpha;
        Biquad b;
        b.b0 = (1.0 - cw) / 2.0 / a0;
        b.b1 = (1.0 - cw) / a0;
        b.b2 = (1.0 - cw) / 2.0 / a0;
        b.a1 = -2.0 * cw / a0;
        b.a2 = (1.0 - alpha) / a0;
        return b;
    }

    double process (double x)
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/** One-octave band around centreHz (two cascaded band-passes, Q = sqrt 2). */
inline std::vector<double> octaveBand (const std::vector<float>& x, double fs, double centreHz)
{
    auto f1 = Biquad::bandpass (centreHz, 1.41421356, fs);
    auto f2 = f1;
    std::vector<double> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = f2.process (f1.process (x[i]));
    return y;
}

//==============================================================================
/** Schroeder energy decay curve in dB (0 dB at the start). */
inline std::vector<double> energyDecayCurveDb (const std::vector<double>& x)
{
    std::vector<double> edc (x.size());
    double acc = 0.0;
    for (size_t i = x.size(); i-- > 0;)
    {
        acc += x[i] * x[i];
        edc[i] = acc;
    }
    const auto total = std::max (edc.empty() ? 0.0 : edc[0], 1.0e-30);
    for (auto& e : edc)
        e = 10.0 * std::log10 (std::max (e / total, 1.0e-30));
    return edc;
}

struct DecayFit
{
    double rt60 = 0.0;
    bool valid = false;
};

/** RT60 from the decay between -5 dB and endDb (T30: -35, T20: -25), least-squares fit. */
inline DecayFit fitRt60 (const std::vector<double>& edcDb, double fs, double endDb)
{
    size_t i0 = 0, i1 = 0;
    while (i0 < edcDb.size() && edcDb[i0] > -5.0)
        ++i0;
    i1 = i0;
    while (i1 < edcDb.size() && edcDb[i1] > endDb)
        ++i1;
    if (i1 >= edcDb.size() || i1 - i0 < 16)
        return {};

    // Least squares on (t, dB).
    double st = 0, sy = 0, stt = 0, sty = 0;
    const auto n = (double) (i1 - i0);
    for (size_t i = i0; i < i1; ++i)
    {
        const auto t = (double) i / fs;
        st += t; sy += edcDb[i]; stt += t * t; sty += t * edcDb[i];
    }
    const auto slope = (n * sty - st * sy) / std::max (n * stt - st * st, 1.0e-30);
    if (slope >= 0.0)
        return {};
    return { -60.0 / slope, true };
}

/** RT60 of a signal (optionally band-filtered first): T30, or T20 if T30 is out of range. */
inline DecayFit estimateRt60 (const std::vector<double>& x, double fs)
{
    const auto edc = energyDecayCurveDb (x);
    if (auto t30 = fitRt60 (edc, fs, -35.0); t30.valid)
        return t30;
    return fitRt60 (edc, fs, -25.0);
}

inline DecayFit estimateRt60 (const std::vector<float>& x, double fs)
{
    return estimateRt60 (std::vector<double> (x.begin(), x.end()), fs);
}

/** Mean RT60 of the 500 Hz, 1 kHz and 2 kHz octave bands of both channels. */
inline double midBandRt60 (const Stereo& ir)
{
    double sum = 0.0;
    int count = 0;
    for (const auto* ch : { &ir.left, &ir.right })
        for (const auto fc : { 500.0, 1000.0, 2000.0 })
            if (const auto fit = estimateRt60 (octaveBand (*ch, ir.sampleRate, fc), ir.sampleRate); fit.valid)
            {
                sum += fit.rt60;
                ++count;
            }
    return count > 0 ? sum / count : 0.0;
}

/** RT60 of one octave band (mean of both channels). */
inline double bandRt60 (const Stereo& ir, double centreHz)
{
    double sum = 0.0;
    int count = 0;
    for (const auto* ch : { &ir.left, &ir.right })
        if (const auto fit = estimateRt60 (octaveBand (*ch, ir.sampleRate, centreHz), ir.sampleRate); fit.valid)
        {
            sum += fit.rt60;
            ++count;
        }
    return count > 0 ? sum / count : 0.0;
}

//==============================================================================
/**
    Normalised echo density (Abel & Huang 2006): fraction of samples in a 20 ms window lying
    outside one standard deviation, divided by the Gaussian expectation (erfc (1/sqrt 2)).
    ~0 for sparse echoes, ~1 for a fully diffuse tail. One value per hopMs.
*/
inline std::vector<double> echoDensityProfile (const std::vector<float>& x, double fs, double windowMs = 20.0,
                                               double hopMs = 1.0)
{
    const auto win = (size_t) (windowMs * 0.001 * fs);
    const auto hop = std::max<size_t> (1, (size_t) (hopMs * 0.001 * fs));
    const double expected = 0.31731050786291415;   // erfc (1 / sqrt 2)

    std::vector<double> w (win);
    double wsum = 0.0;
    for (size_t i = 0; i < win; ++i)
    {
        w[i] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * ((double) i + 0.5) / (double) win);
        wsum += w[i];
    }
    for (auto& v : w)
        v /= wsum;

    std::vector<double> profile;
    for (size_t start = 0; start + win <= x.size(); start += hop)
    {
        double var = 0.0;
        for (size_t i = 0; i < win; ++i)
            var += w[i] * (double) x[start + i] * x[start + i];
        const auto sd = std::sqrt (var);

        double outside = 0.0;
        for (size_t i = 0; i < win; ++i)
            if (std::abs ((double) x[start + i]) > sd)
                outside += w[i];

        profile.push_back (sd > 0.0 ? outside / expected : 0.0);
    }
    return profile;
}

/**
    Band-limits and decimates a signal to 48 kHz when fs is an integer multiple of it, so echo
    density (which counts per sample) compares fairly across sample rates. Other rates pass.
*/
inline std::vector<float> decimateTo48k (const std::vector<float>& x, double fs)
{
    const auto factor = (int) std::lround (fs / 48000.0);
    if (factor <= 1 || std::abs (fs - 48000.0 * factor) > 1.0)
        return x;

    auto f1 = Biquad::lowpass (20000.0, 0.5412, fs);
    auto f2 = Biquad::lowpass (20000.0, 1.3066, fs);
    std::vector<float> y;
    y.reserve (x.size() / (size_t) factor + 1);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto v = f2.process (f1.process (x[i]));
        if (i % (size_t) factor == 0)
            y.push_back ((float) v);
    }
    return y;
}

/** Time (s, window centre) at which the echo density first reaches `threshold`; -1 if never. */
inline double echoDensityTime (const std::vector<float>& x, double fs, double threshold = 0.9)
{
    const auto profile = echoDensityProfile (x, fs);
    // Light smoothing (5 ms) so a single lucky window doesn't count.
    for (size_t i = 0; i + 5 <= profile.size(); ++i)
    {
        double m = 0.0;
        for (size_t k = 0; k < 5; ++k)
            m += profile[i + k];
        if (m / 5.0 >= threshold)
            return ((double) i + 2.0) * 0.001 + 0.010;
    }
    return -1.0;
}

/** Energy of a band above cutoffHz (4th-order highpass), as RMS over [start, end). */
inline double highBandRms (const std::vector<float>& x, double fs, double cutoffHz, size_t start, size_t end)
{
    auto f1 = Biquad::highpass (cutoffHz, 0.5412, fs);
    auto f2 = Biquad::highpass (cutoffHz, 1.3066, fs);
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

} // namespace cerulean::analysis
