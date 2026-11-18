#pragma once

#include "dsp/dynamics/GainComputer.h"
#include "dsp/dynamics/Levels.h"

#include <algorithm>
#include <array>
#include <cmath>

// The feedback loop of NOTES.md (Feedback, Settled 29-30), for the models whose side chain hears
// their own output (FET, Vari-mu, 1.1's Diode bridge). Clean, VCA, Opto and Upward are
// feed-forward and do not use it.

namespace basalt::dynamics
{

/** The ratio a feedback model caps Ratio at unless its NOTES set less; ∞ reads as the cap. */
inline constexpr double feedbackRatioCap = 50.0;

/** The pre-hold's longest h; a model may set less (FET: 1 ms). */
inline constexpr double feedbackMaxHoldMs = 5.0;

inline double cappedInverseRatio (double inverseRatio, double cap) noexcept
{
    return std::max (inverseRatio, 1.0 / cap);
}

/** h = min(Release/10, maxHold), in ms. */
inline double feedbackHoldMs (double releaseMs, double maxHoldMs = feedbackMaxHoldMs) noexcept
{
    return std::min (0.1 * releaseMs, maxHoldMs);
}

/** h in whole samples: the pre-hold window is this plus the look-ahead plus one. */
inline int feedbackHoldSamples (double holdMs, double sampleRate) noexcept
{
    return (int) std::lround (holdMs * 0.001 * sampleRate);
}

/** eq. 4's demand, G⁻¹(y) − y: FeedbackLoop's default, for every loop whose static curve is eq. 4 (FET, Diode bridge). */
struct InverseCurveDemand
{
    double operator() (const Curve& c, double outputDb) const noexcept { return feedbackDemand (c, outputDb); }
};

/**
    Steps 2 to 4 of NOTES.md's loop, per detector channel and sample. The model brings step 1,
    the pre-hold: its key level (peak, RMS or a cell) held over h plus the look-ahead in a
    RunningMax, in dB. Each sample the loop estimates the output as that level less the previous
    reduction (Giannoulis 2012 §3.1.1, which works with an external key too), asks the inverted
    curve for the reduction that output implies, links the demands, and follows them with a smooth
    branching detector (eq. 16) whose attack is divided by the set ratio, with Range on the state.

    What a model may change (NOTES.md, Feedback, What a feedback model's NOTES choose):
    - the demand: a functor (Curve, output level in dB) → reduction in dB, eq. 4's inverse by
      default; a curve that changes with level (Vari-mu) brings its own inverse;
    - the reduction the estimate subtracts, when the audio carries more than the loop's own state
      (Vari-mu's r = max(s, m, ℓ)); by default the loop's state;
    - and it may read each channel's demand after the link (Vari-mu's memories charge on it).

    A curve change fades as every model's does (CurveFade): a second loop, a copy of the running
    one, follows the new curve and the two reductions are blended.

    Never put a decoupled peak hold on the demand: it latches the onset's overshoot,
    (R − 1)·(level − T), and over-compresses by hundreds of dB (NOTES.md, variant DH).
*/
class FeedbackLoop
{
public:
    /** The smooth branching detector's coefficients (NOTES.md, The loop, 5). */
    struct Coefficients
    {
        double attackStep = 1.0;    // 1 − α_A' = (1 − e^(−1/(Attack·fs))) / R_set
        double releasePole = 0.0;   // α_R' = e^(−1/((Release − h)·fs)), uncompensated
    };

    /**
        Attack and Release are the measured times the knobs mean; the attack is divided by the set
        ratio (capped) so the closed loop's pole is the knob's, the release is not, since below
        threshold the loop is open.
    */
    static Coefficients coefficientsFor (double attackMs, double releaseMs, double holdMs, double inverseRatioSet, double sampleRate) noexcept
    {
        return { (1.0 - poleFor (attackMs, sampleRate)) * inverseRatioSet,
                 poleFor (std::max (releaseMs - holdMs, 0.0), sampleRate) };
    }

    void reset() noexcept { state.fill (0.0); }

    /** Starts a channel at the reduction applied (Settled 33). */
    void seed (int channel, double reductionDb) noexcept { state[(size_t) channel] = reductionDb; }

    double reduction (int channel) const noexcept { return state[(size_t) channel]; }

    /**
        One sample. heldLevelDb: each channel's pre-held key level. curve: with the capped ratio
        and the model's own threshold; Range caps the state. link: λ, used when there are two
        channels and the stereo mode is Linked, else 0. Writes each channel's reduction.
        estimateReductionDb: per channel, what the output estimate subtracts (nullptr: the loop's
        state). linkedDemandDb: per channel, receives the demand after the link (nullptr: unused).
    */
    template <typename Demand = InverseCurveDemand>
    void process (const double* heldLevelDb, double* reductionDb, int numChannels, const Curve& curve,
                  const Coefficients& k, double link, const Demand& demandOf = {},
                  const double* estimateReductionDb = nullptr, double* linkedDemandDb = nullptr) noexcept
    {
        std::array<double, maxDetectorChannels> demand {};

        for (int c = 0; c < numChannels; ++c)
        {
            const auto previous = estimateReductionDb != nullptr ? estimateReductionDb[c] : state[(size_t) c];
            demand[(size_t) c] = demandOf (curve, heldLevelDb[c] - previous);
        }

        if (numChannels == 2 && link > 0.0)
        {
            const auto loudest = std::max (demand[0], demand[1]);
            for (auto& d : demand)
                d = (1.0 - link) * d + link * loudest;
        }

        for (int c = 0; c < numChannels; ++c)
        {
            auto& s = state[(size_t) c];
            const auto d = demand[(size_t) c];
            s = d > s ? s + k.attackStep * (d - s) : k.releasePole * s + (1.0 - k.releasePole) * d;
            s = snapState (std::min (s, curve.rangeDb), d);
            reductionDb[c] = s;
            if (linkedDemandDb != nullptr)
                linkedDemandDb[c] = d;
        }
    }

private:
    std::array<double, maxDetectorChannels> state {};
};

} // namespace basalt::dynamics
