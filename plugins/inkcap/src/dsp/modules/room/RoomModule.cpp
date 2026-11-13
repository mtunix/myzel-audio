#include "dsp/modules/room/RoomModule.h"

#include "dsp/modules/room/RoomVoicing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace inkcap::dsp
{

namespace
{
    constexpr int closetType = (int) RoomType::closet, roomType = (int) RoomType::room;
    constexpr int springType = (int) RoomType::spring, hallType = (int) RoomType::hall;

    // The wet at full intensity per type (Match compensates the rest): pink noise comes out of
    // Closet and Room 4 dB, Spring and Hall 6 dB below the dry at unity (NOTES.md, Levels).
    constexpr std::array<float, 4> wetTrims { 1.2f, 0.84f, 1.25f, 0.94f };

    // The tanks' modulation: line i at hz + spread * i / 15, each with its own phase (seeded).
    constexpr double roomModulationHz = 0.31, roomModulationSpread = 0.34;
    constexpr double hallModulationHz = 0.22, hallModulationSpread = 0.25;

    // Below this the input counts as silence for stopping work (-300 dBFS: an upstream filter's
    // last residue); its room would be far below anything audible.
    constexpr float silenceThreshold = 1.0e-15f;

    int64_t floorMod (int64_t a, int64_t b) noexcept
    {
        const auto r = a % b;
        return r < 0 ? r + b : r;
    }
}

void RoomModule::prepare (const ModuleSpec& spec)
{
    fs = spec.sampleRate;
    channels = std::clamp (spec.numChannels, 1, 2);

    closet.prepare (fs);
    roomSpace.prepare (fs);
    hall.prepare (fs);
    spring.prepare (fs);
    for (auto& s : slots)
    {
        s.input.setDuration (fs, fillSeconds);
        s.output.setDuration (fs, ringOutSeconds);
    }

    onOff.prepare (fs);
    presence.setDuration (fs, presenceSeconds);
    amount.coefficient = 1.0 - std::exp (-1.0 / (glideSeconds * fs));
    duckGain.coefficient = 1.0 - std::exp (-1.0 / (duckSeconds * fs));
    sizeCoefficient = 1.0 - std::exp (-(double) subBlock / (sizeSeconds * fs));
    attack = (float) (1.0 - std::exp (-1.0 / (0.005 * fs)));
    release = (float) (1.0 - std::exp (-1.0 / (0.1 * fs)));
    reset();
}

void RoomModule::reset() noexcept
{
    onOff.reset();
    primed = false;
    timelineKnown = false;
    seedKnown = false;
    for (auto& s : slots)
    {
        s.running = false;
        s.dirty = true;     // reset when next started, not here: a hall's lines are megabytes
    }
    for (auto& m : modulation)
        m.primed = false;
    envelope = 0.0f;
    duckGain.jumpTo (1.0);
    duckTarget = 1.0;
    silentRun = 0;
    asleep = false;
    lastIntensity = lastActivity = 0.0f;
}

float RoomModule::wetTrim (int type) const noexcept { return wetTrims[(size_t) type]; }

void RoomModule::resetSlot (int type) noexcept
{
    switch (type)
    {
        case closetType: closet.reset(); break;
        case roomType:   roomSpace.reset(); modulation[0].primed = false; break;
        case springType: spring.reset(); break;
        default:         hall.reset(); modulation[1].primed = false; break;
    }
}

void RoomModule::startSlot (int type) noexcept
{
    auto& s = slots[(size_t) type];
    if (! s.running)
    {
        if (s.dirty)
            resetSlot (type);
        s.running = true;
        s.dirty = false;
        s.input.jumpTo (0.0f);
        s.output.jumpTo (1.0f);
    }
    s.input.moveTo (1.0f);
    s.output.moveTo (1.0f);
}

void RoomModule::followSeed (uint32_t newSeed) noexcept
{
    if (seedKnown && newSeed == seed)
        return;

    // The new seed's modulation takes over through the slew limit: no jump in a delay, no click.
    seed = newSeed;
    seedKnown = true;
    const SeedRng source (seed, Stream::roomModulation);
    for (size_t i = 0; i < wander.size(); ++i)
    {
        wander[i].setSource (source.lane ((uint32_t) i));
        phase[i] = source.lane ((uint32_t) (room::tankLines + (int) i)).uniform (0);
    }
}

void RoomModule::modulate (TankModulation& m, int64_t end, int numSamples, double hz, double spread, float chaos) noexcept
{
    // Chaos 0: a sine per line (steady and periodic); Chaos 1: value noise. Both from the song
    // position, so a render from bar 17 has bar 17's modulation. The slew limit (twice the
    // sine's own top speed) carries a seed change or a jump over without a step.
    const auto seconds = (double) end / fs;
    const auto c = (double) std::clamp (chaos, 0.0f, 1.0f);
    for (int i = 0; i < room::tankLines; ++i)
    {
        const auto rate = hz + spread * (double) i / (double) (room::tankLines - 1);
        const auto sine = std::sin (2.0 * std::numbers::pi * (rate * seconds + phase[(size_t) i]));
        const auto target = (float) ((1.0 - c) * sine + c * wander[(size_t) i].at (2.0 * rate * seconds));
        auto& v = m.value[(size_t) i];
        if (! m.primed)
        {
            v = target;
            continue;
        }
        const auto limit = (float) (4.0 * std::numbers::pi * rate * numSamples / fs);
        v += std::clamp (target - v, -limit, limit);
    }
    m.primed = true;
}

void RoomModule::updateSpaces (int64_t t, int numSamples, const RoomSettings& s, float chaos) noexcept
{
    const auto end = t + numSamples;

    // The spaces glide their lengths (one pole) behind this; the second pole keeps a step in Size
    // from stepping the lines' speed (a pitch step on every line of the tank).
    size.coefficient = numSamples == subBlock ? sizeCoefficient : 1.0 - std::exp (-(double) numSamples / (sizeSeconds * fs));
    const auto position = (float) size.next ((double) s.size);

    if (slots[(size_t) closetType].running)
        closet.update (numSamples, room::sizeAt (closetType, position), chaos, modulation[0].value);
    if (slots[(size_t) roomType].running)
    {
        modulate (modulation[0], end, numSamples, roomModulationHz, roomModulationSpread, chaos);
        roomSpace.update (numSamples, room::sizeAt (roomType, position), chaos, modulation[0].value);
    }
    if (slots[(size_t) springType].running)
        spring.update (numSamples, room::sizeAt (springType, position), chaos, modulation[0].value);
    if (slots[(size_t) hallType].running)
    {
        modulate (modulation[1], end, numSamples, hallModulationHz, hallModulationSpread, chaos);
        hall.update (numSamples, room::sizeAt (hallType, position), chaos, modulation[1].value);
    }

    // Duck: up to 24 dB less room while the input plays (0.6 dB per dB above -40 dBFS); the
    // envelope releases in 100 ms, so the room blooms in the gaps.
    duckTarget = 1.0;
    if (s.duck > 0.0f)
    {
        const auto db = 20.0 * std::log10 (std::max ((double) envelope, 1.0e-9));
        const auto reduction = std::clamp ((double) s.duck, 0.0, 1.0) * std::clamp ((db + 40.0) * 0.6, 0.0, 24.0);
        duckTarget = std::pow (10.0, -reduction / 20.0);
    }
}

void RoomModule::process (const AudioBlock& block, const RoomSettings& s, const BlockContext& context) noexcept
{
    const auto i = moduleIntensity (Module::room, s.amount, context.damage);
    lastIntensity = s.on ? i : 0.0f;
    onOff.set (s.on);
    followSeed (context.seed);
    const auto chaos = chaosAmount (context.chaos);
    const auto type = std::clamp ((int) s.type, 0, 3);

    auto realign = false;
    if (! timelineKnown || context.clock.jumpedAt (block.firstSample))
    {
        offset = std::llround (context.clock.secondsAt (block.firstSample) * fs) - block.firstSample;
        timelineKnown = true;
        realign = true;
    }

    const auto first = ! primed;
    if (first)
    {
        amount.jumpTo (i);
        size.jumpTo (s.size);
        presence.jumpTo (i > 0.0f ? 1.0f : 0.0f);
        primed = true;
    }
    presence.moveTo (i > 0.0f ? 1.0f : 0.0f);

    if (onOff.isIdle() || presence.isAt (0.0f))
    {
        // Nothing to do: the input passes untouched; the spaces start afresh when needed.
        for (int k = 0; k < block.numSamples; ++k)
        {
            onOff.next();
            presence.next();
        }
        amount.jumpTo (i);
        size.jumpTo (s.size);
        duckGain.jumpTo (1.0);
        for (auto& slot : slots)
        {
            slot.running = false;
            slot.dirty = true;
        }
        currentType = type;
        asleep = false;
        silentRun = 0;
        envelope = 0.0f;
        lastActivity = 0.0f;
        return;
    }

    if (type != currentType || ! slots[(size_t) type].running)
    {
        if (type != currentType && slots[(size_t) currentType].running)
        {
            slots[(size_t) currentType].input.moveTo (0.0f);
            slots[(size_t) currentType].output.moveTo (0.0f);
        }
        startSlot (type);
        if (first)
            slots[(size_t) type].input.jumpTo (1.0f);       // a render starts as if it had been playing
        currentType = type;
        asleep = false;
        realign = true;
    }

    const auto stereo = block.numChannels > 1 && channels > 1;
    float* left = block.channels[0];
    float* right = stereo ? block.channels[1] : nullptr;
    const auto sleepAfter = (int64_t) std::ceil (room::ringSeconds (type, room::sizeAt (type, s.size)) * fs);
    auto loudest = 0.0f;

    for (int k = 0; k < block.numSamples; ++k)
    {
        const auto t = block.firstSample + k + offset;
        const auto inLeft = left[k], inRight = stereo ? right[k] : inLeft;

        const auto peak = std::max (std::abs (inLeft), std::abs (inRight));
        envelope = myzel::dsp::flushTiny (envelope + (peak - envelope) * (peak > envelope ? attack : release));

        if (peak >= silenceThreshold)
        {
            silentRun = 0;
            if (asleep)
            {
                asleep = false;
                realign = true;
            }
        }
        else if (silentRun < sleepAfter)
        {
            ++silentRun;
        }

        if (! asleep && (realign || t >= nextBoundary))
        {
            const auto n = subBlock - (int) floorMod (t, subBlock);
            updateSpaces (t, n, s, chaos);
            nextBoundary = t + n;
            realign = false;
        }

        const auto w = (float) amount.next (i) * presence.next();
        const auto on = onOff.next();

        float wetLeft = 0.0f, wetRight = 0.0f;
        if (! asleep)
        {
            auto fading = false;
            for (int m = 0; m < 4; ++m)
            {
                auto& slot = slots[(size_t) m];
                if (! slot.running)
                    continue;

                const auto in = slot.input.next(), out = slot.output.next() * wetTrim (m);
                float l = 0.0f, r = 0.0f;
                switch (m)
                {
                    case closetType: closet.process (in * inLeft, in * inRight, l, r); break;
                    case roomType:   roomSpace.process (in * inLeft, in * inRight, l, r); break;
                    case springType: spring.process (in * inLeft, in * inRight, l, r); break;
                    default:         hall.process (in * inLeft, in * inRight, l, r); break;
                }
                wetLeft += out * l;
                wetRight += out * r;

                if (slot.output.isAt (0.0f))
                {
                    slot.running = false;
                    slot.dirty = true;
                }
                fading = fading || slot.input.isMoving() || slot.output.isMoving();
            }

            if (silentRun >= sleepAfter && ! fading)
                asleep = true;
        }

        const auto duck = (float) duckGain.next (duckTarget);
        const auto dry = 1.0f - dryStepBack * w;
        const auto wet = w * duck;
        if (stereo)
        {
            left[k] = OnOffFade::blend (inLeft, inLeft * dry + wetLeft * wet, on);
            right[k] = OnOffFade::blend (inRight, inRight * dry + wetRight * wet, on);
            loudest = std::max (loudest, std::max (std::abs (wetLeft), std::abs (wetRight)) * wet);
        }
        else
        {
            const auto wetMono = 0.70710678f * (wetLeft + wetRight);
            left[k] = OnOffFade::blend (inLeft, inLeft * dry + wetMono * wet, on);
            loudest = std::max (loudest, std::abs (wetMono) * wet);
        }
    }

    // The display's dot: how much room is sounding, -60 .. -6 dBFS.
    const auto db = 20.0 * std::log10 (std::max ((double) loudest, 1.0e-9));
    lastActivity = (float) std::clamp ((db + 60.0) / 54.0, 0.0, 1.0);
}

} // namespace inkcap::dsp
