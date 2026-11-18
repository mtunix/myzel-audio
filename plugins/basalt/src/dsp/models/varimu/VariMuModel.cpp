#include "dsp/models/varimu/VariMuModel.h"

#include "dsp/dynamics/Ballistics.h"
#include "dsp/dynamics/KeyPath.h"

#include <algorithm>
#include <cmath>

namespace basalt::models
{

using namespace dynamics;

namespace
{
    // κ = R_ref/(Φ·R_∞) measured in M2 on this engine: Φ is what makes the reference step
    // (silence to the carrier at 10 dB of static reduction, hard knee) read Attack 30 ms at 48 kHz
    // (NOTES.md, Measured in M2). Ratio 1 … 20 in 20 log steps; 1 at Ratio 1, where the curve
    // is one line, and at 20, where the loop is the core's.
    constexpr double kappaTable[VariMuModel::kappaRatios] {
        1.0, 1.0, 0.9956, 0.9755, 0.9560, 0.9319, 0.9089, 0.8931, 0.8869, 0.8892,
        0.8974, 0.9090, 0.9222, 0.9357, 0.9489, 0.9613, 0.9726, 0.9828, 0.9920, 1.0,
    };
}

double VariMuModel::holdMs (double releaseTimeMs) noexcept
{
    return feedbackHoldMs (releaseTimeMs);
}

double VariMuModel::referenceInverseRatio (const Curve& c) noexcept
{
    const auto s = varimu::Shape::of (c);
    if (s.base.inverseRatio >= 1.0)
        return 1.0;

    return 1.0 - varimu::evaluate (s, varimu::levelForReduction (s, referenceReductionDb)).slope;
}

double VariMuModel::asymptoticRatio (double inverseRatio) noexcept
{
    const auto s = varimu::Shape::of ({ 0.0, inverseRatio, 0.0, 40.0 });
    return 1.0 / (s.base.inverseRatio - s.rise);
}

double VariMuModel::kappaGridRatio (int i) noexcept
{
    return std::pow (varimu::ratioCap, (double) i / (kappaRatios - 1));
}

double VariMuModel::kappa (double inverseRatio) noexcept
{
    const auto x = std::log (1.0 / std::clamp (inverseRatio, varimu::capInverseRatio, 1.0)) / std::log (varimu::ratioCap) * (kappaRatios - 1);
    const auto i = std::min ((int) x, kappaRatios - 2);
    return kappaTable[i] + (x - i) * (kappaTable[i + 1] - kappaTable[i]);
}

double VariMuModel::attackCalibration (const Curve& c) noexcept
{
    return 1.0 / (referenceInverseRatio (c) * kappa (c.inverseRatio) * asymptoticRatio (c.inverseRatio));
}

double VariMuModel::memory (int channel, int tier) const noexcept
{
    const auto& m = running.memories[(size_t) channel];
    return tier == 0 ? m.fast : m.slow;
}

void VariMuModel::Loop::reset() noexcept
{
    feedback.reset();
    for (auto& i : inverses)
        i.reset();
    memories = {};
    applied = {};
}

void VariMuModel::prepare (double sampleRate)
{
    rate = sampleRate;

    for (auto& h : holds)
        h.prepare (windowCapacity (sampleRate));

    fade.prepare (sampleRate);
    attackGlide.prepare (timeGlideMs, sampleRate);
    releaseGlide.prepare (timeGlideMs, sampleRate);
    autoGlide.prepare (timeGlideMs, sampleRate);

    memoryK.fastCharge = 1.0 - poleFor (fastChargeMs, sampleRate);
    memoryK.slowCharge = 1.0 - poleFor (slowChargeMs, sampleRate);
    callingSamples = (int) std::lround (callingMs * 0.001 * sampleRate);
    reset();
}

void VariMuModel::reset() noexcept
{
    for (auto& h : holds)
        h.reset();

    running.reset();
    incoming.reset();
    output = {};
    primed = false;
    inFade = false;
}

void VariMuModel::seed (const double* reductionDb, int numChannels) noexcept
{
    for (int c = 0; c < std::min (numChannels, maxDetectorChannels); ++c)
    {
        for (auto* loop : { &running, &incoming })
        {
            loop->feedback.seed (c, reductionDb[c]);
            loop->memories[(size_t) c] = {};
            loop->applied[(size_t) c] = reductionDb[c];
        }
        output[(size_t) c] = reductionDb[c];
    }
}

double VariMuModel::staticReduction (const ModelParams& p, double levelDb) const noexcept
{
    return varimu::staticReduction (p.curve, levelDb);
}

double VariMuModel::reduction (int channel) const noexcept
{
    return output[(size_t) channel];
}

double VariMuModel::settleMs (const ModelParams& p) const noexcept
{
    return holdMs (p.releaseMs);
}

void VariMuModel::calibrate (Calibration& c, const Curve& curve) noexcept
{
    c.curve = curve;
    c.shape = varimu::Shape::of (curve);
    c.referenceInverseRatio = referenceInverseRatio (curve);
    c.phi = 1.0 / (c.referenceInverseRatio * kappa (curve.inverseRatio) * asymptoticRatio (curve.inverseRatio));
    c.coefficients = FeedbackLoop::coefficientsFor (std::max (attackMs, attackFloorMs) / c.phi, releaseMs, holdMs (releaseMs),
                                                    c.referenceInverseRatio, rate);
}

void VariMuModel::updateCalibrations() noexcept
{
    if (! CurveFade::same (fade.target(), target.curve))
        calibrate (target, fade.target());
    if (! CurveFade::same (fade.source(), source.curve))
        calibrate (source, fade.source());
}

void VariMuModel::setTimes (double attack, double release, double autoAmount) noexcept
{
    attackMs = attack;
    releaseMs = release;

    const auto h = holdMs (release);
    holdSamples = feedbackHoldSamples (h, rate);

    for (auto* c : { &source, &target })
        c->coefficients = FeedbackLoop::coefficientsFor (std::max (attack, attackFloorMs) / c->phi, release, h, c->referenceInverseRatio, rate);

    // Auto release turned on or off glides the tiers in log time, as the Release knob: 5^w and 12.5^w.
    const auto tier = [&] (double factor) { return poleFor (std::min (std::pow (factor, autoAmount) * release, tierCapMs), rate); };
    memoryK.fastRelease = tier (fastTier);
    memoryK.slowRelease = tier (slowTier);
}

void VariMuModel::setTargets (const ModelParams& p) noexcept
{
    const auto link = p.linked ? p.link : 0.0;
    memoryK.charge = p.autoRelease;

    if (! primed)
    {
        fade.jumpTo (p.curve, link);
        attackGlide.jumpTo (std::log (p.attackMs));
        releaseGlide.jumpTo (std::log (p.releaseMs));
        autoGlide.jumpTo (p.autoRelease ? 1.0 : 0.0);
        setTimes (p.attackMs, p.releaseMs, autoGlide.current());
        calibrate (source, p.curve);
        calibrate (target, p.curve);
        primed = true;
        return;
    }

    fade.setTarget (p.curve, link);
    attackGlide.setTarget (std::log (p.attackMs));
    releaseGlide.setTarget (std::log (p.releaseMs));
    autoGlide.setTarget (p.autoRelease ? 1.0 : 0.0);
}

void VariMuModel::step (Loop& loop, const Calibration& c, double link, const double* levelDb, int numChannels) noexcept
{
    std::array<double, maxDetectorChannels> state {}, demand {};
    const std::array<double, maxDetectorChannels> before { loop.feedback.reduction (0), loop.feedback.reduction (1) };
    const auto rangeDb = c.shape.base.rangeDb;

    // The estimate subtracts what the audio carries, max(s, m, ℓ); the memories charge on the linked
    // demand. The loop asks once per channel, in channel order, so each solve starts from its own
    // channel's last one (any order would still solve, only slower).
    auto channel = 0;
    const auto demandOf = [&loop, &c, &channel] (const Curve&, double y)
    {
        return loop.inverses[(size_t) std::min (channel++, maxDetectorChannels - 1)].demand (c.shape, y);
    };
    loop.feedback.process (levelDb, state.data(), numChannels, c.shape.base, c.coefficients, link, demandOf, loop.applied.data(), demand.data());

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& m = loop.memories[(size_t) ch];
        const auto s = state[(size_t) ch];

        // Calling for reduction: the loop attacks or holds, as opposed to releasing.
        m.calling = demand[(size_t) ch] >= before[(size_t) ch] - snapDb ? callingSamples : std::max (m.calling - 1, 0);

        if (! memoryK.charge && m.fast <= 0.0 && m.slow <= 0.0)
        {
            loop.applied[(size_t) ch] = s;
            continue;
        }

        const auto charging = memoryK.charge && m.calling > 0;
        m.fast = charging && s > m.fast ? m.fast + memoryK.fastCharge * (s - m.fast) : snapState (std::min (memoryK.fastRelease * m.fast, rangeDb), 0.0);
        m.slow = charging && s > m.slow ? m.slow + memoryK.slowCharge * (s - m.slow) : snapState (std::min (memoryK.slowRelease * m.slow, rangeDb), 0.0);
        loop.applied[(size_t) ch] = std::max ({ s, m.fast, m.slow });
    }
}

template <bool heard>
void VariMuModel::run (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    setTargets (p);

    if (! fade.settled() || attackGlide.moving() || releaseGlide.moving() || autoGlide.moving())
    {
        runSamples<heard, false> (p, key, reductionDb);
        return;
    }

    if constexpr (heard)
    {
        if (inFade)
        {
            running = incoming;
            inFade = false;
        }
    }

    runSamples<heard, true> (p, key, reductionDb);
}

template <bool heard, bool steady>
void VariMuModel::runSamples (const ModelParams&, const KeyBlock& key, double* const* reductionDb) noexcept
{
    const auto numChannels = std::min (key.numChannels, maxDetectorChannels);

    for (int i = 0; i < key.numSamples; ++i)
    {
        auto weight = 1.0;
        auto fading = false;

        if constexpr (! steady)
        {
            weight = fade.next();
            fading = fade.fading();
            updateCalibrations();

            if (attackGlide.moving() || releaseGlide.moving() || autoGlide.moving())
                setTimes (std::exp (attackGlide.next()), std::exp (releaseGlide.next()), autoGlide.next());
        }

        std::array<double, maxDetectorChannels> level {};
        const auto window = holdSamples + key.lookaheadAt (i) + 1;

        for (int c = 0; c < numChannels; ++c)
        {
            auto& h = holds[(size_t) c];
            h.setLength (window);
            level[(size_t) c] = peakDb (h.push (saneMagnitude (key.channels[(size_t) c][i])));
        }

        if constexpr (heard)
        {
            // A curve change runs the new curve's loop beside the old one, from its state, and
            // blends the two reductions (dynamics/NOTES.md, Clicks).
            if (! steady && fading != inFade)
            {
                if (fading)
                    incoming = running;
                else
                    running = incoming;
                inFade = fading;
            }

            if (fading)
            {
                step (running, source, fade.sourceLink(), level.data(), numChannels);
                step (incoming, target, fade.targetLink(), level.data(), numChannels);

                for (int c = 0; c < numChannels; ++c)
                {
                    const auto from = running.applied[(size_t) c];
                    output[(size_t) c] = from + weight * (incoming.applied[(size_t) c] - from);
                    reductionDb[c][i] = output[(size_t) c];
                }
            }
            else
            {
                step (running, target, fade.targetLink(), level.data(), numChannels);

                for (int c = 0; c < numChannels; ++c)
                {
                    output[(size_t) c] = running.applied[(size_t) c];
                    reductionDb[c][i] = output[(size_t) c];
                }
            }
        }
    }
}

void VariMuModel::process (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    run<true> (p, key, reductionDb);
}

void VariMuModel::follow (const ModelParams& p, const KeyBlock& key) noexcept
{
    run<false> (p, key, nullptr);

    // A fade heard next starts its second loop from the seed.
    inFade = false;
}

} // namespace basalt::models
