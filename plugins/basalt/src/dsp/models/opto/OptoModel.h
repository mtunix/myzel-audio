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
    Opto (NOTES.md here, Settled 39): feed-forward, a cell instead of the core's ballistics. A 5 ms
    mean square is the light, the light sets the conductance the cell heads for, the cell is the
    bottom leg of a divider, r = q·w·ln(1 + F + S). F is the fast part (rise τ_on from Attack
    through an offline table, first-stage fall τ_ff from Release), S the memory Auto release runs:
    it charges over seconds of light and holds the cell up after F has gone.
*/
class OptoModel final : public dynamics::DynamicsModel
{
public:
    void prepare (double sampleRate) override;
    void reset() noexcept override;
    void seed (const double* reductionDb, int numChannels) noexcept override;
    void process (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept override;
    void follow (const dynamics::ModelParams&, const dynamics::KeyBlock&) noexcept override;
    double staticReduction (const dynamics::ModelParams&, double levelDb) const noexcept override;
    double reduction (int channel) const noexcept override;

    /** Five light constants: the mean square's warm-up. */
    double settleMs (const dynamics::ModelParams&) const noexcept override;

    static constexpr double onsetEpsilon = 0.0024787521766663585;    // e^(−6)
    static constexpr double memoryShare = 0.1;                        // σ
    static constexpr double chargeMs = 3000.0;                        // τ_sc
    static constexpr double attackFloorMs = 2.0;

    /** w = (Knee + 12 dB)/5.5, the width the cell's soft curve takes. */
    static double kneeWidthDb (double kneeDb) noexcept;

    /** τ_m = min(5 ms, 1.5 × Attack), Attack floored at 2 ms: the light alone never takes the whole Attack. */
    static double lightMs (double attackMs) noexcept;

    /** Δ = τ_m·w/4.343, the light's lag behind a stopped key, which the release subtracts. */
    static double lightDelayMs (double attackMs, double kneeDb) noexcept;

    /** τ_on from the generated table (OptoAttackTable.h), trilinear in log Attack, 1/q and 1/w. */
    static double attackConstantMs (double attackMs, double inverseRatio, double kneeDb) noexcept;

    /** τ_ff = (Release − Δ)/ln(G₀/G₁), Release floored at Δ + 1 ms; Ratios under 1.5 take 1.5's. */
    static double releaseConstantMs (double releaseMs, double attackMs, double inverseRatio, double kneeDb) noexcept;

    /** τ_sd = 16 × Release, at most 25 s. */
    static double dischargeMs (double releaseMs) noexcept;

    /** r_s: the settled reduction for a level on the detector scale. */
    static double curveReduction (const dynamics::Curve&, double levelDb) noexcept;

    /** The light's mean square, the fast part and the memory of a detector channel, for the tests. */
    double lightOf (int channel) const noexcept;
    double fastOf (int channel) const noexcept;
    double memoryOf (int channel) const noexcept;

private:
    struct Cell
    {
        double fast = 0.0, memory = 0.0;
    };

    /** Everything a curve fixes: the cell's law and its fast part's poles. */
    struct Law
    {
        dynamics::Curve curve;
        double inverseWidth = 0.0, scale = 0.0;     // 1/w, q·w
        double onsetDb = 0.0, onsetPower = 0.0;     // T − 6w, and 2m there
        double conductanceMax = 0.0, memoryMax = 0.0;
        double rise = 0.0, fall = 0.0;

        double conductance (double levelDb) const noexcept;
        double reduction (const Cell&) const noexcept;
    };

    struct Channel
    {
        dynamics::RmsDetector light;
        dynamics::RunningMax window;
        Cell cell;
        Cell incoming;      // the new curve's cell while a curve change fades in
        double applied = 0.0;
    };

    void setTargets (const dynamics::ModelParams&) noexcept;
    void setLaw (Law&, const dynamics::Curve&) const noexcept;
    void setTimes (double attackMs, double releaseMs) noexcept;
    void setFastPoles (Law&) const noexcept;
    void seedCell (Cell&, const Law&, double reductionDb) const noexcept;

    static bool same (const Cell&, const Cell&) noexcept;

    /** The conductance each detector channel's light asks for under a law, the link applied to the levels. */
    void conductances (const Law&, double link, const double* meanSquares, int numChannels, bool linked, double* g) const noexcept;

    /** One sample of a cell towards conductance g; the reduction it gives. */
    double step (Cell&, const Law&, double g, bool memoryOn) const noexcept;

    /** Auto release glided down hands the memory's share over to the fast part, so nothing steps. */
    void handOver (double kept) noexcept;

    template <bool heard>
    void run (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    template <bool heard, bool steady>
    void runSamples (const dynamics::ModelParams&, const dynamics::KeyBlock&, double* const* reductionDb) noexcept;

    std::array<Channel, dynamics::maxDetectorChannels> channels;
    dynamics::CurveFade fade;
    dynamics::SmoothGlide attackGlide, releaseGlide;   // ln ms
    dynamics::SmoothGlide autoGlide;                    // Auto release: 0 off, 1 on
    Law law, sourceLaw;                                  // fade.target()'s and fade.source()'s

    double rate = 48000.0;
    double attackMs = 10.0, releaseMs = 150.0;
    double chargePole = 0.0, dischargePole = 0.0;
    double autoAmount = 1.0;
    bool primed = false, inFade = false;
    bool warmedUp = false;
};

} // namespace basalt::models
