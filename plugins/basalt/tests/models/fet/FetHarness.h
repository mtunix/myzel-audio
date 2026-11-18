#pragma once

// What FET's tests share: parameters from plain knob values, a prepared model, the staircase the
// static gate runs, and blocks of key with a look-ahead and a schedule of parameters.

#include "dynamics/Harness.h"
#include "models/fet/Reference.h"

#include "dsp/models/fet/FetModel.h"

#include <functional>

namespace basalt::testing::fet
{

namespace ref = basalt::reference::fet;
using basalt::models::FetModel;

/** Knob values to a snapshot; ratio is plain, ∞ is All. Auto release off unless asked. */
inline dynamics::ModelParams params (double t, double ratio, double knee, double range, double attackMs, double releaseMs, bool autoRelease = false)
{
    dynamics::ModelParams p;
    p.curve = { t, std::isinf (ratio) ? 0.0 : 1.0 / ratio, knee, range };
    p.attackMs = attackMs;
    p.releaseMs = releaseMs;
    p.autoRelease = autoRelease;
    return p;
}

inline FetModel prepared (double rate)
{
    FetModel m;
    m.prepare (rate);
    return m;
}

/** Ascending steady levels, each held for its own time, then measured over a tail. */
struct Staircase
{
    Signal key;
    std::vector<double> levels;
    std::vector<size_t> ends;       // one past each step's last sample
    size_t measure = 0;
};

/**
    Levels on the peak scale from `from` to `to` in `step` dB, as 1 kHz-type sines (hz > 0, phase
    continuous) or the carrier (hz = 0); each step held settleS(level) seconds plus measureS.
*/
inline Staircase staircase (double from, double to, double step, double hz, double rate, double measureS,
                            const std::function<double (double)>& settleS)
{
    Staircase s;
    s.measure = (size_t) samplesFor (measureS, rate);
    for (auto level = from; level <= to + 1.0e-9; level += step)
    {
        const auto n = samplesFor (settleS (level), rate) + (int) s.measure;
        const auto a = amplitudeFor (level);
        append (s.key, hz > 0.0 ? sine (a, hz, rate, n, 2.0 * std::numbers::pi * hz * (double) s.key.size() / rate) : carrier (a, n));
        s.levels.push_back (level);
        s.ends.push_back (s.key.size());
    }
    return s;
}

/** One or two channels of key through the model in blocks of `block`, the parameters per block from `at` (its first sample). */
inline std::array<Trace, 2> runScheduled (dynamics::DynamicsModel& m, const Signal& left, const Signal* right, int block,
                                          const std::function<dynamics::ModelParams (int)>& at, const Signal* lookahead = nullptr)
{
    const auto n = (int) left.size();
    std::array<Trace, 2> out { Trace ((size_t) n), right != nullptr ? Trace ((size_t) n) : Trace {} };
    for (int start = 0; start < n; start += block)
    {
        dynamics::KeyBlock k;
        k.numChannels = right != nullptr ? 2 : 1;
        k.numSamples = std::min (block, n - start);
        k.channels[0] = left.data() + start;
        if (right != nullptr)
            k.channels[1] = right->data() + start;
        if (lookahead != nullptr)
            k.lookahead = lookahead->data() + start;
        std::array<double*, 2> o { out[0].data() + start, right != nullptr ? out[1].data() + start : nullptr };
        m.process (at (start), k, o.data());
    }
    return out;
}

/** Time from `step` until r first exceeds `level` dB, interpolated as attackTime(); in seconds. */
inline double timeToReach (const Trace& r, size_t step, double rate, double level)
{
    double prevT = 0.0, prevV = 0.0;
    for (size_t j = 0; step + j < r.size(); ++j)
    {
        const auto t = (double) (j + 1) / rate;
        const auto v = r[step + j];
        if (v > level)
            return prevT + (level - prevV) / (v - prevV) * (t - prevT);
        prevT = t;
        prevV = v;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace basalt::testing::fet
