#include "dsp/modules/hiss/Crackle.h"

#include "dsp/modules/hiss/HissSources.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace inkcap::dsp::hiss
{

namespace
{
    // Lanes of hissCrackleShape, per dust particle (indexed by the cell it first sits in) or per
    // scratch (by the turn).
    enum ShapeLane : uint32_t
    {
        levelLane, kindLane, balanceLane, widthLane, gritLane, turnLane, offsetLane, spanLane,
        scratchAngleLane = 10, scratchOffsetLane = 12, scratchLevelLane = 14, scratchMissLane = 16, scratchBalanceLane = 18
    };

    constexpr double longestGritMs = 0.38 * 5.0;
    constexpr double longestPopMs = 1.25;
    constexpr double scratchWidthMs = 0.3;
    constexpr double scratchLevel = 6.0;
}

void Crackle::prepare (double sampleRate) noexcept
{
    fs = sampleRate;
    cellSamples = std::max (1, (int) std::lround (sampleRate * cellSeconds));
    longestEvent = (int) std::ceil (std::max (longestGritMs, longestPopMs) * 0.001 * sampleRate) + 2;
    setSeed (seed);
    for (auto& v : voices)
        v.active = false;
}

void Crackle::setSeed (uint32_t newSeed) noexcept
{
    seed = newSeed;
    dust = SeedRng (seed, Stream::hissCrackle);
    density = dust.lane (0);
    bursts = dust.lane (1);
    shape = SeedRng (seed, Stream::hissCrackleShape);
    for (int k = 0; k < maxScratches; ++k)
    {
        scratchAngle[(size_t) k] = shape.lane (scratchAngleLane + (uint32_t) k).below (0, cellsPerTurn);
        scratchOffset[(size_t) k] = shape.lane (scratchOffsetLane + (uint32_t) k).below (0, cellSamples);
    }
}

void Crackle::restartAt (int64_t t, const Wear& wear) noexcept
{
    for (auto& v : voices)
        v.active = false;

    const auto last = floorDiv (t, cellSamples);
    for (auto cell = floorDiv (t - longestEvent, cellSamples); cell <= last; ++cell)
        enterCell (cell, wear);
    nextCell = last + 1;
}

double Crackle::densityAt (int64_t cell, double chaos) const noexcept
{
    if (chaos <= 0.0)
        return 1.0;

    // Unsteady wear: the density wanders (a factor of about 0.4 .. 2.5) and comes in bursts.
    const auto seconds = (double) cell * cellSeconds;
    const auto wander = 0.9 * density.smooth (seconds * 0.35);
    const auto burst = 2.2 * std::max (0.0, bursts.smooth (seconds * 1.1) - 0.3);
    return std::exp (chaos * (wander + burst));
}

void Crackle::enterCell (int64_t cell, const Wear& wear) noexcept
{
    if (wear.rate > 0.0)
        addDust (cell, wear);
    if (wear.scratches > 0)
        addScratches (cell, wear);
}

void Crackle::addDust (int64_t cell, const Wear& wear) noexcept
{
    const auto chaos = std::clamp (wear.chaos, 0.0, 1.0);
    const auto extraTurns = (int) std::lround ((maxTurns - 1) * (1.0 - chaos));
    const auto meanTurns = 1.0 + 0.5 * extraTurns;
    const auto perCell = wear.rate * cellSeconds / meanTurns;

    for (int back = 0; back <= extraTurns; ++back)
    {
        const auto origin = cell - (int64_t) back * cellsPerTurn;
        if (! dust.chance (origin, perCell * densityAt (origin, chaos)))
            continue;

        const auto turns = 1 + shape.lane (spanLane).below (origin, extraTurns + 1);
        if (back >= turns)
            continue;

        // Mostly tiny specks, a few big ones; each later turn a little weaker, varied by Chaos.
        const auto u = shape.lane (levelLane).uniform (origin);
        auto level = (1.0 + 7.0 * u * u * u) * (1.0 - 0.2 * back);     // 0 .. +18 dB over the bed
        level *= 1.0 + 0.5 * chaos * shape.lane (turnLane).bipolar (cell);

        const auto kindDraw = shape.lane (kindLane).uniform (origin);
        const auto kind = kindDraw < wear.popShare ? Kind::pop : kindDraw < wear.popShare + 0.3 ? Kind::grit : Kind::tick;
        const auto at = cell * cellSamples + shape.lane (offsetLane).below (origin, cellSamples);
        start (at, kind, level, 0.7 * shape.lane (balanceLane).bipolar (origin), shape.lane (widthLane).uniform (origin),
               shape.lane (gritLane).bits (origin));
    }
}

void Crackle::addScratches (int64_t cell, const Wear& wear) noexcept
{
    const auto angle = (int) floorMod (cell, cellsPerTurn);
    const auto turn = floorDiv (cell, cellsPerTurn);
    const auto chaos = std::clamp (wear.chaos, 0.0, 1.0);

    for (int k = 0; k < std::min (wear.scratches, maxScratches); ++k)
    {
        if (angle != scratchAngle[(size_t) k])
            continue;
        const auto lane = (uint32_t) k;
        if (shape.lane (scratchMissLane + lane).chance (turn, 0.3 * chaos))
            continue;

        const auto level = scratchLevel * (1.0 + 0.4 * chaos * shape.lane (scratchLevelLane + lane).bipolar (turn));
        start (cell * cellSamples + scratchOffset[(size_t) k], Kind::pop, level,
               0.3 * shape.lane (scratchBalanceLane + lane).bipolar (0), -1.0, 0);
    }
}

void Crackle::start (int64_t at, Kind kind, double level, double balance, double width, uint64_t key) noexcept
{
    auto free = std::find_if (voices.begin(), voices.end(), [] (const Voice& v) { return ! v.active; });
    if (free == voices.end())
        return;     // a crowded moment: this speck is not heard (deterministically so)

    auto& v = *free;
    v.active = true;
    v.start = at;
    v.kind = kind;
    v.key = key;
    v.left = (float) (level * (1.0 + balance));
    v.right = (float) (level * (1.0 - balance));

    const auto msToSamples = 0.001 * fs;
    switch (kind)
    {
        case Kind::tick:
            v.length = 2;
            break;

        case Kind::grit:
        {
            const auto tau = std::max (1.0, (0.08 + 0.3 * width) * msToSamples);
            v.decay = (float) (-1.0 / tau);
            v.length = (int) std::ceil (5.0 * tau);
            break;
        }

        case Kind::pop:
        {
            const auto samples = std::max (2.0, (width < 0.0 ? scratchWidthMs : 0.35 + 0.9 * width) * msToSamples);
            v.decay = (float) (std::numbers::pi / samples);
            v.length = (int) std::ceil (samples);
            break;
        }
    }
}

void Crackle::next (int64_t t, const Wear& wear, float& left, float& right) noexcept
{
    while (t >= nextCell * cellSamples)
        enterCell (nextCell++, wear);

    float l = 0.0f, r = 0.0f;
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        const auto k = t - v.start;
        if (k < 0)
            continue;
        if (k >= v.length)
        {
            v.active = false;
            continue;
        }

        // Closed form in k, so a click picked up halfway (restartAt) is the same click.
        float e = 0.0f;
        switch (v.kind)
        {
            case Kind::tick:    e = tickScale * (k == 0 ? 1.0f : -0.5f); break;
            case Kind::grit:    e = gritScale * std::exp ((float) k * v.decay) * gaussian (SeedRng::mix64 (v.key + (uint64_t) k * SeedRng::gamma)); break;
            case Kind::pop:     e = popScale * std::cos (((float) k + 0.5f) * v.decay); break;
        }
        l += v.left * e;
        r += v.right * e;
    }
    left = l;
    right = r;
}

} // namespace inkcap::dsp::hiss
