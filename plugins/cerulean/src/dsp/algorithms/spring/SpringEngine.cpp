#include "dsp/algorithms/spring/SpringEngine.h"

#include "dsp/Mappings.h"

namespace cerulean::dsp
{

namespace
{
    // One-way transit time of each spring (ms at Size 1x). Long-decay tank territory (a round
    // trip of ~58-80 ms), spaced so no two share a small common ratio: their "boings" never lock.
    constexpr std::array<float, 3> transitMs { 28.73f, 34.37f, 39.61f };

    // Each spring's own dispersion (a scale on the Drip coefficients): no two chirp alike.
    constexpr std::array<float, 3> dispersionScale { 1.0f, 0.92f, 1.07f };

    // Drip -> stretched allpass coefficients. The 700 Hz -> 3.8 kHz spread of one stage peaks
    // near a = 0.55; beyond that a bigger a only piles delay into the top few hundred Hz under
    // fc (which the image cut removes). So the first group rises 0.06 -> 0.55 over Drip 0 .. 0.5
    // and the second group (pure delays below 0.5) joins 0 -> 0.55 over 0.5 .. 1: in-band chirp
    // spread grows steadily over the whole knob.
    constexpr float minCoefficient = 0.06f, maxCoefficient = 0.55f;

    // Coil "smear" allpasses in each round trip (ms at Size 1x): the coupling between the coils
    // and the end mounts that slowly turns the chirps into a wash. Diffusion sets how much.
    constexpr std::array<std::array<float, 2>, 3> smearMs {{ { 2.93f, 4.41f }, { 3.37f, 5.03f }, { 3.81f, 5.77f } }};

    // Input weights (L, R) per spring: spring 1 hears the centre, 2 and 3 lean to one side.
    constexpr std::array<std::array<float, 2>, 3> injectWeights {{ { 0.5f, 0.5f }, { 0.65f, 0.35f }, { 0.35f, 0.65f } }};

    // Which output side each spring's far-end pickup feeds (the near end feeds the other side).
    constexpr std::array<bool, 3> farToLeft { true, false, true };

    // Cross-feed of each pickup into the opposite side, by spring count: a real tank is narrow,
    // more springs give a wider (still mono-compatible) image. See NOTES.md.
    constexpr std::array<float, 3> crossFeed { 0.30f, 0.22f, 0.17f };

    // Sizzle: the weak, diffuse modes above the chirp cutoff.
    constexpr float sizzleLineMs = 27.43f;
    constexpr float sizzleRightTap = 0.79f;   // the right output hears the sizzle line earlier
    constexpr std::array<float, 3> sizzleSmearMs { 1.27f, 2.09f, 3.31f };

    constexpr float targetCutoffHz = 4600.0f;   // chirp cutoff fc = fs / 2K, K rounded from this
    constexpr float maxSizeScale = 2.0f;
    constexpr float maxModMs = 0.3f;
    constexpr float nominalModHz = 0.9f;
    constexpr float fadeMs = 30.0f;

    constexpr float outputLevel = 1.0f;
}

//==============================================================================
void SpringEngine::GlidingAllpass::prepare (double fs, float ms, float maxScale)
{
    nominal = ms;
    ap.setMaxDelay ((int) std::ceil (msToSamples (ms * maxScale, fs)) + 8);
    length.prepare (fs);
    length.snap (msToSamples (ms, fs));
}

void SpringEngine::GlidingAllpass::reset() noexcept
{
    ap.reset();
    length.snap (length.getTarget());
    current = blockEnd = length.get();
    step = 0.0f;
}

void SpringEngine::GlidingAllpass::update (int numSamples, float scale, bool frozen, bool snap, double fs) noexcept
{
    auto target = std::max (2.0f, msToSamples (nominal * scale, fs));
    if (frozen)
        target = std::round (target);   // integer: the Lagrange read is exact, the allpass lossless
    if (snap)
        length.snap (target);
    else
        length.setTarget (target);

    // Each block ramps from exactly where the last one ended (no float drift in `current`),
    // so a frozen, integer length is read exactly.
    const auto end = length.advance (numSamples);
    current = snap ? end : blockEnd;
    blockEnd = end;
    step = (end - current) / (float) std::max (numSamples, 1);
}

//==============================================================================
void SpringEngine::GlidingLine::prepare (double fs, float ms, float maxScale, float maxMod, uint32_t randomSeed)
{
    nominal = ms;
    line.setMaxDelay ((int) std::ceil (msToSamples (ms * maxScale + maxMod, fs)) + 8);
    length.prepare (fs);
    length.snap (msToSamples (ms, fs));
    wobble.setSeed (randomSeed);
}

void SpringEngine::GlidingLine::reset() noexcept
{
    line.reset();
    wobble.reset();
    length.snap (length.getTarget());
    current = blockEnd = length.get();
    step = 0.0f;
}

void SpringEngine::GlidingLine::update (int numSamples, float scale, float offsetSamples, float modSamples,
                                        float rateHz, bool frozen, bool snap, double fs) noexcept
{
    auto target = std::max (4.0f, msToSamples (nominal * scale, fs));
    if (frozen)
        target = std::round (target);
    if (snap)
        length.snap (target);
    else
        length.setTarget (target);

    const auto n = std::max (numSamples, 1);
    const auto base = length.advance (n);
    wobble.setRate (rateHz, fs);
    const auto end = std::max (base + offsetSamples + modSamples * wobble.advance (n), minLength);
    current = snap ? end : blockEnd;
    blockEnd = end;
    step = (end - current) / (float) n;
}

//==============================================================================
void SpringEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;
    stretch = std::max (1, (int) std::lround (sampleRate / (2.0 * targetCutoffHz)));
    outward.prepare (stretch);
    backward.prepare (stretch);

    for (size_t s = 0; s < springs.size(); ++s)
    {
        auto& sp = springs[s];
        const auto lineSeed = rngSeed * 7919u + (uint32_t) s * 104729u;
        sp.out.prepare (sampleRate, transitMs[s], maxSizeScale, maxModMs, lineSeed + 1u);
        sp.back.prepare (sampleRate, transitMs[s], maxSizeScale, maxModMs, lineSeed + 2u);
        sp.back.minLength = (float) (maxSubBlock + 4);   // process() defers its writes by a block
        for (size_t k = 0; k < sp.smear.size(); ++k)
            sp.smear[k].prepare (sampleRate, smearMs[s][k], maxSizeScale);
    }

    sizzleLine.prepare (sampleRate, sizzleLineMs, maxSizeScale, maxModMs, rngSeed * 31u + 7u);
    for (size_t k = 0; k < sizzleSmear.size(); ++k)
        sizzleSmear[k].prepare (sampleRate, sizzleSmearMs[k], maxSizeScale);

    for (size_t ch = 0; ch < 2; ++ch)
    {
        lowCut[ch].setCoefficients (95.0f, 0.6f, sampleRate);
        emphasisSplit[ch].setCutoff (1500.0f, sampleRate);
        bandwidth[ch].setCoefficients (6500.0f, 0.6f, sampleRate);

        // 4th-order Butterworth just under fc: removes the mirrored chirp images above it.
        const auto fc = (float) (sampleRate / (2.0 * stretch));
        imageCut1[ch].setCoefficients (0.92f * fc, 0.5412f, sampleRate);
        imageCut2[ch].setCoefficients (0.92f * fc, 1.3066f, sampleRate);
        sizzleTop[ch].setCoefficients (7000.0f, 0.7071f, sampleRate);
    }

    sizzleSplit1.setCoefficients (3200.0f, 0.5412f, sampleRate);
    sizzleSplit2.setCoefficients (3200.0f, 1.3066f, sampleRate);

    reset();
}

void SpringEngine::reset()
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        lowCut[ch].reset();
        emphasisSplit[ch].reset();
        bandwidth[ch].reset();
        drive[ch].reset();
        imageCut1[ch].reset();
        imageCut2[ch].reset();
        sizzleTop[ch].reset();
    }
    emphasis.reset();
    driveGain.reset();
    driveMix.reset();

    for (auto& sp : springs)
    {
        sp.out.reset();
        sp.back.reset();
        sp.decay.reset();
        for (auto& a : sp.smear)
            a.reset();
        sp.active.reset();
        sp.active.value = 0.0f;
        for (auto* r : { &sp.injectL, &sp.injectR, &sp.farL, &sp.farR, &sp.nearL, &sp.nearR })
            r->reset();
    }
    outward.reset();
    backward.reset();
    for (size_t s = 0; s < springs.size(); ++s)
    {
        dispersionFirst[s].reset();
        dispersionRest[s].reset();
    }

    sizzleSplit1.reset();
    sizzleSplit2.reset();
    sizzleLine.reset();
    sizzleDecay.reset();
    for (auto& a : sizzleSmear)
        a.reset();
    sizzleLevel.reset();
    sizzleInput.reset();

    smearGain.reset();
    outputGain.reset();
    snapNext = true;
}

//==============================================================================
void SpringEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                            int numSamples, const EngineParams& p) noexcept
{
    const auto n = std::max (numSamples, 1);
    const auto snap = snapNext;
    snapNext = false;

    // Control rate ---------------------------------------------------------------
    const auto freeze = std::clamp (p.freeze, 0.0f, 1.0f);
    const auto frozen = freeze > 0.0f;
    const auto drip = maps::clamp01 (p.paramB);
    const auto count = maps::springCount (p.paramA);
    const auto scale = std::clamp (p.sizeScale, 0.3f, maxSizeScale);
    const auto high = maps::toneToHighDecay (p.tone);
    const auto lowCoeff = ShelfCoefficients::at (maps::lowCrossoverHz, fs);
    const auto highCoeff = ShelfCoefficients::at (high.crossoverHz, fs);
    const auto modSamples = msToSamples (maxModMs * maps::clamp01 (p.mod), fs) * (1.0f - freeze);
    const auto modHz = nominalModHz * std::clamp (p.modRate, 0.05f, 8.0f);
    const auto omegaMid = twoPi * 1000.0f / (float) fs;

    // Drip: treble emphasis into the tank (0 .. +9 dB above 1.5 kHz), the dwell (drive, +1 .. +8
    // dB into the soft clipper) and the dispersion (see maxCoefficient).
    const auto emphasisAmount = dbToGain (9.0f * drip) - 1.0f;
    emphasis.set (emphasisAmount, n);
    const auto dwell = dbToGain (1.0f + 7.0f * drip);
    driveGain.set (dwell, n);
    driveMix.set (1.0f - freeze, n);   // the drive is bypassed while frozen (input is cut anyway)
    const auto firstCoefficient = minCoefficient + (maxCoefficient - minCoefficient) * std::min (1.0f, 2.0f * drip);
    const auto restCoefficient = maxCoefficient * std::max (0.0f, 2.0f * drip - 1.0f);

    // Springs: fade in/out, pickups, decay.
    const auto fadeCoefficient = 1.0f - std::exp (-(float) n / (fadeMs * 0.001f * (float) fs));
    const auto cross = crossFeed[(size_t) (count - 1)];
    float activeSum = 0.0f;
    for (size_t s = 0; s < springs.size(); ++s)
    {
        auto& sp = springs[s];
        sp.active.update ((int) s < count ? 1.0f : 0.0f, fadeCoefficient, snap, n);
        activeSum += sp.active.value;
    }
    const auto norm = 1.0f / std::sqrt (std::max (activeSum, 1.0f));

    for (size_t s = 0; s < springs.size(); ++s)
    {
        auto& sp = springs[s];
        const auto a1 = firstCoefficient * dispersionScale[s];
        const auto a2 = restCoefficient * dispersionScale[s];
        dispersionFirst[s].set (a1, n);
        dispersionRest[s].set (a2, n);

        // Drip changes the cascade's low-frequency delay (more dispersion = less delay at DC).
        // The transits take up the difference to Drip 0.5, so the spring keeps its length (echo
        // spacing) and moving Drip doesn't bend the pitch of the low end.
        auto offset = Cascade::groupDelay (maxCoefficient * dispersionScale[s], 0.0f, stretch, 0.0f)
                    - Cascade::groupDelay (a1, a2, stretch, 0.0f);
        if (frozen)
            offset = std::round (offset);

        sp.out.update (n, scale, offset, modSamples, modHz * (0.8f + 0.2f * (float) s), frozen, snap, fs);
        sp.back.update (n, scale, offset, modSamples, modHz * (1.15f + 0.15f * (float) s), frozen, snap, fs);
        float smearLength = 0.0f;
        for (auto& ap : sp.smear)
        {
            ap.update (n, scale, frozen, snap, fs);
            smearLength += ap.length.get();
        }

        // Loop length at 1 kHz: both transits, the dispersion's group delay there and the smear.
        const auto loop = sp.out.lastEnd() + sp.back.lastEnd() + smearLength
                        + 2.0f * Cascade::groupDelay (a1, a2, stretch, omegaMid);
        const auto g = computeBandGains (loop, p.decaySeconds, p.lowEnd, high.ratio, freeze, (float) fs);
        sp.decay.setCrossovers (lowCoeff, highCoeff);
        sp.decay.setGains (g.low, g.mid, g.high, n);

        // A loop's steady-state energy at a given RT60 goes with 1 / length: sqrt (Size) on the
        // input keeps the loudness put when Size moves (the ringing tail is not touched).
        const auto level = sp.active.value;
        const auto inject = level * std::sqrt (scale);
        sp.injectL.set (inject * injectWeights[s][0], n);
        sp.injectR.set (inject * injectWeights[s][1], n);
        const auto w = level * norm;
        const auto toFar = farToLeft[s];
        sp.farL.set (w * (toFar ? 1.0f : cross), n);
        sp.farR.set (w * (toFar ? cross : 1.0f), n);
        sp.nearL.set (w * (toFar ? cross : 1.0f), n);
        sp.nearR.set (w * (toFar ? 1.0f : cross), n);
    }

    // Sizzle loop.
    sizzleLine.update (n, scale, 0.0f, modSamples, modHz * 1.37f, frozen, snap, fs);
    float sizzleLength = sizzleLine.base();
    for (auto& ap : sizzleSmear)
    {
        ap.update (n, scale, frozen, snap, fs);
        sizzleLength += ap.length.get();
    }
    {
        const auto g = computeBandGains (sizzleLength, p.decaySeconds, p.lowEnd, high.ratio, freeze, (float) fs);
        sizzleDecay.setCrossovers (lowCoeff, highCoeff);
        sizzleDecay.setGains (g.low, g.mid, g.high, n);
    }
    sizzleLevel.set (0.35f * (0.6f + 0.4f * drip), n);
    sizzleInput.set (std::sqrt (scale), n);

    smearGain.set (0.6f * std::sqrt (maps::clamp01 (p.diffusion)), n);
    outputGain.set (outputLevel / std::sqrt (1.0f + 0.25f * emphasisAmount * emphasisAmount), n);

    // Audio rate -----------------------------------------------------------------
    // In passes over the block. The back transit is longer than a sub-block (~290 samples at the
    // shortest setting, and clamped to maxSubBlock + 4), so every near-end return this block reads
    // was written in an earlier block: the springs' inputs for the whole block are known up
    // front, and each dispersion cascade runs over the block at once (interleaved, see
    // DispersionCascade::processBlock). The back lines' writes are deferred to the end of the
    // block, so pass 1 reads them i samples closer. Same recursion as a per-sample loop (tested:
    // any sub-block size gives a bit-identical impulse response).
    const auto blockSize = std::clamp (numSamples, 0, maxSubBlock);

    // Pass 1: input stage, near-end returns, spring inputs.
    for (int i = 0; i < blockSize; ++i)
    {
        const auto e = emphasis.next();
        const auto dg = driveGain.next();
        const auto dm = driveMix.next();
        const float in[2] = { inL[i], inR[i] };
        auto& driven = blockDriven[(size_t) i];
        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto v = lowCut[ch].highpass (in[ch]);
            v += e * emphasisSplit[ch].highpass (v);
            v = bandwidth[ch].lowpass (v);

            const auto clipped = drive[ch].process (v * dg) / dg;
            driven[ch] = v + dm * (clipped - v);
        }

        const auto sg = smearGain.next();
        auto& x = blockX[(size_t) i];
        for (size_t s = 0; s < springs.size(); ++s)
        {
            auto& sp = springs[s];
            auto r = sp.decay.process (sp.back.readDeferred (i));
            for (auto& ap : sp.smear)
                r = ap.process (r, sg);
            blockNear[(size_t) i][s] = r;
            x[s] = r + sp.injectL.next() * driven[0] + sp.injectR.next() * driven[1];
            blockFirst[(size_t) i][s] = dispersionFirst[s].next();
            blockRest[(size_t) i][s] = dispersionRest[s].next();
        }
        x[3] = 0.0f;
        blockFirst[(size_t) i][3] = blockRest[(size_t) i][3] = 0.0f;
    }

    // Pass 2: outward dispersion, the out transit, both pickups.
    outward.processBlock (blockX.data(), blockFirst.data(), blockRest.data(), blockSize);
    for (int i = 0; i < blockSize; ++i)
    {
        auto& x = blockX[(size_t) i];
        float lfL = 0.0f, lfR = 0.0f;
        for (size_t s = 0; s < springs.size(); ++s)
        {
            auto& sp = springs[s];
            sp.out.line.write (flushTiny (x[s]));
            const auto far = sp.out.read();
            const auto nearEnd = blockNear[(size_t) i][s];
            x[s] = far;
            lfL += sp.farL.next() * far + sp.nearL.next() * nearEnd;
            lfR += sp.farR.next() * far + sp.nearR.next() * nearEnd;
        }
        blockLf[(size_t) i] = { lfL, lfR };
    }

    // Pass 3: backward dispersion into the back transit.
    backward.processBlock (blockX.data(), blockFirst.data(), blockRest.data(), blockSize);
    for (int i = 0; i < blockSize; ++i)
        for (size_t s = 0; s < springs.size(); ++s)
            springs[s].back.line.write (flushTiny (blockX[(size_t) i][s]));

    // Pass 4: sizzle and output.
    for (int i = 0; i < blockSize; ++i)
    {
        // Sizzle: the high modes, diffuse and weak.
        const auto& driven = blockDriven[(size_t) i];
        auto hf = 0.5f * (driven[0] + driven[1]);
        hf = sizzleInput.next() * sizzleSplit2.highpass (sizzleSplit1.highpass (hf));
        const auto sizzleReturn = sizzleDecay.process (sizzleLine.read());
        auto h = sizzleReturn + hf;
        for (auto& ap : sizzleSmear)
            h = ap.process (h, 0.62f);
        sizzleLine.line.write (flushTiny (h));
        const auto sizzleRight = sizzleLine.line.readCubic (sizzleRightTap * sizzleLine.current);
        const auto sl = sizzleLevel.next();

        // Output: remove the chirp images above fc, add the sizzle.
        const auto og = outputGain.next();
        const auto l = imageCut2[0].lowpass (imageCut1[0].lowpass (blockLf[(size_t) i][0]));
        const auto r = imageCut2[1].lowpass (imageCut1[1].lowpass (blockLf[(size_t) i][1]));
        outL[i] = og * (l + sl * sizzleTop[0].lowpass (sizzleReturn));
        outR[i] = og * (r - sl * sizzleTop[1].lowpass (sizzleRight));
    }
}

} // namespace cerulean::dsp
