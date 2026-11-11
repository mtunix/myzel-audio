#pragma once

#include "dsp/common/ReverbBlocks.h"

namespace cerulean::dsp
{

/**
    Stereo early-reflection cluster: a seeded, sparse multi-tap pattern per channel.

    Tap times grow roughly like an image-source pattern (sparse first, denser later), with gains
    falling off over the cluster and random polarity. Each output channel mostly hears its own
    side and a little of the other (crossTaps), which gives a stable, wide image. Tap times
    scale with Size and glide with it.
*/
template <int TapsPerChannel>
class EarlyReflections
{
public:
    struct Setup
    {
        float firstMs = 4.0f;       // first reflection at Size 1x
        float lastMs = 70.0f;       // last reflection at Size 1x
        float maxScale = 2.0f;
        float decayShape = 2.5f;    // gain fall-off over the cluster (higher = steeper)
        float crossAmount = 0.35f;  // share of taps reading the opposite channel
        uint32_t seed = 1;
    };

    void prepare (double sampleRate, const Setup& s)
    {
        fs = sampleRate;
        setup = s;
        const auto maxLen = (int) std::ceil (msToSamples (s.lastMs * s.maxScale * 1.1f, sampleRate)) + 4;
        lineL.setMaxDelay (maxLen);
        lineR.setMaxDelay (maxLen);
        scale.prepare (sampleRate, 180.0f, 0.05f);

        Rng rng (s.seed * 2654435761u + 17u);

        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < TapsPerChannel; ++k)
            {
                auto& t = taps[(size_t) ch][(size_t) k];
                const auto pos = ((float) k + 0.2f + 0.6f * rng.uniform()) / (float) TapsPerChannel;
                const auto shaped = std::pow (pos, 1.4f);
                t.ms = s.firstMs + (s.lastMs - s.firstMs) * shaped;
                t.gain = std::exp (-s.decayShape * shaped) * (0.65f + 0.35f * rng.uniform())
                       * (rng.uniform() < 0.5f ? -1.0f : 1.0f);
                t.cross = rng.uniform() < s.crossAmount;
            }

        // Normalise so the cluster's energy is ~1 per channel.
        for (int ch = 0; ch < 2; ++ch)
        {
            float energy = 0.0f;
            for (const auto& t : taps[(size_t) ch])
                energy += t.gain * t.gain;
            const auto norm = 1.0f / std::sqrt (std::max (energy, 1.0e-6f));
            for (auto& t : taps[(size_t) ch])
                t.gain *= norm;
        }

        reset();
    }

    void reset() noexcept
    {
        lineL.reset();
        lineR.reset();
        scale.snap (scale.getTarget() > 0.0f ? scale.getTarget() : 1.0f);
        current = scale.get();
        step = 0.0f;
        snapNext = true;
    }

    /** Once per sub-block. */
    void update (int numSamples, float sizeScale) noexcept
    {
        if (snapNext)
            scale.snap (sizeScale);
        else
            scale.setTarget (sizeScale);
        snapNext = false;
        const auto start = scale.get();
        const auto end = scale.advance (numSamples);
        current = start;
        step = (end - start) / (float) std::max (numSamples, 1);
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        lineL.write (inL);
        lineR.write (inR);
        current += step;

        const auto samplesPerMs = (float) fs * 0.001f * current;
        float l = 0.0f, r = 0.0f;

        for (const auto& t : taps[0])
            l += t.gain * (t.cross ? lineR : lineL).readLinear (std::max (1.0f, t.ms * samplesPerMs));
        for (const auto& t : taps[1])
            r += t.gain * (t.cross ? lineL : lineR).readLinear (std::max (1.0f, t.ms * samplesPerMs));

        outL = l;
        outR = r;
    }

private:
    struct Tap
    {
        float ms = 0.0f, gain = 0.0f;
        bool cross = false;
    };

    double fs = 48000.0;
    Setup setup;
    DelayLine lineL, lineR;
    std::array<std::array<Tap, (size_t) TapsPerChannel>, 2> taps {};
    GlideValue scale;
    float current = 1.0f, step = 0.0f;
    bool snapNext = true;
};

} // namespace cerulean::dsp
