#include "dsp/models/fet/FetModel.h"

#include <algorithm>
#include <cmath>

namespace basalt::models
{

using namespace dynamics;
using myzel::dsp::isExactly;

namespace
{
    constexpr double log2Of12 = 3.5849625007211562;
    constexpr double log2Of20 = 4.3219280948873626;
} // namespace

double FetModel::thresholdOffsetDb (double inverseRatio) noexcept
{
    // The reference's input thresholds at 8, 12 and 20:1 over 4:1 (NOTES.md), joined linearly in log2 R.
    const auto octaves = std::log2 (1.0 / std::max (inverseRatio, 1.0 / ratioCap));

    if (octaves <= 2.0)
        return 0.0;
    if (octaves <= 3.0)
        return 4.0 * (octaves - 2.0);
    if (octaves <= log2Of12)
        return 4.0 + (octaves - 3.0) / (log2Of12 - 3.0);
    return 5.0 + std::min (octaves - log2Of12, log2Of20 - log2Of12) / (log2Of20 - log2Of12);
}

Curve FetModel::loopCurve (const Curve& knobs) noexcept
{
    auto c = knobs;
    c.inverseRatio = cappedInverseRatio (knobs.inverseRatio, ratioCap);
    if (! isAll (knobs))
        c.thresholdDb += thresholdOffsetDb (knobs.inverseRatio);
    return c;
}

void FetModel::prepare (double sampleRate)
{
    rate = sampleRate;

    for (auto& c : channels)
        c.hold.prepare (windowCapacity (sampleRate));

    lagLine.prepare (maxDetectorChannels, (int) std::ceil (maxLagMs * 0.001 * sampleRate));
    fade.prepare (sampleRate);
    attackGlide.prepare (timeGlideMs, sampleRate);
    releaseGlide.prepare (timeGlideMs, sampleRate);
    lagGlide.prepare (lookaheadGlideMs, sampleRate);
    reset();
}

void FetModel::reset() noexcept
{
    for (auto& c : channels)
    {
        c.hold.reset();
        c.lastPeak = -1.0;
        c.applied = 0.0;
    }

    lagLine.reset();
    loop.reset();
    incoming.reset();
    primed = false;
    inFade = false;
}

void FetModel::seed (const double* reductionDb, int numChannels) noexcept
{
    for (int c = 0; c < std::min (numChannels, maxDetectorChannels); ++c)
    {
        loop.seed (c, reductionDb[c]);
        incoming.seed (c, reductionDb[c]);
        channels[(size_t) c].applied = reductionDb[c];
    }
}

double FetModel::staticReduction (const ModelParams& p, double levelDb) const noexcept
{
    // The loop settles on eq. 4 exactly, so the static curve is eq. 4 at T_eff.
    return dynamics::staticReduction (loopCurve (p.curve), levelDb);
}

double FetModel::reduction (int channel) const noexcept
{
    return channels[(size_t) channel].applied;
}

double FetModel::lagSamplesFor (const Curve& knobs) const noexcept
{
    return isAll (knobs) ? (double) std::lround (allLagMs * 0.001 * rate) : 0.0;
}

double FetModel::settleMs (const ModelParams& p) const noexcept
{
    const auto samples = feedbackHoldSamples (holdMs (p.releaseMs), rate) + lagSamplesFor (p.curve) + 1.0;
    return 1000.0 * samples / rate;
}

void FetModel::setTimes (double attackTimeMs, double releaseTimeMs) noexcept
{
    attackMs = attackTimeMs;
    releaseMs = releaseTimeMs;
    holdSamples = feedbackHoldSamples (holdMs (releaseMs), rate);

    // The release subtracts the hold as the window holds it, in whole samples. Taken at Ratio 1:
    // a curve's attack step is this one times its 1/R (coefficientsFor).
    unitCoefficients = FeedbackLoop::coefficientsFor (attackMs, releaseMs, 1000.0 * holdSamples / rate, 1.0, rate);
}

FeedbackLoop::Coefficients FetModel::coefficientsFor (const Curve& c) const noexcept
{
    return { unitCoefficients.attackStep * c.inverseRatio, unitCoefficients.releasePole };
}

void FetModel::setTargets (const ModelParams& p) noexcept
{
    const auto curve = loopCurve (p.curve);
    const auto link = p.linked ? p.link : 0.0;

    if (! primed)
    {
        fade.jumpTo (curve, link);
        attackGlide.jumpTo (std::log (p.attackMs));
        releaseGlide.jumpTo (std::log (p.releaseMs));
        lagGlide.jumpTo (lagSamplesFor (p.curve));
        setTimes (p.attackMs, p.releaseMs);
        primed = true;
        return;
    }

    fade.setTarget (curve, link);
    attackGlide.setTarget (std::log (p.attackMs));
    releaseGlide.setTarget (std::log (p.releaseMs));
    lagGlide.setTarget (lagSamplesFor (p.curve));
}

template <bool heard, bool steady>
void FetModel::runSamples (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    const auto numChannels = std::min (key.numChannels, maxDetectorChannels);
    const auto linked = numChannels == 2 && p.linked;

    // Below the knee's foot the demand is 0 whatever the state, so the log is skipped there. A
    // fade has two feet, so only steady blocks take the shortcut.
    const auto footMagnitude = steady ? magnitudeFor (fade.target().thresholdDb - 0.5 * fade.target().kneeDb) : -1.0;
    auto lag = lagGlide.current();

    for (int i = 0; i < key.numSamples; ++i)
    {
        auto weight = 1.0;
        auto fading = false;

        if constexpr (! steady)
        {
            weight = fade.next();
            fading = fade.fading();
            lag = lagGlide.next();

            const auto timesMoving = attackGlide.moving() || releaseGlide.moving();
            if (timesMoving)
                setTimes (std::exp (attackGlide.next()), std::exp (releaseGlide.next()));

            if constexpr (heard)
            {
                if (timesMoving || fading)
                {
                    sourceK = coefficientsFor (fade.source());
                    targetK = coefficientsFor (fade.target());
                }
            }
        }

        // The pre-hold's window: h, plus the look-ahead, on the key All delays by its lag.
        const auto window = holdSamples + key.lookaheadAt (i) + 1;
        std::array<double, maxDetectorChannels> magnitude {}, level {};

        for (int c = 0; c < numChannels; ++c)
            magnitude[(size_t) c] = saneMagnitude (key.channels[(size_t) c][i]);
        lagLine.push (magnitude.data());

        // While the lag glides, a position between two samples reads the louder of them: the
        // pre-hold never sees a magnitude no sample had, so the glide only moves its window's edge.
        const auto lagWhole = std::floor (lag);
        const auto between = lag > lagWhole;

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto delayed = lagLine.read (c, lagWhole);
            if (between)
                delayed = std::max (delayed, lagLine.read (c, lagWhole + 1.0));

            ch.hold.setLength (window);
            const auto peak = ch.hold.push (delayed);

            if constexpr (heard)
            {
                if (peak <= footMagnitude)
                {
                    level[(size_t) c] = silentDb;
                    continue;
                }

                // A held peak repeats for many samples; so does its level.
                if (! isExactly (peak, ch.lastPeak))
                {
                    ch.lastPeak = peak;
                    ch.lastLevelDb = peakDb (peak);
                }
                level[(size_t) c] = ch.lastLevelDb;
            }
        }

        if constexpr (heard)
        {
            // A curve change runs the new curve's loop beside the old one, from its state, and
            // blends the two reductions (NOTES.md in dynamics, Clicks).
            if (! steady && fading != inFade)
            {
                if (fading)
                    incoming = loop;
                else
                    loop = incoming;
                inFade = fading;
            }

            std::array<double, maxDetectorChannels> r {};

            if (fading)
            {
                std::array<double, maxDetectorChannels> from {};
                loop.process (level.data(), from.data(), numChannels, fade.source(), sourceK, linked ? fade.sourceLink() : 0.0);
                incoming.process (level.data(), r.data(), numChannels, fade.target(), targetK, linked ? fade.targetLink() : 0.0);
                for (int c = 0; c < numChannels; ++c)
                    r[(size_t) c] = from[(size_t) c] + weight * (r[(size_t) c] - from[(size_t) c]);
            }
            else
            {
                loop.process (level.data(), r.data(), numChannels, fade.target(), targetK, linked ? fade.targetLink() : 0.0);
            }

            for (int c = 0; c < numChannels; ++c)
            {
                channels[(size_t) c].applied = r[(size_t) c];
                reductionDb[c][i] = r[(size_t) c];
            }
        }
    }
}

void FetModel::process (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    setTargets (p);
    sourceK = coefficientsFor (fade.source());
    targetK = coefficientsFor (fade.target());

    if (fade.settled() && ! attackGlide.moving() && ! releaseGlide.moving() && ! lagGlide.moving())
    {
        if (inFade)
        {
            loop = incoming;
            inFade = false;
        }
        runSamples<true, true> (p, key, reductionDb);
        return;
    }

    runSamples<true, false> (p, key, reductionDb);
}

void FetModel::follow (const ModelParams& p, const KeyBlock& key) noexcept
{
    setTargets (p);
    runSamples<false, false> (p, key, nullptr);

    // A fade heard next starts its incoming loop from the seed.
    inFade = false;
}

} // namespace basalt::models
