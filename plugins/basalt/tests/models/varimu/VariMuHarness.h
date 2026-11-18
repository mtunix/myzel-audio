#pragma once

// What every Vari-mu test file shares: the settings, a prepared model, the staircase the static
// gates read, and the reference step the attack calibration and the attack gate measure.

#include "dynamics/Harness.h"
#include "models/varimu/Reference.h"

#include "dsp/models/varimu/VariMuModel.h"

#include <algorithm>
#include <vector>

namespace basalt::testing::varimu
{

namespace ref = measure::reference::varimu;
using models::VariMuModel;

inline dynamics::ModelParams params (double t, double inverseRatio, double knee, double range, double attackMs, double releaseMs, bool autoRelease = false)
{
    dynamics::ModelParams p;
    p.curve = { t, inverseRatio, knee, range };
    p.attackMs = attackMs;
    p.releaseMs = releaseMs;
    p.autoRelease = autoRelease;
    return p;
}

inline VariMuModel prepared (double rate)
{
    VariMuModel m;
    m.prepare (rate);
    return m;
}

/** A key of one or two channels, with the model's reduction for it. */
inline Trace reductionFor (VariMuModel& m, const dynamics::ModelParams& p, const Signal& key, int block = 64)
{
    return runModel (m, p, key, nullptr, block)[0];
}

/** The carrier's amplitude whose static reduction is `depth` dB (the peak reading). */
inline double carrierAmplitudeFor (const dynamics::ModelParams& p, double depth)
{
    return amplitudeFor (ref::levelForReduction (depth, p.curve.thresholdDb, p.curve.inverseRatio, p.curve.kneeDb));
}

/** The closed loop's slowest time constant at the level x, in s: τ·R_ref / R_local, τ = Attack/Φ (NOTES.md, Attack). */
inline double loopSettleS (const dynamics::ModelParams& p, double x)
{
    const auto tau = std::max (p.attackMs, ref::attackFloorMs) * 0.001 / ref::attackCalibration (p.curve.inverseRatio, p.curve.kneeDb);
    const auto rRef = p.curve.inverseRatio < 1.0 ? ref::localRatioAtReduction (ref::referenceReductionDb, p.curve.inverseRatio, p.curve.kneeDb) : 1.0;
    const auto local = ref::localRatio (x, p.curve.thresholdDb, p.curve.inverseRatio, p.curve.kneeDb);
    return tau * rRef / local;
}

struct Staircase
{
    std::vector<double> levels;         // dB on the detector scale
    std::vector<double> settled;        // the mean reduction over each step's last 20 ms
};

/**
    Ascending 1 dB steps from `from` to `to` dB, each held until the loop has settled (twelve of its
    slowest constants at that level, at least 0.15 s) and read over its last 20 ms: the carrier, or a
    sine of `hz` with its phase continuous.
*/
inline Staircase staircase (VariMuModel& m, const dynamics::ModelParams& p, double rate, double from, double to, double hz = 0.0)
{
    Staircase s;
    Signal key;
    std::vector<size_t> ends;
    const auto measure = samplesFor (0.020, rate);

    for (auto level = from; level <= to + 1.0e-9; level += 1.0)
    {
        const auto settle = samplesFor (std::clamp (12.0 * loopSettleS (p, level), 0.15, 4.0), rate);
        const auto n = settle + measure;
        const auto a = amplitudeFor (level);
        append (key, hz > 0.0 ? sine (a, hz, rate, n, 2.0 * std::numbers::pi * hz * (double) key.size() / rate) : carrier (a, n));
        s.levels.push_back (level);
        ends.push_back (key.size());
    }

    const auto r = reductionFor (m, p, key);
    for (const auto end : ends)
        s.settled.push_back (mean (r, end - (size_t) measure, end));
    return s;
}

struct Step
{
    double attackS = 0.0, settled = 0.0, overshoot = 0.0;
};

/**
    The reference step and its deeper siblings (NOTES.md, Attack): from silence to the carrier at the
    level of `depth` dB of static reduction, held `holdS`; the 63 % time against the settled
    reduction, and the largest excursion over it.
*/
inline Step attackStep (VariMuModel& m, const dynamics::ModelParams& p, double depth, double holdS, double rate)
{
    constexpr int pre = 16;
    auto key = silence (pre);
    append (key, carrier (carrierAmplitudeFor (p, depth), samplesFor (holdS, rate)));
    m.reset();
    const auto r = reductionFor (m, p, key);
    Step s;
    s.settled = r.back();
    s.attackS = attackTime (r, (size_t) pre, rate, s.settled);
    s.overshoot = *std::max_element (r.begin(), r.end()) - s.settled;
    return s;
}

} // namespace basalt::testing::varimu
