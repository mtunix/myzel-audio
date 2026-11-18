#include "dsp/models/clean/CleanModel.h"

#include "dsp/dynamics/KeyPath.h"
#include "dsp/dynamics/Stereo.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace basalt::models
{

using namespace dynamics;
using myzel::dsp::isExactly;

double CleanModel::blendFor (double attackMs) noexcept
{
    return 0.5 * std::clamp (std::log10 (attackMs), 0.0, 1.0);
}

double CleanModel::kappaFor (double b) noexcept
{
    return 1.0 + 0.118 * b;
}

double CleanModel::attackConstantMs (double attackMs) noexcept
{
    return attackMs / kappaFor (blendFor (attackMs));
}

double CleanModel::rmsConstantMs (double attackMs) noexcept
{
    return 0.25 * attackMs;
}

double CleanModel::releaseDragMs (double attackMs, double inverseRatio) noexcept
{
    constexpr auto depthDb = 10.0;
    constexpr auto dbPerNeper = 10.0 / std::numbers::ln10;
    const auto b = blendFor (attackMs);
    const auto c = std::max (1.0 - inverseRatio, 0.5);
    const auto overDb = depthDb / c + carrierRmsOffsetDb * (1.0 - b);
    return b * c * overDb * overDb * rmsConstantMs (attackMs) / (2.0 * dbPerNeper * depthDb);
}

double CleanModel::minimumReleaseMs (double attackMs, double inverseRatio) noexcept
{
    return minimumReleaseRatio * attackConstantMs (attackMs) + releaseDragMs (attackMs, inverseRatio);
}

double CleanModel::autoReleaseFactor (double crestSquared) noexcept
{
    return std::clamp (std::sqrt (2.0 / crestSquared), 0.125, std::numbers::sqrt2);
}

void CleanModel::prepare (double sampleRate)
{
    rate = sampleRate;
    const auto capacity = windowCapacity (sampleRate);

    for (auto& c : channels)
    {
        c.peakWindow.prepare (capacity);
        c.rmsWindow.prepare (capacity);
        c.crest.setTimeConstant (CrestFactor::defaultMs, sampleRate);
    }

    linkedCrest.setTimeConstant (CrestFactor::defaultMs, sampleRate);
    fade.prepare (sampleRate);
    attackGlide.prepare (timeGlideMs, sampleRate);
    releaseGlide.prepare (timeGlideMs, sampleRate);
    autoGlide.prepare (timeGlideMs, sampleRate);
    reset();
}

void CleanModel::reset() noexcept
{
    for (auto& c : channels)
    {
        c.rms.reset();
        c.crest.reset();
        c.peakWindow.reset();
        c.rmsWindow.reset();
        c.ballistics.reset();
        c.incoming.reset();
        c.applied = 0.0;
        c.releasePole = fixedReleasePole;
        c.steeringCrest = CrestFactor::resetCrestSquared;
        c.lastPeak = -1.0;
    }

    linkedCrest.reset();
    autoCountdown = 0;
    primed = false;
    inFade = false;
    warmedUp = false;
}

void CleanModel::seed (const double* reductionDb, int numChannels) noexcept
{
    for (int c = 0; c < std::min (numChannels, maxDetectorChannels); ++c)
    {
        auto& ch = channels[(size_t) c];
        ch.ballistics.reset (reductionDb[c]);
        ch.incoming.reset (reductionDb[c]);
        ch.applied = reductionDb[c];
    }
}

double CleanModel::staticReduction (const ModelParams& p, double levelDb) const noexcept
{
    // On a sine both halves of the blend read its peak, so the blend does not enter.
    return dynamics::staticReduction (p.curve, levelDb);
}

double CleanModel::reduction (int channel) const noexcept
{
    return channels[(size_t) channel].applied;
}

double CleanModel::settleMs (const ModelParams& p) const noexcept
{
    const auto rms = blendFor (p.attackMs) > 0.0 ? 5.0 * rmsConstantMs (p.attackMs) : 0.0;
    return std::max (rms, p.autoRelease ? crestSettleMs : 0.0);
}

double CleanModel::crestSquared (int channel) const noexcept
{
    return channels[(size_t) channel].steeringCrest;
}

void CleanModel::setTargets (const ModelParams& p) noexcept
{
    // Link fades with the curve: a jump would turn the quieter channel's rising target into a
    // corner where it meets a hold that still releases (NOTES.md in dynamics, Clicks).
    const auto link = p.linked ? p.link : 0.0;

    if (! primed)
    {
        fade.jumpTo (p.curve, link);
        attackGlide.jumpTo (std::log (p.attackMs));
        releaseGlide.jumpTo (std::log (p.releaseMs));
        autoGlide.jumpTo (p.autoRelease ? 1.0 : 0.0);
        dragInverseRatio = p.curve.inverseRatio;
        setTimes (p.attackMs, p.releaseMs);
        footStale = true;
        primed = true;
        return;
    }

    fade.setTarget (p.curve, link);
    attackGlide.setTarget (std::log (p.attackMs));
    releaseGlide.setTarget (std::log (p.releaseMs));
    autoGlide.setTarget (p.autoRelease ? 1.0 : 0.0);

    // δ follows the ratio heading in; it only bends the release's slope, so it needs no glide.
    if (! isExactly (p.curve.inverseRatio, dragInverseRatio))
    {
        dragInverseRatio = p.curve.inverseRatio;
        setTimes (std::exp (attackGlide.current()), std::exp (releaseGlide.current()));
    }
}

void CleanModel::setTimes (double attackMs, double releaseTimeMs) noexcept
{
    blend = blendFor (attackMs);
    tauAttackMs = attackMs / kappaFor (blend);
    attackPole = poleFor (tauAttackMs, rate);
    releaseMs = releaseTimeMs;
    releaseDrag = releaseDragMs (attackMs, dragInverseRatio);
    fixedReleasePole = poleFor (releaseConstantMs (releaseMs - releaseDrag, tauAttackMs), rate);

    for (auto& c : channels)
        c.rms.setTimeConstant (rmsConstantMs (attackMs), rate);

    autoCountdown = 0;
}

void CleanModel::updateAutoRelease (int numChannels, bool sharedSteering) noexcept
{
    const auto linkedC2 = linkedCrest.crestSquared();
    const auto amount = autoGlide.current();

    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels[(size_t) c];
        const auto own = ch.crest.crestSquared();
        ch.steeringCrest = sharedSteering ? linkedC2 : own;

        // Auto release turned on or off glides the factor in log time, as the Release knob.
        const auto factor = autoReleaseFactor (ch.steeringCrest);
        const auto scaled = amount >= 1.0 ? factor : std::pow (factor, amount);
        ch.releasePole = poleFor (releaseConstantMs (releaseMs * scaled - releaseDrag, tauAttackMs), rate);
    }
}

template <bool heard>
void CleanModel::run (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    setTargets (p);

    if (! fade.settled() || attackGlide.moving() || releaseGlide.moving() || autoGlide.moving())
    {
        runSamples<heard, false> (p, key, reductionDb);
        return;
    }

    // What the first steady sample of the per-sample path would do after a fade.
    if (footStale)
    {
        kneeFootMagnitude = magnitudeFor (fade.target().thresholdDb - 0.5 * fade.target().kneeDb);
        for (auto& c : channels)
            c.lastPeak = -1.0;
        footStale = false;
    }

    if constexpr (heard)
    {
        if (inFade)
        {
            for (auto& c : channels)
                c.ballistics = c.incoming;
            inFade = false;
        }
    }

    runSamples<heard, true> (p, key, reductionDb);
}

template <bool heard, bool steady>
void CleanModel::runSamples (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    const auto numChannels = std::min (key.numChannels, maxDetectorChannels);
    const auto linked = numChannels == 2 && p.linked;

    for (int i = 0; i < key.numSamples; ++i)
    {
        auto weight = 1.0;
        auto fading = false;

        if constexpr (! steady)
        {
            weight = fade.next();
            fading = fade.fading();

            // Below the knee's foot the reduction is exactly 0, so the logs are skipped there. A
            // fade evaluates both curves in full and leaves the cached peak reductions stale.
            if (fading)
            {
                footStale = true;
            }
            else if (footStale)
            {
                kneeFootMagnitude = magnitudeFor (fade.target().thresholdDb - 0.5 * fade.target().kneeDb);
                for (auto& c : channels)
                    c.lastPeak = -1.0;
                footStale = false;
            }
        }

        const auto& curve = fade.target();
        const auto& previous = fade.source();

        // Whatever steers the ballistics comes once from a linked pair, or the gains would differ
        // (NOTES.md in dynamics, Stereo). At Link 0 % each channel steers itself, so Linked at 0 %
        // stays Unlinked to the bit.
        const auto sharedSteering = linked && fade.targetLink() > 0.0;

        if constexpr (! steady)
        {
            const auto timesMoving = attackGlide.moving() || releaseGlide.moving();
            const auto autoMoving = autoGlide.moving();
            if (timesMoving)
                setTimes (std::exp (attackGlide.next()), std::exp (releaseGlide.next()));
            if (autoMoving)
                autoGlide.next();

            if constexpr (heard)
            {
                if ((timesMoving || autoMoving) && autoGlide.current() > 0.0)
                {
                    updateAutoRelease (numChannels, sharedSteering);
                    autoCountdown = autoUpdateInterval;
                }
            }
        }

        const auto footPower = kneeFootMagnitude * kneeFootMagnitude;
        const auto window = key.lookaheadAt (i) + 1;
        const auto b = blend;
        const auto peakShare = 1.0 - b;

        std::array<double, maxDetectorChannels> r {}, before {};
        auto loudestSquare = 0.0;

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            const auto k = key.channels[(size_t) c][i];
            const auto squared = k * k;

            loudestSquare = std::max (loudestSquare, squared);

            ch.peakWindow.setLength (window);
            ch.rmsWindow.setLength (window);
            const auto peak = ch.peakWindow.push (std::abs (k));
            double meanSquare;
            if constexpr (heard)
            {
                ch.crest.process (squared);
                meanSquare = ch.rmsWindow.push (ch.rms.process (squared));
            }
            else
            {
                ch.crest.processWarming (squared);
                meanSquare = ch.rmsWindow.push (ch.rms.processWarming (squared));
            }

            if constexpr (heard)
            {
                if (fading)
                {
                    const auto peakLevel = peakDb (peak);
                    const auto rmsLevel = b > 0.0 ? meanSquareDb (meanSquare) : silentDb;
                    r[(size_t) c] = peakShare * curveReduction (curve, peakLevel) + b * curveReduction (curve, rmsLevel);
                    before[(size_t) c] = peakShare * curveReduction (previous, peakLevel) + b * curveReduction (previous, rmsLevel);
                    continue;
                }

                // A held window maximum repeats for many samples; its reduction does too while the curve stands.
                if (! isExactly (peak, ch.lastPeak))
                {
                    ch.lastPeak = peak;
                    ch.lastPeakReduction = peak > kneeFootMagnitude ? curveReduction (curve, peakDb (peak)) : 0.0;
                }

                const auto rPeak = ch.lastPeakReduction;
                const auto rRms = b > 0.0 && 2.0 * meanSquare > footPower ? curveReduction (curve, meanSquareDb (meanSquare)) : 0.0;
                r[(size_t) c] = peakShare * rPeak + b * rRms;
            }
        }

        if (numChannels == 2)
        {
            if constexpr (heard)
                linkedCrest.process (loudestSquare);
            else
                linkedCrest.processWarming (loudestSquare);
        }

        if constexpr (heard)
        {
            if (linked)
            {
                linkReductions (r.data(), fade.targetLink());
                if (fading)
                    linkReductions (before.data(), fade.sourceLink());
            }

            if (autoGlide.current() > 0.0 && --autoCountdown <= 0)
            {
                updateAutoRelease (numChannels, sharedSteering);
                autoCountdown = autoUpdateInterval;
            }

            // A curve change runs the new curve's ballistics beside the old ones, starting from
            // their state, and blends the two reductions: an instant attack towards the new
            // target then enters through the fade's weight, never as a corner in the gain.
            if (! steady && fading != inFade)
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    auto& ch = channels[(size_t) c];
                    if (fading)
                        ch.incoming = ch.ballistics;
                    else
                        ch.ballistics = ch.incoming;
                }
                inFade = fading;
            }

            for (int c = 0; c < numChannels; ++c)
            {
                auto& ch = channels[(size_t) c];
                const SmoothDecoupled::Poles poles { attackPole, autoGlide.current() > 0.0 ? ch.releasePole : fixedReleasePole };
                const auto target = std::min (r[(size_t) c], curve.rangeDb);

                if (fading)
                {
                    const auto from = ch.ballistics.process (std::min (before[(size_t) c], previous.rangeDb), poles);
                    const auto to = ch.incoming.process (target, poles);
                    ch.applied = from + weight * (to - from);
                }
                else
                {
                    ch.applied = ch.ballistics.process (target, poles);
                }

                reductionDb[c][i] = ch.applied;
            }
        }
    }
}

void CleanModel::process (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    if (warmedUp)
    {
        for (auto& c : channels)
        {
            c.rms.endWarmUp();
            c.crest.endWarmUp();
        }
        linkedCrest.endWarmUp();
        warmedUp = false;
    }

    run<true> (p, key, reductionDb);
}

void CleanModel::follow (const ModelParams& p, const KeyBlock& key) noexcept
{
    run<false> (p, key, nullptr);
    warmedUp = true;

    // Steer again on the first sample heard, forget a peak reduction cached for an older curve,
    // and let a fade heard next start its ballistics from the seed.
    autoCountdown = 0;
    inFade = false;
    for (auto& c : channels)
        c.lastPeak = -1.0;
}

} // namespace basalt::models
