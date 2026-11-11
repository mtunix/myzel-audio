#include "dsp/algorithms/rise/RiseEngine.h"

namespace cerulean::dsp
{

namespace
{
    // Tank A lengths (ms at Size 1x): 41-163 ms, mean ~93 ms, ~20 % longer than Hall's, for a
    // slower, larger (cathedral-like) space; no two sharing a small common factor.
    constexpr std::array<float, 16> tankAMs {
        41.3f, 46.9f, 52.7f, 58.1f, 64.3f, 70.9f, 77.3f, 84.7f,
        92.1f, 99.7f, 108.3f, 117.1f, 127.3f, 137.9f, 149.3f, 162.7f
    };
    // Tank B: the same spread x 1.0737, so two notes in different tanks never share modes.
    constexpr float tankBRatio = 1.0737f;

    // Long, sticky input diffusion: the swell hides the onset anyway, so the diffusers only
    // need to make the texture smooth (bowed rather than picked).
    // Whole samples at 48/96/192 kHz (n / 48 ms), so the reads at nominal Size are exact and
    // the onset's echo pattern is the same at every rate (no extra Lagrange smear at 48 kHz).
    constexpr std::array<float, 6> diffuseLMs { 149.0f / 48.0f, 206.0f / 48.0f, 341.0f / 48.0f, 542.0f / 48.0f, 859.0f / 48.0f, 1262.0f / 48.0f };
    constexpr std::array<float, 6> diffuseRMs { 178.0f / 48.0f, 235.0f / 48.0f, 379.0f / 48.0f, 610.0f / 48.0f, 926.0f / 48.0f, 1349.0f / 48.0f };

    constexpr float maxSizeScale = 2.0f;
    constexpr float maxModMs = 3.0f;

    // Output gain: steady pink noise at Decay 2 s (swell complete) comes out near -3 dB.
    constexpr float tankOutputGain = 0.5f;

    // Detector (see NOTES.md).
    constexpr float lookAheadMs = 5.0f;        // wet-only look-ahead: the routing switches before the onset arrives
    constexpr float routeFadeMs = 2.0f;
    constexpr float envAttackMs = 0.3f;
    constexpr float envReleaseMs = 30.0f;
    constexpr float baselineMs = 60.0f;        // running baseline the onset must jump above
    constexpr float onsetRatio = 2.8f;         // +9 dB above the baseline
    constexpr float gateDb = -60.0f;           // below this, nothing triggers (hum, noise floor)
    constexpr float rearmRatio = 0.25f;        // re-arm once the note fell 12 dB below its peak
    constexpr float peakReleaseMs = 4000.0f;   // the note peak itself sags slowly (2.2 dB/s)
    constexpr float refractoryMs = 50.0f;      // never two triggers within one pick
    constexpr float wetDuckMs = 8.0f;          // a re-used tank's older tail fades this fast
    constexpr float dryDuckMs = 3.0f;          // a still-open dry closes this fast on a new pick
    constexpr float dryCloseMs = 250.0f;       // dry fades out once the note has died away
    constexpr float energyMs = 100.0f;         // per-tank loudness, for choosing the quieter tank
    constexpr float releaseBloomMs = 600.0f;   // once a note has died, its wet swell completes within this

    float perSample (float ms, double fs) { return std::exp (-1.0f / (ms * 0.001f * (float) fs)); }
}

void RiseEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;

    for (auto* d : { &wetDetector, &dryDetector })
    {
        d->attack = 1.0f - perSample (envAttackMs, fs);
        d->release = 1.0f - perSample (envReleaseMs, fs);
        d->baseCoeff = 1.0f - perSample (baselineMs, fs);
        d->peakRelease = perSample (peakReleaseMs, fs);
        d->gate = dbToGain (gateDb);
        d->refractory = (int) std::lround (refractoryMs * 0.001 * fs);
    }
    wetDuckK = perSample (wetDuckMs, fs);
    dryDuckK = perSample (dryDuckMs, fs);
    dryCloseK = perSample (dryCloseMs, fs);
    energyK = perSample (energyMs, fs);

    lookAheadSamples = std::max (1, (int) std::lround (lookAheadMs * 0.001 * fs));
    lookAheadL.setMaxDelay (lookAheadSamples + 2);
    lookAheadR.setMaxDelay (lookAheadSamples + 2);
    routeStep = 1.0f / msToSamples (routeFadeMs, fs);

    diffuserL.prepare (sampleRate, diffuseLMs, maxSizeScale);
    diffuserR.prepare (sampleRate, diffuseRMs, maxSizeScale);

    std::array<float, 16> tankBMs {};
    for (size_t i = 0; i < tankBMs.size(); ++i)
        tankBMs[i] = tankAMs[i] * tankBRatio;

    tanks[0].prepare (sampleRate, { .delayMs = tankAMs, .maxScale = maxSizeScale, .maxModMs = maxModMs,
                                    .nominalModHz = 0.35f, .seed = seed * 2u + 1u,
                                    .mixing = Fdn<lines>::Mixing::hadamard });
    tanks[1].prepare (sampleRate, { .delayMs = tankBMs, .maxScale = maxSizeScale, .maxModMs = maxModMs,
                                    .nominalModHz = 0.35f, .seed = seed * 2u + 2u,
                                    .mixing = Fdn<lines>::Mixing::hadamard });
    reset();
}

void RiseEngine::reset()
{
    wetDetector.reset();
    dryDetector.reset();
    detectorL = detectorR = nullptr;
    triggerCount = 0;

    bandwidthL.reset();
    bandwidthR.reset();
    lookAheadL.reset();
    lookAheadR.reset();
    diffuserL.reset();
    diffuserR.reset();
    for (auto& t : tanks)
        t.reset();
    for (auto& s : tankSwell)
        s.reset();
    tankEnergy = {};
    currentTank = 0;
    route = routeTarget = 0.0f;

    diffusionGain.reset();
    blendRamp.reset();
    freezeRamp.reset();
    drySwell.reset();
    dryGain.fill (1.0f);
}

RiseEngine::OnsetDetector::Event RiseEngine::OnsetDetector::process (float level) noexcept
{
    Event e;
    env += (level - env) * (level > env ? attack : release);
    env = flushTiny (env);
    peak = std::max (peak * peakRelease, env);
    since = std::min (since + 1, 1 << 30);

    if (! armed && since >= refractory && (env < rearmRatio * peak || env < gate))
    {
        armed = true;
        e.rearm = true;
    }

    if (armed && env > gate && env > onsetRatio * baseline)
    {
        armed = false;
        since = 0;
        peak = env;
        e.trigger = true;
    }

    baseline += (env - baseline) * baseCoeff;   // linear running baseline of the envelope
    baseline = flushTiny (baseline);
    return e;
}

void RiseEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                          int numSamples, const EngineParams& p) noexcept
{
    // The player's undelayed signal for the dry swell, when the chain provides it.
    const auto* dryInL = detectorL != nullptr ? detectorL : inL;
    const auto* dryInR = detectorR != nullptr ? detectorR : inR;
    detectorL = detectorR = nullptr;

    // Control rate ---------------------------------------------------------------
    const auto bandwidthHz = std::clamp (9500.0f * std::pow (2.0f, 0.9f * p.tone), 4000.0f, 18000.0f);
    bandwidthL.setCutoff (bandwidthHz, fs);
    bandwidthR.setCutoff (bandwidthHz, fs);

    diffuserL.setScale (p.sizeScale);
    diffuserR.setScale (p.sizeScale);
    diffuserL.advance (numSamples);
    diffuserR.advance (numSamples);
    diffusionGain.set (std::min (0.78f, 0.7f * std::sqrt (maps::clamp01 (p.diffusion)) + 0.06f), numSamples);

    const Fdn<lines>::Controls controls { .scale = p.sizeScale, .rt60 = p.decaySeconds, .lowEnd = p.lowEnd,
                                          .high = maps::toneToHighDecay (p.tone), .freeze = p.freeze,
                                          .modDepthMs = 2.2f * maps::clamp01 (p.mod), .modRate = p.modRate,
                                          .randomBlend = 0.7f };
    tanks[0].update (numSamples, controls);
    tanks[1].update (numSamples, controls);

    blendRamp.set (maps::clamp01 (p.paramB), numSamples);
    freezeRamp.set (std::clamp (p.freeze, 0.0f, 1.0f), numSamples);

    // Swell and close rates. While frozen nothing moves, so the held sound can't change level.
    const auto run = 1.0f - std::clamp (p.freeze, 0.0f, 1.0f);
    const auto riseInc = run / msToSamples (maps::riseTimeMs (p.paramA), fs);
    const auto bloomInc = std::max (riseInc, run / msToSamples (releaseBloomMs, fs));
    auto slowed = [run] (float k) { return 1.0f - (1.0f - k) * run; };
    const auto wetDuck = slowed (wetDuckK), dryDuck = slowed (dryDuckK), dryClose = slowed (dryCloseK);

    // Audio rate -----------------------------------------------------------------
    constexpr float inject = 1.0f / 4.0f;   // 1 / sqrt (lines)

    for (int n = 0; n < numSamples; ++n)
    {
        // Wet: onsets as they reach the tanks (the engine input, i.e. after pre-delay).
        if (wetDetector.process (std::max (std::abs (inL[n]), std::abs (inR[n]))).trigger)
        {
            ++triggerCount;

            // The new note goes to the quieter tank; the other keeps ringing as it is.
            const auto chosen = tankEnergy[1] < tankEnergy[0] ? 1 : 0;
            currentTank = chosen;
            routeTarget = (float) chosen;
            tankSwell[(size_t) chosen].trigger();
        }

        // Dry: onsets as the player plays them.
        const auto dryEvent = dryDetector.process (std::max (std::abs (dryInL[n]), std::abs (dryInR[n])));
        if (dryEvent.rearm)
            drySwell.closing = true;   // the note has died away: the dry fades out, ready to swell again
        if (dryEvent.trigger)
            drySwell.trigger();

        // Wet path: 5 ms behind the detector, so a trigger always precedes the note it found.
        lookAheadL.write (bandwidthL.lowpass (inL[n]));
        lookAheadR.write (bandwidthR.lowpass (inR[n]));
        const auto l = lookAheadL.read (lookAheadSamples);
        const auto r = lookAheadR.read (lookAheadSamples);

        const auto g = diffusionGain.next();
        const auto dl = diffuserL.process (l, g) * inject;
        const auto dr = diffuserR.process (r, g) * inject;

        if (route < routeTarget)
            route = std::min (routeTarget, route + routeStep);
        else if (route > routeTarget)
            route = std::max (routeTarget, route - routeStep);
        const std::array<float, 2> feed { 1.0f - route, route };

        float sumL = 0.0f, sumR = 0.0f;
        for (size_t k = 0; k < 2; ++k)
        {
            std::array<float, lines> in, y;
            const auto a = feed[k] * dl, b = feed[k] * dr;
            for (int i = 0; i < lines; ++i)
                in[(size_t) i] = hadamardSign (1 + (int) k * 3, i) * a + hadamardSign (2 + (int) k * 5, i) * b;

            tanks[k].process (in.data(), y.data());

            float tl = 0.0f, tr = 0.0f;
            for (int i = 0; i < lines; ++i)
            {
                tl += hadamardSign (3 + (int) k * 3, i) * y[(size_t) i];
                tr += hadamardSign (5 + (int) k * 4, i) * y[(size_t) i];
            }

            // A note shorter than the Rise time: once it has died away, the rest of its wet swell
            // blooms up quickly instead of creeping on under a tail that is already decaying.
            const auto inc = wetDetector.armed && (int) k == currentTank ? bloomInc : riseInc;
            const auto gain = tankSwell[k].next (inc, wetDuck, 1.0f);
            tl *= gain;
            tr *= gain;
            tankEnergy[k] = flushTiny (tankEnergy[k] * energyK + (1.0f - energyK) * (tl * tl + tr * tr));
            sumL += tl;
            sumR += tr;
        }

        outL[n] = tankOutputGain * sumL;
        outR[n] = tankOutputGain * sumR;

        // Dry gain: the same S-curve, blended in by Param B. While frozen the engine can't hear
        // the input (the chain cuts it), so the dry is left alone rather than stuck closed.
        const auto dry = drySwell.next (riseInc, dryDuck, dryClose);
        const auto amount = blendRamp.next() * (1.0f - freezeRamp.next());
        dryGain[(size_t) n] = 1.0f - amount * (1.0f - dry);
    }
}

} // namespace cerulean::dsp
