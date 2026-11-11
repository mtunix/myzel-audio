#include "dsp/algorithms/plate/PlateEngine.h"

#include "dsp/Mappings.h"

namespace cerulean::dsp
{

namespace
{
    // Dattorro's reference rate: every length below is in samples at this rate (Size 1x).
    constexpr float dattorroRate = 29761.0f;

    // Input diffusers per side: two short stages for an instantly dense onset, Dattorro's 142/107,
    // then 199/149 (Dattorro's 379/277, shortened: the long pair spread the onset over ~40 ms and
    // made the echo-density build-up depend on the sample rate). Right side: neighbouring primes.
    // The sides are cross-rotated after every stage, so each stage doubles the echo paths.
    constexpr std::array<float, 6> diffuseLNominal { 37.0f, 59.0f, 142.0f, 107.0f, 199.0f, 149.0f };
    constexpr std::array<float, 6> diffuseRNominal { 43.0f, 53.0f, 151.0f, 101.0f, 191.0f, 157.0f };
    constexpr float crossC = 0.8f, crossS = 0.6f;   // rotation by ~37 degrees (c^2 + s^2 = 1)

    // Tank, in the order of the enum. Dattorro's D2 (3720 / 3163) is split into AP3 + D2 so the loop
    // length (and with it the modal density) stays exactly Dattorro's.
    enum { eApL1, eDL1, eApL2, eApL3, eDL2, eApR1, eDR1, eApR2, eApR3, eDR2 };
    constexpr std::array<float, 10> tankNominal {
        672.0f, 4453.0f, 1800.0f, 487.0f, 3720.0f - 487.0f,     // left:  AP1, D1, AP2, AP3, D2
        908.0f, 4217.0f, 2656.0f, 383.0f, 3163.0f - 383.0f      // right: AP1, D1, AP2, AP3, D2
    };

    // The input enters every half twice: at its start (Dattorro) and again before AP2 (the other
    // side's diffuser output), so the energy is spread over the loop from the first pass and the
    // envelope starts at full level instead of blooming over ~150 ms.
    constexpr float startInject = 0.7071f, midInject = 0.7071f;

    struct TapDef { int element; float position; float sign; };

    // Output taps, positions at 29761 Hz: per side Dattorro's seven (Table 2; left reads mostly the
    // right half and vice versa, which is what makes the plate wide) plus three that the injected
    // signal reaches within ~8-15 ms, so the onset is at full level at once instead of stepping
    // up as Dattorro's taps join at 52-100 ms (a 5 dB "second hit" at ~70 ms).
    constexpr std::array<TapDef, 20> tapDefs {{
        { eDR1, 266.0f, 1.0f }, { eDR1, 2974.0f, 1.0f }, { eApR2, 1913.0f, -1.0f }, { eDR2, 1996.0f, 1.0f },
        { eDL1, 1990.0f, -1.0f }, { eApL2, 187.0f, -1.0f }, { eDL2, 1066.0f, -1.0f },
        { eApR2, 241.0f, -1.0f }, { eDL1, 457.0f, 1.0f }, { eApR3, 251.0f, 1.0f },
        { eDL1, 353.0f, 1.0f }, { eDL1, 3627.0f, 1.0f }, { eApL2, 1228.0f, -1.0f }, { eDL2, 2673.0f, 1.0f },
        { eDR1, 2111.0f, -1.0f }, { eApR2, 335.0f, -1.0f }, { eDR2, 121.0f, -1.0f },
        { eApL2, 283.0f, -1.0f }, { eDR1, 409.0f, 1.0f }, { eApL3, 199.0f, 1.0f }
    }};

    constexpr float maxSizeScale = 2.0f;
    constexpr float maxSmear = 1.8f;   // Density above the middle stretches the input diffusers

    // Modulated allpasses: AP1 (Dattorro's excursion, ~0.5 ms at the default Mod) and AP2.
    struct ModDef { int element; float depthMs; float rateHz; };
    constexpr std::array<ModDef, 4> modDefs {{
        { eApL1, 1.00f, 0.83f }, { eApR1, 1.00f, 0.97f }, { eApL2, 0.45f, 0.61f }, { eApR2, 0.45f, 0.71f }
    }};
    constexpr float maxModMs = 1.0f;
    constexpr float modRandomBlend = 0.5f;

    // Dattorro's 0.6 output scale times the level trim (steady pink noise at Decay 2 s -> ~-3 dB).
    constexpr float outputGain = 0.6f * 0.64f;

    /**
        Schroeder allpass whose delay carries the decay gain gamma^D: replacing every z^-D of a
        network by (gamma z^-1)^D multiplies its impulse response by gamma^n exactly, so the
        whole plate (diffusers and the allpass recirculations included) decays at the set RT60.
    */
    inline float allpass (DelayLine& line, float x, float d, float g, float gamma) noexcept
    {
        const auto delayed = gamma * line.readCubic (d);
        const auto w = x + g * delayed;
        line.write (flushTiny (w));
        return delayed - g * w;
    }
}

void PlateEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;
    const auto rate = (float) sampleRate / dattorroRate;
    const auto maxModSamples = msToSamples (maxModMs, sampleRate);

    for (int i = 0; i < numDiffusers; ++i)
    {
        diffuseL[(size_t) i].setMaxDelay ((int) std::ceil (diffuseLNominal[(size_t) i] * rate * maxSizeScale * maxSmear) + 8);
        diffuseR[(size_t) i].setMaxDelay ((int) std::ceil (diffuseRNominal[(size_t) i] * rate * maxSizeScale * maxSmear) + 8);
        diffuseLenL[(size_t) i].prepare (sampleRate, diffuseLNominal[(size_t) i] * rate, 2.0f);
        diffuseLenR[(size_t) i].prepare (sampleRate, diffuseRNominal[(size_t) i] * rate, 2.0f);
    }

    for (int i = 0; i < numTank; ++i)
    {
        tank[(size_t) i].setMaxDelay ((int) std::ceil (tankNominal[(size_t) i] * rate * maxSizeScale + maxModSamples) + 8);
        tankLen[(size_t) i].prepare (sampleRate, tankNominal[(size_t) i] * rate, 4.0f);
    }

    for (size_t i = 0; i < tapDefs.size(); ++i)
        tapLen[i].prepare (sampleRate, tapDefs[i].position * rate, 2.0f);

    for (int i = 0; i < numMod; ++i)
    {
        auto& m = mods[(size_t) i];
        m.sine.setStartPhase ((float) i * 0.25f + 0.1f);
        m.random.setSeed (seed * 2654435761u + (uint32_t) i * 40503u + 7u);
    }

    reset();
}

void PlateEngine::reset()
{
    for (auto& d : diffuseL) d.reset();
    for (auto& d : diffuseR) d.reset();
    for (auto& d : tank) d.reset();
    for (auto& l : diffuseLenL) l.reset();
    for (auto& l : diffuseLenR) l.reset();
    for (auto& l : tankLen) l.reset();
    for (auto& l : tapLen) l.reset();

    for (auto& m : mods)
    {
        m.sine.reset();
        m.random.reset();
        m.offset.reset();
    }

    for (auto& r : diffuseGammaL) r.reset();
    for (auto& r : diffuseGammaR) r.reset();
    for (auto& r : tankGamma) r.reset();
    for (auto& r : tapGain) r.reset();

    decayL.reset();
    decayR.reset();
    decayL2.reset();
    decayR2.reset();
    bandwidthL.reset();
    bandwidthR.reset();

    for (auto* r : { &bandwidthG, &lowCrossG, &highCrossG, &inDiff1, &inDiff2, &decayDiff1, &decayDiff2, &decayDiff3 })
        r->reset();

    snapNext = true;
}

void PlateEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                           int numSamples, const EngineParams& p) noexcept
{
    const auto n = std::max (numSamples, 1);
    const auto snap = snapNext;
    snapNext = false;

    // Control rate ---------------------------------------------------------------
    const auto scale = std::clamp (p.sizeScale, 0.25f, maxSizeScale);
    const auto density = maps::clamp01 (p.paramA);
    const auto metal = maps::clamp01 (p.paramB);
    const auto lower = std::min (density * 2.0f, 1.0f);            // grainy .. Dattorro
    const auto upper = std::max (density * 2.0f - 1.0f, 0.0f);     // Dattorro .. extra smooth

    const auto smear = scale * (1.0f + (maxSmear - 1.0f) * upper);
    for (auto& l : diffuseLenL) l.update (smear, n, snap);
    for (auto& l : diffuseLenR) l.update (smear, n, snap);
    for (auto& l : tankLen) l.update (scale, n, snap);
    for (auto& l : tapLen) l.update (scale, n, snap);
    const auto metalTilt = (0.5f - metal) * 2.0f;                  // +1 damped .. -1 ringing
    const auto diffusionAmount = std::min (std::sqrt (maps::clamp01 (p.diffusion) / 0.75f), 1.1f);

    inDiff1.set (std::min (0.80f, (0.30f + 0.450f * lower + 0.05f * upper) * diffusionAmount), n);
    inDiff2.set (std::min (0.72f, (0.25f + 0.375f * lower + 0.08f * upper) * diffusionAmount), n);
    decayDiff1.set (std::min (0.78f, (0.35f + 0.35f * lower + 0.04f * upper) * (1.0f + 0.1f * metalTilt)), n);
    decayDiff2.set (0.5f + (metalTilt > 0.0f ? 0.1f : 0.25f) * metalTilt, n);
    decayDiff3.set (0.35f * upper, n);

    // Tone and Metal: the high band. Metal bends the ratio towards 1 (ring) or 0 (damped).
    const auto high = maps::toneToHighDecay (p.tone);
    // A plate is brighter than a hall at Tone = flat: its high band rings longer and from higher
    // up. The bias fades out towards full dark, so Dark still means dark.
    const auto plateBias = 1.0f - std::max (-p.tone, 0.0f);
    const auto highRatio = std::pow (high.ratio, (1.0f - 0.2f * plateBias) * std::pow (2.0f, 1.2f * metalTilt));
    const auto highCross = std::clamp (high.crossoverHz * (1.0f + 0.25f * plateBias) * std::pow (2.0f, -0.7f * metalTilt),
                                       1000.0f, 16000.0f);
    lowCrossG.set (OnePole::coefficientFor (maps::lowCrossoverHz, fs), n);
    highCrossG.set (OnePole::coefficientFor (highCross, fs), n);

    const auto bandwidthHz = std::clamp (13000.0f * std::pow (2.0f, 0.9f * p.tone - 0.3f * metalTilt), 3500.0f, 20000.0f);
    bandwidthG.set (OnePole::coefficientFor (bandwidthHz, fs), n);

    // Decay. Every delay element carries gamma^D (mid band), so the response is windowed by an
    // exact exponential; shelves add the low/high band difference over their share of the loop.
    // Freeze scales every decay *rate* by (1 - freeze) (log domain): each band's loop gain is
    // then gamma_band^(L (1 - freeze)) <= 1 for any freeze < 1, and exactly 1 when frozen.
    // (Blending each factor linearly towards 1 is not safe: a boosting low shelf times a
    // blended mid gain can exceed 1.)
    const auto freeze = std::clamp (p.freeze, 0.0f, 1.0f);
    const auto rtMid = std::max (p.decaySeconds, 1.0e-3f);
    const auto live = freeze >= 1.0f ? 0.0f : 1.0f - freeze;
    const auto perSampleDb = -60.0f * live / (rtMid * (float) fs);
    auto gamma = [&] (float length) { return live > 0.0f ? std::pow (10.0f, perSampleDb * length / 20.0f) : 1.0f; };

    for (int i = 0; i < numDiffusers; ++i)
    {
        diffuseGammaL[(size_t) i].set (gamma (diffuseLenL[(size_t) i].end()), n);
        diffuseGammaR[(size_t) i].set (gamma (diffuseLenR[(size_t) i].end()), n);
    }
    for (int i = 0; i < numTank; ++i)
        tankGamma[(size_t) i].set (gamma (tankLen[(size_t) i].end()), n);
    for (size_t i = 0; i < tapDefs.size(); ++i)
        tapGain[i].set (tapDefs[i].sign * gamma (tapLen[i].end()), n);

    // Two shelving filters per half (after D1 and after D2), each covering its own share of the
    // loop: a single first-order shelf per 360 ms half saturates long before a dark target.
    auto shelves = [&] (int first, int count, BandDecay& filter)
    {
        float length = 0.0f;
        for (int i = first; i < first + count; ++i)
            length += tankLen[(size_t) i].end();

        auto ratio = [&] (float rtBand)
        {
            const auto db = -60.0f * live * length / (float) fs * (1.0f / std::max (rtBand, 1.0e-3f) - 1.0f / rtMid);
            return live > 0.0f ? std::pow (10.0f, db / 20.0f) : 1.0f;
        };
        filter.setRatios (ratio (rtMid * p.lowEnd), ratio (rtMid * highRatio), n);
    };
    shelves (eApL1, 2, decayL);
    shelves (eApL2, 3, decayL2);
    shelves (eApR1, 2, decayR);
    shelves (eApR2, 3, decayR2);

    // Modulation of the decay-diffusion allpasses (off when frozen: lossless, no pitch drift).
    for (int i = 0; i < numMod; ++i)
    {
        auto& m = mods[(size_t) i];
        const auto rate = modDefs[(size_t) i].rateHz * p.modRate;
        m.sine.setRate (rate, fs);
        m.random.setRate (rate * 1.7f, fs);
        const auto depth = msToSamples (modDefs[(size_t) i].depthMs * maps::clamp01 (p.mod), fs) * (1.0f - freeze);
        const auto shape = (1.0f - modRandomBlend) * m.sine.advance (n) + modRandomBlend * m.random.advance (n);
        m.offset.set (freeze >= 1.0f ? 0.0f : depth * shape, n);
    }

    // Audio rate -----------------------------------------------------------------
    for (int s = 0; s < numSamples; ++s)
    {
        const auto bw = bandwidthG.next();
        bandwidthL.setCoefficient (bw);
        bandwidthR.setCoefficient (bw);
        auto l = bandwidthL.lowpass (inL[s]);
        auto r = bandwidthR.lowpass (inR[s]);

        const auto g1 = inDiff1.next(), g2 = inDiff2.next();
        for (int i = 0; i < numDiffusers; ++i)
        {
            const auto g = i < numDiffusers - 2 ? g1 : g2;
            const auto a = allpass (diffuseL[(size_t) i], l, diffuseLenL[(size_t) i].next(), g, diffuseGammaL[(size_t) i].next());
            const auto b = allpass (diffuseR[(size_t) i], r, diffuseLenR[(size_t) i].next(), g, diffuseGammaR[(size_t) i].next());
            // Orthogonal L/R rotation between stages: every stage doubles the echo paths.
            l = crossC * a + crossS * b;
            r = crossC * b - crossS * a;
        }

        float len[numTank], gam[numTank];
        for (int i = 0; i < numTank; ++i)
        {
            len[i] = tankLen[(size_t) i].next();
            gam[i] = tankGamma[(size_t) i].next();
        }
        len[eApL1] += mods[0].offset.next();
        len[eApR1] += mods[1].offset.next();
        len[eApL2] += mods[2].offset.next();
        len[eApR2] += mods[3].offset.next();

        const auto lowG = lowCrossG.next(), highG = highCrossG.next();

        // Feedback ends: read every delay before anything is written this sample.
        const auto fromD1L = gam[eDL1] * tank[eDL1].readCubic (len[eDL1]);
        const auto fromD2L = decayL2.process (gam[eDL2] * tank[eDL2].readCubic (len[eDL2]), lowG, highG);
        const auto fromD1R = gam[eDR1] * tank[eDR1].readCubic (len[eDR1]);
        const auto fromD2R = decayR2.process (gam[eDR2] * tank[eDR2].readCubic (len[eDR2]), lowG, highG);

        const auto dd1 = decayDiff1.next(), dd2 = decayDiff2.next(), dd3 = decayDiff3.next();

        // Left half.
        tank[eDL1].write (flushTiny (allpass (tank[eApL1], startInject * l + fromD2R, len[eApL1], -dd1, gam[eApL1])));
        {
            auto y = decayL.process (fromD1L, lowG, highG) + midInject * r;
            y = allpass (tank[eApL2], y, len[eApL2], dd2, gam[eApL2]);
            y = allpass (tank[eApL3], y, len[eApL3], -dd3, gam[eApL3]);
            tank[eDL2].write (flushTiny (y));
        }

        // Right half.
        tank[eDR1].write (flushTiny (allpass (tank[eApR1], startInject * r + fromD2L, len[eApR1], -dd1, gam[eApR1])));
        {
            auto y = decayR.process (fromD1R, lowG, highG) + midInject * l;
            y = allpass (tank[eApR2], y, len[eApR2], dd2, gam[eApR2]);
            y = allpass (tank[eApR3], y, len[eApR3], -dd3, gam[eApR3]);
            tank[eDR2].write (flushTiny (y));
        }

        // Output taps (each carries gamma^position, so the windowing stays exact at the taps).
        float yl = 0.0f, yr = 0.0f;
        for (int t = 0; t < numTaps; ++t)
        {
            const auto& a = tapDefs[(size_t) t];
            yl += tapGain[(size_t) t].next() * tank[(size_t) a.element].readCubic (tapLen[(size_t) t].next());
            const auto& b = tapDefs[(size_t) (t + numTaps)];
            yr += tapGain[(size_t) (t + numTaps)].next() * tank[(size_t) b.element].readCubic (tapLen[(size_t) (t + numTaps)].next());
        }

        outL[s] = outputGain * yl;
        outR[s] = outputGain * yr;
    }
}

} // namespace cerulean::dsp
