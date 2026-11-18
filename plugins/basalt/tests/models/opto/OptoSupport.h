#pragma once

// What the Opto's tests share: the knobs as a snapshot, the carrier at a level on the light's
// mean-square reading, and NOTES.md's time to a fraction of the held reduction.

#include "Reference.h"
#include "dynamics/Harness.h"

#include "dsp/models/opto/OptoModel.h"

namespace basalt::testing::opto
{

namespace ref = basalt::measure::reference::opto;
using basalt::models::OptoModel;

inline constexpr double rate = 48000.0;

inline dynamics::ModelParams params (double t, double inverseRatio, double knee, double range, double attackMs, double releaseMs, bool autoRelease = false)
{
    dynamics::ModelParams p;
    p.curve = { t, inverseRatio, knee, range };
    p.attackMs = attackMs;
    p.releaseMs = releaseMs;
    p.autoRelease = autoRelease;
    return p;
}

inline OptoModel prepared (double sampleRate = rate)
{
    OptoModel m;
    m.prepare (sampleRate);
    return m;
}

/** The carrier whose mean square reads `levelDb` on the detector scale (3.01 dB above its amplitude). */
inline Signal carrierAt (double levelDb, int n)
{
    return carrier (amplitudeFor (levelDb - dynamics::carrierRmsOffsetDb), n);
}

/** The carrier at the level whose static reduction is `depthDb`. */
inline Signal carrierFor (const dynamics::ModelParams& p, double depthDb, int n)
{
    return carrierAt (ref::levelForReduction (depthDb, p.curve.thresholdDb, p.curve.inverseRatio, p.curve.kneeDb), n);
}

/**
    Time from a step at input sample `step` until r first falls to `fraction` of `from` (the
    reduction at t = 0), on the core's convention: the output from input step + j at (j + 1)/fs,
    interpolated. In seconds; NaN if it never gets there.
*/
inline double fallTime (const Trace& r, size_t step, double sampleRate, double from, double fraction)
{
    const auto target = fraction * from;
    double prevT = 0.0, prevV = from;

    for (size_t j = 0; step + j < r.size(); ++j)
    {
        const auto t = (double) (j + 1) / sampleRate;
        const auto v = r[step + j];
        if (v <= target)
            return prevT + (prevV - target) / (prevV - v) * (t - prevT);
        prevT = t;
        prevV = v;
    }

    return std::numeric_limits<double>::quiet_NaN();
}

struct Held
{
    double from = 0.0, seconds = 0.0;
};

/** The carrier at `depthDb` for `holdS`, then silence: the reduction held and the time to `fraction` of it. */
inline Held releaseAfterHold (OptoModel& m, const dynamics::ModelParams& p, double depthDb, double holdS, double fraction, double tailS)
{
    auto key = carrierFor (p, depthDb, samplesFor (holdS, rate));
    const auto end = key.size();
    append (key, silence (samplesFor (tailS, rate)));
    const auto r = runModel (m, p, key, nullptr, 256)[0];
    const auto from = r[end - 1];
    return { from, fallTime (r, end, rate, from, fraction) };
}

/** The same stimulus through the transcription, τ_on from the engine's table so only the cell's law is compared. */
inline ref::Settings referenceFor (const dynamics::ModelParams& p)
{
    ref::Settings s;
    s.thresholdDb = p.curve.thresholdDb;
    s.inverseRatio = p.curve.inverseRatio;
    s.kneeDb = p.curve.kneeDb;
    s.rangeDb = p.curve.rangeDb;
    s.attackMs = p.attackMs;
    s.releaseMs = p.releaseMs;
    s.autoRelease = p.autoRelease;
    s.rate = rate;
    s.attackConstantMs = OptoModel::attackConstantMs (p.attackMs, p.curve.inverseRatio, p.curve.kneeDb);
    return s;
}

} // namespace basalt::testing::opto
