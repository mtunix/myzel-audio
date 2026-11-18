#pragma once

#include "common/dsp/Filters.h"
#include "common/dsp/FloatCompare.h"
#include "common/testing/PortableRandom.h"
#include "dsp/detector/DetectorProbe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

// Synthetic test signals for the detector (PLAN.md, Measurement: harmonic vowels, noise-burst
// esses, resonant peaks) and small helpers around the offline probe.
namespace halite::testing
{

using Samples = std::vector<float>;

inline int samplesFor (double seconds, double rate) { return (int) std::lround (seconds * rate); }

inline double rmsOf (const Samples& x, int from = 0, int to = -1)
{
    to = to < 0 ? (int) x.size() : to;
    auto sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (sum / std::max (1, to - from));
}

inline Samples scaled (Samples x, double gain)
{
    for (auto& v : x)
        v = (float) (v * gain);
    return x;
}

/** Gaussian noise through an 8th-order Butterworth band-pass (four sections on each edge), at the given RMS. */
inline Samples bandNoise (double lowHz, double highHz, int n, double rate, unsigned seed, double rms)
{
    constexpr std::array<double, 4> qs { 0.5097956, 0.6013449, 0.8999762, 2.5629154 };
    std::array<myzel::dsp::TptSvf, 4> hp, lp;
    for (size_t s = 0; s < qs.size(); ++s)
    {
        hp[s].setCoefficients (lowHz, qs[s], rate);
        lp[s].setCoefficients (highHz, qs[s], rate);
    }

    myzel::testing::PortableRandom rng (seed);
    Samples out ((size_t) n);
    for (int i = -4096; i < n; ++i)     // the filters settle before the first sample kept
    {
        auto x = rng.gaussian();
        for (size_t s = 0; s < qs.size(); ++s)
            x = lp[s].processLowpass (hp[s].processHighpass (x));
        if (i >= 0)
            out[(size_t) i] = (float) x;
    }
    const auto gain = rms / std::max (rmsOf (out), 1.0e-30);
    return scaled (std::move (out), gain);
}

/** A periodic sound: harmonics of f0 up to topHz, sloping by tiltDbPerOctave from the first, random phases. */
struct Harmonics
{
    double f0 = 220.0;
    double topHz = 16000.0;
    double tiltDbPerOctave = 0.0;   // 0: equal amplitudes (NOTES.md's bright harmonic signal)
    double vibratoCents = 0.0;      // peak deviation
    double vibratoHz = 5.5;
    double peakHz = 0.0;            // a resonance on top of the tilt (0: none), as a harsh vowel has
    double peakDb = 0.0;
    double peakOctaves = 0.5;       // its width between the −3 dB points of the boost in dB
    double rms = 0.1;
    unsigned seed = 1;
};

inline Samples harmonic (const Harmonics& h, int n, double rate)
{
    const auto count = std::max (1, (int) std::floor (h.topHz / (h.f0 * std::exp2 (std::abs (h.vibratoCents) / 1200.0))));
    myzel::testing::PortableRandom rng (h.seed);

    std::vector<double> amplitude ((size_t) count), phase ((size_t) count);
    for (int k = 0; k < count; ++k)
    {
        auto db = h.tiltDbPerOctave * std::log2 (k + 1.0);
        if (h.peakHz > 0.0)
        {
            const auto distance = std::log2 ((k + 1.0) * h.f0 / h.peakHz) / (0.5 * h.peakOctaves);
            db += h.peakDb / (1.0 + distance * distance);
        }
        amplitude[(size_t) k] = std::pow (10.0, db / 20.0);
        phase[(size_t) k] = rng.uniform (0.0, 2.0 * std::numbers::pi);
    }

    Samples out ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const auto f = h.f0 * std::exp2 (h.vibratoCents / 1200.0 * std::sin (2.0 * std::numbers::pi * h.vibratoHz * i / rate));
        const auto step = 2.0 * std::numbers::pi * f / rate;
        auto x = 0.0;
        for (int k = 0; k < count; ++k)
        {
            x += amplitude[(size_t) k] * std::sin (phase[(size_t) k]);
            phase[(size_t) k] = std::fmod (phase[(size_t) k] + step * (k + 1), 2.0 * std::numbers::pi);
        }
        out[(size_t) i] = (float) x;
    }
    const auto gain = h.rms / std::max (rmsOf (out), 1.0e-30);
    return scaled (std::move (out), gain);
}

/** A spoken-vowel stand-in: harmonics of f0 falling 12 dB per octave, so its body is strong and its top end quiet. */
inline Samples vowel (double f0, int n, double rate, double rms, unsigned seed = 1)
{
    return harmonic ({ .f0 = f0, .topHz = 0.45 * rate, .tiltDbPerOctave = -12.0, .rms = rms, .seed = seed }, n, rate);
}

/** Keeps x in [from, to) with 2 ms raised-cosine edges, silence elsewhere. */
inline Samples gated (Samples x, int from, int to, double rate)
{
    const auto edge = std::max (1, samplesFor (0.002, rate));
    for (int i = 0; i < (int) x.size(); ++i)
    {
        auto g = 0.0;
        if (i >= from && i < to)
        {
            const auto d = std::min (i - from, to - 1 - i);
            g = d >= edge ? 1.0 : 0.5 - 0.5 * std::cos (std::numbers::pi * d / edge);
        }
        x[(size_t) i] = (float) (x[(size_t) i] * g);
    }
    return x;
}

inline Samples sum (const Samples& a, const Samples& b)
{
    Samples out (std::max (a.size(), b.size()), 0.0f);
    for (size_t i = 0; i < a.size(); ++i)
        out[i] += a[i];
    for (size_t i = 0; i < b.size(); ++i)
        out[i] += b[i];
    return out;
}

/** The probe on one or two channels. */
inline dsp::probe::Result probeChannels (const std::vector<const Samples*>& channels, double rate, const dsp::DetectorSettings& s,
                                   const std::vector<int>& blocks = { 512 }, const std::vector<const Samples*>& key = {})
{
    std::array<const float*, 2> in {}, side {};
    for (size_t c = 0; c < channels.size(); ++c)
        in[c] = channels[c]->data();
    for (size_t c = 0; c < key.size(); ++c)
        side[c] = key[c]->data();
    return dsp::probe::run (in.data(), (int) channels.size(), (int) channels[0]->size(), rate, s,
                            key.empty() ? nullptr : side.data(), (int) key.size(), blocks);
}

inline dsp::probe::Result probeOf (const Samples& mono, double rate, const dsp::DetectorSettings& s, const std::vector<int>& blocks = { 512 })
{
    return probeChannels ({ &mono }, rate, s, blocks);
}

/** The hop that contains sample i. */
inline int hopAt (int sample) { return sample / dsp::Detector::hopSize; }

inline int hopAt (double seconds, double rate) { return hopAt (samplesFor (seconds, rate)); }

/** Settings with every detector constant at its default, Voice, the given cue. */
inline dsp::DetectorSettings voiceSettings (dsp::NoiseCueKind cue = dsp::NoiseCueKind::periodicity)
{
    dsp::DetectorSettings s;
    s.noiseCue = cue;
    return s;
}

} // namespace halite::testing
