#include "dsp/algorithms/billow/BillowEngine.h"

namespace cerulean::dsp
{

namespace
{
    // Main tank (ms at Size 1x): 34-135 ms, no small common factors. A hall-sized spread: the
    // size of the space comes from the bloom, the tank has to be dense from the first moment.
    constexpr std::array<float, 16> tankMs {
        33.83f, 38.51f, 43.47f, 48.37f, 53.29f, 58.09f, 63.71f, 68.93f,
        74.57f, 80.99f, 87.61f, 94.97f, 103.13f, 112.13f, 122.47f, 134.53f
    };

    // Bloom network (ms at Size 1x): long lines, so the bloom is a slow, smooth swell.
    constexpr std::array<float, 8> bloomMs { 97.31f, 109.87f, 123.73f, 137.09f, 151.33f, 167.89f, 185.29f, 203.87f };

    // Input diffusers (ms at Size 1x), six stages so the tail is dense from the start (the
    // bloom then grows out of a smooth wash, never out of separate echoes). Different per side.
    constexpr std::array<float, 6> diffuseLMs { 2.71f, 4.13f, 6.29f, 9.37f, 13.81f, 19.03f };
    constexpr std::array<float, 6> diffuseRMs { 3.07f, 4.79f, 7.01f, 10.27f, 14.93f, 20.39f };

    constexpr float maxSizeScale = 2.0f;

    // The input counts as "playing" while its fast envelope is within 20 dB of its recent peak
    // (and above -80 dBFS, so hiss and dither never open the bloom). Relative, so the bloom
    // behaves the same at any input level.
    constexpr float presenceRatio = 0.1f, presenceFloor = 1.0e-4f;

    constexpr float directGain = 0.35f;
    constexpr float bloomLevel = 2.5f;
    constexpr float tankOutputGain = 0.12f;
}

void BillowEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;

    diffuserL.prepare (sampleRate, diffuseLMs, maxSizeScale);
    diffuserR.prepare (sampleRate, diffuseRMs, maxSizeScale);

    bloom.prepare (sampleRate, { .delayMs = bloomMs, .maxScale = maxSizeScale, .maxModMs = 3.0f,
                                 .nominalModHz = 0.3f, .seed = randomSeed * 3u + 7u,
                                 .mixing = Fdn<bloomLines>::Mixing::hadamard });
    tank.prepare (sampleRate, { .delayMs = tankMs, .maxScale = maxSizeScale, .maxModMs = 2.5f,
                                .nominalModHz = 0.4f, .seed = randomSeed,
                                .mixing = Fdn<tankLines>::Mixing::hadamard });

    // Near-instant attack, so even a single-sample impulse opens the bloom at any sample rate.
    fastDetector.setTimes (0.02f, 20.0f, sampleRate);
    peakDetector.setTimes (0.02f, 2000.0f, sampleRate);
    reset();
}

void BillowEngine::reset()
{
    bandwidthL.reset();
    bandwidthR.reset();
    diffuserL.reset();
    diffuserR.reset();
    bloom.reset();
    tank.reset();
    bandwidth.reset();
    diffusionGain.reset();
    bloomGain.reset();
    fastDetector.reset();
    peakDetector.reset();
    envelope = 0.0f;
    holdSamples = 0;
}

void BillowEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                            int numSamples, const EngineParams& p) noexcept
{
    const auto feedback = maps::clamp01 (p.paramB);
    const auto freeze = std::clamp (p.freeze, 0.0f, 1.0f);
    const auto bloomSeconds = maps::bloomLengthMs (p.paramA) * 0.001f;

    // Control rate ---------------------------------------------------------------
    const auto bandwidthHz = std::clamp (9000.0f * std::pow (2.0f, 0.9f * p.tone), 3500.0f, 18000.0f);
    bandwidth.set (OnePole::coefficientFor (bandwidthHz, fs), numSamples);

    diffuserL.setScale (p.sizeScale);
    diffuserR.setScale (p.sizeScale);
    diffuserL.advance (numSamples);
    diffuserR.advance (numSamples);
    diffusionGain.set (0.76f * std::sqrt (maps::clamp01 (p.diffusion)), numSamples);

    // The bloom network's memory: long enough that it is still full when the bloom peaks.
    const auto bloomRt = std::clamp (bloomSeconds * (6.0f + 6.0f * feedback), 0.9f, 40.0f);
    // The bloom darkens gently as it builds, independently of Tone: Tone shapes the main tank
    // (and the input), so the bloom keeps the spectral balance of the reverb it feeds. (Tying it
    // to Tone as well left a dark bloom with no highs at all, under which the dark tank's fast
    // high-band decay could no longer even be measured.)
    constexpr maps::HighDecay bloomHigh { 0.6f, 5000.0f };

    bloom.update (numSamples, { .scale = p.sizeScale, .rt60 = bloomRt, .lowEnd = p.lowEnd, .high = bloomHigh,
                                .freeze = 0.0f, .modDepthMs = 2.5f * maps::clamp01 (p.mod), .modRate = p.modRate,
                                .randomBlend = 0.8f });
    tank.update (numSamples, { .scale = p.sizeScale, .rt60 = p.decaySeconds, .lowEnd = p.lowEnd,
                               .high = maps::toneToHighDecay (p.tone), .freeze = freeze,
                               .modDepthMs = 1.8f * maps::clamp01 (p.mod), .modRate = p.modRate,
                               .randomBlend = 0.65f });

    // How loud the bloom gets against the direct reverb. A short Decay lets the tank hold less of
    // the bloom, so it gets more of it (the rise stays similar across Decay). Frozen: no bloom.
    const auto decayCompensation = std::clamp (std::pow (3.0f / std::max (p.decaySeconds, 0.1f), 0.3f), 0.4f, 2.5f);
    bloomGain.set (bloomLevel * dbToGain (8.0f * feedback) * decayCompensation * (1.0f - freeze), numSamples);

    const auto riseStep = 1.0f / (bloomSeconds * (float) fs);
    const auto releaseSeconds = std::clamp (0.25f * bloomSeconds, 0.08f, 0.5f);
    const auto fallStep = 1.0f / (releaseSeconds * (float) fs);
    const auto hold = (int) (bloomSeconds * (float) fs);

    // Audio rate -----------------------------------------------------------------
    constexpr float injectTank = 1.0f / 4.0f;                  // 1 / sqrt (16)
    const float injectBloom = 1.0f / std::sqrt ((float) bloomLines);

    for (int n = 0; n < numSamples; ++n)
    {
        // Bloom envelope: open while the input plays and for one bloom length after it stops;
        // while open it rises (from wherever it is) over the bloom length, then it closes over
        // the release.
        const auto level = std::max (std::abs (inL[n]), std::abs (inR[n]));
        const auto fast = fastDetector.process (level);
        const auto recentPeak = peakDetector.process (level);
        if (fast > presenceRatio * recentPeak && fast > presenceFloor)
            holdSamples = hold;

        if (holdSamples > 0)
        {
            --holdSamples;
            envelope = std::min (1.0f, envelope + riseStep);
        }
        else
        {
            envelope = std::max (0.0f, envelope - fallStep);
        }
        const auto shaped = envelope;   // amplitude ramp: the bloom energy grows ~t^2

        const auto bw = bandwidth.next();
        bandwidthL.setCoefficient (bw);
        bandwidthR.setCoefficient (bw);
        const auto g = diffusionGain.next();
        const auto dl = diffuserL.process (bandwidthL.lowpass (inL[n]), g);
        const auto dr = diffuserR.process (bandwidthR.lowpass (inR[n]), g);

        std::array<float, bloomLines> bIn, bOut;
        for (int i = 0; i < bloomLines; ++i)
            bIn[(size_t) i] = injectBloom * (hadamardSign (1, i) * dl + hadamardSign (2, i) * dr);
        bloom.process (bIn.data(), bOut.data());

        float bl = 0.0f, br = 0.0f;
        for (int i = 0; i < bloomLines; ++i)
        {
            bl += hadamardSign (3, i) * bOut[(size_t) i];
            br += hadamardSign (5, i) * bOut[(size_t) i];
        }

        const auto bg = bloomGain.next() * shaped * injectBloom;
        const auto tl = directGain * dl + bg * bl;
        const auto tr = directGain * dr + bg * br;

        std::array<float, tankLines> tIn, y;
        for (int i = 0; i < tankLines; ++i)
            tIn[(size_t) i] = injectTank * (hadamardSign (1, i) * tl + hadamardSign (2, i) * tr);
        tank.process (tIn.data(), y.data());

        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < tankLines; ++i)
        {
            sumL += hadamardSign (3, i) * y[(size_t) i];
            sumR += hadamardSign (5, i) * y[(size_t) i];
        }

        outL[n] = tankOutputGain * sumL;
        outR[n] = tankOutputGain * sumR;
    }
}

} // namespace cerulean::dsp
