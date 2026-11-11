#include "dsp/algorithms/hall/HallEngine.h"

namespace cerulean::dsp
{

namespace
{
    // Tank lengths (ms at Size 1x): spread over 33-131 ms, no two sharing a small common factor,
    // so the modes interleave evenly. Mean ~76 ms: dense enough for a smooth tail, long enough to
    // sound like a large room rather than a metallic box.
    constexpr std::array<float, 16> tankMs {
        33.17f, 37.91f, 42.73f, 47.11f, 52.39f, 56.83f, 61.51f, 67.27f,
        72.53f, 78.97f, 85.19f, 91.67f, 99.13f, 107.71f, 118.49f, 131.21f
    };

    // Input diffusers (ms at Build/Size 1x). Slightly different per side: decorrelated onset.
    constexpr std::array<float, 4> diffuseLMs { 3.07f, 5.27f, 8.93f, 13.69f };
    constexpr std::array<float, 4> diffuseRMs { 3.71f, 6.13f, 9.67f, 14.87f };

    constexpr float maxBuildScale = 3.0f;
    constexpr float maxSizeScale = 2.0f;

    // Output gain: sets the wet level so steady pink noise at Decay 2 s comes out near -3 dB.
    constexpr float tankOutputGain = 0.42f;
    constexpr float earlyMaxGain = 0.75f;
}

void HallEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;

    early.prepare (sampleRate, { .firstMs = 7.0f, .lastMs = 85.0f, .maxScale = maxSizeScale,
                                 .decayShape = 2.2f, .crossAmount = 0.3f, .seed = seed * 3u + 1u });

    diffuserL.prepare (sampleRate, diffuseLMs, maxSizeScale * maxBuildScale);
    diffuserR.prepare (sampleRate, diffuseRMs, maxSizeScale * maxBuildScale);

    tank.prepare (sampleRate, { .delayMs = tankMs, .maxScale = maxSizeScale, .maxModMs = 2.0f,
                                .nominalModHz = 0.45f, .seed = seed,
                                .mixing = Fdn<lines>::Mixing::hadamard });
    reset();
}

void HallEngine::reset()
{
    bandwidthL.reset();
    bandwidthR.reset();
    early.reset();
    diffuserL.reset();
    diffuserR.reset();
    tank.reset();
    earlyGain.reset();
    diffusionGain.reset();
}

void HallEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                          int numSamples, const EngineParams& p) noexcept
{
    // Control rate ---------------------------------------------------------------
    const auto bandwidthHz = std::clamp (9500.0f * std::pow (2.0f, 0.9f * p.tone), 4000.0f, 18000.0f);
    bandwidthL.setCutoff (bandwidthHz, fs);
    bandwidthR.setCutoff (bandwidthHz, fs);

    const auto build = maps::clamp01 (p.paramB);
    const auto diffuserScale = p.sizeScale * (0.5f + (maxBuildScale - 0.5f) * build);
    diffuserL.setScale (diffuserScale);
    diffuserR.setScale (diffuserScale);
    diffuserL.advance (numSamples);
    diffuserR.advance (numSamples);
    diffusionGain.set (std::min (0.78f, 0.7f * std::sqrt (maps::clamp01 (p.diffusion)) + 0.06f * build), numSamples);

    early.update (numSamples, p.sizeScale);
    earlyGain.set (earlyMaxGain * std::pow (maps::clamp01 (p.paramA), 1.3f), numSamples);

    tank.update (numSamples, { .scale = p.sizeScale, .rt60 = p.decaySeconds, .lowEnd = p.lowEnd,
                               .high = maps::toneToHighDecay (p.tone), .freeze = p.freeze,
                               .modDepthMs = 1.6f * maps::clamp01 (p.mod), .modRate = p.modRate,
                               .randomBlend = 0.65f });

    // Audio rate -----------------------------------------------------------------
    constexpr float inject = 1.0f / 4.0f;   // 1 / sqrt (lines)

    for (int n = 0; n < numSamples; ++n)
    {
        const auto l = bandwidthL.lowpass (inL[n]);
        const auto r = bandwidthR.lowpass (inR[n]);

        float erL, erR;
        early.process (l, r, erL, erR);

        const auto g = diffusionGain.next();
        const auto dl = diffuserL.process (l, g) * inject;
        const auto dr = diffuserR.process (r, g) * inject;

        std::array<float, lines> in, y;
        for (int i = 0; i < lines; ++i)
            in[(size_t) i] = hadamardSign (1, i) * dl + hadamardSign (2, i) * dr;

        tank.process (in.data(), y.data());

        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < lines; ++i)
        {
            sumL += hadamardSign (3, i) * y[(size_t) i];
            sumR += hadamardSign (5, i) * y[(size_t) i];
        }

        const auto eg = earlyGain.next();
        outL[n] = tankOutputGain * sumL + eg * erL;
        outR[n] = tankOutputGain * sumR + eg * erR;
    }
}

} // namespace cerulean::dsp
