#include "dsp/models/vca/VcaModel.h"

#include "dsp/dynamics/Ballistics.h"
#include "dsp/dynamics/KeyPath.h"
#include "dsp/dynamics/Stereo.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace basalt::models
{

using namespace dynamics;

namespace
{
    // Eq. 4 on the RMS level, skipping the log below the knee's foot, where the reduction is 0.
    double rmsReduction (const Curve& c, double footMeanSquare, double meanSquare) noexcept
    {
        return meanSquare > footMeanSquare ? curveReduction (c, meanSquareDb (meanSquare)) : 0.0;
    }

    // Power-sum link (NOTES.md here, 1. Detector, RMS): r_c = (1 − λ)·r(L_c) + λ·r(L_Σ). Each side
    // is computed only where its weight is not 0, so Link 0 % is Unlinked and Link 100 % one
    // reduction for both channels, to the bit.
    template <typename Reduction>
    void linkByPowerSum (double* r, int numChannels, double link, double sumMeanSquare, const double* meanSquares, Reduction&& reductionOf) noexcept
    {
        const auto summed = link > 0.0 ? reductionOf (sumMeanSquare) : 0.0;

        for (int c = 0; c < numChannels; ++c)
            r[c] = link >= 1.0 ? summed : (1.0 - link) * reductionOf (meanSquares[c]) + link * summed;
    }
} // namespace

VcaModel::VcaModel (Detector d) noexcept
    : kind (d)
{
}

Curve VcaModel::effectiveCurve (const Curve& c) noexcept
{
    auto e = c;
    e.thresholdDb = c.thresholdDb - thresholdShiftDb * c.inverseRatio;
    return e;
}

double VcaModel::rmsReleaseFloorMs (double attackMs, double inverseRatio) noexcept
{
    return inverseRatio < 1.0 ? rmsTimeConstantMs (attackMs) * std::numbers::ln10 / (1.0 - inverseRatio)
                              : std::numeric_limits<double>::infinity();
}

double VcaModel::peakFastMs (double attackMs, double releaseMs) noexcept
{
    return releaseConstantMs (releaseMs, attackMs);
}

double VcaModel::peakSlowMs (double releaseMs) noexcept
{
    return std::min (autoSlowPerRelease * releaseMs, autoSlowMaxMs);
}

void VcaModel::prepare (double sampleRate)
{
    rate = sampleRate;
    const auto capacity = windowCapacity (sampleRate);

    for (auto& c : channels)
        c.window.prepare (capacity);

    linkedWindow.prepare (capacity);
    fade.prepare (sampleRate);
    attackGlide.prepare (timeGlideMs, sampleRate);
    releaseGlide.prepare (timeGlideMs, sampleRate);
    autoGlide.prepare (timeGlideMs, sampleRate);
    reset();
}

void VcaModel::reset() noexcept
{
    for (auto& c : channels)
    {
        c.rms.reset();
        c.window.reset();
        c.peak.reset();
        c.peakIncoming.reset();
        c.meter.reset();
        c.meterIncoming.reset();
        c.applied = 0.0;
        c.lastPeak = -1.0;
    }

    linkedWindow.reset();
    primed = false;
    footStale = true;
    inFade = false;
    warmedUp = false;
}

void VcaModel::seed (const double* reductionDb, int numChannels) noexcept
{
    for (int c = 0; c < std::min (numChannels, maxDetectorChannels); ++c)
    {
        auto& ch = channels[(size_t) c];
        ch.peak.reset (reductionDb[c]);
        ch.peakIncoming.reset (reductionDb[c]);
        ch.meter.reset (reductionDb[c]);
        ch.meterIncoming.reset (reductionDb[c]);
        ch.applied = reductionDb[c];
    }
}

double VcaModel::staticReduction (const ModelParams& p, double levelDb) const noexcept
{
    // A sine reads its peak on both detectors, so they share the curve.
    return dynamics::staticReduction (effectiveCurve (p.curve), levelDb);
}

double VcaModel::reduction (int channel) const noexcept
{
    return channels[(size_t) channel].applied;
}

double VcaModel::settleMs (const ModelParams& p) const noexcept
{
    return kind == Detector::rms ? 5.0 * rmsTimeConstantMs (p.attackMs) : 0.0;
}

void VcaModel::setTargets (const ModelParams& p) noexcept
{
    const auto link = p.linked ? p.link : 0.0;
    const auto autoTarget = kind == Detector::peak && p.autoRelease ? 1.0 : 0.0;

    if (! primed)
    {
        fade.jumpTo (p.curve, link);
        attackGlide.jumpTo (std::log (p.attackMs));
        releaseGlide.jumpTo (std::log (p.releaseMs));
        autoGlide.jumpTo (autoTarget);
        setTimes (p.attackMs, p.releaseMs);
        footStale = true;
        primed = true;
        return;
    }

    fade.setTarget (p.curve, link);
    attackGlide.setTarget (std::log (p.attackMs));
    releaseGlide.setTarget (std::log (p.releaseMs));
    autoGlide.setTarget (autoTarget);
}

void VcaModel::setTimes (double attackMs, double releaseMs) noexcept
{
    if (kind == Detector::peak)
    {
        const auto fastMs = peakFastMs (attackMs, releaseMs);
        peakCoefficients.attack = poleFor (attackMs, rate);
        peakCoefficients.release = poleFor (fastMs, rate);
        peakCoefficients.network = TwoStageNetwork::forTimes (fastMs, peakSlowMs (releaseMs), autoCapacitorRatio, rate);
        return;
    }

    releaseStep = rmsReleaseDepthDb * 1000.0 / (releaseMs * rate);
    for (auto& c : channels)
        c.rms.setTimeConstant (rmsTimeConstantMs (attackMs), rate);
}

void VcaModel::refreshFoot() noexcept
{
    const auto e = effectiveCurve (fade.target());
    footMagnitude = magnitudeFor (e.thresholdDb - 0.5 * e.kneeDb);
    footMeanSquare = 0.5 * footMagnitude * footMagnitude;

    for (auto& c : channels)
        c.lastPeak = -1.0;

    footStale = false;
}

void VcaModel::startOrEndFade (bool fading, int numChannels) noexcept
{
    // A curve change runs the new curve's ballistics beside the old, starting from their state,
    // and blends the two reductions (NOTES.md in dynamics, Clicks).
    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels[(size_t) c];
        if (fading)
        {
            ch.peakIncoming = ch.peak;
            ch.meterIncoming = ch.meter;
        }
        else
        {
            ch.peak = ch.peakIncoming;
            ch.meter = ch.meterIncoming;
        }
    }

    inFade = fading;
}

template <bool heard>
void VcaModel::run (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    setTargets (p);

    const auto steady = fade.settled() && ! attackGlide.moving() && ! releaseGlide.moving() && ! autoGlide.moving();

    if (steady)
    {
        if (footStale)
            refreshFoot();

        if constexpr (heard)
            if (inFade)
                startOrEndFade (false, maxDetectorChannels);
    }

    if (kind == Detector::peak)
    {
        if (steady)
            runPeak<heard, true> (p, key, reductionDb);
        else
            runPeak<heard, false> (p, key, reductionDb);
    }
    else
    {
        if (steady)
            runRms<heard, true> (p, key, reductionDb);
        else
            runRms<heard, false> (p, key, reductionDb);
    }
}

template <bool heard, bool steady>
void VcaModel::runPeak (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
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

            if (fading)
                footStale = true;
            else if (footStale)
                refreshFoot();

            if (attackGlide.moving() || releaseGlide.moving())
                setTimes (std::exp (attackGlide.next()), std::exp (releaseGlide.next()));
            if (autoGlide.moving())
                autoGlide.next();
        }

        const auto window = key.lookaheadAt (i) + 1;
        std::array<double, maxDetectorChannels> r {}, before {};

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            ch.window.setLength (window);
            const auto peak = ch.window.push (saneMagnitude (key.channels[(size_t) c][i]));

            if constexpr (heard)
            {
                if (fading)
                {
                    const auto level = peakDb (peak);
                    r[(size_t) c] = curveReduction (effectiveCurve (fade.target()), level);
                    before[(size_t) c] = curveReduction (effectiveCurve (fade.source()), level);
                    continue;
                }

                // A held window maximum repeats for many samples; its reduction does too while the curve stands.
                if (! myzel::dsp::isExactly (peak, ch.lastPeak))
                {
                    ch.lastPeak = peak;
                    ch.lastPeakReduction = peak > footMagnitude ? curveReduction (effectiveCurve (fade.target()), peakDb (peak)) : 0.0;
                }

                r[(size_t) c] = ch.lastPeakReduction;
            }
        }

        if constexpr (heard)
        {
            if (linked)
            {
                linkReductions (r.data(), fade.targetLink());
                if (fading)
                    linkReductions (before.data(), fade.sourceLink());
            }

            if (! steady && fading != inFade)
                startOrEndFade (fading, numChannels);

            const auto amount = autoGlide.current();
            const auto range = fade.target().rangeDb;

            for (int c = 0; c < numChannels; ++c)
            {
                auto& ch = channels[(size_t) c];
                const auto target = std::min (r[(size_t) c], range);

                if (fading)
                {
                    const auto from = ch.peak.process (std::min (before[(size_t) c], fade.source().rangeDb), peakCoefficients, amount);
                    const auto to = ch.peakIncoming.process (target, peakCoefficients, amount);
                    ch.applied = from + weight * (to - from);
                }
                else
                {
                    ch.applied = ch.peak.process (target, peakCoefficients, amount);
                }

                reductionDb[c][i] = ch.applied;
            }
        }
    }
}

template <bool heard, bool steady>
void VcaModel::runRms (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
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

            if (fading)
                footStale = true;
            else if (footStale)
                refreshFoot();

            if (attackGlide.moving() || releaseGlide.moving())
                setTimes (std::exp (attackGlide.next()), std::exp (releaseGlide.next()));
        }

        const auto window = key.lookaheadAt (i) + 1;
        std::array<double, maxDetectorChannels> meanSquares {}, levels {};

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            const auto k = saneMagnitude (key.channels[(size_t) c][i]);
            if constexpr (heard)
                meanSquares[(size_t) c] = ch.rms.process (k * k);
            else
                meanSquares[(size_t) c] = ch.rms.processWarming (k * k);
            ch.window.setLength (window);
            levels[(size_t) c] = ch.window.push (meanSquares[(size_t) c]);
        }

        auto summed = 0.0;
        if (linked)
        {
            linkedWindow.setLength (window);
            summed = linkedWindow.push (0.5 * (meanSquares[0] + meanSquares[1]));
        }

        if constexpr (heard)
        {
            std::array<double, maxDetectorChannels> r {}, before {};
            const auto curve = effectiveCurve (fade.target());

            if (fading)
            {
                const auto previous = effectiveCurve (fade.source());
                linkByPowerSum (r.data(), numChannels, linked ? fade.targetLink() : 0.0, summed, levels.data(),
                                [&curve] (double m) { return rmsReduction (curve, 0.0, m); });
                linkByPowerSum (before.data(), numChannels, linked ? fade.sourceLink() : 0.0, summed, levels.data(),
                                [&previous] (double m) { return rmsReduction (previous, 0.0, m); });
            }
            else
            {
                const auto foot = footMeanSquare;
                linkByPowerSum (r.data(), numChannels, linked ? fade.targetLink() : 0.0, summed, levels.data(),
                                [&curve, foot] (double m) { return rmsReduction (curve, foot, m); });
            }

            if (! steady && fading != inFade)
                startOrEndFade (fading, numChannels);

            const auto range = fade.target().rangeDb;

            for (int c = 0; c < numChannels; ++c)
            {
                auto& ch = channels[(size_t) c];
                const auto target = std::min (r[(size_t) c], range);

                if (fading)
                {
                    const auto from = ch.meter.process (std::min (before[(size_t) c], fade.source().rangeDb), releaseStep);
                    const auto to = ch.meterIncoming.process (target, releaseStep);
                    ch.applied = from + weight * (to - from);
                }
                else
                {
                    ch.applied = ch.meter.process (target, releaseStep);
                }

                reductionDb[c][i] = ch.applied;
            }
        }
    }
}

void VcaModel::process (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    if (warmedUp)
    {
        for (auto& c : channels)
            c.rms.endWarmUp();
        warmedUp = false;
    }

    run<true> (p, key, reductionDb);
}

void VcaModel::follow (const ModelParams& p, const KeyBlock& key) noexcept
{
    run<false> (p, key, nullptr);
    warmedUp = true;

    // Forget a peak reduction cached for an older curve, and let a fade heard next start its
    // ballistics from the seed.
    inFade = false;
    for (auto& c : channels)
        c.lastPeak = -1.0;
}

} // namespace basalt::models
