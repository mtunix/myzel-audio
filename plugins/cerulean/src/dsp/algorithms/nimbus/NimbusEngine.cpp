#include "dsp/algorithms/nimbus/NimbusEngine.h"

namespace cerulean::dsp
{

namespace
{
    // Tank lines (ms at Size 1x): 62-212 ms, mean ~122 ms, no small common factors. Long lines
    // make the space huge; the in-loop allpasses below fill the gaps between their echoes.
    constexpr std::array<float, 16> lineMs {
        62.33f, 67.91f, 74.17f, 80.71f, 87.29f, 94.87f, 102.73f, 110.39f,
        119.83f, 128.27f, 139.13f, 149.93f, 162.07f, 176.29f, 191.87f, 211.73f
    };

    // Three in-loop allpasses per line (ms at Size 1x): short (2.3-5.3 ms), medium (4-13 ms) and
    // long (13-31 ms), assigned so that line length and allpass lengths are uncorrelated.
    constexpr std::array<std::array<float, 3>, 16> apMs { {
        { 3.07f, 7.31f, 23.17f }, { 4.43f, 11.83f, 16.21f }, { 2.71f, 4.37f, 28.63f }, { 5.27f, 9.67f, 19.43f },
        { 3.53f, 12.91f, 14.27f }, { 2.29f, 5.53f, 30.71f }, { 4.79f, 8.29f, 21.89f }, { 3.31f, 10.79f, 17.33f },
        { 5.03f, 6.11f, 26.47f }, { 2.53f, 12.37f, 15.13f }, { 3.89f, 4.91f, 24.31f }, { 4.21f, 9.13f, 29.59f },
        { 2.93f, 7.79f, 13.61f }, { 4.61f, 11.27f, 20.53f }, { 3.71f, 5.87f, 18.29f }, { 2.39f, 8.83f, 27.37f }
    } };

    // Pre-diffusion (ms at Size 1x, Distance scale 1x). Slightly different per side.
    constexpr std::array<float, 8> preLMs { 4.13f, 6.37f, 9.71f, 13.27f, 17.93f, 23.41f, 29.83f, 37.19f };
    constexpr std::array<float, 8> preRMs { 4.71f, 7.03f, 10.61f, 14.39f, 19.07f, 24.73f, 31.39f, 38.83f };

    constexpr float maxSizeScale = 2.0f;
    constexpr float minDistanceScale = 0.3f, maxDistanceScale = 2.2f;

    float distanceScale (float b) noexcept
    {
        return minDistanceScale * std::pow (maxDistanceScale / minDistanceScale, b);
    }

    // Output gain: steady pink noise at Decay 2 s comes out near -3 dB.
    constexpr float tankOutputGain = 0.74f;
    constexpr float earlyMaxGain = 0.35f;
}

void NimbusEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;

    preL.prepare (sampleRate, preLMs, maxSizeScale * maxDistanceScale);
    preR.prepare (sampleRate, preRMs, maxSizeScale * maxDistanceScale);

    tank.prepare (sampleRate, { .lineMs = lineMs, .apMs = apMs, .maxScale = maxSizeScale,
                                .maxLineModMs = 8.0f, .maxApModMs = 1.2f,
                                .lineModHz = 0.22f, .apModHz = 0.37f, .seed = randomSeed });
    reset();
}

void NimbusEngine::reset()
{
    bandwidthL.reset();
    bandwidthR.reset();
    preL.reset();
    preR.reset();
    tank.reset();
    bandwidth.reset();
    preGain.reset();
    earlyGain.reset();
}

void NimbusEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                            int numSamples, const EngineParams& p) noexcept
{
    const auto density = maps::clamp01 (p.paramA);
    const auto distance = maps::clamp01 (p.paramB);
    const auto mod = maps::clamp01 (p.mod);

    // Control rate ---------------------------------------------------------------
    // Dark by default; Distance darkens further (air absorption of a far-away cloud).
    const auto bandwidthHz = std::clamp (7500.0f * std::pow (2.0f, 0.9f * p.tone - (distance - 0.5f)),
                                         2500.0f, 18000.0f);
    bandwidth.set (OnePole::coefficientFor (bandwidthHz, fs), numSamples);

    const auto preScale = p.sizeScale * distanceScale (distance);
    preL.setScale (preScale);
    preR.setScale (preScale);
    preL.advance (numSamples);
    preR.advance (numSamples);
    // Density thins or thickens the pre-diffusion too (x0.5 .. x1.5 around the default), so the
    // sparse end really is a cloud of separate echoes even at the default Diffusion.
    const auto preCoeff = (0.68f * std::sqrt (maps::clamp01 (p.diffusion)) + 0.06f * distance) * (0.5f + density);
    preGain.set (std::min (0.75f, preCoeff), numSamples);

    // A little of the pre-diffused signal straight out: a close cloud starts at once, a far
    // one only arrives through the tank.
    earlyGain.set (earlyMaxGain * (1.0f - distance) * (1.0f - distance), numSamples);

    auto high = maps::toneToHighDecay (p.tone);
    high.ratio *= 0.8f;          // darker in the loop than the global Tone mapping
    high.crossoverHz *= 0.9f;

    tank.update (numSamples, { .scale = p.sizeScale, .rt60 = p.decaySeconds, .lowEnd = p.lowEnd,
                               .high = high, .freeze = p.freeze,
                               .lineModMs = 7.0f * mod, .apModMs = 1.0f * mod, .modRate = p.modRate,
                               .apGain = 0.7f * std::sqrt (density) });

    // Audio rate -----------------------------------------------------------------
    constexpr float inject = 1.0f / 4.0f;   // 1 / sqrt (lines)

    for (int n = 0; n < numSamples; ++n)
    {
        const auto bw = bandwidth.next();
        bandwidthL.setCoefficient (bw);
        bandwidthR.setCoefficient (bw);
        const auto l = bandwidthL.lowpass (inL[n]);
        const auto r = bandwidthR.lowpass (inR[n]);

        const auto g = preGain.next();
        const auto dl = preL.process (l, g);
        const auto dr = preR.process (r, g);

        std::array<float, lines> in, y;
        for (int i = 0; i < lines; ++i)
            in[(size_t) i] = inject * (hadamardSign (1, i) * dl + hadamardSign (2, i) * dr);

        tank.process (in.data(), y.data());

        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < lines; ++i)
        {
            sumL += hadamardSign (3, i) * y[(size_t) i];
            sumR += hadamardSign (5, i) * y[(size_t) i];
        }

        const auto eg = earlyGain.next();
        outL[n] = tankOutputGain * sumL + eg * dl;
        outR[n] = tankOutputGain * sumR + eg * dr;
    }
}

} // namespace cerulean::dsp
