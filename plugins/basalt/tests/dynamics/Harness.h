#pragma once

// Signals, a block runner and NOTES.md's measurement definition (dynamics/NOTES.md, What the
// knobs mean) for the dynamics core's unit tests.

#include "dsp/dynamics/Feedback.h"
#include "dsp/dynamics/KeyPath.h"
#include "dsp/dynamics/Levels.h"
#include "dsp/dynamics/Model.h"
#include "dsp/dynamics/RunningMax.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

namespace basalt::testing
{

using Signal = std::vector<double>;
using Trace = std::vector<double>;

inline double amplitudeFor (double db) { return std::pow (10.0, db / 20.0); }

/** The constant-magnitude carrier: ±A alternating every sample, DC-free, |k| and k² constant. */
inline Signal carrier (double amplitude, int n)
{
    Signal s ((size_t) n);
    for (int i = 0; i < n; ++i)
        s[(size_t) i] = (i % 2 == 0 ? amplitude : -amplitude);
    return s;
}

inline Signal sine (double amplitude, double hz, double rate, int n, double phase = 0.0)
{
    Signal s ((size_t) n);
    for (int i = 0; i < n; ++i)
        s[(size_t) i] = amplitude * std::sin (2.0 * std::numbers::pi * hz * i / rate + phase);
    return s;
}

inline Signal silence (int n) { return Signal ((size_t) n, 0.0); }

inline void append (Signal& a, const Signal& b) { a.insert (a.end(), b.begin(), b.end()); }

/** Pink noise (Kellett's refined filter on white), unit-ish level; scale it afterwards. */
inline Signal pinkNoise (int n, std::uint32_t seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> white (0.0, 1.0);
    Signal s ((size_t) n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& v : s)
    {
        const auto w = white (rng);
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        v = 0.11 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362);
        b6 = w * 0.115926;
    }
    return s;
}

inline void scale (Signal& s, double gain)
{
    for (auto& v : s)
        v *= gain;
}

/** Runs a model over a key of one or two channels in blocks; the reduction per channel and sample. */
inline std::array<Trace, 2> runModel (dynamics::DynamicsModel& model, const dynamics::ModelParams& p,
                                      const Signal& left, const Signal* right = nullptr, int block = 64,
                                      const Signal* lookahead = nullptr)
{
    const auto n = (int) left.size();
    const auto channels = right != nullptr ? 2 : 1;
    std::array<Trace, 2> out { Trace ((size_t) n, 0.0), right != nullptr ? Trace ((size_t) n, 0.0) : Trace {} };

    for (int start = 0; start < n; start += block)
    {
        const auto len = std::min (block, n - start);
        dynamics::KeyBlock key;
        key.numChannels = channels;
        key.numSamples = len;
        key.channels[0] = left.data() + start;
        if (right != nullptr)
            key.channels[1] = right->data() + start;
        if (lookahead != nullptr)
            key.lookahead = lookahead->data() + start;

        std::array<double*, 2> r { out[0].data() + start, right != nullptr ? out[1].data() + start : nullptr };
        model.process (p, key, r.data());
    }

    return out;
}

/** The feedback building block driven as a model would: peak pre-hold, then the loop, one or two channels. */
struct FeedbackHarness
{
    double rate = 48000.0;
    double maxHoldMs = dynamics::feedbackMaxHoldMs;
    double ratioCap = dynamics::feedbackRatioCap;
    bool compensate = true;     // false: the attack coefficient not divided by the ratio (Floru's law)

    std::array<Trace, 2> run (const dynamics::ModelParams& p, const Signal& left, const Signal* right = nullptr, int lookaheadSamples = 0)
    {
        using namespace dynamics;

        const auto channels = right != nullptr ? 2 : 1;
        std::array<RunningMax, 2> hold;
        for (auto& h : hold)
            h.prepare (windowCapacity (rate));

        auto curve = p.curve;
        curve.inverseRatio = cappedInverseRatio (curve.inverseRatio, ratioCap);
        const auto holdMs = feedbackHoldMs (p.releaseMs, maxHoldMs);
        const auto window = feedbackHoldSamples (holdMs, rate) + lookaheadSamples + 1;
        const auto k = FeedbackLoop::coefficientsFor (p.attackMs, p.releaseMs, holdMs, compensate ? curve.inverseRatio : 1.0, rate);

        FeedbackLoop loop;
        std::array<Trace, 2> out { Trace (left.size()), right != nullptr ? Trace (left.size()) : Trace {} };

        for (size_t i = 0; i < left.size(); ++i)
        {
            std::array<double, 2> level {}, r {};
            for (int c = 0; c < channels; ++c)
            {
                const auto x = c == 0 ? left[i] : (*right)[i];
                hold[(size_t) c].setLength (window);
                level[(size_t) c] = peakDb (hold[(size_t) c].push (std::abs (x)));
            }
            loop.process (level.data(), r.data(), channels, curve, k, channels == 2 && p.linked ? p.link : 0.0);
            for (int c = 0; c < channels; ++c)
                out[(size_t) c][i] = r[(size_t) c];
        }

        return out;
    }
};

/**
    Time from a step at input sample `step` until r first reaches 63.2 % of `settled`: the output
    sample from input step + j sits at (j + 1)/fs, the state before the step (r = 0) at t = 0,
    crossings interpolated linearly. In seconds; NaN if it never gets there.
*/
inline double attackTime (const Trace& r, size_t step, double rate, double settled)
{
    const auto target = (1.0 - std::exp (-1.0)) * settled;
    double prevT = 0.0, prevV = 0.0;

    for (size_t j = 0; step + j < r.size(); ++j)
    {
        const auto t = (double) (j + 1) / rate;
        const auto v = r[step + j];
        if (v >= target)
            return prevT + (target - prevV) / (v - prevV) * (t - prevT);
        prevT = t;
        prevV = v;
    }

    return std::numeric_limits<double>::quiet_NaN();
}

/** Time from a step to silence at `step` until r first falls to 36.8 % of `from` (the reduction at t = 0). */
inline double releaseTime (const Trace& r, size_t step, double rate, double from)
{
    const auto target = std::exp (-1.0) * from;
    double prevT = 0.0, prevV = from;

    for (size_t j = 0; step + j < r.size(); ++j)
    {
        const auto t = (double) (j + 1) / rate;
        const auto v = r[step + j];
        if (v <= target)
            return prevT + (prevV - target) / (prevV - v) * (t - prevT);
        prevT = t;
        prevV = v;
    }

    return std::numeric_limits<double>::quiet_NaN();
}

/** NOTES.md's tolerance: ±10 %, or ±0.25 sample where that is wider (times under 2.5 samples). */
inline bool withinTimeGate (double measured, double expected, double rate)
{
    return std::abs (measured - expected) <= std::max (0.10 * expected, 0.25 / rate);
}

inline double mean (const Trace& r, size_t from, size_t to)
{
    auto s = 0.0;
    for (auto i = from; i < to; ++i)
        s += r[i];
    return s / (double) std::max<size_t> (to - from, 1);
}

inline int samplesFor (double seconds, double rate) { return (int) std::lround (seconds * rate); }

} // namespace basalt::testing
