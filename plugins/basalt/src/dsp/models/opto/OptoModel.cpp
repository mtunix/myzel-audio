#include "dsp/models/opto/OptoModel.h"

#include "dsp/dynamics/Ballistics.h"
#include "dsp/dynamics/KeyPath.h"
#include "dsp/models/opto/OptoAttackTable.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace basalt::models
{

using namespace dynamics;

namespace
{
    constexpr double dbPerNeper = 10.0 / std::numbers::ln10;
    constexpr double lightMaxMs = 5.0, lightPerAttack = 1.5;
    constexpr double slopeShareFloor = 1.0 / 3.0;      // the time laws take Ratio 1.5's q below it
    constexpr double referenceDepthDb = 10.0;
    constexpr double dischargePerRelease = 16.0, dischargeMaxMs = 25000.0;
    constexpr double maxExponent = 700.0;              // e^700 is still finite

    double timeSlopeShare (double inverseRatio) noexcept { return std::max (1.0 - inverseRatio, slopeShareFloor); }
}

double OptoModel::kneeWidthDb (double kneeDb) noexcept
{
    return (kneeDb + 12.0) / 5.5;
}

double OptoModel::lightMs (double attackMs) noexcept
{
    return std::min (lightMaxMs, lightPerAttack * std::max (attackMs, attackFloorMs));
}

double OptoModel::lightDelayMs (double attackMs, double kneeDb) noexcept
{
    return lightMs (attackMs) * kneeWidthDb (kneeDb) / dbPerNeper;
}

double OptoModel::attackConstantMs (double attackMs, double inverseRatio, double kneeDb) noexcept
{
    using namespace optotable;

    const auto a = std::max (attackMs, attackFloorMs);
    const auto fa = std::clamp (std::log (a / attackStartMs) / attackStep, 0.0, attackPoints - 1.0);
    const auto fs = std::clamp ((1.0 / timeSlopeShare (inverseRatio) - inverseShareLow) / (inverseShareHigh - inverseShareLow) * (ratioPoints - 1), 0.0, ratioPoints - 1.0);
    const auto fk = std::clamp ((1.0 / kneeWidthDb (kneeDb) - inverseWidthLow) / (inverseWidthHigh - inverseWidthLow) * (kneePoints - 1), 0.0, kneePoints - 1.0);

    const auto ia = std::min ((int) fa, attackPoints - 2);
    const auto is = std::min ((int) fs, ratioPoints - 2);
    const auto ik = std::min ((int) fk, kneePoints - 2);
    const double ua = fa - ia, us = fs - is, uk = fk - ik;

    auto plane = [&] (int i)
    {
        const auto& t = logAttackConstant[i];
        const auto low = t[is][ik] + uk * (t[is][ik + 1] - t[is][ik]);
        const auto high = t[is + 1][ik] + uk * (t[is + 1][ik + 1] - t[is + 1][ik]);
        return low + us * (high - low);
    };

    const auto v = plane (ia) + ua * (plane (ia + 1) - plane (ia));
    return a * std::exp (v);
}

double OptoModel::releaseConstantMs (double releaseMs, double attackMs, double inverseRatio, double kneeDb) noexcept
{
    const auto qw = timeSlopeShare (inverseRatio) * kneeWidthDb (kneeDb);
    const auto spread = std::log (std::expm1 (referenceDepthDb / qw) / std::expm1 (0.5 * referenceDepthDb / qw));
    const auto delay = lightDelayMs (attackMs, kneeDb);
    return (std::max (releaseMs, delay + 1.0) - delay) / spread;
}

double OptoModel::dischargeMs (double releaseMs) noexcept
{
    return std::min (dischargePerRelease * releaseMs, dischargeMaxMs);
}

double OptoModel::curveReduction (const Curve& c, double levelDb) noexcept
{
    const auto w = kneeWidthDb (c.kneeDb);
    const auto g = std::max (std::exp ((levelDb - c.thresholdDb) / w) - onsetEpsilon, 0.0);
    return std::min ((1.0 - c.inverseRatio) * w * std::log1p (g), c.rangeDb);
}

bool OptoModel::same (const Cell& a, const Cell& b) noexcept
{
    using myzel::dsp::isExactly;
    return isExactly (a.fast, b.fast) && isExactly (a.memory, b.memory);
}

double OptoModel::Law::conductance (double levelDb) const noexcept
{
    return std::max (std::exp ((levelDb - curve.thresholdDb) * inverseWidth) - onsetEpsilon, 0.0);
}

double OptoModel::Law::reduction (const Cell& c) const noexcept
{
    return scale * std::log1p (c.fast + c.memory);
}

void OptoModel::prepare (double sampleRate)
{
    rate = sampleRate;

    for (auto& c : channels)
        c.window.prepare (windowCapacity (sampleRate));

    fade.prepare (sampleRate);
    attackGlide.prepare (timeGlideMs, sampleRate);
    releaseGlide.prepare (timeGlideMs, sampleRate);
    autoGlide.prepare (timeGlideMs, sampleRate);
    chargePole = poleFor (chargeMs, sampleRate);
    reset();
}

void OptoModel::reset() noexcept
{
    for (auto& c : channels)
    {
        c.light.reset();
        c.window.reset();
        c.cell = {};
        c.incoming = {};
        c.applied = 0.0;
    }

    primed = false;
    inFade = false;
    warmedUp = false;
}

void OptoModel::seedCell (Cell& cell, const Law& l, double reductionDb) const noexcept
{
    // The cell's state is a conductance, so the reduction goes in through the divider's law.
    cell.memory = 0.0;
    cell.fast = reductionDb > 0.0 && l.scale > 0.0 ? std::min (std::expm1 (std::min (reductionDb / l.scale, maxExponent)), l.conductanceMax) : 0.0;
}

void OptoModel::seed (const double* reductionDb, int numChannels) noexcept
{
    for (int c = 0; c < std::min (numChannels, maxDetectorChannels); ++c)
    {
        auto& ch = channels[(size_t) c];
        seedCell (ch.cell, inFade ? sourceLaw : law, reductionDb[c]);
        seedCell (ch.incoming, law, reductionDb[c]);
        ch.applied = reductionDb[c];
    }
}

double OptoModel::staticReduction (const ModelParams& p, double levelDb) const noexcept
{
    return curveReduction (p.curve, levelDb);
}

double OptoModel::reduction (int channel) const noexcept
{
    return channels[(size_t) channel].applied;
}

double OptoModel::settleMs (const ModelParams& p) const noexcept
{
    return 5.0 * lightMs (p.attackMs);
}

double OptoModel::lightOf (int channel) const noexcept { return channels[(size_t) channel].light.meanSquare(); }
double OptoModel::fastOf (int channel) const noexcept { return channels[(size_t) channel].cell.fast; }
double OptoModel::memoryOf (int channel) const noexcept { return channels[(size_t) channel].cell.memory; }

void OptoModel::setLaw (Law& l, const Curve& c) const noexcept
{
    const auto w = kneeWidthDb (c.kneeDb);
    l.curve = c;
    l.inverseWidth = 1.0 / w;
    l.scale = (1.0 - c.inverseRatio) * w;
    l.onsetDb = c.thresholdDb - 6.0 * w;
    l.onsetPower = std::pow (10.0, 0.1 * l.onsetDb);

    if (c.rangeDb <= 0.0)
        l.conductanceMax = 0.0;
    else
        l.conductanceMax = l.scale > 0.0 ? std::expm1 (std::min (c.rangeDb / l.scale, maxExponent)) : std::numeric_limits<double>::max();

    l.memoryMax = memoryShare * l.conductanceMax;
    setFastPoles (l);
}

void OptoModel::setFastPoles (Law& l) const noexcept
{
    l.rise = poleFor (attackConstantMs (attackMs, l.curve.inverseRatio, l.curve.kneeDb), rate);
    l.fall = poleFor (releaseConstantMs (releaseMs, attackMs, l.curve.inverseRatio, l.curve.kneeDb), rate);
}

void OptoModel::setTimes (double newAttackMs, double newReleaseMs) noexcept
{
    attackMs = newAttackMs;
    releaseMs = newReleaseMs;

    for (auto& c : channels)
        c.light.setTimeConstant (lightMs (attackMs), rate);

    setFastPoles (law);
    setFastPoles (sourceLaw);
    dischargePole = poleFor (dischargeMs (releaseMs), rate);
}

void OptoModel::setTargets (const ModelParams& p) noexcept
{
    const auto link = p.linked ? p.link : 0.0;

    if (! primed)
    {
        fade.jumpTo (p.curve, link);
        attackGlide.jumpTo (std::log (p.attackMs));
        releaseGlide.jumpTo (std::log (p.releaseMs));
        autoGlide.jumpTo (p.autoRelease ? 1.0 : 0.0);
        autoAmount = autoGlide.current();
        attackMs = p.attackMs;
        releaseMs = p.releaseMs;
        setLaw (law, p.curve);
        sourceLaw = law;
        setTimes (p.attackMs, p.releaseMs);
        primed = true;
        return;
    }

    fade.setTarget (p.curve, link);
    attackGlide.setTarget (std::log (p.attackMs));
    releaseGlide.setTarget (std::log (p.releaseMs));
    autoGlide.setTarget (p.autoRelease ? 1.0 : 0.0);
}

double OptoModel::step (Cell& cell, const Law& l, double g, bool memoryOn) const noexcept
{
    using myzel::dsp::isExactly;

    if (isExactly (g, 0.0) && isExactly (cell.fast, 0.0) && isExactly (cell.memory, 0.0))
        return 0.0;

    // Range caps what the light asks for, so a cell rising into it rounds off instead of hitting
    // the state's clamp as a corner; the clamps below keep a cell above a lowered Range in bounds.
    g = std::min (g, l.conductanceMax);

    if (memoryOn)
    {
        const auto target = memoryShare * g;
        cell.memory += (1.0 - (target > cell.memory ? chargePole : dischargePole)) * (target - cell.memory);
    }

    const auto target = std::max (g - cell.memory, 0.0);
    cell.fast += (1.0 - (target > cell.fast ? l.rise : l.fall)) * (target - cell.fast);

    // Range: the cell never holds more than the reduction shown, so its release starts from it.
    cell.memory = std::min (cell.memory, l.memoryMax);
    cell.fast = std::min (cell.fast, l.conductanceMax - cell.memory);

    const auto r = l.reduction (cell);

    // Dark and under 10⁻⁶ dB: exactly released (dynamics/NOTES.md, Idle).
    if (isExactly (g, 0.0) && r < snapDb)
    {
        cell = {};
        return 0.0;
    }

    return r;
}

void OptoModel::handOver (double kept) noexcept
{
    auto move = [kept] (Cell& c)
    {
        const auto memory = kept > 0.0 ? c.memory * kept : 0.0;
        c.fast += c.memory - memory;
        c.memory = memory;
    };

    for (auto& ch : channels)
    {
        move (ch.cell);
        move (ch.incoming);
    }
}

void OptoModel::conductances (const Law& l, double link, const double* meanSquares, int numChannels, bool linked, double* g) const noexcept
{
    // Linked stereo links the light's levels before the cells (NOTES.md, Detector). At Link 100 %
    // the louder channel's light drives both, so both cells see the same conductance to the bit.
    if (! linked || link <= 0.0)
    {
        for (int c = 0; c < numChannels; ++c)
            g[c] = 2.0 * meanSquares[c] > l.onsetPower ? l.conductance (meanSquareDb (meanSquares[c])) : 0.0;
        return;
    }

    if (link >= 1.0)
    {
        const auto loudest = std::max (meanSquares[0], meanSquares[1]);
        g[0] = g[1] = 2.0 * loudest > l.onsetPower ? l.conductance (meanSquareDb (loudest)) : 0.0;
        return;
    }

    if (2.0 * std::max (meanSquares[0], meanSquares[1]) <= l.onsetPower)
    {
        g[0] = g[1] = 0.0;
        return;
    }

    const double u[] { meanSquareDb (meanSquares[0]), meanSquareDb (meanSquares[1]) };
    const auto top = std::max (u[0], u[1]);
    for (int c = 0; c < 2; ++c)
    {
        const auto level = (1.0 - link) * u[c] + link * top;
        g[c] = level > l.onsetDb ? l.conductance (level) : 0.0;
    }
}

template <bool heard>
void OptoModel::run (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
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
            for (auto& c : channels)
                c.cell = c.incoming;
            inFade = false;
        }
    }

    runSamples<heard, true> (p, key, reductionDb);
}

template <bool heard, bool steady>
void OptoModel::runSamples (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
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

            if (! CurveFade::same (fade.target(), law.curve))
            {
                sourceLaw = law;
                setLaw (law, fade.target());
            }

            if (attackGlide.moving() || releaseGlide.moving())
                setTimes (std::exp (attackGlide.next()), std::exp (releaseGlide.next()));

            if (autoGlide.moving())
            {
                const auto before = autoAmount;
                autoAmount = autoGlide.next();
                if (heard && autoAmount < before)
                    handOver (before > 0.0 ? autoAmount / before : 0.0);
            }
        }

        const auto window = key.lookaheadAt (i) + 1;
        std::array<double, maxDetectorChannels> light {};

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            const auto k = key.channels[(size_t) c][i];
            const auto squared = k * k;
            const auto m = heard ? ch.light.process (squared) : ch.light.processWarming (squared);
            ch.window.setLength (window);
            light[(size_t) c] = ch.window.push (m);
        }

        if constexpr (heard)
        {
            const auto memoryOn = autoAmount > 0.0;
            std::array<double, maxDetectorChannels> g {}, gSource {};
            conductances (law, fade.targetLink(), light.data(), numChannels, linked, g.data());

            if constexpr (steady)
            {
                auto& first = channels[0];

                // Linked at 100 %, the two cells are one cell until something sets them apart.
                const auto twins = numChannels == 2 && myzel::dsp::isExactly (g[0], g[1]) && same (first.cell, channels[1].cell);

                first.applied = step (first.cell, law, g[0], memoryOn);
                reductionDb[0][i] = first.applied;

                if (numChannels == 2)
                {
                    auto& second = channels[1];
                    if (twins)
                    {
                        second.cell = first.cell;
                        second.applied = first.applied;
                    }
                    else
                    {
                        second.applied = step (second.cell, law, g[1], memoryOn);
                    }
                    reductionDb[1][i] = second.applied;
                }
            }
            else
            {
                if (fading)
                    conductances (sourceLaw, fade.sourceLink(), light.data(), numChannels, linked, gSource.data());

                // A curve change runs the new curve's cell beside the old one, starting from its
                // state, and blends the two reductions, so the new law enters through the weight.
                if (fading != inFade)
                {
                    for (int c = 0; c < numChannels; ++c)
                    {
                        auto& ch = channels[(size_t) c];
                        if (fading)
                            ch.incoming = ch.cell;
                        else
                            ch.cell = ch.incoming;
                    }
                    inFade = fading;
                }

                for (int c = 0; c < numChannels; ++c)
                {
                    auto& ch = channels[(size_t) c];
                    if (fading)
                    {
                        const auto from = step (ch.cell, sourceLaw, gSource[(size_t) c], memoryOn);
                        const auto to = step (ch.incoming, law, g[(size_t) c], memoryOn);
                        ch.applied = from + weight * (to - from);
                    }
                    else
                    {
                        ch.applied = step (ch.cell, law, g[(size_t) c], memoryOn);
                    }
                    reductionDb[c][i] = ch.applied;
                }
            }
        }
    }
}

void OptoModel::process (const ModelParams& p, const KeyBlock& key, double* const* reductionDb) noexcept
{
    if (warmedUp)
    {
        for (auto& c : channels)
            c.light.endWarmUp();
        warmedUp = false;
    }

    run<true> (p, key, reductionDb);
}

void OptoModel::follow (const ModelParams& p, const KeyBlock& key) noexcept
{
    run<false> (p, key, nullptr);
    warmedUp = true;
    inFade = false;
}

} // namespace basalt::models
