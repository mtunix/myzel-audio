#include "dsp/algorithms/halo/HaloEngine.h"

namespace cerulean::dsp
{

namespace
{
    // Tank lengths (ms at Size 1x): 37-144 ms, mean ~82 ms, no small common factors. A little
    // longer than Hall's: shimmer wants a big, slow space, and longer passes slow the climb.
    constexpr std::array<float, 16> tankMs {
        37.31f, 41.93f, 46.69f, 51.13f, 56.87f, 62.33f, 67.91f, 73.69f,
        80.29f, 87.11f, 94.73f, 102.89f, 111.67f, 121.31f, 132.07f, 143.93f
    };

    // The shifted lines: spread over short, middle and long lines so the climb is not tied
    // to one loop time.
    constexpr std::array<int, HaloEngine::shifterCount> shiftedLine { 1, 6, 10, 14 };

    // Input diffusers (ms at Size 1x): six stages, slightly different per side. A soft,
    // smeared onset suits shimmer, and it gets the echo density up cleanly.
    constexpr std::array<float, HaloEngine::diffuserStages> diffuseLMs { 2.93f, 4.31f, 6.07f, 8.87f, 12.71f, 17.33f };
    constexpr std::array<float, HaloEngine::diffuserStages> diffuseRMs { 3.37f, 4.79f, 6.73f, 9.61f, 13.57f, 18.59f };

    constexpr float maxSizeScale = 2.0f;
    constexpr float tankOutputGain = 0.48f;

    // Mid-band decay lost per shift event (dB), per interval (see NOTES.md, "RT60").
    // It grows slowly with Decay (longer tails spend longer in the band the energy migrates
    // through): migration = perShift * (Decay / 2.5 s)^growth.
    constexpr std::array<float, maps::numHaloIntervals> migrationDbPerShift { 3.15f, 1.4f, 1.7f, 2.4f, 2.9f };
    constexpr std::array<float, maps::numHaloIntervals> migrationGrowth { 0.35f, 0.2f, 0.2f, 0.2f, 0.2f };
    // Octave-down also loses real energy at the low cut, more so the more of the loop is
    // shifted: an extra amount^0.4 (1 for the others).
    constexpr std::array<float, maps::numHaloIntervals> migrationAmountExponent { 0.4f, 0.0f, 0.0f, 0.0f, 0.0f };

    float meanTankSeconds (float sizeScale) noexcept
    {
        float sum = 0.0f;
        for (auto ms : tankMs)
            sum += ms;
        return 0.001f * sizeScale * sum / (float) tankMs.size();
    }
}

std::pair<float, float> HaloEngine::intervalRatios (int index) noexcept
{
    auto semis = [] (float s) { return std::exp2 (s / 12.0f); };
    switch (std::clamp (index, 0, maps::numHaloIntervals - 1))
    {
        case 0:  return { semis (-12.0f), 0.0f };
        case 1:  return { semis (5.0f), 0.0f };
        case 2:  return { semis (7.0f), 0.0f };
        case 3:  return { semis (12.0f), 0.0f };
        default: return { semis (12.0f), semis (19.0f) };
    }
}

float HaloEngine::shiftedShare (float amount, float sizeScale) noexcept
{
    // Proportional to the pass time, so the climb (intervals per second) does not depend on Size.
    return std::min (maps::clamp01 (amount) * std::max (sizeScale, 0.0f), 1.0f);
}

float HaloEngine::compensatedRt60 (float decaySeconds, int intervalIndex, float amount, float sizeScale) noexcept
{
    const auto idx = (size_t) std::clamp (intervalIndex, 0, maps::numHaloIntervals - 1);
    const auto a = shiftedShare (amount, sizeScale);
    const auto [r1, r2] = intervalRatios ((int) idx);
    const auto latency = r2 > 0.0f ? 0.5f * (halo::GrainVoice::meanLatencySeconds (r1) + halo::GrainVoice::meanLatencySeconds (r2))
                                   : halo::GrainVoice::meanLatencySeconds (r1);
    const auto share = (float) shifterCount / (float) lines;
    const auto pass = meanTankSeconds (sizeScale) + share * a * latency;
    const auto shiftsPerSecond = share * a / pass;
    const auto migration = migrationDbPerShift[idx] * shiftsPerSecond
                         * std::pow (std::max (decaySeconds, 0.1f) / 2.5f, migrationGrowth[idx])
                         * std::pow (a, migrationAmountExponent[idx]);   // dB/s

    const auto natural = 60.0f / std::max (decaySeconds, 1.0e-3f);      // dB/s
    // Never compensate more than 6x: at very long Decay with lots of shimmer the climb itself
    // sets how long a note stays in its own register (by design).
    const auto loop = std::max (natural - migration, natural / 6.0f);
    return 60.0f / loop;
}

void HaloEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;

    diffuserL.prepare (sampleRate, diffuseLMs, maxSizeScale);
    diffuserR.prepare (sampleRate, diffuseRMs, maxSizeScale);

    tank.prepare (sampleRate, { .delayMs = tankMs, .maxScale = maxSizeScale, .maxModMs = 2.0f,
                                .nominalModHz = 0.4f, .seed = seed * 31u + 7u,
                                .mixing = Fdn<lines>::Mixing::hadamard });

    const auto maxRatio = intervalRatios (maps::numHaloIntervals - 1).second;
    for (int k = 0; k < shifterCount; ++k)
        shifters[(size_t) k].prepare (sampleRate, maxRatio, seed * 977u + (uint32_t) k * 7907u + 3u);

    voiceSlew = 1.0f / (0.06f * (float) sampleRate);   // voice 2 fades in/out over 60 ms
    reset();
}

void HaloEngine::reset()
{
    bandwidthL.reset();
    bandwidthR.reset();
    diffuserL.reset();
    diffuserR.reset();
    tank.reset();
    for (auto& s : shifters)
        s.reset();
    diffusionGain.reset();
    latencyGain1.reset();
    latencyGain2.reset();
    dryShare.reset();
    shiftShare.reset();
    snapNext = true;
}

void HaloEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                          int numSamples, const EngineParams& p) noexcept
{
    // Control rate ---------------------------------------------------------------
    const auto freeze = std::clamp (p.freeze, 0.0f, 1.0f);
    const auto bandwidthHz = std::clamp (9500.0f * std::pow (2.0f, 0.9f * p.tone), 4000.0f, 18000.0f);
    bandwidthL.setCutoff (bandwidthHz, fs);
    bandwidthR.setCutoff (bandwidthHz, fs);

    diffuserL.setScale (p.sizeScale);
    diffuserR.setScale (p.sizeScale);
    diffuserL.advance (numSamples);
    diffuserR.advance (numSamples);
    diffusionGain.set (0.78f * std::sqrt (maps::clamp01 (p.diffusion)), numSamples);

    const auto interval = maps::haloIntervalIndex (p.paramA);
    const auto [ratio1, ratio2] = intervalRatios (interval);
    const auto shiftHighCutHz = 5200.0f * std::pow (2.0f, 0.9f * std::min (p.tone, 0.0f));
    for (auto& s : shifters)
    {
        s.setRatios (ratio1, ratio2 > 0.0f ? ratio2 : ratio1);
        s.setHighCut (shiftHighCutHz, fs);
    }

    const auto amount = maps::clamp01 (p.paramB);

    // The stretch against migration applies to the mid band and to the band the climb moves
    // energy *away* from. The band it moves energy *into* (the air for upward intervals, the
    // lows for -12) keeps its nominal RT60 (Decay x Tone ratio / Decay x Low End), so what piles
    // up there still dies on time. The stretch is smoothed (~80 ms): it jumps with the interval.
    const auto targetStretch = compensatedRt60 (p.decaySeconds, interval, amount, p.sizeScale)
                             / std::max (p.decaySeconds, 1.0e-3f);
    const bool upward = ratio1 > 1.0f;
    const auto targetLow = upward ? targetStretch : 1.0f;    // stretch of the low band's RT
    const auto targetHigh = upward ? 1.0f : targetStretch;   // stretch of the high band's RT
    if (snapNext)
    {
        stretch = targetStretch;
        lowStretch = targetLow;
        highStretch = targetHigh;
    }
    else
    {
        const auto k = 1.0f - std::exp (-(float) numSamples / (0.08f * (float) fs));
        stretch += (targetStretch - stretch) * k;
        lowStretch += (targetLow - lowStretch) * k;
        highStretch += (targetHigh - highStretch) * k;
    }
    const auto rt60 = p.decaySeconds * stretch;
    auto high = maps::toneToHighDecay (p.tone);
    high.ratio = std::min (high.ratio * highStretch / stretch, 1.0f);

    tank.update (numSamples, { .scale = p.sizeScale, .rt60 = rt60, .lowEnd = p.lowEnd * lowStretch / stretch,
                               .high = high, .freeze = freeze,
                               .modDepthMs = 1.6f * maps::clamp01 (p.mod), .modRate = p.modRate,
                               .randomBlend = 0.6f });

    // The shifters fade out of the loop with Freeze: at 1 the loop is the plain, lossless FDN.
    const auto a = shiftedShare (amount, p.sizeScale) * (1.0f - freeze);
    dryShare.set (std::sqrt (1.0f - a), numSamples);
    shiftShare.set (std::sqrt (a), numSamples);

    // Extra loss for the shifter's own latency, so shifted paths decay like any other.
    auto latencyGain = [&] (float ratio)
    {
        const auto g = maps::decayGain (halo::GrainVoice::meanLatencySeconds (ratio) * (float) fs, rt60, (float) fs);
        return g + (1.0f - g) * freeze;
    };
    latencyGain1.set (latencyGain (ratio1), numSamples);
    latencyGain2.set (latencyGain (ratio2 > 0.0f ? ratio2 : ratio1), numSamples);

    const auto target1 = ratio2 > 0.0f ? std::sqrt (0.5f) : 1.0f;
    const auto target2 = ratio2 > 0.0f ? std::sqrt (0.5f) : 0.0f;
    if (snapNext)
    {
        voiceLevel1 = target1;
        voiceLevel2 = target2;
        snapNext = false;
    }

    const bool shifting = shiftShare.get() > 0.0f || shiftShare.target() > 0.0f;

    // Audio rate -----------------------------------------------------------------
    constexpr float inject = 1.0f / 4.0f;   // 1 / sqrt (lines)
    std::array<float, lines> in, y;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto g = diffusionGain.next();
        const auto dl = diffuserL.process (bandwidthL.lowpass (inL[n]), g) * inject;
        const auto dr = diffuserR.process (bandwidthR.lowpass (inR[n]), g) * inject;

        for (int i = 0; i < lines; ++i)
            in[(size_t) i] = hadamardSign (1, i) * dl + hadamardSign (2, i) * dr;

        tank.read (y.data());

        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < lines; ++i)
        {
            sumL += hadamardSign (3, i) * y[(size_t) i];
            sumR += hadamardSign (5, i) * y[(size_t) i];
        }
        outL[n] = tankOutputGain * sumL;
        outR[n] = tankOutputGain * sumR;

        // Voice levels slew per sample (interval changes to/from the two-voice setting).
        voiceLevel1 += std::clamp (target1 - voiceLevel1, -voiceSlew, voiceSlew);
        voiceLevel2 += std::clamp (target2 - voiceLevel2, -voiceSlew, voiceSlew);
        const auto v1 = voiceLevel1 * latencyGain1.next();
        const auto v2 = voiceLevel2 * latencyGain2.next();
        const auto c = dryShare.next();
        const auto s = shiftShare.next();

        for (int k = 0; k < shifterCount; ++k)
        {
            auto& x = y[(size_t) shiftedLine[(size_t) k]];
            const auto shifted = shifters[(size_t) k].process (x, v1, v2, shifting);
            if (shifting)
                x = c * x + s * shifted;
        }

        tank.write (y.data(), in.data());
    }
}

} // namespace cerulean::dsp
