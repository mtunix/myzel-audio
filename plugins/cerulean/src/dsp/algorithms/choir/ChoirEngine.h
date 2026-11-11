#pragma once

#include "dsp/Engine.h"
#include "dsp/algorithms/choir/FormantBank.h"
#include "dsp/common/Fdn.h"

namespace cerulean::dsp
{

/**
    Choir: vowel-like voices emerging from the tail.

    in -> bandwidth LP -> 6-stage allpass diffuser per side -> 16-line Hadamard FDN
       -> plain tail (rows 3/5) ----------------------------------------------------------+
       -> 4 voices, each: its own pair of orthogonal taps of the tank -> short delay with   |
          vibrato + slow detune drift -> 4-formant band-pass bank (vowel, scaled for the   +-> out
          voice's vocal-tract length) -> pan/width ------------------------------------------+

    The formant bank is outside the loop: Decay, Low End, Tone and Freeze behave exactly as in
    a hall, and the voices colour what the tail is doing.

    Param A (Vowel):  maps::vowelPosition, continuous A-E-I-O-U morph. With Mod up the vowel
                      also drifts slowly on its own (and a little differently per voice).
    Param B (Voices): maps::voiceCount 1..4, fractional voices fade in; each added voice has a
                      different tract length, delay, vibrato and place in the stereo field.
    See NOTES.md for the tuning rationale and measurements.
*/
class ChoirEngine final : public Engine
{
public:
    explicit ChoirEngine (uint32_t randomSeed = 1) : seed (randomSeed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

    static constexpr int lines = 16;
    static constexpr int maxVoices = 4;
    static constexpr int diffuserStages = 6;

    /** Level of voice k (0..3) for a fractional voice count (1..4): 1, then fading in. */
    static float voiceLevel (int k, float voiceCount) noexcept
    {
        return k == 0 ? 1.0f : std::clamp (voiceCount - (float) k, 0.0f, 1.0f);
    }

private:
    struct Voice
    {
        DelayLine lineL, lineR;
        choir::FormantBank bank;
        SineLfo vibrato;
        SmoothRandom detune, vowelDrift;
        float delay = 0.0f, delayStep = 0.0f;
        LinearRamp level;
    };

    uint32_t seed;
    double fs = 48000.0;

    OnePole bandwidthL, bandwidthR;
    AllpassChain<diffuserStages> diffuserL, diffuserR;
    Fdn<lines> tank;
    std::array<Voice, (size_t) maxVoices> voices;
    SmoothRandom vowelWander;
    LinearRamp diffusionGain;
    bool snapNext = true;
};

} // namespace cerulean::dsp
