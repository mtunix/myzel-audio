#pragma once

#include "dsp/chain/EchoChain.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <numbers>
#include <vector>

/**
    Signals and measurements for Mulberry's tests and its measurement tool (pure C++, no JUCE):
    rendering through the real chain, echo arrival times, per-repeat tone levels, click detection
    and AES6 weighted flutter.
*/
namespace mulberry::analysis
{

struct Stereo
{
    std::vector<float> left, right;
    double sampleRate = 48000.0;

    size_t size() const noexcept { return left.size(); }
    static Stereo silence (size_t n, double fs) { return { std::vector<float> (n), std::vector<float> (n), fs }; }
};

inline double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-30)); }
inline double fromDb (double db) { return std::pow (10.0, db / 20.0); }

/** Settings that make the chain output the wet echoes only, at unity, with no modulation. */
inline dsp::ChainSettings wetOnly (Machine m)
{
    dsp::ChainSettings s;
    s.machine = m;
    s.wetOnly = true;
    s.mix = 1.0f;
    s.wobble = 0.0f;
    s.noise = 0.0f;
    s.plan.width = 1.0f;
    return s;
}

using SettingsAt = std::function<dsp::ChainSettings (size_t position)>;

/** Renders through a prepared chain in blocks; settings may change per block. */
inline Stereo render (dsp::EchoChain& chain, const SettingsAt& settings, const Stereo& in, size_t length,
                      int block = 256, bool monoInput = false)
{
    Stereo out = Stereo::silence (length, in.sampleRate);
    std::copy_n (in.left.begin(), std::min (in.size(), length), out.left.begin());
    std::copy_n (in.right.begin(), std::min (in.size(), length), out.right.begin());

    dsp::ChainMeters m;
    for (size_t pos = 0; pos < length; pos += (size_t) block)
    {
        const auto n = (int) std::min<size_t> ((size_t) block, length - pos);
        chain.process (out.left.data() + pos, out.right.data() + pos, n, settings (pos), m, monoInput);
    }
    return out;
}

inline Stereo render (dsp::EchoChain& chain, const dsp::ChainSettings& s, const Stereo& in, size_t length, int block = 256)
{
    return render (chain, [&s] (size_t) { return s; }, in, length, block);
}

inline Stereo impulse (size_t n, double fs, float amplitude = 1.0f, size_t at = 0)
{
    auto s = Stereo::silence (n, fs);
    s.left[at] = s.right[at] = amplitude;
    return s;
}

/** A sine burst with raised-cosine edges (5 ms). */
inline Stereo toneBurst (double freq, double fs, double seconds, double amplitude, size_t total)
{
    auto s = Stereo::silence (total, fs);
    const auto n = (size_t) (seconds * fs);
    const auto edge = (size_t) (0.005 * fs);
    for (size_t i = 0; i < std::min (n, total); ++i)
    {
        double env = 1.0;
        if (i < edge)
            env = 0.5 - 0.5 * std::cos (std::numbers::pi * (double) i / (double) edge);
        else if (i > n - edge)
            env = 0.5 - 0.5 * std::cos (std::numbers::pi * (double) (n - i) / (double) edge);
        const auto v = (float) (amplitude * env * std::sin (2.0 * std::numbers::pi * freq * (double) i / fs));
        s.left[i] = s.right[i] = v;
    }
    return s;
}

/** A sine, faded in over fadeInSeconds (raised cosine), so its start leaves no click in the echoes. */
inline Stereo sine (double freq, double fs, double seconds, double amplitude, double fadeInSeconds = 0.05)
{
    const auto n = (size_t) (seconds * fs);
    const auto fade = fadeInSeconds * fs;
    auto s = Stereo::silence (n, fs);
    for (size_t i = 0; i < n; ++i)
    {
        const auto env = (double) i < fade ? 0.5 - 0.5 * std::cos (std::numbers::pi * (double) i / fade) : 1.0;
        s.left[i] = s.right[i] = (float) (env * amplitude * std::sin (2.0 * std::numbers::pi * freq * (double) i / fs));
    }
    return s;
}

/** Paul Kellet's pink noise filter on seeded white noise, about unit RMS. */
inline std::vector<float> pinkNoise (size_t n, uint32_t seed)
{
    std::vector<float> out (n);
    uint32_t state = seed ? seed : 1u;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const auto w = (double) state / 4294967296.0 * 2.0 - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        out[i] = (float) ((b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11 * 2.2);
        b6 = w * 0.115926;
    }
    return out;
}

inline double rms (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, x.size());
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return to > from ? std::sqrt (sum / (double) (to - from)) : 0.0;
}

inline double peak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, x.size());
    double p = 0.0;
    for (size_t i = from; i < to; ++i)
        p = std::max (p, (double) std::abs (x[i]));
    return p;
}

inline bool allFinite (const Stereo& s)
{
    for (size_t i = 0; i < s.size(); ++i)
        if (! std::isfinite (s.left[i]) || ! std::isfinite (s.right[i]))
            return false;
    return true;
}

/**
    Group delay (samples) of the response in x[from, to) at a frequency, relative to sample 0:
    tau = Re{ DFT(n x) / DFT(x) }. For an echo of an impulse, this is its arrival time.
*/
inline double groupDelay (const std::vector<float>& x, size_t from, size_t to, double freq, double fs)
{
    to = std::min (to, x.size());
    const auto w = 2.0 * std::numbers::pi * freq / fs;
    std::complex<double> a {}, b {};
    for (size_t i = from; i < to; ++i)
    {
        const auto e = (double) x[i] * std::polar (1.0, -w * (double) (i - from));   // libc++: no negative magnitude
        a += e;
        b += (double) (i - from) * e;
    }
    return (double) from + (b / a).real();
}

/** Amplitude of a tone in x[from, from + n) (Hann window, coherent gain corrected). */
inline double toneAmplitude (const std::vector<float>& x, size_t from, size_t n, double freq, double fs)
{
    n = std::min (n, x.size() - std::min (from, x.size()));
    const auto w = 2.0 * std::numbers::pi * freq / fs;
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

/** Peak of the 3rd difference (a broadband discontinuity detector) after `start`. */
inline double thirdDifferencePeak (const std::vector<float>& x, size_t start, size_t end = SIZE_MAX)
{
    end = std::min (end, x.size());
    double p = 0.0;
    for (size_t i = std::max<size_t> (start, 3); i < end; ++i)
        p = std::max (p, std::abs ((double) x[i] - 3.0 * x[i - 1] + 3.0 * x[i - 2] - x[i - 3]));
    return p;
}

/** 3rd-difference crest in dB: the click detector of the glide and automation gates. */
inline double clickCrestDb (const std::vector<float>& x, size_t start, size_t end = SIZE_MAX)
{
    return toDb (thirdDifferencePeak (x, start, end) / std::max (rms (x, start, end), 1.0e-12));
}

//==============================================================================
/**
    Weighted peak flutter after AES6-2008 / IEC 60386, in percent: the instantaneous frequency of
    a 3150 Hz test tone, its relative deviation weighted by the standard's curve (a band-pass
    peaking at 4 Hz), reported as the 2-sigma value (the standard's statistical method).

    The weighting is a 2nd-order high-pass at 0.404 Hz (Q 0.677), a 1st-order high-pass at 1.80 Hz
    and a 1st-order low-pass at 10.8 Hz, normalised to 0 dB at 4 Hz: fitted to the standard's
    table, within 1.0 dB from 0.2 to 200 Hz (checked in TransportTests).
*/
class FlutterMeter
{
public:
    static double weightingDb (double f) { return weightingDbRaw (f) - weightingDbRaw (4.0); }

    /** Weighted 2-sigma peak flutter (percent) of a recording of a 3150 Hz tone. */
    static double weightedPercent (const std::vector<float>& x, size_t from, size_t to, double fs, double toneHz = 3150.0)
    {
        to = std::min (to, x.size());
        if (to <= from + 1000)
            return 0.0;

        // Quadrature demodulation, then a 4-pole low-pass at 250 Hz (the mixing image at twice the
        // tone is -100 dB down, so a perfect tone reads 0), decimated to 1 kHz.
        const auto decim = std::max (1, (int) std::lround (fs / 1000.0));
        const auto rate = fs / decim;
        std::vector<double> deviation;
        std::complex<double> lp1 {}, lp2 {}, lp3 {}, lp4 {}, previous {};
        const auto a = 1.0 - std::exp (-2.0 * std::numbers::pi * 250.0 / fs);
        const auto w = 2.0 * std::numbers::pi * toneHz / fs;
        bool havePrevious = false;

        for (size_t i = from; i < to; ++i)
        {
            const auto z = (double) x[i] * std::polar (1.0, -w * (double) (i - from));
            lp1 += a * (z - lp1);
            lp2 += a * (lp1 - lp2);
            lp3 += a * (lp2 - lp3);
            lp4 += a * (lp3 - lp4);
            if ((i - from) % (size_t) decim == 0)
            {
                if (havePrevious)
                {
                    const auto dphi = std::arg (lp4 * std::conj (previous));
                    deviation.push_back (dphi * rate / (2.0 * std::numbers::pi) / toneHz);
                }
                previous = lp4;
                havePrevious = true;
            }
        }

        // Weighting filter (bilinear sections at the decimated rate), then skip its settling.
        Section hp2 = Section::highpass2 (hp2Hz, hp2Q, rate);
        Section hp1 = Section::highpass1 (hp1Hz, rate);
        Section lp1f = Section::lowpass1 (lp1Hz, rate);
        const auto norm = std::pow (10.0, -weightingDbRaw (4.0) / 20.0);
        std::vector<double> weighted;
        for (const auto d : deviation)
            weighted.push_back (norm * lp1f.process (hp1.process (hp2.process (d))));

        const auto skip = std::min (weighted.size(), (size_t) (3.0 * rate));
        double sum = 0.0, sumSq = 0.0;
        const auto count = (double) (weighted.size() - skip);
        for (size_t i = skip; i < weighted.size(); ++i)
        {
            sum += weighted[i];
            sumSq += weighted[i] * weighted[i];
        }
        if (count < 10.0)
            return 0.0;
        const auto mean = sum / count;
        const auto sigma = std::sqrt (std::max (0.0, sumSq / count - mean * mean));
        return 100.0 * 2.0 * sigma;
    }

private:
    static constexpr double hp2Hz = 0.404, hp2Q = 0.677, hp1Hz = 1.804, lp1Hz = 10.8;

    static double weightingDbRaw (double f)
    {
        const auto x = f / hp2Hz;
        const auto hp2 = x * x / std::sqrt ((1.0 - x * x) * (1.0 - x * x) + x * x / (hp2Q * hp2Q));
        const auto hp1 = (f / hp1Hz) / std::sqrt (1.0 + (f / hp1Hz) * (f / hp1Hz));
        const auto lp1 = 1.0 / std::sqrt (1.0 + (f / lp1Hz) * (f / lp1Hz));
        return 20.0 * std::log10 (hp2 * hp1 * lp1);
    }

    struct Section
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        double process (double x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        static Section highpass2 (double f0, double q, double fs)
        {
            const auto k = std::tan (std::numbers::pi * f0 / fs);
            const auto norm = 1.0 / (1.0 + k / q + k * k);
            return { norm, -2.0 * norm, norm, 2.0 * (k * k - 1.0) * norm, (1.0 - k / q + k * k) * norm };
        }

        static Section highpass1 (double f0, double fs)
        {
            const auto k = std::tan (std::numbers::pi * f0 / fs);
            const auto norm = 1.0 / (1.0 + k);
            return { norm, -norm, 0.0, (k - 1.0) * norm, 0.0 };
        }

        static Section lowpass1 (double f0, double fs)
        {
            const auto k = std::tan (std::numbers::pi * f0 / fs);
            const auto norm = 1.0 / (1.0 + k);
            return { k * norm, k * norm, 0.0, (k - 1.0) * norm, 0.0 };
        }
    };
};

} // namespace mulberry::analysis
