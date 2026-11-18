#pragma once

#include "dsp/dynamics/Ballistics.h"
#include "dsp/dynamics/Feedback.h"
#include "dsp/dynamics/GainComputer.h"
#include "dsp/dynamics/Glide.h"
#include "dsp/dynamics/KeyPath.h"
#include "dsp/dynamics/Model.h"
#include "dsp/dynamics/RunningMax.h"

#include <array>

namespace basalt::models
{

/**
    FET (NOTES.md here, Settled 38): the core's feedback loop on the peak level, pre-held for
    min(Release/10, 1 ms); Ratio capped at 20, the threshold rising with it as the reference's
    table (+4, +5, +6 dB at 8, 12, 20:1); Ratio's ∞ step read as All: 20:1 at the 4:1 threshold
    with the key 1 ms late. Attack and Release are the measured times over the whole knobs; Auto
    release is not read (no source documents one).
*/
class FetModel final : public dynamics::DynamicsModel
{
public:
    static constexpr double ratioCap = 20.0;
    static constexpr double maxHoldMs = 1.0;
    static constexpr double allLagMs = 1.0;

    /** The longest lag the key line holds: M3 voices All's lag within 0.25 … 2 ms. */
    static constexpr double maxLagMs = 2.0;

    void prepare (double sampleRate) override;
    void reset() noexcept override;
    void seed (const double* reductionDb, int numChannels) noexcept override;
    void process (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept override;
    void follow (const dynamics::ModelParams&, const dynamics::KeyBlock&) noexcept override;
    double staticReduction (const dynamics::ModelParams&, double levelDb) const noexcept override;
    double reduction (int channel) const noexcept override;

    /** The pre-hold plus All's lag: what the key-only states need before they read the key. */
    double settleMs (const dynamics::ModelParams&) const noexcept override;

    /** Ratio's ∞ step: 1/R exactly 0. */
    static bool isAll (const dynamics::Curve& knobs) noexcept { return knobs.inverseRatio <= 0.0; }

    /** ΔT for a finite 1/R: 0 up to 4:1, then linear in log2 R through +4, +5, +6 dB at 8, 12, 20:1. */
    static double thresholdOffsetDb (double inverseRatio) noexcept;

    /** The curve the loop settles on: T + ΔT and Ratio capped at 20; All is 20:1 at T. Knee and Range as set. */
    static dynamics::Curve loopCurve (const dynamics::Curve& knobs) noexcept;

    /** h = min(Release/10, 1 ms). */
    static double holdMs (double releaseMs) noexcept { return dynamics::feedbackHoldMs (releaseMs, maxHoldMs); }

private:
    struct Channel
    {
        dynamics::RunningMax hold;
        double lastPeak = -1.0, lastLevelDb = dynamics::silentDb;
        double applied = 0.0;
    };

    void setTargets (const dynamics::ModelParams&) noexcept;
    void setTimes (double attackMs, double releaseMs) noexcept;
    dynamics::FeedbackLoop::Coefficients coefficientsFor (const dynamics::Curve&) const noexcept;
    double lagSamplesFor (const dynamics::Curve& knobs) const noexcept;

    /** steady: no curve fade and no glide in this block, so the per-sample checks drop out. */
    template <bool heard, bool steady>
    void runSamples (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    std::array<Channel, dynamics::maxDetectorChannels> channels;
    dynamics::KeyDelay lagLine;                     // the key's magnitude, for All's lag
    dynamics::FeedbackLoop loop;
    dynamics::FeedbackLoop incoming;                // the new curve's loop while a curve change fades in
    dynamics::FeedbackLoop::Coefficients unitCoefficients;     // the time knobs' at Ratio 1
    dynamics::FeedbackLoop::Coefficients sourceK, targetK;
    dynamics::CurveFade fade;
    dynamics::SmoothGlide attackGlide, releaseGlide;    // ln ms
    dynamics::LinearGlide lagGlide;                     // samples, as the core glides the look-ahead tap

    double rate = 48000.0;
    double attackMs = 10.0, releaseMs = 150.0;
    int holdSamples = 0;
    bool primed = false, inFade = false;
};

} // namespace basalt::models
