#pragma once

#include "detector/Signals.h"
#include "dsp/chain/HaliteChain.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstring>
#include <functional>
#include <memory>

// The chain over whole buffers, as processBlock runs it, and the measurements the reduction gates
// use (PLAN.md, Measurement and quality gates).
namespace halite::testing
{

using Channels = std::vector<Samples>;

/** Called before each block with its first sample; may change the settings. */
using Automation = std::function<void (int position, dsp::ChainSettings&)>;

struct ChainRun
{
    double rate = 48000.0;
    dsp::ChainSettings settings;
    std::vector<int> blocks { 512 };       // cycled
    const Channels* key = nullptr;
    Automation automate;
};

/** The chain over `in` (1 or 2 channels) under flush-to-zero, as processBlock; the output. */
inline Channels runChain (const Channels& in, const ChainRun& run, dsp::HaliteChain* reuse = nullptr)
{
    const juce::ScopedNoDenormals noDenormals;
    auto owned = std::make_unique<dsp::HaliteChain>();
    auto& chain = reuse != nullptr ? *reuse : *owned;
    if (reuse == nullptr)
        chain.prepare (run.rate, 4096, (int) in.size(), run.settings.detector.latency);

    auto out = in;
    auto s = run.settings;
    const auto n = (int) in[0].size();
    size_t k = 0;
    for (int pos = 0; pos < n;)
    {
        const auto len = std::min (run.blocks[k++ % run.blocks.size()], n - pos);
        if (run.automate)
            run.automate (pos, s);

        std::array<float*, 2> channels {};
        std::array<const float*, 2> side {};
        for (size_t c = 0; c < out.size(); ++c)
            channels[c] = out[c].data() + pos;
        if (run.key != nullptr)
            for (size_t c = 0; c < run.key->size(); ++c)
                side[c] = (*run.key)[c].data() + pos;

        chain.process (channels.data(), (int) out.size(), len, s, run.key != nullptr ? side.data() : nullptr,
                       run.key != nullptr ? (int) run.key->size() : 0);
        pos += len;
    }
    return out;
}

inline bool sameBits (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; }

/** The first sample at which `out` is not `in` delayed by `delay` (silence before), bit for bit; −1 if none. */
inline int firstDifference (const Samples& out, const Samples& in, int delay)
{
    for (int i = 0; i < (int) out.size(); ++i)
        if (! sameBits (out[(size_t) i], i >= delay ? in[(size_t) (i - delay)] : 0.0f))
            return i;
    return -1;
}

/** x moved later by `delay` samples, silence before. */
inline Samples delayed (const Samples& x, int delay)
{
    Samples y (x.size(), 0.0f);
    for (size_t i = (size_t) std::max (delay, 0); i < x.size(); ++i)
        y[i] = x[i - (size_t) delay];
    return y;
}

/** Pink noise (−3 dB per octave above 20 Hz, Kellet's refined filter) at the given RMS. */
inline Samples pinkNoise (int n, double rms, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> gauss;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    Samples out ((size_t) n);
    for (int i = -4096; i < n; ++i)
    {
        const auto w = gauss (rng);
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const auto y = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        if (i >= 0)
            out[(size_t) i] = (float) y;
    }
    return scaled (std::move (out), rms / std::max (rmsOf (out), 1.0e-30));
}

inline Samples sine (double hz, int n, double rate, double amplitude, double phase = 0.0)
{
    Samples out ((size_t) n);
    for (int i = 0; i < n; ++i)
        out[(size_t) i] = (float) (amplitude * std::sin (2.0 * std::numbers::pi * hz * i / rate + phase));
    return out;
}

/** Amplitude of the component at `hz` over [from, to), by least squares on a sine and a cosine. */
inline double amplitudeAt (const Samples& x, double hz, double rate, int from, int to)
{
    double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0;
    for (int i = from; i < to; ++i)
    {
        const auto ph = 2.0 * std::numbers::pi * hz * i / rate;
        const auto s = std::sin (ph), c = std::cos (ph);
        ss += s * s;
        cc += c * c;
        sc += s * c;
        xs += x[(size_t) i] * s;
        xc += x[(size_t) i] * c;
    }
    const auto det = ss * cc - sc * sc;
    const auto a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
    return std::hypot (a, b);
}

/** Peak of the 3rd difference over the RMS, in dB, over [from, to): the click gate of the siblings. */
inline double clickCrestDb (const Samples& x, int from, int to)
{
    auto peak = 0.0, sum = 0.0;
    for (int i = std::max (from, 3); i < to; ++i)
    {
        const auto d = (double) x[(size_t) i] - 3.0 * x[(size_t) i - 1] + 3.0 * x[(size_t) i - 2] - (double) x[(size_t) i - 3];
        peak = std::max (peak, std::abs (d));
    }
    for (int i = from; i < to; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];
    const auto rms = std::sqrt (sum / std::max (1, to - from));
    return 20.0 * std::log10 (std::max (peak, 1.0e-30) / std::max (rms, 1.0e-30));
}

} // namespace halite::testing
