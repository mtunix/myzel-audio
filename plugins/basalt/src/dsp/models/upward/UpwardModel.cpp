#include "dsp/models/upward/UpwardModel.h"

#include "dsp/dynamics/Ballistics.h"
#include "dsp/dynamics/KeyPath.h"

#include <algorithm>
#include <cmath>

namespace basalt::models
{

using namespace dynamics;

namespace
{
    /** 10^(dB/10): a level as the power 2m it reads from. */
    double powerFor (double db) noexcept { return std::pow (10.0, 0.1 * db); }

    /**
        One sample of the lift's ballistics (NOTES.md, Ballistics): above the target it decays on
        the attack's one-pole, below it rises at most `step` dB and lands on the target exactly.
    */
    double moveLift (double lift, double target, double attackPole, double step) noexcept
    {
        lift = target < lift ? attackPole * lift + (1.0 - attackPole) * target : std::min (target, lift + step);
        return snapState (lift, target);
    }

    /** (1 − λ)·ℓ_c + λ·ℓ_loud: exactly ℓ_c at λ = 0 and exactly ℓ_loud at λ = 1. */
    double linkLift (double own, double loudest, double link) noexcept
    {
        return (1.0 - link) * own + link * loudest;
    }
}

double UpwardModel::lift (const Curve& c, double floorDb, double x) noexcept
{
    const auto u = c.thresholdDb - x;
    const auto halfKnee = 0.5 * c.kneeDb;

    if (u <= -halfKnee || x <= floorDb)
        return 0.0;

    auto below = u;
    if (c.kneeDb > 0.0 && u <= halfKnee)
    {
        const auto v = u + halfKnee;
        below = v * v / (2.0 * c.kneeDb);
    }

    const auto capped = std::min ((1.0 - c.inverseRatio) * below, c.rangeDb);
    const auto s = std::min ((x - floorDb) / floorFadeDb, 1.0);
    return s * s * (3.0 - 2.0 * s) * capped;
}

UpwardModel::Bounds UpwardModel::boundsFor (const Curve& c, double floorDb) noexcept
{
    return { powerFor (c.thresholdDb + 0.5 * c.kneeDb), powerFor (floorDb), powerFor (c.thresholdDb) };
}

double UpwardModel::liftAt (const Curve& c, const Bounds& b, double floorDb, double twiceMeanSquare) noexcept
{
    // Most of the time the level is over the knee or under Floor, where the lift is exactly 0.
    // Written so that a NaN level lifts nothing either (the chain's key is sane, Settled 59).
    if (! (twiceMeanSquare < b.top && twiceMeanSquare > b.bottom))
        return 0.0;

    return lift (c, floorDb, meanSquareDb (0.5 * twiceMeanSquare));
}

void UpwardModel::prepare (double sampleRate)
{
    rate = sampleRate;
    const auto capacity = windowCapacity (sampleRate);

    for (auto& c : channels)
    {
        c.window.prepare (capacity);
        c.rms.setTimeConstant (rmsMs, sampleRate);
    }

    memoryPole = poleFor (autoMemoryMs, sampleRate);
    fade.prepare (sampleRate);
    attackGlide.prepare (timeGlideMs, sampleRate);
    releaseGlide.prepare (timeGlideMs, sampleRate);
    autoGlide.prepare (timeGlideMs, sampleRate);
    floorGlide.prepare (timeGlideMs, sampleRate);
    reset();
}

void UpwardModel::reset() noexcept
{
    for (auto& c : channels)
    {
        c.rms.reset();
        c.window.reset();
        c.lift = c.incoming = c.applied = 0.0;
        c.memory = 0.0;
    }

    linkedMemory = 0.0;
    sharedSteering = false;
    primed = false;
    inFade = false;
    warmedUp = false;
}

void UpwardModel::seed (const double* reductionDb, int numChannels) noexcept
{
    // A reduction seeds a lift below 0, which comes back at the Release rate: the gain rises as
    // Upward's gain always rises, instead of jumping inside the switch's crossfade.
    for (int c = 0; c < std::min (numChannels, maxDetectorChannels); ++c)
    {
        auto& ch = channels[(size_t) c];
        ch.lift = ch.incoming = 0.0 - reductionDb[c];
        ch.applied = reductionDb[c];
    }
}

double UpwardModel::staticReduction (const ModelParams& p, double levelDb) const noexcept
{
    return 0.0 - lift (p.curve, p.floorDb, levelDb);
}

double UpwardModel::reduction (int channel) const noexcept
{
    return channels[(size_t) channel].applied;
}

double UpwardModel::settleMs (const ModelParams&) const noexcept
{
    return 5.0 * rmsMs;
}

double UpwardModel::autoMemory (int channel) const noexcept
{
    return sharedSteering ? linkedMemory : channels[(size_t) channel].memory;
}

void UpwardModel::setTargets (const ModelParams& p) noexcept
{
    const auto link = p.linked ? p.link : 0.0;

    if (! primed)
    {
        fade.jumpTo (p.curve, link);
        attackGlide.jumpTo (std::log (p.attackMs));
        releaseGlide.jumpTo (std::log (p.releaseMs));
        autoGlide.jumpTo (p.autoRelease ? 1.0 : 0.0);
        floorGlide.jumpTo (p.floorDb);
        setTimes (p.attackMs, p.releaseMs);
        boundsStale = true;
        primed = true;
        return;
    }

    fade.setTarget (p.curve, link);
    attackGlide.setTarget (std::log (p.attackMs));
    releaseGlide.setTarget (std::log (p.releaseMs));
    autoGlide.setTarget (p.autoRelease ? 1.0 : 0.0);
    floorGlide.setTarget (p.floorDb);
}

void UpwardModel::setTimes (double attackMs, double releaseMs) noexcept
{
    attackPole = poleFor (attackMs, rate);
    liftStep = liftPerReleaseDb / (releaseMs * 0.001 * rate);
}

template <bool heard>
void UpwardModel::run (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    setTargets (p);

    if (! fade.settled() || attackGlide.moving() || releaseGlide.moving() || autoGlide.moving() || floorGlide.moving())
    {
        runSamples<heard, false> (p, key, reductionDb);
        return;
    }

    if (boundsStale)
    {
        targetBounds = boundsFor (fade.target(), floorGlide.current());
        boundsStale = false;
    }

    if constexpr (heard)
    {
        if (inFade)
        {
            for (auto& c : channels)
                c.lift = c.incoming;
            inFade = false;
        }
    }

    runSamples<heard, true> (p, key, reductionDb);
}

template <bool heard, bool steady>
void UpwardModel::runSamples (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    const auto numChannels = std::min (key.numChannels, maxDetectorChannels);
    const auto linked = numChannels == 2 && p.linked;
    const auto memoryGain = 1.0 - memoryPole;

    for (int i = 0; i < key.numSamples; ++i)
    {
        auto weight = 1.0;
        auto fading = false;

        if constexpr (! steady)
        {
            weight = fade.next();
            fading = fade.fading();

            if (attackGlide.moving() || releaseGlide.moving())
                setTimes (std::exp (attackGlide.next()), std::exp (releaseGlide.next()));
            if (autoGlide.moving())
                autoGlide.next();

            // Floor glides, and a fade's curves are new: the bounds follow per sample until both rest.
            if (floorGlide.moving() || fading || boundsStale)
            {
                floorGlide.next();
                targetBounds = boundsFor (fade.target(), floorGlide.current());
                if (fading)
                    sourceBounds = boundsFor (fade.source(), floorGlide.current());
                boundsStale = floorGlide.moving() || fading;
            }
        }

        const auto& curve = fade.target();
        const auto floorDb = floorGlide.current();

        // Whatever steers the ballistics comes once from a linked pair, so Link 100 % gives both
        // channels the same gain to the bit; at Link 0 % each steers itself, as Unlinked.
        sharedSteering = linked && fade.targetLink() > 0.0;

        const auto window = key.lookaheadAt (i) + 1;
        std::array<double, maxDetectorChannels> level {};

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            const auto k = key.channels[(size_t) c][i];
            ch.window.setLength (window);

            if constexpr (heard)
                level[(size_t) c] = 2.0 * ch.window.push (ch.rms.process (k * k));
            else
                level[(size_t) c] = 2.0 * ch.window.push (ch.rms.processWarming (k * k));

            ch.memory = flushLevel (memoryPole * ch.memory + (level[(size_t) c] > targetBounds.threshold ? memoryGain : 0.0));
        }

        const auto loudest = numChannels == 2 ? std::max (level[0], level[1]) : level[0];
        if (numChannels == 2)
            linkedMemory = flushLevel (memoryPole * linkedMemory + (loudest > targetBounds.threshold ? memoryGain : 0.0));

        if constexpr (heard)
        {
            std::array<double, maxDetectorChannels> target {}, before {};

            for (int c = 0; c < numChannels; ++c)
                target[(size_t) c] = liftAt (curve, targetBounds, floorDb, level[(size_t) c]);

            // The louder channel wins: its lift, not the smaller of the two, which inside the
            // Floor fade would belong to the quieter channel (NOTES.md, Detector).
            if (linked)
            {
                const auto loud = liftAt (curve, targetBounds, floorDb, loudest);
                for (int c = 0; c < numChannels; ++c)
                    target[(size_t) c] = linkLift (target[(size_t) c], loud, fade.targetLink());
            }

            if (fading)
            {
                const auto& previous = fade.source();
                for (int c = 0; c < numChannels; ++c)
                    before[(size_t) c] = liftAt (previous, sourceBounds, floorDb, level[(size_t) c]);

                if (linked)
                {
                    const auto loud = liftAt (previous, sourceBounds, floorDb, loudest);
                    for (int c = 0; c < numChannels; ++c)
                        before[(size_t) c] = linkLift (before[(size_t) c], loud, fade.sourceLink());
                }
            }

            // A curve change runs the new curve's ballistics beside the old ones, from their
            // state, and blends the two lifts on the fade's raised cosine.
            if (! steady && fading != inFade)
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    auto& ch = channels[(size_t) c];
                    if (fading)
                        ch.incoming = ch.lift;
                    else
                        ch.lift = ch.incoming;
                }
                inFade = fading;
            }

            const auto autoAmount = autoGlide.current();

            for (int c = 0; c < numChannels; ++c)
            {
                auto& ch = channels[(size_t) c];
                const auto memory = sharedSteering ? linkedMemory : ch.memory;
                const auto step = autoAmount > 0.0 ? liftStep / (1.0 + autoSlowdown * memory * autoAmount) : liftStep;

                double applied;
                if (fading)
                {
                    const auto from = ch.lift = moveLift (ch.lift, before[(size_t) c], attackPole, step);
                    const auto to = ch.incoming = moveLift (ch.incoming, target[(size_t) c], attackPole, step);
                    applied = from + weight * (to - from);
                }
                else
                {
                    applied = ch.lift = moveLift (ch.lift, target[(size_t) c], attackPole, step);
                }

                ch.applied = 0.0 - applied;
                reductionDb[c][i] = ch.applied;
            }
        }
    }
}

void UpwardModel::process (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    if (warmedUp)
    {
        for (auto& c : channels)
            c.rms.endWarmUp();
        warmedUp = false;
    }

    run<true> (p, key, reductionDb);
}

void UpwardModel::follow (const ModelParams& p, const KeyBlock& key) noexcept
{
    run<false> (p, key, nullptr);
    warmedUp = true;

    // A fade heard next starts its ballistics from the seed.
    inFade = false;
}

} // namespace basalt::models
