#pragma once

#include "dsp/dynamics/Ballistics.h"
#include "dsp/dynamics/Detectors.h"
#include "dsp/dynamics/GainComputer.h"
#include "dsp/dynamics/Glide.h"
#include "dsp/dynamics/Model.h"
#include "dsp/dynamics/RunningMax.h"

#include <array>

namespace basalt::models
{

/**
    Clean (NOTES.md here, Settled 34): feed-forward; peak and RMS reductions blended in dB, the
    RMS share and window following Attack; eq. 4; smooth decoupled ballistics with the attack
    calibrated by κ and the release solved by h(ρ); Auto release from the crest factor at half the
    2013 law.
*/
class CleanModel final : public dynamics::DynamicsModel
{
public:
    void prepare (double sampleRate) override;
    void reset() noexcept override;
    void seed (const double* reductionDb, int numChannels) noexcept override;
    void process (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept override;
    void follow (const dynamics::ModelParams&, const dynamics::KeyBlock&) noexcept override;
    double staticReduction (const dynamics::ModelParams&, double levelDb) const noexcept override;
    double reduction (int channel) const noexcept override;

    /** Five RMS time constants with the RMS half in the blend, and with Auto release at least crestSettleMs. */
    double settleMs (const dynamics::ModelParams&) const noexcept override;

    /** What the crest detectors follow before a warm-up's mean square reads a 40 Hz period or two. */
    static constexpr double crestSettleMs = 50.0;

    /** The RMS share b: 0 to 1 ms, rising with log Attack to 0.5 at 10 ms. */
    static double blendFor (double attackMs) noexcept;

    /** κ(b) = 1 + 0.118·b undoes the RMS half's lag. */
    static double kappaFor (double blend) noexcept;

    /** τ_A = Attack/κ(b), in ms. */
    static double attackConstantMs (double attackMs) noexcept;

    /** τ_RMS = Attack/4, in ms. */
    static double rmsConstantMs (double attackMs) noexcept;

    /**
        How much the RMS half's own fall lengthens the release at the reference step (10 dB on
        the carrier): its reduction falls linearly in dB at 4.343/τ_RMS dB per ms, and the first
        stage relaxes towards it instead of 0, which delays the release by the area under it over
        the depth, b·c·ΔR²·τ_RMS / (2·4.343·D) with ΔR = D/c + 3.01·(1 − b) (NOTES.md, Ballistics).
        Ratios under 2 take Ratio 2's figure. 0 at pure peak.
    */
    static double releaseDragMs (double attackMs, double inverseRatio) noexcept;

    /** The shortest release Clean gives, 1.01·τ_A plus the drag; shorter settings measure this. */
    static double minimumReleaseMs (double attackMs, double inverseRatio) noexcept;

    /** Release_auto / Release = clamp(√(2/C²), 1/8, √2). */
    static double autoReleaseFactor (double crestSquared) noexcept;

    /** The C² steering a detector channel's release now (both channels share one in Linked). */
    double crestSquared (int channel) const noexcept;

    static constexpr int autoUpdateInterval = 16;

private:
    struct Channel
    {
        dynamics::RmsDetector rms;
        dynamics::CrestFactor crest;
        dynamics::RunningMax peakWindow, rmsWindow;
        dynamics::SmoothDecoupled ballistics;
        dynamics::SmoothDecoupled incoming;     // the new curve's ballistics while a curve change fades in
        double applied = 0.0;
        double releasePole = 0.0;
        double steeringCrest = dynamics::CrestFactor::resetCrestSquared;
        double lastPeak = -1.0, lastPeakReduction = 0.0;
    };

    void setTargets (const dynamics::ModelParams&) noexcept;
    void setTimes (double attackMs, double releaseMs) noexcept;
    void updateAutoRelease (int numChannels, bool sharedSteering) noexcept;

    template <bool heard>
    void run (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    /** steady: no curve fade and no time glide in this block, so the per-sample checks drop out. */
    template <bool heard, bool steady>
    void runSamples (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    std::array<Channel, dynamics::maxDetectorChannels> channels;
    dynamics::CrestFactor linkedCrest;   // max(k_L², k_R²): one C² for a linked pair
    dynamics::CurveFade fade;
    dynamics::SmoothGlide attackGlide, releaseGlide;   // ln ms
    dynamics::SmoothGlide autoGlide;                    // Auto release: 0 off, 1 on

    double rate = 48000.0;
    double blend = 0.5, attackPole = 0.0, releaseMs = 150.0, tauAttackMs = 10.0;
    double fixedReleasePole = 0.0;
    double kneeFootMagnitude = 0.0;
    double releaseDrag = 0.0, dragInverseRatio = 0.25;
    int autoCountdown = 0;
    bool primed = false, footStale = true, inFade = false;
    bool warmedUp = false;      // follow() ran since reset(): the next process() ends the detectors' warm-up
};

} // namespace basalt::models
