#include "dsp/modules/warble/WarbleModule.h"

#include "dsp/modules/warble/WarbleShape.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace inkcap::dsp
{

namespace
{
    // The random part runs on a lattice this many times the LFO's rate: value noise moves
    // slower than its lattice, and this gives it about the sine's pitch swing at the same depth.
    constexpr double noiseRate = 4.0;

    // A synced phase that jumped (a new note value) glides back into place, faster than its own
    // rate by at most this share, so the swing never more than briefly deepens.
    constexpr double relockShare = 0.25;

    double wrapPhase (double p) noexcept { return p - std::floor (p); }
    double wrapSigned (double p) noexcept { return p - std::round (p); }
}

void WarbleModule::prepare (const ModuleSpec& spec)
{
    fs = spec.sampleRate;
    channels = std::clamp (spec.numChannels, 1, CaptureBuffer::maxChannels);
    capture.prepare (channels, restSamples + (int) std::ceil (2.0 * longestSwingSeconds * fs) + 4);
    depth.coefficient = 1.0 - std::exp (-1.0 / (depthSeconds * fs));
    share.coefficient = spread.coefficient = 1.0 - std::exp (-1.0 / (shapeSeconds * fs));
    onOff.prepare (fs);
    seedFade.setDuration (fs, seedFadeSeconds);
    reset();
}

void WarbleModule::reset() noexcept
{
    capture.restart();
    onOff.reset();
    resting = true;
    primed = false;
    phase = shift = noisePosition = 0.0;
    depth.jumpTo (0.0);
    centre = swingNow = 0.0;
    lastSync = false;
    lastNote = -1;
    lastDelay = (double) restSamples;
    seedKnown = false;
    seedFade.jumpTo (1.0f);
    lastIntensity = lastActivity = 0.0f;
}

void WarbleModule::anchorPhase (const GridClock& clock, int64_t sample, double noteBeats, bool sync) noexcept
{
    if (sync)
    {
        const auto cycles = clock.ppqAt (sample) / noteBeats;
        phase = wrapPhase (cycles);
        noisePosition = noiseRate * cycles;
    }
    else
    {
        const auto cycles = hz * clock.secondsAt (sample);
        phase = wrapPhase (cycles);
        noisePosition = noiseRate * cycles;
    }
    shift = 0.0;
}

void WarbleModule::process (const AudioBlock& block, const WarbleSettings& s, const BlockContext& context) noexcept
{
    const auto& clock = context.clock;
    const auto i = moduleIntensity (Module::warble, s.amount, context.damage);
    lastIntensity = s.on ? i : 0.0f;
    onOff.set (s.on);

    // A new type waits until the head is home: a slow, deep wobble cannot turn into a fast one
    // without the delay first travelling back, at the centre's speed limit.
    if (resting || ! primed)
        active = s.type;
    const auto& shape = warble::shapeOf (active);

    const auto note = s.sync ? warble::syncIndex (active, s.rate) : -1;
    const auto noteBeats = s.sync ? warble::noteValues[(size_t) note].beats : 1.0;
    hz = s.sync ? clock.getBpm() / 60.0 / noteBeats : warble::freeHz (active, s.rate);

    const auto target = i > 0.0f && active == s.type
                            ? std::min (shape.maxDeviation * warble::depthShare ((double) i) * fs / (2.0 * std::numbers::pi * hz),
                                        longestSwingSeconds * fs)
                            : 0.0;

    const auto stereo = std::min (block.numChannels, channels) > 1;
    if (onOff.isIdle() || (target <= 0.0 && centre <= 0.0))
    {
        // Off, or at home (Damage 0, a type change): the input restSamples back, exactly. The
        // buffer keeps its history, so the head glides out again without reading a gap.
        for (int k = 0; k < block.numSamples; ++k)
        {
            onOff.next();
            const std::array<float, 2> frame { block.channels[0][k], stereo ? block.channels[1][k] : 0.0f };
            capture.write (frame.data());
            block.channels[0][k] = capture.at (0, restSamples);
            if (stereo)
                block.channels[1][k] = capture.at (1, restSamples);
        }
        if (onOff.isIdle())
        {
            depth.jumpTo (0.0);
            centre = swingNow = 0.0;
        }
        resting = true;
        primed = true;
        lastActivity = 0.0f;
        return;
    }

    if (resting)
    {
        anchorPhase (clock, block.firstSample, noteBeats, s.sync);

        // A render's first block starts warbling at once (as that point does in a render from
        // earlier on); a module that comes back on starts from the input and the centre glides out.
        centre = primed ? 0.0 : target;
        depth.jumpTo (centre);
        swingNow = centre;
        lastDelay = (double) restSamples + centre;
        jumpShape = true;
        resting = false;
    }
    else if (clock.jumpedAt (block.firstSample))
    {
        anchorPhase (clock, block.firstSample, noteBeats, s.sync);
    }
    else if (s.sync && (! lastSync || note != lastNote))
    {
        // A new note value: the phase glides into place, the wander carries on where it is.
        const auto before = phase, along = noisePosition;
        anchorPhase (clock, block.firstSample, noteBeats, true);
        shift = wrapSigned (before - phase);
        phase = before;
        noisePosition = along;
    }
    lastSync = s.sync;
    lastNote = note;
    primed = true;

    if (! seedKnown || context.seed != seed)
    {
        if (seedKnown)
        {
            oldWander = wander;
            oldWanderRight = wanderRight;
            seedFade.jumpTo (0.0f);
            seedFade.moveTo (1.0f);
        }
        seed = context.seed;
        seedKnown = true;
        wander.setSource (SeedRng (seed, Stream::warbleDrift));
        wanderRight.setSource (SeedRng (seed, Stream::warbleSpread));
    }

    const auto shareTarget = warble::randomShare (active, context.chaos);
    const auto spreadTarget = (double) std::clamp (s.spread, 0.0f, 1.0f);
    if (jumpShape)
    {
        share.jumpTo (shareTarget);
        spread.jumpTo (spreadTarget);
        jumpShape = false;
    }
    constexpr auto rest = (double) restSamples;
    const auto step = hz / fs;
    const auto relockStep = relockShare * step;
    const auto slope = centreSlope;
    auto busiest = 0.0;

    for (int k = 0; k < block.numSamples; ++k)
    {
        if (s.sync)
        {
            if (shift > 0.0)
                shift = std::max (0.0, shift - relockStep);
            else if (shift < 0.0)
                shift = std::min (0.0, shift + relockStep);
            phase = wrapPhase (clock.ppqAt (block.firstSample + k) / noteBeats + shift);
        }
        else
        {
            phase = wrapPhase (phase + step);
        }
        noisePosition += noiseRate * step;

        const auto blend = (double) seedFade.next();
        auto noiseLeft = wander.at (noisePosition);
        auto noiseRight = stereo ? wanderRight.at (noisePosition) : 0.0;
        if (blend < 1.0)
        {
            const auto oldLeft = oldWander.at (noisePosition), oldRight = stereo ? oldWanderRight.at (noisePosition) : 0.0;
            noiseLeft = oldLeft + blend * (noiseLeft - oldLeft);
            noiseRight = oldRight + blend * (noiseRight - oldRight);
        }

        // Chaos, Type and Spread change the shape of m (t), Damage its depth: glide, never step.
        const auto randomShare = share.next (shareTarget);
        const auto spreadNow = spread.next (spreadTarget);
        centre += std::clamp (target - centre, -slope, slope);
        swingNow += std::clamp (depth.next (target) - swingNow, -slope, slope);     // as fast as the centre: a depth change bends, never lurches
        const auto swing = std::min (swingNow, centre);

        const auto left = (1.0 - randomShare) * warble::lfoSine (phase) + randomShare * noiseLeft;
        const auto delayLeft = rest + centre + swing * left;

        const std::array<float, 2> input { block.channels[0][k], stereo ? block.channels[1][k] : 0.0f };
        capture.write (input.data());

        const auto w = onOff.next();
        block.channels[0][k] = OnOffFade::blend (capture.at (0, restSamples), capture.read (0, delayLeft), w);

        if (stereo)
        {
            const auto rightPhase = phase + 0.25 * spreadNow;
            const auto right = (1.0 - randomShare) * warble::lfoSine (rightPhase)
                               + randomShare * (noiseLeft + spreadNow * (noiseRight - noiseLeft));
            block.channels[1][k] = OnOffFade::blend (capture.at (1, restSamples), capture.read (1, rest + centre + swing * right), w);
        }

        busiest = std::max (busiest, std::abs (delayLeft - lastDelay));
        lastDelay = delayLeft;
    }

    lastActivity = (float) std::min (1.0, busiest / shape.maxDeviation) * onOff.current();
}

} // namespace inkcap::dsp
