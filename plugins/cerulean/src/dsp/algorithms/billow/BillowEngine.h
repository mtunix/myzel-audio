#pragma once

#include "dsp/Engine.h"
#include "dsp/common/Fdn.h"

namespace cerulean::dsp
{

/**
    Billow: a reverb that keeps building after the note. It blooms, then decays with Decay.

    in -> bandwidth LP -> 6-stage allpass diffuser per side --+---------------------------+
                                                              |                           v
                                    bloom network B (8-line FDN, decays at the Bloom      M: 16-line FDN
                                    memory set by Bloom x Feedback, never above unity) -> (Decay) -> out
                                              x bloom envelope (open while the input plays and
                                                one bloom length after; rises over the bloom
                                                length, then closes)

    The bloom envelope is the point: while it is open, the regenerating network B keeps pouring
    energy into the main tank, so the tail grows; once it closes, only M rings and the tail decays
    exactly at Decay. Everything is bounded by construction (B's loop gain < 1, envelope <= 1).

    Param A (Bloom):    bloom length, maps::bloomLengthMs (150 ms .. 4 s): how long the tail keeps
                        growing after the input stops.
    Param B (Feedback): how much the bloom feeds on itself: B's memory (6x .. 12x the bloom length)
                        and its level into the tank. Subtle swell .. huge build.
    See NOTES.md for the design rationale and measurements.
*/
class BillowEngine final : public Engine
{
public:
    explicit BillowEngine (uint32_t seed = 1) : randomSeed (seed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

private:
    static constexpr int tankLines = 16;
    static constexpr int bloomLines = 8;

    uint32_t randomSeed;
    double fs = 48000.0;

    OnePole bandwidthL, bandwidthR;
    AllpassChain<6> diffuserL, diffuserR;
    Fdn<bloomLines> bloom;
    Fdn<tankLines> tank;
    LinearRamp bandwidth, diffusionGain, bloomGain;

    // Bloom envelope (per sample): open while the input plays and one bloom length after it.
    EnvelopeFollower fastDetector, peakDetector;
    float envelope = 0.0f;      // 0 .. 1, linear ramp (the bloom gain)
    int holdSamples = 0;        // samples left before the envelope starts to close
};

} // namespace cerulean::dsp
