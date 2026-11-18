#pragma once

#include "dsp/dynamics/Feedback.h"
#include "dsp/dynamics/GainComputer.h"
#include "dsp/dynamics/Glide.h"
#include "dsp/dynamics/Model.h"
#include "dsp/dynamics/RunningMax.h"
#include "dsp/models/varimu/VariMuCurve.h"

#include <array>

namespace basalt::models
{

/**
    Vari-mu (NOTES.md here, Settled 40): the core's feedback loop on the held peak, with eq. 4 plus
    a rise towards 20:1 as its curve (cap 20), the attack compensated at the curve's reference
    point and calibrated by Φ(Ratio, Knee), floored at 0.1 ms, and an Auto release of three tiers:
    the reduction is the largest of the loop's state and two memories that charge only while the
    side chain calls for reduction and release at 5 and 12.5 × Release (at most 25 s).
*/
class VariMuModel final : public dynamics::DynamicsModel
{
public:
    void prepare (double sampleRate) override;
    void reset() noexcept override;
    void seed (const double* reductionDb, int numChannels) noexcept override;
    void process (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept override;
    void follow (const dynamics::ModelParams&, const dynamics::KeyBlock&) noexcept override;
    double staticReduction (const dynamics::ModelParams&, double levelDb) const noexcept override;
    double reduction (int channel) const noexcept override;

    /** The pre-hold: the only key-only state. */
    double settleMs (const dynamics::ModelParams&) const noexcept override;

    /** Attack below this reads as this: under it the one-sample loop overshoots on the rising loop gain. */
    static constexpr double attackFloorMs = 0.1;

    /** The attack's reference point: where the curve's reduction is this deep. */
    static constexpr double referenceReductionDb = 10.0;

    /** Auto release: the memories' release as multiples of Release, their cap, their charging constants. */
    static constexpr double fastTier = 5.0, slowTier = 12.5;
    static constexpr double tierCapMs = 25000.0;
    static constexpr double fastChargeMs = 300.0, slowChargeMs = 5000.0;

    /** The memories charge only while the demand reached the state within this long (one cycle of 40 Hz). */
    static constexpr double callingMs = 25.0;

    /** h = min(Release/10, 5 ms), the core's pre-hold. */
    static double holdMs (double releaseMs) noexcept;

    /** 1/R_ref: the curve's local slope where its reduction is 10 dB; 1 at Ratio 1. */
    static double referenceInverseRatio (const dynamics::Curve&) noexcept;

    /** R_∞: the curve's asymptotic local ratio, 1/(1/R − A), 20 from Ratio 1.5 on, where a step from silence starts. */
    static double asymptoticRatio (double inverseRatio) noexcept;

    /**
        Φ(Ratio, Knee) = R_ref / (κ(Ratio)·R_∞): the attack's calibration (NOTES.md, Attack). κ is
        measured in M2 on this engine and tabulated over log Ratio; Φ itself moves too fast with
        the rise's fade-in and with the knee to tabulate (NOTES.md, Implementation (M2)).
    */
    static double attackCalibration (const dynamics::Curve&) noexcept;

    /** κ(Ratio), linear in log Ratio between the table's points, Ratio 1 … 20 in 20 log steps. */
    static double kappa (double inverseRatio) noexcept;
    static constexpr int kappaRatios = 20;
    static double kappaGridRatio (int i) noexcept;

    /** The two memories of a detector channel, in dB: tier 0 the 5 × one, tier 1 the 12.5 × one. */
    double memory (int channel, int tier) const noexcept;

private:
    struct Memory
    {
        double fast = 0.0, slow = 0.0;
        int calling = 0;    // samples left in which the demand counts as having reached the state
    };

    /** One loop with its memories: the running one, and during a curve fade the new curve's beside it. */
    struct Loop
    {
        dynamics::FeedbackLoop feedback;
        std::array<varimu::Inverse, dynamics::maxDetectorChannels> inverses;
        std::array<Memory, dynamics::maxDetectorChannels> memories {};
        std::array<double, dynamics::maxDetectorChannels> applied {};   // max(s, m, ℓ), what the estimate subtracts

        void reset() noexcept;
    };

    /** What a curve's attack needs, computed when the curve changes (coefficients only, never the state). */
    struct Calibration
    {
        dynamics::Curve curve;
        varimu::Shape shape;
        double referenceInverseRatio = 1.0, phi = 1.0;
        dynamics::FeedbackLoop::Coefficients coefficients;
    };

    struct MemoryCoefficients
    {
        double fastCharge = 0.0, slowCharge = 0.0, fastRelease = 0.0, slowRelease = 0.0;
        bool charge = false;
    };

    void setTargets (const dynamics::ModelParams&) noexcept;
    void setTimes (double attackMs, double releaseMs, double autoAmount) noexcept;
    void calibrate (Calibration&, const dynamics::Curve&) noexcept;
    void updateCalibrations() noexcept;
    void step (Loop&, const Calibration&, double link, const double* levelDb, int numChannels) noexcept;

    template <bool heard>
    void run (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    /** steady: no curve fade and no time glide in this block, so the coefficients stand. */
    template <bool heard, bool steady>
    void runSamples (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    std::array<dynamics::RunningMax, dynamics::maxDetectorChannels> holds;
    Loop running, incoming;     // incoming: the new curve's loop while a curve change fades in
    std::array<double, dynamics::maxDetectorChannels> output {};   // the reduction applied, blended during a fade
    Calibration source, target;
    MemoryCoefficients memoryK;
    dynamics::CurveFade fade;
    dynamics::SmoothGlide attackGlide, releaseGlide;    // ln ms
    dynamics::SmoothGlide autoGlide;                     // Auto release: 0 off, 1 on

    double rate = 48000.0;
    double attackMs = 10.0, releaseMs = 150.0;
    int holdSamples = 0, callingSamples = 0;
    bool primed = false, inFade = false;
};

} // namespace basalt::models
