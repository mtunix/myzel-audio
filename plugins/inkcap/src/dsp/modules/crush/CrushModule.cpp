#include "dsp/modules/crush/CrushModule.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace inkcap::dsp
{

namespace
{
    constexpr double dbToNeper = std::numbers::ln10 / 20.0;

    int typeIndex (CrushType t) noexcept { return std::clamp ((int) t, 0, (int) crush::voicings.size() - 1); }

    /** The display's dot: the saturated band's distortion, -50 dB (nothing) .. -10 dB (all). */
    float wearActivity (const crush::Wear& w) noexcept
    {
        if (w.band <= 1.0e-20)
            return 0.0f;
        const auto db = 10.0 * std::log10 (std::max (w.change / w.band, 1.0e-12));
        return (float) std::clamp ((db + 50.0) / 40.0, 0.0, 1.0);
    }
}

int CrushModule::pairLatencySamples()
{
    return (int) std::lround (myzel::dsp::Oversampler2x().latencySamples());
}

void CrushModule::Channel::prepare (double oversampledRate, double sampleRate)
{
    tape.prepare (oversampledRate);
    cassette.prepare (oversampledRate);
    valve.prepare (oversampledRate);
    fuzz.prepare (oversampledRate);
    tilt.setup (crush::ToneVoicing::pivotHz, sampleRate);
    pair.reset();
    clean.fill (0.0f);
}

void CrushModule::Channel::resetStages() noexcept
{
    tape.reset();
    cassette.reset();
    valve.reset();
    fuzz.reset();
    tilt.reset();
}

void CrushModule::Channel::update (int t, double intensity) noexcept
{
    switch (t)
    {
        case 0:  tape.update (intensity); break;
        case 1:  cassette.update (intensity); break;
        case 2:  valve.update (intensity); break;
        default: fuzz.update (intensity); break;
    }
}

void CrushModule::Channel::flush (int t) noexcept
{
    switch (t)
    {
        case 0:  tape.flush(); break;
        case 1:  cassette.flush(); break;
        case 2:  valve.flush(); break;
        default: fuzz.flush(); break;
    }
    tilt.flush();
}

void CrushModule::prepare (const ModuleSpec& spec)
{
    fs = spec.sampleRate;
    channelCount = std::clamp (spec.numChannels, 1, (int) channels.size());
    latency = pairLatencySamples();
    assert (latency > 0 && latency <= (int) channels[0].clean.size());

    for (auto& c : channels)
        c.prepare (2.0 * fs, fs);
    shares.assign ((size_t) std::max (spec.maxBlockSize, maxShareBlock), -1.0f);
    delays.assign (shares.size(), 0.0f);
    typeDelayIntensity.fill (-1.0);

    const auto glide = 1.0 - std::exp (-1.0 / (0.5 * glideSeconds * fs));     // two poles: about glideSeconds in all
    intensityGlide.coefficient = driveGlide.coefficient = toneGlide.coefficient = chaosGlide.coefficient = glide;
    driftSeed.setDuration (fs, seedFadeSeconds);
    tiltCoefficient = crush::ToneVoicing::maxDb * dbToNeper;
    wet.prepare (fs);
    typeFade.setDuration (fs, typeFadeSeconds);
    reset();
}

void CrushModule::reset() noexcept
{
    for (auto& c : channels)
    {
        c.pair.reset();
        c.clean.fill (0.0f);
        c.resetStages();
    }
    cleanIndex = 0;
    wet.reset();
    typeFade.jumpTo (1.0f);
    previousType = -1;
    primed = false;
    stagesLive = false;
    asleep = false;
    quietSamples = 0;
    updateNow = true;
    seedKnown = false;
    driftSeed.jumpTo (1.0f);
    wander = 0.0;
    lastIntensity = lastActivity = 0.0f;
}

void CrushModule::followSeed (uint32_t newSeed) noexcept
{
    if (seedKnown && newSeed == seed)
        return;
    if (seedKnown)
    {
        oldDrift = drift;
        driftSeed.jumpTo (0.0f);
        driftSeed.moveTo (1.0f);
    }
    seed = newSeed;
    seedKnown = true;
    drift.setSource (SeedRng (seed, Stream::crushDrift));
}

double CrushModule::wanderDb (const GridClock& clock, int64_t sample, double chaos, double intensity, float seedMix) noexcept
{
    if (chaos <= 0.0 || intensity <= 0.0)
        return 0.0;
    const auto position = clock.secondsAt (sample) * driftLatticeHz;
    auto v = drift.at (position);
    if (seedMix < 1.0f)
    {
        const auto old = oldDrift.at (position);
        v = old + (double) seedMix * (v - old);
    }
    return driftDb * chaos * intensity * v;
}

double CrushModule::headDelay (int t, double intensity) noexcept
{
    if (t > 1)
        return 0.0;
    if (! myzel::dsp::isExactly (intensity, typeDelayIntensity[(size_t) t]))
    {
        typeDelayIntensity[(size_t) t] = intensity;
        const auto& stage = t == 0 ? channels[0].tape : channels[0].cassette;
        typeDelay[(size_t) t] = std::max (0.0, 0.5 * stage.getHead().groupDelay (headDelayHz));     // the head runs at 2x
    }
    return typeDelay[(size_t) t];
}

double CrushModule::drivenDb (int t, double intensity, double position, double wanderDb) noexcept
{
    // Chaos wanders the drive, but never past the type's top: there the aliasing gate is met.
    const auto kind = (CrushType) t;
    return std::min (intensity * crush::driveDb (kind, position) + wanderDb, crush::voicingOf (kind).maxDriveDb);
}

void CrushModule::startType (int t) noexcept
{
    if (t == type)
        return;

    if (! stagesLive)
    {
        type = t;
        return;
    }

    // Back to the type that is still fading out: turn the fade around from where it is.
    if (t == previousType && typeFade.isMoving())
    {
        const auto at = 1.0f - typeFade.current();
        previousType = type;
        type = t;
        typeFade.jumpTo (at);
        typeFade.moveTo (1.0f);
        return;
    }

    // A third type mid-fade: the quieter of the two drops out.
    if (typeFade.isMoving() && typeFade.current() < 0.5f)
        type = previousType;

    previousType = type;
    type = t;
    for (auto& c : channels)
    {
        switch (type)
        {
            case 0:  c.tape.reset(); break;
            case 1:  c.cassette.reset(); break;
            case 2:  c.valve.reset(); break;
            default: c.fuzz.reset(); break;
        }
    }
    typeFade.jumpTo (0.0f);
    typeFade.moveTo (1.0f);
    updateNow = true;
}

void CrushModule::process (const AudioBlock& block, const CrushSettings& s, const BlockContext& context) noexcept
{
    const auto target = (double) moduleIntensity (Module::crush, s.amount, context.damage);
    lastIntensity = s.on ? (float) target : 0.0f;
    wet.set (s.on && target > 0.0);

    const auto wanted = typeIndex (s.type);
    const auto drivePosition = (double) std::clamp (s.drive, 0.0f, 1.0f);
    const auto toneTarget = (double) std::clamp (s.tone, -1.0f, 1.0f);
    const auto chaosTarget = (double) chaosAmount (context.chaos);
    followSeed (context.seed);
    if (! primed)
    {
        type = wanted;
        intensityGlide.jumpTo (target);
        driveGlide.jumpTo (drivePosition);
        toneGlide.jumpTo (toneTarget);
        chaosGlide.jumpTo (chaosTarget);
        primed = true;
    }
    else
    {
        startType (wanted);
    }

    const auto nc = std::min (block.numChannels, channelCount);

    if (wet.isIdle())
    {
        // Off, or Damage 0 at rest: the clean path to the bit. The latency is the ring's, so the
        // pair rests too and starts afresh under the 20 ms fade (NOTES.md, Latency).
        for (int k = 0; k < block.numSamples; ++k)
        {
            wet.next();
            shares[(size_t) k] = -1.0f;
            for (int c = 0; c < nc; ++c)
                std::swap (block.channels[c][k], channels[(size_t) c].clean[(size_t) cleanIndex]);
            if (++cleanIndex == latency)
                cleanIndex = 0;
        }

        if (stagesLive)
        {
            for (auto& ch : channels)
                ch.resetStages();
            stagesLive = false;
            asleep = false;
            quietSamples = 0;
            previousType = -1;
            typeFade.jumpTo (1.0f);
        }
        intensityGlide.jumpTo (target);
        driveGlide.jumpTo (drivePosition);
        toneGlide.jumpTo (toneTarget);
        chaosGlide.jumpTo (chaosTarget);
        driftSeed.jumpTo (1.0f);
        lastActivity = 0.0f;
        return;
    }

    if (! stagesLive)
    {
        for (auto& ch : channels)
            ch.pair.reset();
        stagesLive = true;
        updateNow = true;
    }

    crush::Wear wear;
    crush::Wear ignored;
    for (int k = 0; k < block.numSamples; ++k)
    {
        const auto i = intensityGlide.next (target);
        const auto position = driveGlide.next (drivePosition);
        const auto tone = toneGlide.next (toneTarget);
        const auto chaosNow = chaosGlide.next (chaosTarget);
        const auto seedMix = driftSeed.next();

        const auto fading = typeFade.isMoving() && previousType >= 0;
        if (updateNow || ((block.firstSample + k) % updateInterval) == 0)
        {
            // The wander is slow: held between grid points it steps by a thousandth of a dB at most.
            wander = wanderDb (context.clock, block.firstSample + k, chaosNow, i, seedMix);
            for (auto& ch : channels)
            {
                ch.update (type, i);
                ch.flush (type);
                if (fading)
                {
                    ch.update (previousType, i);
                    ch.flush (previousType);
                }
            }
            currentDelay = headDelay (type, i);
            previousDelay = fading ? headDelay (previousType, i) : 0.0;
            updateNow = false;
        }

        const crush::StageFrame frame { driveGain.at (dbToNeper * drivenDb (type, i, position, wander)), i };
        const crush::StageFrame previousFrame { fading ? previousDriveGain.at (dbToNeper * drivenDb (previousType, i, position, wander)) : 1.0,
                                                i };
        const auto mix = (double) typeFade.next();
        const auto w = wet.next();
        shares[(size_t) k] = w;
        delays[(size_t) k] = (float) (fading ? previousDelay + mix * (currentDelay - previousDelay) : currentDelay);
        const auto high = tiltGain.at (tiltCoefficient * tone * i);
        const auto low = 1.0 / high;

        auto silent = true;
        for (int c = 0; c < nc; ++c)
            silent = silent && myzel::dsp::isExactly (block.channels[c][k], 0.0f);

        if (asleep && silent)
        {
            for (int c = 0; c < nc; ++c)
                std::swap (block.channels[c][k], channels[(size_t) c].clean[(size_t) cleanIndex]);
            if (++cleanIndex == latency)
                cleanIndex = 0;
            continue;
        }
        if (asleep)
        {
            // Sound again after a rest: the stages start from rest, as in a new render.
            for (auto& ch : channels)
            {
                ch.pair.reset();
                ch.resetStages();
            }
            previousType = -1;
            typeFade.jumpTo (1.0f);
            asleep = false;
        }

        auto quiet = silent;
        for (int c = 0; c < nc; ++c)
        {
            auto& ch = channels[(size_t) c];
            const auto x = block.channels[c][k];
            const auto clean = ch.clean[(size_t) cleanIndex];
            ch.clean[(size_t) cleanIndex] = x;

            double a, b;
            ch.pair.upsample ((double) x, a, b);
            auto& counted = c == 0 ? wear : ignored;
            auto ya = ch.run (type, a, frame, counted);
            auto yb = ch.run (type, b, frame, counted);
            if (fading)
            {
                const auto pa = ch.run (previousType, a, previousFrame, ignored);
                const auto pb = ch.run (previousType, b, previousFrame, ignored);
                ya = pa + mix * (ya - pa);
                yb = pb + mix * (yb - pb);
            }
            const auto y = ch.pair.downsample (ya, yb);
            const auto lp = ch.tilt.process (y);
            const auto shaped = (float) crush::flushed (low * lp + high * (y - lp));
            block.channels[c][k] = OnOffFade::blend (clean, shaped, w);
            quiet = quiet && std::abs (block.channels[c][k]) < sleepLevel;
        }
        if (++cleanIndex == latency)
            cleanIndex = 0;

        if (fading && ! typeFade.isMoving())
            previousType = -1;

        // A tape keeps its remanence and a valve settles towards its operating point for ever:
        // after a while of silence below -140 dB the module rests on the clean path, exact zeros.
        quietSamples = quiet ? quietSamples + 1 : 0;
        if (quietSamples >= sleepSamples)
        {
            asleep = true;
            quietSamples = 0;
        }
    }

    lastActivity = wearActivity (wear) * wet.current();
}

} // namespace inkcap::dsp
