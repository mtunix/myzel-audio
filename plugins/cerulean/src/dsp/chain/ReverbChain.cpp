#include "dsp/chain/ReverbChain.h"

#include "dsp/Mappings.h"
#include "dsp/chain/EngineFactory.h"

namespace cerulean::dsp
{

namespace
{
    constexpr float paramSmoothingMs = 50.0f;   // engine parameters (per sub-block)
    constexpr float gainSmoothingMs = 20.0f;    // mix / output / width / duck (per sample)
    constexpr float freezeRampMs = 50.0f;
    constexpr float bypassRampMs = 30.0f;
    constexpr float fadeOutMs = 30.0f;          // trails off, stealing, ringing over the limit
    constexpr float ringingSilence = 1.0e-5f;   // -100 dBFS: a ringing tail is over
    constexpr float dormantInput = 1.0e-7f;
    constexpr float dormantOutput = 1.0e-6f;

    // Freeze layering (see the class comment).
    constexpr float layerPadMs = 300.0f;        // pad level smoothing: slow, so a held pad reads steady
    constexpr float layerPlayMs = 100.0f;       // playing level smoothing
    constexpr float layerCaptureMs = 300.0f;    // the pad's level just after pressing Freeze
    constexpr float layerDeadbandDb = 1.5f;     // a held pad wobbles a little: no leak inside this
    constexpr float layerFeedCloseDb = 3.0f;    // the feed closes over this much above the deadband
    constexpr float layerLeakRangeDb = 6.0f;    // ... and the leak reaches its maximum over this
    constexpr float layerMaxLeak = 0.4f;        // engine freeze >= 0.6 (FDN lengths stay integer)
    constexpr float layerFeedMs = 20.0f;        // feed gain smoothing

    float smoothTowards (float current, float target, float k) noexcept
    {
        return current + (target - current) * k;
    }

    /**
        Wet-level compensation for Size: gain = sizeScale^k. A feedback network's steady-state
        energy goes as 1 / loop length at a fixed RT60, so without this Size would also be a
        volume knob (Hall -6 dB from 0.3x to 2x, Billow -19 dB). k is fitted per algorithm from
        steady pink noise at Decay 2 s (CeruleanMeasure / the "Size keeps the level" chain test);
        Room and Spring already compensate inside their engines.
    */
    float sizeLevelExponent (Algorithm a) noexcept
    {
        switch (a)
        {
            case Algorithm::room:   return 0.12f;
            case Algorithm::hall:   return 0.50f;
            case Algorithm::plate:  return 0.49f;
            case Algorithm::spring: return -0.12f;
            case Algorithm::nimbus: return 0.76f;
            case Algorithm::halo:   return 0.57f;
            case Algorithm::rise:   return 0.61f;
            case Algorithm::billow: return 1.16f;
            case Algorithm::choir:  return 0.59f;
            case Algorithm::echo:   return 0.44f;
        }
        return 0.5f;
    }

    float lowCutOffHz (double fs) { return std::min (5.0f, (float) fs * 0.4f); }
    float highCutOffHz (double fs) { return std::min (30000.0f, (float) fs * 0.48f); }
}

ReverbChain::ReverbChain()
{
    slots.resize ((size_t) numAlgorithms * 2);

    for (int a = 0; a < numAlgorithms; ++a)
        for (int k = 0; k < 2; ++k)
        {
            auto& slot = slots[(size_t) (a * 2 + k)];
            slot.algorithm = static_cast<Algorithm> (a);
            slot.engine = createEngine (slot.algorithm, (uint32_t) (a * 2 + k + 1));
        }
}

ReverbChain::~ReverbChain() = default;

void ReverbChain::prepare (double sampleRate, int)
{
    fs = sampleRate;

    for (auto& slot : slots)
        slot.engine->prepare (sampleRate, Engine::maxSubBlock);

    const auto maxPreDelay = (int) std::ceil (msToSamples (maxPreDelayMs, sampleRate)) + 8;
    preDelayL.setMaxDelay (maxPreDelay);
    preDelayR.setMaxDelay (maxPreDelay);
    preDelaySamples.prepare (sampleRate, 120.0f, 0.25f);

    for (auto* s : { &dryGain, &wetGain, &outGain, &widthAmount, &duckEnable })
        s->prepare (sampleRate, gainSmoothingMs);
    engineDryGainSmooth.prepare (sampleRate, 3.0f);
    engineDryGainSmooth.snap (1.0f);

    // Sub-block (control) rate smoothers.
    const auto controlRate = sampleRate / Engine::maxSubBlock;
    lowCutHz.prepare (controlRate, paramSmoothingMs);
    highCutHz.prepare (controlRate, paramSmoothingMs);
    toneSmooth.prepare (controlRate, paramSmoothingMs);

    prepared = true;
    reset();
}

void ReverbChain::reset()
{
    for (auto& slot : slots)
        setSlotIdle (slot);

    activeSlot = -1;

    lowCutL.reset(); lowCutR.reset(); highCutL.reset(); highCutR.reset();
    preDelayL.reset(); preDelayR.reset();
    duckDetector.reset();
    duckReductionDb = 0.0f;
    tiltSplitL.reset(); tiltSplitR.reset();

    freezeRamp = 0.0f;
    activeRamp = 1.0f;
    bypassResetDone = false;

    layering = false;
    padSquares = 0.0;
    padLevel = playLevel = layerCeiling = 0.0f;
    layerCaptureSamples = 0;
    layerLeak = layerFeed = layerFeedTarget = 0.0f;
}

void ReverbChain::setSlotIdle (Slot& slot) noexcept
{
    if (slot.state != SlotState::idle || slot.dormant)
        slot.engine->reset();
    slot.state = SlotState::idle;
    slot.gain = 1.0f;
    slot.gainStep = 0.0f;
    slot.quietSamples = 0;
    slot.dormant = false;
    slot.sizeGain.reset();
}

int ReverbChain::getBusyEngineCount() const noexcept
{
    int n = 0;
    for (const auto& slot : slots)
        if (slot.state != SlotState::idle && ! slot.dormant)
            ++n;
    return n;
}

//==============================================================================
void ReverbChain::retire (int index, bool trails)
{
    auto& slot = slots[(size_t) index];

    if (trails && ! slot.dormant)
    {
        slot.state = SlotState::ringing;
        slot.ringOrder = ringCounter++;
        slot.quietSamples = 0;
    }
    else if (slot.dormant)
    {
        setSlotIdle (slot);
    }
    else
    {
        slot.state = SlotState::fading;
        slot.gainStep = -1.0f / msToSamples (fadeOutMs, fs);
    }
}

void ReverbChain::enforceRingingLimit()
{
    for (;;)
    {
        int count = 0, oldest = -1;
        for (int i = 0; i < (int) slots.size(); ++i)
            if (slots[(size_t) i].state == SlotState::ringing)
            {
                ++count;
                if (oldest < 0 || slots[(size_t) i].ringOrder < slots[(size_t) oldest].ringOrder)
                    oldest = i;
            }

        if (count <= maxRinging)
            return;

        slots[(size_t) oldest].state = SlotState::fading;
        slots[(size_t) oldest].gainStep = -1.0f / msToSamples (fadeOutMs, fs);
    }
}

void ReverbChain::switchEngine (const ChainSettings& s)
{
    currentAlgorithm = s.algorithm;
    currentPresetGeneration = s.presetGeneration;

    const auto old = activeSlot;
    const auto first = static_cast<int> (s.algorithm) * 2;

    int chosen = -1;
    for (int c = first; c < first + 2; ++c)
        if (c != old && slots[(size_t) c].state == SlotState::idle)
        {
            chosen = c;
            break;
        }

    bool steal = false;
    if (chosen < 0)
    {
        chosen = (first != old) ? first : first + 1;
        steal = true;
    }

    if (old >= 0)
        retire (old, s.trails);

    auto& slot = slots[(size_t) chosen];
    activeSlot = chosen;

    if (steal)
    {
        // Both instances are busy: fade this one out quickly, then it takes over.
        slot.state = SlotState::stealing;
        slot.dormant = false;
        slot.gainStep = -1.0f / msToSamples (fadeOutMs, fs);
    }
    else
    {
        slot.state = SlotState::active;
        slot.gain = 1.0f;
        slot.gainStep = 0.0f;
        slot.quietSamples = 0;
        slot.dormant = false;
        smoothActiveParams (s, true);
    }

    enforceRingingLimit();
}

void ReverbChain::smoothActiveParams (const ChainSettings& s, bool snap)
{
    if (activeSlot < 0)
        return;

    auto& p = slots[(size_t) activeSlot].params;
    const auto& t = s.engine;
    const auto k = snap ? 1.0f : 1.0f - std::exp (-(float) Engine::maxSubBlock / msToSamples (paramSmoothingMs, fs));

    // Decay moves in the log domain: 0.3 s -> 8 s must not jump the loop gain in its first step.
    p.decaySeconds = std::exp (smoothTowards (std::log (std::max (p.decaySeconds, 0.01f)), std::log (std::max (t.decaySeconds, 0.01f)), k));
    p.sizeScale    = smoothTowards (p.sizeScale, t.sizeScale, k);
    p.mod          = smoothTowards (p.mod, t.mod, k);
    p.modRate      = smoothTowards (p.modRate, t.modRate, k);
    p.tone         = smoothTowards (p.tone, t.tone, k);
    p.lowEnd       = smoothTowards (p.lowEnd, t.lowEnd, k);
    p.diffusion    = smoothTowards (p.diffusion, t.diffusion, k);
    p.paramA       = smoothTowards (p.paramA, t.paramA, k);
    p.paramB       = smoothTowards (p.paramB, t.paramB, k);
    p.echoTimeMs   = smoothTowards (p.echoTimeMs, t.echoTimeMs, k);
    p.freeze       = freezeRamp * (1.0f - layerLeak);
}

//==============================================================================
void ReverbChain::process (float* left, float* right, int numSamples, const ChainSettings& s,
                           ChainMeters& meters) noexcept
{
    meters = {};
    if (! prepared || numSamples <= 0)
        return;

    if (activeSlot < 0)
    {
        // First block after reset(): start every smoother at its target.
        currentAlgorithm = s.algorithm;
        currentPresetGeneration = s.presetGeneration;
        activeSlot = static_cast<int> (s.algorithm) * 2;
        auto& slot = slots[(size_t) activeSlot];
        slot.state = SlotState::active;
        slot.gain = 1.0f;
        slot.dormant = false;

        freezeRamp = s.freeze ? 1.0f : 0.0f;
        activeRamp = s.bypass ? 0.0f : 1.0f;
        smoothActiveParams (s, true);

        const auto mix = std::clamp (s.mix, 0.0f, 1.0f);
        dryGain.snap (s.wetOnly ? 0.0f : (mix <= 0.5f ? 1.0f : 2.0f * (1.0f - mix)));
        wetGain.snap (s.wetOnly ? 1.0f : std::min (1.0f, 2.0f * mix));
        outGain.snap (dbToGain (s.outputDb));
        widthAmount.snap (s.width);
        duckEnable.snap (s.duck ? 1.0f : 0.0f);
        toneSmooth.snap (s.engine.tone);
        lowCutHz.snap (s.inLowCutHz <= 20.01f ? lowCutOffHz (fs) : s.inLowCutHz);
        highCutHz.snap (s.inHighCutHz >= 19999.5f ? highCutOffHz (fs) : s.inHighCutHz);
        preDelaySamples.snap (msToSamples (std::clamp (s.preDelayMs, 0.0f, maxPreDelayMs), fs));
    }
    else if (s.algorithm != currentAlgorithm || s.presetGeneration != currentPresetGeneration)
    {
        switchEngine (s);
    }

    lastTargets = s.engine;
    freezeStep = (s.freeze ? 1.0f : -1.0f) / msToSamples (freezeRampMs, fs);
    activeStep = (s.bypass ? -1.0f : 1.0f) / msToSamples (bypassRampMs, fs);

    if (! s.bypass)
        bypassResetDone = false;

    blockMeters = {};
    inputSquares = wetSquares = outputSquares = 0.0;

    for (int offset = 0; offset < numSamples; offset += Engine::maxSubBlock)
    {
        const auto n = std::min (Engine::maxSubBlock, numSamples - offset);
        processSubBlock (left + offset, right + offset, n, s);
    }

    const auto norm = 1.0 / (2.0 * numSamples);
    meters = blockMeters;
    meters.inputRms = (float) std::sqrt (inputSquares * norm);
    meters.wetRms = (float) std::sqrt (wetSquares * norm);
    meters.outputRms = (float) std::sqrt (outputSquares * norm);
    meters.freeze = freezeRamp;
    meters.busyEngines = getBusyEngineCount();
}

void ReverbChain::processSubBlock (float* left, float* right, int n, const ChainSettings& s) noexcept
{
    // Control rate -------------------------------------------------------------------
    const auto lowTarget = s.inLowCutHz <= 20.01f ? lowCutOffHz (fs) : s.inLowCutHz;
    const auto highTarget = s.inHighCutHz >= 19999.5f ? highCutOffHz (fs) : s.inHighCutHz;
    const auto lc = lowCutHz.next (lowTarget);
    const auto hc = highCutHz.next (highTarget);
    lowCutL.setCoefficients (lc, 0.7071f, fs);
    lowCutR.setCoefficients (lc, 0.7071f, fs);
    highCutL.setCoefficients (hc, 0.7071f, fs);
    highCutR.setCoefficients (hc, 0.7071f, fs);

    const auto tone = toneSmooth.next (s.engine.tone);
    const auto tiltDb = maps::wetTiltMaxDb * std::clamp (tone, -1.0f, 1.0f);
    const auto tiltHigh = dbToGain (tiltDb), tiltLow = 1.0f / tiltHigh;
    const auto tiltCoeff = OnePole::coefficientFor (maps::wetTiltPivotHz, fs);
    tiltSplitL.setCoefficient (tiltCoeff);
    tiltSplitR.setCoefficient (tiltCoeff);

    // Detector: fast, so the duck lets go as soon as the playing stops; the gain then blooms
    // back over the user's release time.
    duckDetector.setTimes (5.0f, 60.0f, fs);
    const auto duckAttack = 1.0f - std::exp (-1.0f / msToSamples (15.0f, fs));
    const auto duckRelease = 1.0f - std::exp (-1.0f / msToSamples (std::clamp (s.duckReleaseMs, 10.0f, 5000.0f), fs));
    preDelaySamples.setTarget (msToSamples (std::clamp (s.preDelayMs, 0.0f, maxPreDelayMs), fs));

    const auto mix = std::clamp (s.mix, 0.0f, 1.0f);
    const auto dryTarget = s.wetOnly ? 0.0f : (mix <= 0.5f ? 1.0f : 2.0f * (1.0f - mix));
    const auto wetTarget = s.wetOnly ? 1.0f : std::min (1.0f, 2.0f * mix);
    const auto outTarget = dbToGain (std::clamp (s.outputDb, -60.0f, 24.0f));
    const auto widthTarget = std::clamp (s.width, 0.0f, 1.5f);
    const auto duckTarget = s.duck ? 1.0f : 0.0f;
    const auto duckThreshold = s.duckThresholdDb;
    const auto duckAmount = std::max (s.duckAmountDb, 0.0f);

    // Input stage (per sample) --------------------------------------------------------
    std::array<float, Engine::maxSubBlock> activeAt {}, duckAt {};
    const auto feedStart = layerFeed;
    const auto feedStep = (layerFeedTarget - layerFeed) / (float) n;
    double feedSquares = 0.0;

    for (int k = 0; k < n; ++k)
    {
        const auto x = left[k], y = right[k];

        blockMeters.inputPeak = std::max (blockMeters.inputPeak, std::max (std::abs (x), std::abs (y)));
        inputSquares += (double) x * x + (double) y * y;

        // Duck: soft knee over 12 dB above the threshold.
        const auto env = duckDetector.process (std::max (std::abs (x), std::abs (y)));
        const auto envDb = 20.0f * std::log10 (env + 1.0e-9f);
        const auto over = std::clamp ((envDb - duckThreshold) / 12.0f, 0.0f, 1.0f);
        const auto reductionTarget = duckAmount * over;
        duckReductionDb += (reductionTarget - duckReductionDb) * (reductionTarget > duckReductionDb ? duckAttack : duckRelease);
        const auto duckGain = dbToGain (-duckReductionDb);
        duckAt[(size_t) k] = 1.0f + (duckGain - 1.0f) * duckEnable.next (duckTarget);

        const auto fl = highCutL.lowpass (lowCutL.highpass (x));
        const auto fr = highCutR.lowpass (lowCutR.highpass (y));

        detectorL[(size_t) k] = fl;   // what the player is doing now: before pre-delay and freeze
        detectorR[(size_t) k] = fr;
        preDelayL.write (fl);
        preDelayR.write (fr);
        const auto d = 1.0f + preDelaySamples.next();

        freezeRamp = std::clamp (freezeRamp + freezeStep, 0.0f, 1.0f);
        activeRamp = std::clamp (activeRamp + activeStep, 0.0f, 1.0f);
        activeAt[(size_t) k] = activeRamp;

        // Frozen: the input is cut, or (layering) fed in at the layer gain.
        const auto feed = feedStart + feedStep * (float) (k + 1);
        const auto inputGain = ((1.0f - freezeRamp) + freezeRamp * feed) * activeRamp;
        const auto pl = preDelayL.readLinear (d), pr = preDelayR.readLinear (d);
        feedSquares += (double) pl * pl + (double) pr * pr;
        engineInL[(size_t) k] = pl * inputGain;
        engineInR[(size_t) k] = pr * inputGain;
    }
    layerFeed = layerFeedTarget;

    // Engines ----------------------------------------------------------------------
    if (s.bypass && ! s.trails && activeRamp <= 0.0f && ! bypassResetDone)
    {
        for (auto& slot : slots)
        {
            if (&slot == &slots[(size_t) activeSlot])
            {
                slot.engine->reset();
                slot.dormant = true;
                slot.quietSamples = 0;
            }
            else
            {
                setSlotIdle (slot);
            }
        }
        bypassResetDone = true;
    }

    smoothActiveParams (s, false);
    runEngines (n, true);
    updateLayering (n, s, feedSquares);

    // Dry gain requested by the active engine (Rise), smoothed so engine changes never click.
    const float* engineDryGain = nullptr;
    if (activeSlot >= 0 && slots[(size_t) activeSlot].state == SlotState::active && ! slots[(size_t) activeSlot].dormant)
        engineDryGain = slots[(size_t) activeSlot].engine->getDryGain();

    // Wet stage and mix (per sample) ---------------------------------------------------
    for (int k = 0; k < n; ++k)
    {
        auto wl = wetL[(size_t) k], wr = wetR[(size_t) k];

        const auto lpL = tiltSplitL.lowpass (wl);
        const auto lpR = tiltSplitR.lowpass (wr);
        wl = tiltLow * lpL + tiltHigh * (wl - lpL);
        wr = tiltLow * lpR + tiltHigh * (wr - lpR);

        const auto width = widthAmount.next (widthTarget);
        const auto mid = 0.5f * (wl + wr);
        const auto side = 0.5f * (wl - wr) * width;
        wl = (mid + side) * duckAt[(size_t) k];
        wr = (mid - side) * duckAt[(size_t) k];

        blockMeters.wetPeak = std::max (blockMeters.wetPeak, std::max (std::abs (wl), std::abs (wr)));
        wetSquares += (double) wl * wl + (double) wr * wr;

        const auto a = activeAt[(size_t) k];
        const auto dg = dryGain.next (dryTarget);
        const auto wg = wetGain.next (wetTarget);
        const auto og = outGain.next (outTarget);

        const auto engineDry = engineDryGainSmooth.next (engineDryGain != nullptr ? engineDryGain[k] : 1.0f);
        const auto dryScale = a * dg * og * engineDry + (1.0f - a);
        const auto wetScale = wg * og * (s.trails ? 1.0f : a);

        const auto outL = left[k] * dryScale + wl * wetScale;
        const auto outR = right[k] * dryScale + wr * wetScale;
        left[k] = outL;
        right[k] = outR;

        blockMeters.outputPeak = std::max (blockMeters.outputPeak, std::max (std::abs (outL), std::abs (outR)));
        outputSquares += (double) outL * outL + (double) outR * outR;
    }

    blockMeters.duckGain = duckAt[(size_t) (n - 1)];
}

void ReverbChain::updateLayering (int n, const ChainSettings& s, double feedSquares) noexcept
{
    const auto smoothing = [this, n] (float ms) { return 1.0f - std::exp (-(float) n / msToSamples (ms, fs)); };
    const auto rmsOf = [n] (double squares) { return (float) std::sqrt (squares / (2.0 * n)); };

    padLevel += (rmsOf (padSquares) - padLevel) * smoothing (layerPadMs);
    playLevel += (rmsOf (feedSquares) - playLevel) * smoothing (layerPlayMs);
    padSquares = 0.0;

    const auto wasLayering = layering;
    layering = s.freezeLayer && s.freeze && ! s.bypass;

    if (! layering)
    {
        layerFeedTarget -= layerFeedTarget * smoothing (layerFeedMs);
        layerLeak -= layerLeak * smoothing (layerFeedMs);
        layerCeiling = 0.0f;
        return;
    }

    if (! wasLayering)
    {
        layerCeiling = padLevel;
        layerCaptureSamples = (int) msToSamples (layerCaptureMs, fs);
    }

    if (layerCaptureSamples > 0)
    {
        layerCeiling = std::max (layerCeiling, padLevel);
        layerCaptureSamples -= n;
    }

    layerCeiling = std::max (layerCeiling, playLevel * dbToGain (layerHeadroomDb));

    constexpr float tiny = 1.0e-6f;
    const auto overDb = 20.0f * std::log10 ((padLevel + tiny) / (layerCeiling + tiny)) - layerDeadbandDb;
    const auto feedOpen = std::clamp (1.0f - overDb / layerFeedCloseDb, 0.0f, 1.0f);
    // Leak only while new sound is coming in: it makes room for new layers, and with nothing
    // coming in, the pad must hold like a plain freeze.
    const auto playDb = 20.0f * std::log10 ((playLevel * dbToGain (layerHeadroomDb) + tiny) / (layerCeiling + tiny));
    const auto playing = std::clamp ((playDb + 12.0f) / 6.0f, 0.0f, 1.0f);
    const auto leakTarget = layerMaxLeak * std::clamp (overDb / layerLeakRangeDb, 0.0f, 1.0f) * playing;

    layerFeedTarget += (dbToGain (layerFeedDb) * feedOpen - layerFeedTarget) * smoothing (layerFeedMs);
    layerLeak += (leakTarget - layerLeak) * smoothing (layerFeedMs);
}

void ReverbChain::runEngines (int n, bool) noexcept
{
    std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
    std::fill (wetR.begin(), wetR.begin() + n, 0.0f);

    float inputPeak = 0.0f;
    for (int k = 0; k < n; ++k)
        inputPeak = std::max (inputPeak, std::max (std::abs (engineInL[(size_t) k]), std::abs (engineInR[(size_t) k])));

    const auto freezeRelease = (float) n / msToSamples (freezeRampMs, fs);

    for (int i = 0; i < (int) slots.size(); ++i)
    {
        auto& slot = slots[(size_t) i];
        if (slot.state == SlotState::idle)
            continue;

        const auto isActive = (i == activeSlot && slot.state == SlotState::active);

        if (isActive && slot.dormant)
        {
            if (inputPeak < dormantInput)
                continue;
            slot.dormant = false;   // the engine was reset when it went dormant
            slot.quietSamples = 0;
        }

        if (! isActive)
            slot.params.freeze = std::max (0.0f, slot.params.freeze - freezeRelease);   // old tails decay

        const auto* inL = isActive ? engineInL.data() : zeros.data();
        const auto* inR = isActive ? engineInR.data() : zeros.data();

        slot.engine->setDetectorInput (isActive ? detectorL.data() : zeros.data(), isActive ? detectorR.data() : zeros.data());
        slot.engine->process (inL, inR, engineOutL.data(), engineOutR.data(), n, slot.params);

        slot.sizeGain.set (std::pow (std::max (slot.params.sizeScale, 0.1f), sizeLevelExponent (slot.algorithm)), n);

        float peak = 0.0f;
        double squares = 0.0;
        for (int k = 0; k < n; ++k)
        {
            const auto l = engineOutL[(size_t) k], r = engineOutR[(size_t) k];
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            squares += (double) l * l + (double) r * r;
            const auto g = slot.gain * slot.sizeGain.next();
            wetL[(size_t) k] += l * g;
            wetR[(size_t) k] += r * g;
            slot.gain = std::clamp (slot.gain + slot.gainStep, 0.0f, 1.0f);
        }

        if (isActive)
            padSquares = squares;

        // Guard: a misbehaving engine must never take the host down.
        if (! std::isfinite (peak))
        {
            slot.engine->reset();
            std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
            std::fill (wetR.begin(), wetR.begin() + n, 0.0f);
            peak = 0.0f;
        }

        switch (slot.state)
        {
            case SlotState::fading:
                if (slot.gain <= 0.0f)
                    setSlotIdle (slot);
                break;

            case SlotState::stealing:
                if (slot.gain <= 0.0f)
                {
                    slot.engine->reset();
                    slot.state = SlotState::active;
                    slot.gain = 1.0f;
                    slot.gainStep = 0.0f;
                    slot.quietSamples = 0;
                    slot.params = lastTargets;
                    slot.params.freeze = freezeRamp;
                }
                break;

            case SlotState::ringing:
                slot.quietSamples = peak < ringingSilence ? slot.quietSamples + n : 0;
                if (slot.quietSamples > (int) (0.5 * fs))
                    setSlotIdle (slot);
                break;

            case SlotState::active:
                slot.quietSamples = (inputPeak < dormantInput && peak < dormantOutput) ? slot.quietSamples + n : 0;
                if (slot.quietSamples > (int) fs)
                {
                    slot.engine->reset();
                    slot.dormant = true;
                    slot.quietSamples = 0;
                }
                break;

            case SlotState::idle:
                break;
        }
    }
}

} // namespace cerulean::dsp
