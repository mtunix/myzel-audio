#pragma once

#include "dsp/Engine.h"
#include "dsp/algorithms/nimbus/CloudTank.h"

namespace cerulean::dsp
{

/**
    Nimbus: a huge, dark, endlessly diffuse ambient cloud, voiced for long decays and pads.

    in -> bandwidth LP (Tone, Distance) -> 8-stage allpass pre-diffusion per side (Distance)
       -> 16-line "cloud" tank: every line is a slowly, randomly modulated delay with two
          modulated allpass diffusers *inside* the loop (Density), per-band decay, Hadamard mix
       -> decorrelated L/R taps (+ a little of the pre-diffused signal when Distance is low)

    Param A (Density):  in-loop diffusion. 0 = a sparse cloud of echoes, 1 = a thick fog.
    Param B (Distance): how far away the cloud is. Longer pre-diffusion, darker input and no
                        direct "early" energy at high values: the cloud arrives late and smeared.
    See NOTES.md for the tuning rationale and measurements.
*/
class NimbusEngine final : public Engine
{
public:
    explicit NimbusEngine (uint32_t seed = 1) : randomSeed (seed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

private:
    static constexpr int lines = 16;
    static constexpr int preStages = 8;

    uint32_t randomSeed;
    double fs = 48000.0;

    OnePole bandwidthL, bandwidthR;
    AllpassChain<preStages> preL, preR;
    nimbus::CloudTank<lines, 3> tank;
    LinearRamp bandwidth, preGain, earlyGain;
};

} // namespace cerulean::dsp
