#include "dsp/chain/EchoChain.h"

#include "dsp/chain/MachineFactory.h"

namespace mulberry::dsp
{

namespace
{
    constexpr float gainSmoothingMs = 20.0f;
    constexpr float paramSmoothingMs = 20.0f;   // feedback, diffusion, cuts (per sub-block)
    constexpr float bypassRampMs = 30.0f;
    constexpr float fadeOutMs = 30.0f;
    constexpr float ringingSilence = 1.0e-5f;   // -100 dBFS
    constexpr float dormantInput = 1.0e-7f;
    constexpr float dormantOutput = 1.0e-6f;
    constexpr float maxDiffusion = 0.7f;

    float lowCutOffHz (double fs) { return std::min (5.0f, (float) fs * 0.4f); }
    float highCutOffHz (double fs) { return std::min (22000.0f, (float) fs * 0.45f); }

    float smoothingCoefficient (int n, float ms, double fs) noexcept
    {
        return 1.0f - std::exp (-(float) n / std::max (1.0f, msToSamples (ms, fs)));
    }
}

EchoChain::EchoChain()
{
    for (int i = 0; i < numSlots; ++i)
        for (int m = 0; m < numMachines; ++m)
            slots[(size_t) i].machines[(size_t) m] = createMachine (static_cast<mulberry::Machine> (m), (uint32_t) (i * numMachines + m + 1));
}

EchoChain::~EchoChain() = default;

void EchoChain::prepare (double sampleRate, int)
{
    fs = sampleRate;

    for (auto& slot : slots)
    {
        slot.memory.allocate (sampleRate);
        for (auto& m : slot.machines)
            m->prepare (sampleRate, slot.memory);
        slot.loop.prepare (sampleRate);
    }

    holdLooper.prepare (sampleRate, 12.5);
    for (auto* s : { &driveGain, &makeUpGain, &dryGain, &wetGain, &outGain, &duckEnable })
        s->prepare (sampleRate, gainSmoothingMs);

    prepared = true;
    reset();
}

void EchoChain::reset()
{
    for (auto& slot : slots)
        setIdle (slot);

    activeSlot = -1;
    holdLooper.reset();
    duckDetector.reset();
    duckReductionDb = 0.0f;
    bypassRamp = 1.0f;
    bypassResetDone = false;
}

void EchoChain::setIdle (Slot& slot) noexcept
{
    if (slot.machine != nullptr && (slot.state != SlotState::idle || slot.dormant))
        slot.machine->reset();
    slot.state = SlotState::idle;
    slot.gain = 1.0f;
    slot.gainStep = 0.0f;
    slot.quietSamples = 0;
    slot.dormant = false;
}

int EchoChain::getBusySlotCount() const noexcept
{
    int n = 0;
    for (const auto& slot : slots)
        if (slot.state != SlotState::idle && ! slot.dormant)
            ++n;
    return n;
}

const EchoMachine* EchoChain::activeMachine() const noexcept
{
    return activeSlot >= 0 ? slots[(size_t) activeSlot].machine : nullptr;
}

TapPlanInput EchoChain::planInputFor (const ChainSettings& s, bool monoInput) const noexcept
{
    auto in = s.plan;
    in.headMask = (s.machine == mulberry::Machine::loop && in.mode != Mode::pattern) ? character::headMask (s.paramA) : 1;
    in.monoInput = monoInput;
    return in;
}

MachineSettings EchoChain::machineSettingsFor (const ChainSettings& s, const TapPlan& plan, const LoopContext& loop) const noexcept
{
    MachineSettings m;
    m.timeMs = { s.timeMs[0], plan.twoTransports ? s.timeMs[1] : s.timeMs[0] };
    m.paramA = s.paramA;
    m.paramB = s.paramB;
    m.wobble = s.wobble;
    m.wobbleRate = s.wobbleRate;
    m.wobbleShape = s.wobbleShape;
    m.noise = s.noise;
    m.twoTransports = plan.twoTransports;
    m.feedbackOffsetSamples = -(double) loop.diffuserDelaySamples();
    return m;
}

void EchoChain::startSlot (Slot& slot, const ChainSettings& s) noexcept
{
    slot.kind = s.machine;
    slot.machine = slot.machines[(size_t) s.machine].get();
    slot.machine->reset();
    slot.loop.reset();
    slot.state = SlotState::active;
    slot.settings = s;
    slot.gain = 1.0f;
    slot.gainStep = 0.0f;
    slot.quietSamples = 0;
    slot.dormant = false;
    slot.fresh = true;
}

void EchoChain::retire (Slot& slot, bool trails) noexcept
{
    if (trails && ! slot.dormant)
    {
        slot.state = SlotState::ringing;
        slot.ringOrder = ringCounter++;
        slot.quietSamples = 0;
    }
    else if (slot.dormant)
    {
        setIdle (slot);
    }
    else
    {
        slot.state = SlotState::fading;
        slot.gainStep = -1.0f / msToSamples (fadeOutMs, fs);
    }
}

void EchoChain::switchSlot (const ChainSettings& s) noexcept
{
    currentMachine = s.machine;
    currentGeneration = s.presetGeneration;

    const auto old = activeSlot;
    const auto other = old == 0 ? 1 : 0;
    auto& next = slots[(size_t) other];

    if (old >= 0)
        retire (slots[(size_t) old], s.trails);

    activeSlot = other;

    if (next.state == SlotState::idle)
    {
        startSlot (next, s);
    }
    else
    {
        // Both slots are busy: this one fades out quickly, then it takes over.
        next.state = SlotState::stealing;
        next.gainStep = -1.0f / msToSamples (fadeOutMs, fs);
        next.settings = s;
    }
}

void EchoChain::buildReads (Slot& slot, int n) noexcept
{
    const auto fadeSamples = std::max (1.0f, msToSamples (planFadeMs, fs));
    const auto a0 = slot.planFade;
    const auto a1 = std::min (1.0f, a0 + (float) n / fadeSamples);
    const auto fading = a0 < 1.0f;

    for (size_t l = 0; l < 2; ++l)
    {
        auto& list = slot.reads[l];
        list.count = 0;
        const auto& now = slot.plan.lines[l];
        const auto& before = slot.previousPlan.lines[l];

        auto add = [&list] (const Read& r, float start, float step, bool steady)
        {
            if (list.count >= ReadList::maxReads)
                return;
            auto& e = list.entries[(size_t) list.count++];
            e.read = r;
            e.fadeStart = start;
            e.fadeStep = step;
            e.feedback = r.isFeedback();
            e.ramp = false;
            e.steady = steady;
        };

        if (! fading)
        {
            for (int i = 0; i < now.numReads; ++i)
                add (now.reads[(size_t) i], 1.0f, 0.0f, true);
        }
        else if (slot.weightRamp)
        {
            const auto e0 = a0 * a0 * (3.0f - 2.0f * a0), e1 = a1 * a1 * (3.0f - 2.0f * a1);
            for (int i = 0; i < now.numReads; ++i)
            {
                auto r = now.reads[(size_t) i];
                const auto& b = before.reads[(size_t) i];
                std::array<float, numBuses> step {};
                for (size_t w = 0; w < (size_t) numBuses; ++w)
                {
                    const auto change = r.weights[w] - b.weights[w];
                    r.weights[w] = b.weights[w] + e0 * change;
                    step[w] = (e1 - e0) * change / (float) n;
                }
                add (r, 1.0f, 0.0f, true);
                auto& e = list.entries[(size_t) list.count - 1];
                e.ramp = true;
                e.weightStep = step;
                e.feedback = r.isFeedback() || b.isFeedback();
            }
        }
        else
        {
            const auto step = (a1 - a0) / (float) n;
            for (int i = 0; i < before.numReads; ++i)
                add (before.reads[(size_t) i], 1.0f - a0, -step, false);
            for (int i = 0; i < now.numReads; ++i)
                add (now.reads[(size_t) i], a0, step, false);
        }
    }

    slot.planFade = a1;
}

double EchoChain::holdLengthSamples (const Slot& slot, int channel) const noexcept
{
    const auto line = slot.plan.holdLine[(size_t) channel];
    return (double) slot.plan.holdFraction[(size_t) channel] * (double) slot.machine->currentTimeMs (line) * 0.001 * fs;
}

//==============================================================================
void EchoChain::process (float* left, float* right, int numSamples, const ChainSettings& s,
                         ChainMeters& meters, bool monoInput) noexcept
{
    meters = {};
    if (! prepared || numSamples <= 0)
        return;

    if (activeSlot < 0)
    {
        // First block after reset(): start every smoother at its target.
        currentMachine = s.machine;
        currentGeneration = s.presetGeneration;
        activeSlot = 0;
        startSlot (slots[0], s);

        bypassRamp = s.bypass ? 0.0f : 1.0f;
        const auto drive = dbToGain (std::clamp (s.driveDb, -24.0f, 36.0f));
        driveGain.snap (drive);
        makeUpGain.snap (1.0f / drive);
        dryGain.snap (s.wetOnly ? 0.0f : maps::dryGain (s.mix));
        wetGain.snap (s.wetOnly ? 1.0f : maps::wetGain (s.mix));
        outGain.snap (dbToGain (s.outputDb));
        duckEnable.snap (s.duck ? 1.0f : 0.0f);
    }
    else if (s.machine != currentMachine || s.presetGeneration != currentGeneration)
    {
        switchSlot (s);
    }

    bypassStep = (s.bypass ? -1.0f : 1.0f) / msToSamples (bypassRampMs, fs);
    if (! s.bypass)
        bypassResetDone = false;

    blockMeters = {};
    inputSquares = wetSquares = outputSquares = 0.0;

    for (int offset = 0; offset < numSamples; offset += LoopContext::maxSubBlock)
    {
        const auto n = std::min (LoopContext::maxSubBlock, numSamples - offset);
        processSubBlock (left + offset, right + offset, n, s, monoInput);
    }

    const auto norm = 1.0 / (2.0 * numSamples);
    meters = blockMeters;
    meters.inputRms = (float) std::sqrt (inputSquares * norm);
    meters.wetRms = (float) std::sqrt (wetSquares * norm);
    meters.outputRms = (float) std::sqrt (outputSquares * norm);
    meters.hold = holdLooper.holdAmount();
    meters.busySlots = getBusySlotCount();
    if (const auto* m = activeMachine())
        meters.timeMs = { m->currentTimeMs (0), m->currentTimeMs (1) };
}

void EchoChain::runSlot (Slot& slot, bool isActive, int n) noexcept
{
    const auto& s = slot.settings;
    const auto active = isActive;

    // Feedback, diffusion and the loop cuts follow their targets at control rate.
    const auto k = slot.fresh ? 1.0f : smoothingCoefficient (n, paramSmoothingMs, fs);
    const auto ringingCap = (! active || (s.bypass && s.trails)) ? maps::ringingFeedbackCap : maps::maxFeedback;
    const auto fbTarget = std::clamp (s.feedback, 0.0f, ringingCap);
    const auto fbFrom = slot.feedbackNow;
    slot.feedbackNow += (fbTarget - slot.feedbackNow) * k;

    slot.diffusionNow += (maxDiffusion * std::clamp (s.diffuse, 0.0f, 1.0f) - slot.diffusionNow) * k;

    const auto lowOff = s.lowCutHz <= 20.01f;
    const auto highOff = s.highCutHz >= 19999.5f;
    const auto lowTarget = lowOff ? lowCutOffHz (fs) : s.lowCutHz;
    const auto highTarget = highOff ? highCutOffHz (fs) : s.highCutHz;
    slot.lowHzNow = std::exp (std::log (std::max (slot.lowHzNow, 1.0f)) + (std::log (lowTarget) - std::log (std::max (slot.lowHzNow, 1.0f))) * k);
    slot.highHzNow = std::exp (std::log (std::max (slot.highHzNow, 1.0f)) + (std::log (highTarget) - std::log (std::max (slot.highHzNow, 1.0f))) * k);
    // Off bypasses a cut exactly (the loop context ramps the blend), so Off never shifts the echo time.
    const auto lowMix = lowOff ? 0.0f : 1.0f;
    const auto highMix = highOff ? 0.0f : 1.0f;

    if (slot.fresh)
    {
        slot.loop.snapCuts (lowMix, highMix);
        slot.feedbackNow = fbTarget;
    }

    buildReads (slot, n);

    std::fill (slot.outL.begin(), slot.outL.begin() + n, 0.0f);
    std::fill (slot.outR.begin(), slot.outR.begin() + n, 0.0f);

    const auto* in0 = active ? routed0.data() : zeros.data();
    const auto* in1 = active ? routed1.data() : zeros.data();
    slot.loop.begin (in0, in1, slot.outL.data(), slot.outR.data(),
                     slot.fresh ? slot.feedbackNow : fbFrom, slot.feedbackNow, slot.diffusionNow,
                     slot.lowHzNow, lowMix, slot.highHzNow, highMix, n);

    slot.machine->process (slot.loop, slot.reads, machineSettingsFor (s, slot.plan, slot.loop), n);
    slot.fresh = false;
}

void EchoChain::processSubBlock (float* left, float* right, int n, const ChainSettings& s, bool monoInput) noexcept
{
    auto& active = slots[(size_t) activeSlot];

    // The live slot follows the settings; its tap plan crossfades when the routing changes.
    if (active.state == SlotState::active)
    {
        active.settings = s;
        const auto input = planInputFor (s, monoInput);

        if (active.fresh)
        {
            active.planInput = input;
            active.plan = active.previousPlan = compileTapPlan (input);
            active.planFade = 1.0f;
            active.weightRamp = false;
        }
        else if (! (input == active.planInput) && active.planFade >= 1.0f)
        {
            active.previousPlan = active.plan;
            active.plan = compileTapPlan (input);
            active.planInput = input;
            active.weightRamp = active.plan.samePositions (active.previousPlan);
            active.planFade = 0.0f;
        }
    }

    // Input stage (per sample) --------------------------------------------------------
    const auto driveTarget = dbToGain (std::clamp (s.driveDb, -24.0f, 36.0f));
    duckDetector.setTimes (5.0f, 60.0f, fs);
    const auto duckAttack = 1.0f - std::exp (-1.0f / msToSamples (15.0f, fs));
    const auto duckRelease = 1.0f - std::exp (-1.0f / msToSamples (std::clamp (s.duckReleaseMs, 10.0f, 5000.0f), fs));
    const auto duckTarget = s.duck ? 1.0f : 0.0f;
    const auto duckAmount = std::max (s.duckAmountDb, 0.0f);

    std::array<float, LoopContext::maxSubBlock> bypassAt {}, duckAt {};
    float inputPeak = 0.0f;

    // Hold cuts the input to the loop (the held loop plays on its own), eased over 20 ms.
    const auto holdInput = s.hold ? 0.0f : 1.0f;
    const auto inputFrom = active.inputGain;
    const auto inputStep = std::clamp (holdInput - inputFrom, -(float) n / msToSamples (20.0f, fs), (float) n / msToSamples (20.0f, fs)) / (float) n;
    const auto easedInput = [inputFrom, inputStep] (int k)
    {
        const auto f = inputFrom + inputStep * (float) (k + 1);
        return f * f * (3.0f - 2.0f * f);
    };

    for (int k = 0; k < n; ++k)
    {
        const auto x = left[k], y = right[k];
        blockMeters.inputPeak = std::max (blockMeters.inputPeak, std::max (std::abs (x), std::abs (y)));
        inputSquares += (double) x * x + (double) y * y;

        const auto env = duckDetector.process (std::max (std::abs (x), std::abs (y)));
        const auto envDb = 20.0f * std::log10 (env + 1.0e-9f);
        const auto over = std::clamp ((envDb - s.duckThresholdDb) / 12.0f, 0.0f, 1.0f);
        const auto reductionTarget = duckAmount * over;
        duckReductionDb += (reductionTarget - duckReductionDb) * (reductionTarget > duckReductionDb ? duckAttack : duckRelease);
        duckAt[(size_t) k] = 1.0f + (dbToGain (-duckReductionDb) - 1.0f) * duckEnable.next (duckTarget);

        bypassRamp = std::clamp (bypassRamp + bypassStep, 0.0f, 1.0f);
        bypassAt[(size_t) k] = bypassRamp;

        const auto g = driveGain.next (driveTarget) * bypassRamp * easedInput (k);
        inL[(size_t) k] = x * g;
        inR[(size_t) k] = y * g;
        inputPeak = std::max (inputPeak, std::max (std::abs (inL[(size_t) k]), std::abs (inR[(size_t) k])));
    }
    active.inputGain = inputFrom + inputStep * (float) n;

    // Routing into the lines, blended per sample while the plan crossfades.
    {
        const auto& now = active.plan.lines;
        const auto& before = active.previousPlan.lines;
        const auto a0 = active.planFade;
        const auto a1 = std::min (1.0f, a0 + (float) n / std::max (1.0f, msToSamples (planFadeMs, fs)));
        for (int k = 0; k < n; ++k)
        {
            const auto f = a0 + (a1 - a0) * (float) (k + 1) / (float) n;
            const auto a = f * f * (3.0f - 2.0f * f);
            const auto blend = [a] (float from, float to) { return from + a * (to - from); };
            routed0[(size_t) k] = blend (before[0].inputFromLeft, now[0].inputFromLeft) * inL[(size_t) k]
                                + blend (before[0].inputFromRight, now[0].inputFromRight) * inR[(size_t) k];
            routed1[(size_t) k] = blend (before[1].inputFromLeft, now[1].inputFromLeft) * inL[(size_t) k]
                                + blend (before[1].inputFromRight, now[1].inputFromRight) * inR[(size_t) k];
        }
    }

    // Bypass without trails: once silent, clear everything.
    if (s.bypass && ! s.trails && bypassRamp <= 0.0f && ! bypassResetDone)
    {
        for (auto& slot : slots)
        {
            if (&slot == &active)
            {
                slot.machine->reset();
                slot.loop.reset();
                slot.dormant = true;
                slot.quietSamples = 0;
            }
            else
            {
                setIdle (slot);
            }
        }
        holdLooper.reset();
        bypassResetDone = true;
    }

    // Slots ----------------------------------------------------------------------------
    std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
    std::fill (wetR.begin(), wetR.begin() + n, 0.0f);

    for (int i = 0; i < numSlots; ++i)
    {
        auto& slot = slots[(size_t) i];
        if (slot.state == SlotState::idle)
            continue;

        const auto isActive = i == activeSlot && slot.state == SlotState::active;

        if (isActive && slot.dormant)
        {
            if (inputPeak < dormantInput)
                continue;
            slot.dormant = false;   // the machine was reset when it went dormant
            slot.quietSamples = 0;
        }

        runSlot (slot, isActive, n);

        float peak = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            const auto l = slot.outL[(size_t) k], r = slot.outR[(size_t) k];
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            wetL[(size_t) k] += l * slot.gain;
            wetR[(size_t) k] += r * slot.gain;
            slot.gain = std::clamp (slot.gain + slot.gainStep, 0.0f, 1.0f);
        }

        // A misbehaving machine must never take the host down.
        if (! std::isfinite (peak))
        {
            slot.machine->reset();
            slot.loop.reset();
            std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
            std::fill (wetR.begin(), wetR.begin() + n, 0.0f);
            peak = 0.0f;
        }

        // Quiet long enough to cover the longest read: the echoes are over.
        const auto longest = (long long) (fs * (0.0032 * (double) std::max (slot.settings.timeMs[0], slot.settings.timeMs[1]) + 0.5));

        switch (slot.state)
        {
            case SlotState::fading:
                if (slot.gain <= 0.0f)
                    setIdle (slot);
                break;

            case SlotState::stealing:
                if (slot.gain <= 0.0f)
                {
                    auto settings = slot.settings;
                    startSlot (slot, settings);
                }
                break;

            case SlotState::ringing:
                slot.quietSamples = peak < ringingSilence ? slot.quietSamples + n : 0;
                if (slot.quietSamples > longest)
                    setIdle (slot);
                break;

            case SlotState::active:
                slot.quietSamples = (inputPeak < dormantInput && peak < dormantOutput) ? slot.quietSamples + n : 0;
                if (slot.quietSamples > longest + (long long) fs && ! s.hold)
                {
                    slot.machine->reset();
                    slot.loop.reset();
                    slot.dormant = true;
                    slot.quietSamples = 0;
                }
                break;

            case SlotState::idle:
                break;
        }
    }

    // Wet stage and mix (per sample) ---------------------------------------------------
    const auto mix = std::clamp (s.mix, 0.0f, 1.0f);
    const auto dryTarget = s.wetOnly ? 0.0f : maps::dryGain (mix);
    const auto wetTarget = s.wetOnly ? 1.0f : maps::wetGain (mix);
    const auto outTarget = dbToGain (std::clamp (s.outputDb, -60.0f, 24.0f));
    const auto makeUpTarget = 1.0f / driveTarget;
    const std::array<double, 2> holdLengths { holdLengthSamples (active, 0), holdLengthSamples (active, 1) };
    const auto holdDecay = std::clamp (s.feedback, 0.0f, 0.99f);

    for (int k = 0; k < n; ++k)
    {
        auto wl = wetL[(size_t) k], wr = wetR[(size_t) k];
        holdLooper.process (wl, wr, s.hold && ! s.bypass, holdLengths, holdDecay);

        const auto m = makeUpGain.next (makeUpTarget) * duckAt[(size_t) k];
        wl *= m;
        wr *= m;

        blockMeters.wetPeak = std::max (blockMeters.wetPeak, std::max (std::abs (wl), std::abs (wr)));
        wetSquares += (double) wl * wl + (double) wr * wr;

        const auto a = bypassAt[(size_t) k];
        const auto og = outGain.next (outTarget);
        const auto dryScale = a * dryGain.next (dryTarget) * og + (1.0f - a);
        const auto wetScale = wetGain.next (wetTarget) * og * (s.trails ? 1.0f : a);

        const auto outL = left[k] * dryScale + wl * wetScale;
        const auto outR = right[k] * dryScale + wr * wetScale;
        left[k] = outL;
        right[k] = outR;

        blockMeters.outputPeak = std::max (blockMeters.outputPeak, std::max (std::abs (outL), std::abs (outR)));
        outputSquares += (double) outL * outL + (double) outR * outR;
    }

    blockMeters.duckGain = duckAt[(size_t) (n - 1)];
}

} // namespace mulberry::dsp
