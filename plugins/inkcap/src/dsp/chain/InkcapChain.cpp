#include "dsp/chain/InkcapChain.h"

#include <algorithm>
#include <cmath>

namespace inkcap::dsp
{

namespace
{
    float peakOf (const float* x, int n) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
            p = std::max (p, std::abs (x[i]));
        return p;
    }
}

void InkcapChain::prepare (double sampleRate, int maximumBlockSize, int /*numChannels*/)
{
    fs = sampleRate;
    chunkSize = std::clamp (maximumBlockSize, 64, maxShareBlock);

    // The modules always get room for stereo: a host that changes the layout without preparing
    // again still gets both sides processed. They never see more than a chunk at a time.
    const ModuleSpec spec { sampleRate, chunkSize, maxChannels };

    grid.prepare (sampleRate);
    glitch.prepare (spec);
    warble.prepare (spec);
    crush.prepare (spec);
    bits.prepare (spec);
    hiss.prepare (spec);
    room.prepare (spec);

    latencySamples = glitch.latencySamples() + warble.latencySamples() + crush.latencySamples() + bits.latencySamples()
                     + hiss.latencySamples() + room.latencySamples();

    for (auto& line : dryLine)
        line.assign ((size_t) std::max (latencySamples, 1), 0.0f);
    for (auto& chunk : dryChunk)
        chunk.assign ((size_t) chunkSize, 0.0f);
    for (auto& chunk : mixChunk)
        chunk.assign ((size_t) chunkSize, 0.0f);
    matchedDry.prepare (sampleRate, crush.latencySamples(), latencySamples);

    bypassAmount.setDuration (sampleRate, bypassSeconds);
    mixAmount.setTime (sampleRate, mixSeconds);
    outputGain.db.setTime (sampleRate, outputSeconds);
    matchGain.db.setTime (sampleRate, matchSeconds);
    reset();
}

void InkcapChain::reset() noexcept
{
    grid.reset();
    glitch.reset();
    warble.reset();
    crush.reset();
    bits.reset();
    hiss.reset();
    room.reset();

    for (auto& line : dryLine)
        std::fill (line.begin(), line.end(), 0.0f);
    matchedDry.reset();

    dryIndex = 0;
    primed = false;
}

int InkcapChain::loopWrapOffset (const HostPosition& host, double sampleRate, double fallbackBpm, int numSamples) noexcept
{
    if (! (host.hasPpq && host.playing && host.looping) || ! std::isfinite (host.ppq) || ! std::isfinite (host.loopEndPpq)
        || host.loopEndPpq <= host.loopStartPpq || host.ppq >= host.loopEndPpq)
        return 0;

    const auto bpm = std::clamp (host.bpm > 0.0 && std::isfinite (host.bpm) ? host.bpm : fallbackBpm, GridClock::minBpm, GridClock::maxBpm);
    const auto perSample = bpm / (60.0 * sampleRate);
    const auto wrap = std::ceil ((host.loopEndPpq - Grid::epsilon - host.ppq) / perSample);
    return wrap > 0.0 && wrap < (double) numSamples ? (int) wrap : 0;
}

HostPosition InkcapChain::afterLoopWrap (const HostPosition& host, double sampleRate, double fallbackBpm, int wrapOffset) noexcept
{
    const auto bpm = std::clamp (host.bpm > 0.0 && std::isfinite (host.bpm) ? host.bpm : fallbackBpm, GridClock::minBpm, GridClock::maxBpm);
    auto after = host;
    after.ppq = host.loopStartPpq + std::max (0.0, host.ppq + (double) wrapOffset * bpm / (60.0 * sampleRate) - host.loopEndPpq);
    if (host.hasSeconds)
        after.seconds = host.seconds + (double) wrapOffset / sampleRate - (host.loopEndPpq - host.loopStartPpq) * 60.0 / bpm;
    after.hasBarStart = false;
    return after;
}

void InkcapChain::process (float* const* channels, int numChannels, int numSamples, const ChainSettings& s,
                           const HostPosition& host) noexcept
{
    numChannels = std::min (numChannels, maxChannels);
    if (numSamples <= 0 || numChannels <= 0 || dryLine[0].empty())
        return;

    outputTargetDb = (double) s.outputDb;
    mixTarget = (double) std::clamp (s.mix, 0.0f, 1.0f);
    matchTargetDb = (double) match::compensationDb (s);
    const auto bypassTarget = s.bypass ? 1.0f : 0.0f;

    if (! primed)
    {
        matchGain.jumpTo (matchTargetDb);
        outputGain.jumpTo (outputTargetDb);
        mixAmount.jumpTo (mixTarget);
        bypassAmount.jumpTo (bypassTarget);
        primed = true;
    }
    else
    {
        bypassAmount.moveTo (bypassTarget);
    }

    for (int c = 0; c < numChannels; ++c)
        Telemetry::storeMax (meters.inputPeak[(size_t) c], peakOf (channels[c], numSamples));

    if (const auto wrap = loopWrapOffset (host, fs, s.fallbackBpm, numSamples); wrap > 0)
    {
        processSegment (channels, numChannels, 0, wrap, s, host);
        const auto after = afterLoopWrap (host, fs, s.fallbackBpm, wrap);
        processSegment (channels, numChannels, wrap, numSamples - wrap, s, after);
        publish (s, after, numSamples - wrap);
    }
    else
    {
        processSegment (channels, numChannels, 0, numSamples, s, host);
        publish (s, host, numSamples);
    }

    for (int c = 0; c < numChannels; ++c)
        Telemetry::storeMax (meters.outputPeak[(size_t) c], peakOf (channels[c], numSamples));
}

void InkcapChain::processSegment (float* const* channels, int numChannels, int offset, int numSamples, const ChainSettings& s,
                                  const HostPosition& host) noexcept
{
    grid.advance (host, s.fallbackBpm, numSamples);

    const BlockContext context { grid, s.damage, s.chaos, s.seed };
    std::array<float*, maxChannels> at {};
    for (int done = 0; done < numSamples; done += chunkSize)
    {
        for (int c = 0; c < numChannels; ++c)
            at[(size_t) c] = channels[c] + offset + done;
        processChunk (at.data(), numChannels, std::min (chunkSize, numSamples - done), grid.blockStart() + done, s, context);
    }
}

void InkcapChain::processChunk (float* const* at, int numChannels, int numSamples, int64_t firstSample, const ChainSettings& s,
                                const BlockContext& context) noexcept
{
    std::array<float*, maxChannels> mixDry {};
    for (int c = 0; c < numChannels; ++c)
    {
        std::copy (at[c], at[c] + numSamples, dryChunk[(size_t) c].begin());
        std::copy (at[c], at[c] + numSamples, mixChunk[(size_t) c].begin());
        mixDry[(size_t) c] = mixChunk[(size_t) c].data();
    }

    const AudioBlock block { at, numChannels, numSamples, firstSample };
    glitch.process (block, s.glitch, context);
    warble.process (block, s.warble, context);
    crush.process (block, s.crush, context);
    bits.process (block, s.bits, context);
    hiss.process (block, s.hiss, context);
    room.process (block, s.room, context);

    if (mixTarget < 1.0 || mixAmount.value < 1.0)
        matchedDry.process (mixDry.data(), numChannels, numSamples, crush.pairShares(), crush.colourDelays(), bits.converterShares(),
                           bits.converterDelays());
    else
        matchedDry.skip (mixDry.data(), numChannels, numSamples);

    const auto length = (int) dryLine[0].size();
    const auto delay = latencySamples > 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto match = matchGain.next (matchTargetDb);
        const auto mix = (float) mixAmount.next (mixTarget);
        const auto og = outputGain.next (outputTargetDb);
        const auto bp = bypassAmount.next();

        for (int c = 0; c < numChannels; ++c)
        {
            auto dry = dryChunk[(size_t) c][(size_t) i];
            if (delay)
            {
                auto& slot = dryLine[(size_t) c][(size_t) dryIndex];
                std::swap (dry, slot);
            }

            // Every glide and fade lands exactly on 0 or 1, so at rest this is exactly one of its
            // terms: the wet, the dry, or (Damage 0, where the wet is the dry) the input delayed.
            const auto wet = at[c][i] * match;
            const auto mixDryNow = mixDry[(size_t) c][i];
            const auto mixed = mix >= 1.0f ? wet : (mix <= 0.0f ? mixDryNow : mixDryNow + mix * (wet - mixDryNow));
            const auto y = mixed * og;
            at[c][i] = bp <= 0.0f ? y : (bp >= 1.0f ? dry : y + bp * (dry - y));
        }

        if (delay && ++dryIndex == length)
            dryIndex = 0;
    }
}

void InkcapChain::publish (const ChainSettings& s, const HostPosition& host, int numSamples) noexcept
{
    constexpr auto relaxed = std::memory_order_relaxed;

    const std::array<bool, numModules> on { s.glitch.on, s.warble.on, s.crush.on, s.bits.on, s.hiss.on, s.room.on };
    const std::array<float, numModules> intensity { glitch.intensity(), warble.intensity(), crush.intensity(),
                                                    bits.intensity(), hiss.intensity(), room.intensity() };
    const std::array<float, numModules> activity { glitch.activity(), warble.activity(), crush.activity(),
                                                   bits.activity(), hiss.activity(), room.activity() };
    float wear = 0.0f;
    int count = 0;
    for (size_t m = 0; m < (size_t) numModules; ++m)
    {
        meters.modules[m].on.store (on[m], relaxed);
        meters.modules[m].intensity.store (intensity[m], relaxed);
        meters.modules[m].activity.store (activity[m], relaxed);
        if (on[m])
        {
            wear += intensity[m];
            ++count;
        }
    }
    meters.wear.store (count > 0 ? wear / (float) count : 0.0f, relaxed);

    meters.ppq.store (grid.ppqAt (grid.blockStart() + numSamples), relaxed);
    meters.bpm.store (grid.getBpm(), relaxed);
    meters.followingHost.store (grid.isFollowingHost(), relaxed);
    meters.timeSigNumerator.store (grid.getTimeSigNumerator(), relaxed);
    meters.timeSigDenominator.store (grid.getTimeSigDenominator(), relaxed);
    if (host.hasBarStart && std::isfinite (host.barStartPpq))
        meters.barStartPpq.store (host.barStartPpq, relaxed);
    meters.hasBarStart.store (host.hasBarStart && grid.isFollowingHost(), relaxed);

    meters.glitchSeed.store (glitch.seedInUse(), relaxed);
    if (const auto* voice = glitch.playingVoice())
    {
        meters.glitchEventType.store ((int) voice->event().type, relaxed);
        meters.glitchEventSlot.store (voice->event().slot, relaxed);
        meters.glitchEventProgress.store (voice->progress(), relaxed);
    }
    else
    {
        meters.glitchEventType.store (-1, relaxed);
        meters.glitchEventProgress.store (0.0f, relaxed);
    }

    meters.blocks.fetch_add (1, relaxed);
}

} // namespace inkcap::dsp
