#pragma once

#include "dsp/Engine.h"
#include "dsp/algorithms/halo/GrainShifter.h"
#include "dsp/common/Fdn.h"

#include <utility>

namespace cerulean::dsp
{

/**
    Halo: shimmer. Octaves (or fifths, fourths...) rising into the tail.

    in -> bandwidth LP -> 6-stage allpass diffuser per side -> 16-line Hadamard FDN -> out
                                                                 ^                |
                                     4 of the 16 lines: sqrt(1-A) x + sqrt(A) Shift(x)

    The pitch shifter sits only inside the loop, so the first arrival is unshifted and every
    trip through a shifted line climbs one more interval. Shifted paths are band-limited
    (110 Hz - 5.2 kHz) so the climb fades into air instead of whistling.

    Param A (Interval): maps::haloIntervalIndex -> -12, +5, +7, +12, +12/+19 (two voices).
    Param B (Amount):   energy share of the shifted signal on the shifter lines (0 = plain lush
                        reverb, 1 = full shimmer).
    Freeze fades the shifters out of the loop (the climb stops, the held cloud stays).
    See NOTES.md for the tuning rationale and measurements.
*/
class HaloEngine final : public Engine
{
public:
    explicit HaloEngine (uint32_t randomSeed = 1) : seed (randomSeed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

    static constexpr int lines = 16;
    static constexpr int shifterCount = 4;
    static constexpr int diffuserStages = 6;

    /** Pitch ratios of the two shifter voices for an interval index (ratio 0 = voice off). */
    static std::pair<float, float> intervalRatios (int index) noexcept;

    /**
        Energy share of the shifted signal on a shifter line: Amount, scaled with Size so the
        climb rate (intervals per second) stays the same at every Size (capped at 1).
    */
    static float shiftedShare (float amount, float sizeScale) noexcept;

    /**
        Effective loop RT60 for a Decay: lengthened so the mid band's measured decay still
        matches Decay while shifting moves energy out of it (see NOTES.md, "RT60").
    */
    static float compensatedRt60 (float decaySeconds, int intervalIndex, float amount, float sizeScale) noexcept;

private:
    /** A ramp over one sub-block that lands exactly on its target (so 0 and 1 are exact). */
    struct ExactRamp
    {
        void reset() noexcept { primed = false; }
        void set (float target, int n) noexcept
        {
            end = target;
            if (! primed) { value = target; remaining = 0; primed = true; return; }
            remaining = std::max (n, 1);
            step = (target - value) / (float) remaining;
        }
        float next() noexcept
        {
            if (remaining > 0 && --remaining == 0)
                value = end;
            else if (remaining > 0)
                value += step;
            return value;
        }
        float get() const noexcept { return value; }
        float target() const noexcept { return end; }

        float value = 0.0f, step = 0.0f, end = 0.0f;
        int remaining = 0;
        bool primed = false;
    };

    uint32_t seed;
    double fs = 48000.0;

    OnePole bandwidthL, bandwidthR;
    AllpassChain<diffuserStages> diffuserL, diffuserR;
    Fdn<lines> tank;
    std::array<halo::GrainShifter, (size_t) shifterCount> shifters;

    LinearRamp diffusionGain, latencyGain1, latencyGain2;
    ExactRamp dryShare, shiftShare;
    float voiceLevel1 = 1.0f, voiceLevel2 = 0.0f, voiceSlew = 0.001f;
    float stretch = 1.0f, lowStretch = 1.0f, highStretch = 1.0f;
    bool snapNext = true;
};

} // namespace cerulean::dsp
