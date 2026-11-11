#pragma once

#include "dsp/Engine.h"
#include "dsp/common/EarlyReflections.h"
#include "dsp/common/Fdn.h"

namespace cerulean::dsp
{

/**
    Hall: a big, smooth, gently modulated concert hall.

    in -> bandwidth LP -> early-reflection cluster ------------------------------+
       -> 4-stage allpass diffuser per side (Build) -> 16-line Hadamard FDN -> +-> out

    Param A (Early): level of the early-reflection cluster (0 = none, pure late field).
    Param B (Build): how the late field arrives. Low = an instant, dense wall; high = longer,
                     stickier input diffusion, so the hall swells in over ~100 ms.
    See NOTES.md for the tuning rationale.
*/
class HallEngine final : public Engine
{
public:
    explicit HallEngine (uint32_t randomSeed = 1) : seed (randomSeed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

private:
    static constexpr int lines = 16;

    uint32_t seed;
    double fs = 48000.0;

    OnePole bandwidthL, bandwidthR;
    EarlyReflections<14> early;
    AllpassChain<4> diffuserL, diffuserR;
    Fdn<lines> tank;
    LinearRamp earlyGain, diffusionGain;
};

} // namespace cerulean::dsp
