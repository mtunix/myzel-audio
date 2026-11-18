#pragma once

#include "dsp/dynamics/Detectors.h"
#include "dsp/dynamics/GainComputer.h"
#include "dsp/dynamics/Glide.h"
#include "dsp/dynamics/Model.h"
#include "dsp/dynamics/RunningMax.h"

#include <array>

namespace basalt::models
{

/**
    Upward (NOTES.md here, Settled 41): feed-forward on a 10 ms RMS; below Threshold the lift ℓ
    rises by (1 − 1/R) dB per dB, capped by Range and faded to nothing over the 12 dB above Floor;
    the lift goes away on a one-pole of Attack and comes in at a constant 10 dB per Release, up
    to 5 × slower with Auto release after time spent above Threshold. The reduction it hands the
    chain is −ℓ.
*/
class UpwardModel final : public dynamics::DynamicsModel
{
public:
    static constexpr double rmsMs = 10.0;
    static constexpr double floorFadeDb = 12.0;

    /** Release is the time this much lift takes to come in. */
    static constexpr double liftPerReleaseDb = 10.0;

    /** Auto release: h follows the share of time above Threshold with this constant, and k = 1 + autoSlowdown·h. */
    static constexpr double autoMemoryMs = 2000.0;
    static constexpr double autoSlowdown = 4.0;

    void prepare (double sampleRate) override;
    void reset() noexcept override;
    void seed (const double* reductionDb, int numChannels) noexcept override;
    void process (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept override;
    void follow (const dynamics::ModelParams&, const dynamics::KeyBlock&) noexcept override;
    double staticReduction (const dynamics::ModelParams&, double levelDb) const noexcept override;
    double reduction (int channel) const noexcept override;

    /** Five RMS time constants. */
    double settleMs (const dynamics::ModelParams&) const noexcept override;

    /** ℓ(x) in dB, ≥ 0: the static lift at a detector level for this curve and Floor. */
    static double lift (const dynamics::Curve&, double floorDb, double levelDb) noexcept;

    /** The Auto release memory h (0 … 1) steering a detector channel now; both share one in Linked. */
    double autoMemory (int channel) const noexcept;

private:
    struct Channel
    {
        dynamics::RmsDetector rms;
        dynamics::RunningMax window;
        double lift = 0.0;              // y, the lift the ballistics hold
        double incoming = 0.0;          // the new curve's y while a curve change fades in
        double applied = 0.0;           // the reduction handed out last, −y or the fade's blend
        double memory = 0.0;            // h, from this channel's level
    };

    /** The level-dependent parts of the curve, as powers 2m, so most samples skip the log. */
    struct Bounds
    {
        double top = 0.0;       // T + W/2: no lift at or above
        double bottom = 0.0;    // Floor: no lift at or below
        double threshold = 0.0; // T: Auto release's memory charges above it
    };

    void setTargets (const dynamics::ModelParams&) noexcept;
    void setTimes (double attackMs, double releaseMs) noexcept;
    static Bounds boundsFor (const dynamics::Curve&, double floorDb) noexcept;
    static double liftAt (const dynamics::Curve&, const Bounds&, double floorDb, double twiceMeanSquare) noexcept;

    template <bool heard>
    void run (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    /** steady: no curve fade and no glide in this block, so the per-sample checks drop out. */
    template <bool heard, bool steady>
    void runSamples (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    std::array<Channel, dynamics::maxDetectorChannels> channels;
    double linkedMemory = 0.0;      // h from the louder channel: one for a linked pair
    bool sharedSteering = false;

    dynamics::CurveFade fade;
    dynamics::SmoothGlide attackGlide, releaseGlide;    // ln ms
    dynamics::SmoothGlide autoGlide;                     // Auto release: 0 off, 1 on
    dynamics::SmoothGlide floorGlide;                    // dB

    double rate = 48000.0;
    double attackPole = 0.0;
    double liftStep = 0.0;          // dB per sample at k = 1
    double memoryPole = 0.0;
    Bounds targetBounds, sourceBounds;
    bool primed = false, inFade = false, boundsStale = true;
    bool warmedUp = false;          // follow() ran since reset(): the next process() ends the RMS warm-up
};

} // namespace basalt::models
