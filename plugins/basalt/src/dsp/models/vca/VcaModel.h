#pragma once

#include "dsp/dynamics/Detectors.h"
#include "dsp/dynamics/GainComputer.h"
#include "dsp/dynamics/Glide.h"
#include "dsp/dynamics/Model.h"
#include "dsp/dynamics/RunningMax.h"
#include "dsp/models/vca/VcaBallistics.h"

#include <array>

namespace basalt::models
{

/**
    VCA (NOTES.md here, Settled 23, 35–37): feed-forward, eq. 4 with the threshold moving with the
    ratio, T − 6 dB/R, and one of two detectors, fixed per instance:

    - Peak, the console bus type: the sample peak, the louder channel winning at Link 100 %, the
      core's smooth decoupled ballistics with Attack 1:1 and the release solved by h(ρ), and Auto
      release as a two-capacitor network, slow stage 120 × Release up to 20 s.
    - RMS, the dbx/THAT type: one mean square of τ = Attack/0.45442, linked stereo by power sum,
      the attack the detector's own, the release at most 10 dB per Release. Auto release is ignored.

    The chain runs the two detectors as two engines and switches between them as between models
    (seed and a 30 ms blend in dB), so this class reads neither ModelParams::vcaRms nor, for RMS,
    ModelParams::autoRelease.
*/
class VcaModel final : public dynamics::DynamicsModel
{
public:
    enum class Detector
    {
        peak,
        rms
    };

    explicit VcaModel (Detector) noexcept;

    void prepare (double sampleRate) override;
    void reset() noexcept override;
    void seed (const double* reductionDb, int numChannels) noexcept override;
    void process (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept override;
    void follow (const dynamics::ModelParams&, const dynamics::KeyBlock&) noexcept override;
    double staticReduction (const dynamics::ModelParams&, double levelDb) const noexcept override;
    double reduction (int channel) const noexcept override;

    /** RMS: five time constants of the mean square. Peak: 0, its only key-only state is the look-ahead window. */
    double settleMs (const dynamics::ModelParams&) const noexcept override;

    Detector detector() const noexcept { return kind; }

    /** S in T_eff = T − S/R (NOTES.md, 2. Gain computer). */
    static constexpr double thresholdShiftDb = 6.0;

    /** Attack is the 63.2 % time of a 10 dB step from Threshold on a one-pole on the square: 0.45442 τ. */
    static constexpr double rmsAttackPerTimeConstant = 0.45442;

    /** RMS: the reduction recovers this many dB per Release. */
    static constexpr double rmsReleaseDepthDb = 10.0;

    /** Peak Auto: τ2 = min(120 × Release, 20 s), C₂/C₁ = 10. */
    static constexpr double autoSlowPerRelease = 120.0;
    static constexpr double autoSlowMaxMs = 20000.0;
    static constexpr double autoCapacitorRatio = 10.0;

    /** The curve both detectors compute: Threshold moved to T − S/R; exactly the identity at Ratio 1. */
    static dynamics::Curve effectiveCurve (const dynamics::Curve&) noexcept;

    static double rmsTimeConstantMs (double attackMs) noexcept { return attackMs / rmsAttackPerTimeConstant; }

    /** How fast the RMS detector alone lets the reduction recover 10 dB: τ·ln 10 / (1 − 1/R); infinite at Ratio 1. */
    static double rmsReleaseFloorMs (double attackMs, double inverseRatio) noexcept;

    /** Peak: the first stage's constant τ1 = Attack·h(Release/Attack), with Auto's fast capacitor. */
    static double peakFastMs (double attackMs, double releaseMs) noexcept;

    /** Peak Auto: the slow stage's τ2. */
    static double peakSlowMs (double releaseMs) noexcept;

private:
    struct Channel
    {
        dynamics::RmsDetector rms;
        dynamics::RunningMax window;            // Peak: |k|; RMS: the mean square
        PeakBallistics peak, peakIncoming;      // incoming: the new curve's while a curve change fades in
        RateLimitedRelease meter, meterIncoming;
        double applied = 0.0;
        double lastPeak = -1.0, lastPeakReduction = 0.0;
    };

    void setTargets (const dynamics::ModelParams&) noexcept;
    void setTimes (double attackMs, double releaseMs) noexcept;
    void refreshFoot() noexcept;
    void startOrEndFade (bool fading, int numChannels) noexcept;

    template <bool heard>
    void run (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    /** steady: no curve fade and no time glide in this block, so the per-sample checks drop out. */
    template <bool heard, bool steady>
    void runPeak (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    template <bool heard, bool steady>
    void runRms (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    Detector kind;
    std::array<Channel, dynamics::maxDetectorChannels> channels;
    dynamics::RunningMax linkedWindow;          // RMS, linked: the window on the power sum
    dynamics::CurveFade fade;
    dynamics::SmoothGlide attackGlide, releaseGlide;   // ln ms
    dynamics::SmoothGlide autoGlide;                    // Peak's Auto release: 0 off, 1 on

    double rate = 48000.0;
    PeakBallistics::Coefficients peakCoefficients;
    double releaseStep = 0.0;                   // RMS: dB per sample
    double footMagnitude = 0.0;                 // Peak: |k| at T_eff − W/2, under which the reduction is 0
    double footMeanSquare = 0.0;                // RMS: the mean square there
    bool primed = false, footStale = true, inFade = false;
    bool warmedUp = false;      // follow() ran since reset(): the next process() ends the mean squares' warm-up
};

} // namespace basalt::models
