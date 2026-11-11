#pragma once

#include "dsp/Engine.h"
#include "dsp/common/Fdn.h"

namespace cerulean::dsp
{

/**
    Echo: a warm, wobbly multi-head tape echo melting into a plate.

    in -> record EQ -> (+) -> tape saturation -> tape (mono loop, wow & flutter)
                        ^                         | heads at T/3 (left), 2T/3 (right), T (centre)
                        |                         v
                        +-- x Repeats <- HP 110 Hz <- LP (head/tape loss) <- main head (T)
       heads -> playback LP -> echoes ---------------------------------------------+-> out
       echoes + some of the input -> plate tank (4 diffusers + 16-line FDN) x amount(Decay) +

    The repeats land at multiples of T (feedback from the main head only), the side heads add a
    lighter triplet pulse between them. Every pass through the loop loses highs and a little low
    end, so each repeat is darker and thinner; the soft saturation and Repeats <= 95 % keep the
    loop contractive (it can never run away). Echo time glides like tape (a pitch glide).

    Param A: unused (the echo time comes resolved in EngineParams::echoTimeMs, free or synced).
    Param B (Repeats): feedback 0 .. 95 %.
    Freeze: the plate holds; the echoes stop (tape feedback, echo output and plate send fade out),
            so the frozen loop is the linear plate only.
    See NOTES.md for the tuning and measurements.
*/
class EchoEngine final : public Engine
{
public:
    explicit EchoEngine (uint32_t randomSeed = 1) : seed (randomSeed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

    /**
        Repeats (Param B, 0..1) and echo time -> tape feedback gain: up to 95 % per repeat, but
        never a longer trail than a 24 s RT60 (that only limits long echo times near the top of
        the knob: 95 % holds up to 185 ms, 500 ms tops out at 87 %, 1.5 s at 65 %).
    */
    static float feedbackFor (float repeats01, float echoMs) noexcept
    {
        constexpr float maxTrailSeconds = 24.0f;
        const auto ceiling = std::min (0.95f, std::pow (10.0f, -3.0f * echoMs * 0.001f / maxTrailSeconds));
        return ceiling * maps::clamp01 (repeats01);
    }

    /** The tape's soft saturation: tanh-like (Pade), |y| <= |x|, slope <= 1, smooth limit at +-1. */
    static float saturate (float x) noexcept
    {
        x = std::clamp (x, -3.0f, 3.0f);
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /** The tape's current (gliding) echo time in ms, for tests and metering. */
    float getCurrentEchoMs() const noexcept { return timeNow * 1000.0f / (float) fs; }

    /** Head positions as fractions of the echo time. */
    static constexpr std::array<float, 3> headRatio { 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };

private:
    static constexpr int lines = 16;

    /**
        The echo time as a tape transport: it is pulled towards the target (one-pole, tau) at a
        bounded speed (max slew, samples per sample), and that speed itself changes smoothly
        (accel time constant), like a motor with inertia. So a time change is a pitch glide that
        starts and ends gently, never a kink in the waveform.
    */
    struct TapeTransport
    {
        float value = 0.0f, velocity = 0.0f, target = 0.0f;
        float pull = 0.0f, accel = 1.0f, maxSlew = 1.0f;

        void prepare (double sampleRate, float tauMs, float accelMs, float maxSlewPerSample) noexcept
        {
            pull = 1.0f / (tauMs * 0.001f * (float) sampleRate);
            accel = 1.0f - std::exp (-1.0f / (accelMs * 0.001f * (float) sampleRate));
            maxSlew = maxSlewPerSample;
        }

        void snap (float v) noexcept { value = target = v; velocity = 0.0f; }

        float next() noexcept
        {
            const auto desired = std::clamp ((target - value) * pull, -maxSlew, maxSlew);
            velocity += (desired - velocity) * accel;
            value += velocity;
            if (std::abs (target - value) < 1.0e-3f && std::abs (velocity) < 1.0e-6f)
            {
                value = target;
                velocity = 0.0f;
            }
            return value;
        }
    };

    uint32_t seed;
    double fs = 48000.0;

    // Tape
    DelayLine tape;
    TapeTransport echoTime;            // samples
    float timeNow = 0.0f;
    bool snapNext = true;
    SineLfo wowSine, flutterSine;
    SmoothRandom wowRandom, flutterRandom;
    float modNow = 0.0f, modStep = 0.0f;
    OnePole recordLowCut, recordHighCut, loopHighLoss, loopLowLoss, playbackL, playbackR;
    LinearRamp feedback, echoLevel, sendLevel, reverbLevel;

    // Plate
    OnePole bandwidthL, bandwidthR;
    AllpassChain<4> diffuserL, diffuserR;
    Fdn<lines> tank;
    LinearRamp diffusionGain;
};

} // namespace cerulean::dsp
