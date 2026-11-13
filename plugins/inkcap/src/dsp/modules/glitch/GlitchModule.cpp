#include "dsp/modules/glitch/GlitchModule.h"

#include "common/dsp/FloatCompare.h"

#include <algorithm>
#include <cmath>

namespace inkcap::dsp
{

namespace
{
    // How far back of its start Skip's repeats read beyond their piece: the 2 ms jump fades.
    constexpr int readMargin = 8;
}

void GlitchModule::prepare (const ModuleSpec& spec)
{
    fs = spec.sampleRate;
    channels = std::clamp (spec.numChannels, 1, CaptureBuffer::maxChannels);
    capture.prepare (channels, (int) std::ceil (captureSeconds * fs));
    for (auto& v : voices)
        v.prepare (fs);
    cutLength = std::max (1, (int) std::lround (glitch::cutSeconds * fs));
    onOff.prepare (fs);
    reset();
}

void GlitchModule::reset() noexcept
{
    capture.restart();
    for (auto& v : voices)
        v.stop();
    current = previous = -1;
    onOff.reset();
    idle = true;
    nextSlot = validFrom = 0;
    seed = 0;
    lastIntensity = lastActivity = 0.0f;
}

void GlitchModule::process (const AudioBlock& block, const GlitchSettings& s, const BlockContext& context) noexcept
{
    const auto& clock = context.clock;
    onOff.set (s.on);
    lastIntensity = s.on ? moduleIntensity (Module::glitch, s.amount, context.damage) : 0.0f;

    if (onOff.isIdle() && current < 0 && previous < 0)
    {
        idle = true;
        lastActivity = 0.0f;
        return;
    }

    const auto params = glitch::patternFor (s, context.damage, context.chaos);
    const auto gridMoved = ! myzel::dsp::isExactly (params.grid.slotBeats, grid.slotBeats)
                           || ! myzel::dsp::isExactly (params.grid.swing, grid.swing);
    grid = params.grid;

    if (idle || clock.jumpedAt (block.firstSample))
    {
        if (idle)
        {
            capture.restart();
            seed = context.seed;
        }
        else
        {
            cutToInput();
        }

        validFrom = block.firstSample;
        nextSlot = grid.firstSlotFrom (clock.ppqAt (block.firstSample));
        idle = false;
    }
    else if (gridMoved || clock.sampleAt (grid.slotStart (nextSlot)) < block.firstSample - 1)
    {
        // A new Rate or Groove (or a slot the clock passed): carry on with the new grid's next slot.
        nextSlot = grid.firstSlotFrom (clock.ppqAt (block.firstSample));
    }

    for (int i = 0; i < block.numSamples;)
    {
        const auto boundary = clock.sampleAt (grid.slotStart (nextSlot)) - block.firstSample;
        if (boundary >= block.numSamples)
        {
            renderFrames (block, i, block.numSamples);
            break;
        }

        const auto at = (int) std::max<int64_t> (boundary, i);
        renderFrames (block, i, at);
        fire (nextSlot++, block.firstSample + at, params, context.seed, s.on);
        i = at;
    }

    if (onOff.isIdle())
    {
        for (auto& v : voices)
            v.stop();
        current = previous = -1;
    }

    lastActivity = current >= 0 ? onOff.current() * voices[(size_t) current].envelope() : 0.0f;
}

void GlitchModule::fire (int64_t slot, int64_t sample, const glitch::PatternParams& params, uint32_t instanceSeed, bool on) noexcept
{
    seed = instanceSeed;
    if (! on)
        return;

    const auto event = glitch::eventAt (seed, params, slot);
    if (! event)
        return;

    // Skip repeats the audio before its slot: after a jump (or when the module came on) that is
    // not the song's, so the event is not played. Chew, Stall and Bend start on the input and
    // only fall behind it, never reading before their own start, so they play on the slot of a
    // jump too: the downbeat after play, a locate or a loop (NOTES.md, After a jump).
    const auto reach = event->type == GlitchType::skip ? (int64_t) std::lround (event->lengthSeconds * fs) + readMargin : 0;
    const auto played = sample - reach >= validFrom;

    if (eventLog != nullptr)
        eventLog->fired (*event, sample, played);

    if (! played)
        return;

    if (previous >= 0)
        voices[(size_t) previous].stop();

    const auto next = current == 0 ? 1 : 0;
    if (current >= 0)
    {
        previous = current;
        cutPosition = 0;
    }
    else
    {
        previous = -1;
    }

    voices[(size_t) next].start (*event);
    current = next;
}

void GlitchModule::cutToInput() noexcept
{
    if (current < 0)
        return;

    if (previous >= 0)
        voices[(size_t) previous].stop();
    previous = current;
    current = -1;
    cutPosition = 0;
}

void GlitchModule::renderFrames (const AudioBlock& block, int from, int to) noexcept
{
    const auto nc = std::min (block.numChannels, channels);
    std::array<float, CaptureBuffer::maxChannels> input {}, now {}, before {};

    for (int i = from; i < to; ++i)
    {
        for (int c = 0; c < nc; ++c)
            input[(size_t) c] = block.channels[c][i];
        capture.write (input.data());

        auto out = input;
        if (current >= 0)
        {
            voices[(size_t) current].render (capture, input.data(), now.data(), nc);
            out = now;
            if (! voices[(size_t) current].isPlaying())
                current = -1;
        }

        if (previous >= 0)
        {
            voices[(size_t) previous].render (capture, input.data(), before.data(), nc);
            const auto g = equalPower ((double) cutPosition / (double) cutLength);
            for (int c = 0; c < nc; ++c)
                out[(size_t) c] = g.out * before[(size_t) c] + g.in * out[(size_t) c];
            if (++cutPosition >= cutLength)
            {
                voices[(size_t) previous].stop();
                previous = -1;
            }
        }

        const auto w = onOff.next();
        for (int c = 0; c < nc; ++c)
            block.channels[c][i] = OnOffFade::blend (input[(size_t) c], out[(size_t) c], w);
    }
}

} // namespace inkcap::dsp
