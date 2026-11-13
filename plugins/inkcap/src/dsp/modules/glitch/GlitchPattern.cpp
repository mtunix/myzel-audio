#include "dsp/modules/glitch/GlitchPattern.h"

#include "dsp/DamageCurves.h"
#include "dsp/common/SeedRng.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace inkcap::dsp::glitch
{

namespace
{
    struct LengthRange
    {
        double shortest, longest;       // seconds at Length 0 and 1 (log in between)
        double lowest, highest;         // what Chaos may stretch it to
    };

    constexpr LengthRange lengthRange (GlitchType type) noexcept
    {
        switch (type)
        {
            case GlitchType::chew:  return { 0.060, 0.600, 0.040, 0.800 };
            case GlitchType::stall: return { 0.050, 0.700, 0.040, 0.900 };
            case GlitchType::bend:  return { 0.080, 0.800, 0.060, 1.000 };
            case GlitchType::skip:
            case GlitchType::any:   break;
        }
        return { 0.020, 0.120, 0.020, 0.120 };
    }

    int64_t floorMod (int64_t a, int64_t n) noexcept { return a - Grid::floorDiv (a, n) * n; }

    /** 2^(chaos * spread * a bipolar draw): Chaos stretches a value by up to `octaves` either way. */
    double variation (const SeedRng& lane, int64_t slot, float chaos, double octaves) noexcept
    {
        return std::exp2 ((double) chaos * octaves * lane.bipolar (slot));
    }

    GlitchType pickType (uint32_t seed, const PatternParams& p, int64_t slot, int64_t ordinal) noexcept
    {
        if (p.type != GlitchType::any)
            return p.type;

        const SeedRng rng (seed, Stream::glitchType);

        // Chaos 0: the seed's own order of the four, one after the other.
        std::array<GlitchType, 4> order { GlitchType::skip, GlitchType::chew, GlitchType::stall, GlitchType::bend };
        const auto shuffle = rng.lane (0);
        for (int j = 3; j > 0; --j)
            std::swap (order[(size_t) j], order[(size_t) shuffle.below (j, j + 1)]);

        if (! rng.chance (slot, p.chaos))
            return order[(size_t) floorMod (ordinal, 4)];

        // Chaos up: a free pick, and the wilder failures (Stall, Bend) more likely.
        const std::array<double, 4> weights { 1.0, 1.0, 0.5 + p.chaos, 0.5 + p.chaos };
        auto u = rng.lane (1).uniform (slot) * (weights[0] + weights[1] + weights[2] + weights[3]);
        for (size_t i = 0; i < 3; ++i)
        {
            if (u < weights[i])
                return (GlitchType) i;
            u -= weights[i];
        }
        return GlitchType::bend;
    }
}

PatternParams patternFor (const GlitchSettings& s, float damage, float chaos) noexcept
{
    const auto i = moduleIntensity (Module::glitch, s.amount, damage);

    PatternParams p;
    p.density = maxDensity * i * i;
    p.severity = 0.35f + 0.65f * i;
    p.chaos = chaosAmount (chaos);
    p.type = s.type;
    p.length = std::clamp (s.length, 0.0f, 1.0f);
    p.grid.slotBeats = glitchRates[(size_t) std::clamp (s.rate, 0, numGlitchRates - 1)].beats;
    p.grid.swing = std::clamp ((double) s.groove, 0.5, 0.75);
    return p;
}

int periodFor (float density) noexcept
{
    if (! (density > 0.0f))
        return 0;

    const auto wanted = std::log (1.0 / (double) density);
    auto best = steadyPeriods.front();
    for (const auto n : steadyPeriods)
        if (std::abs (std::log ((double) n) - wanted) < std::abs (std::log ((double) best) - wanted))
            best = n;
    return best;
}

float meanDensity (const PatternParams& p) noexcept
{
    const auto n = periodFor (p.density);
    return n == 0 ? 0.0f : (1.0f - p.chaos) / (float) n + p.chaos * p.density;
}

double lengthSecondsFor (GlitchType type, float position) noexcept
{
    const auto r = lengthRange (type);
    return r.shortest * std::pow (r.longest / r.shortest, (double) std::clamp (position, 0.0f, 1.0f));
}

float lengthPositionFor (GlitchType type, double seconds) noexcept
{
    const auto r = lengthRange (type);
    const auto clamped = std::clamp (seconds, r.shortest, r.longest);
    return (float) (std::log (clamped / r.shortest) / std::log (r.longest / r.shortest));
}

std::optional<Event> eventAt (uint32_t seed, const PatternParams& p, int64_t slot) noexcept
{
    const auto n = periodFor (p.density);
    if (n == 0)
        return std::nullopt;

    const SeedRng slots (seed, Stream::glitchSlots);
    const auto offset = slots.lane (0).below (n, n);
    const auto relative = slot - offset;
    const auto steady = floorMod (relative, n) == 0;

    // Chaos 0: exactly the steady slots. Chaos 1: each slot fires with the density's probability.
    const auto probability = (1.0 - p.chaos) * (steady ? 1.0 : 0.0) + (double) p.chaos * p.density;
    if (! slots.chance (slot, probability))
        return std::nullopt;

    Event e;
    e.slot = slot;
    e.startBeat = p.grid.slotStart (slot);
    e.type = pickType (seed, p, slot, Grid::floorDiv (relative, n));

    const SeedRng shape (seed, Stream::glitchShape);
    const auto range = lengthRange (e.type);
    e.lengthSeconds = std::clamp (lengthSecondsFor (e.type, p.length) * variation (shape.lane (0), slot, p.chaos, 1.0),
                                  range.lowest, range.highest);

    switch (e.type)
    {
        case GlitchType::skip:
        {
            const auto drawn = 2 + shape.lane (1).below (slot, 7);
            e.repeats = std::clamp ((int) std::lround (4.0 + p.chaos * (drawn - 4)), 2, 8);
            break;
        }
        case GlitchType::chew:
            e.depth = std::clamp ((float) (6.0 + 24.0 * p.severity * variation (shape.lane (2), slot, p.chaos, 0.5)), 6.0f, 30.0f);
            break;
        case GlitchType::stall:
            e.holdSeconds = std::min (0.45, p.chaos * shape.lane (3).uniform (slot) * 0.5 * e.lengthSeconds);
            e.restartSeconds = std::min (0.6, 0.6 * e.lengthSeconds * variation (shape.lane (4), slot, p.chaos, 0.5));
            break;
        case GlitchType::bend:
            e.depth = std::clamp ((float) (1.0 + 11.0 * p.severity * variation (shape.lane (2), slot, p.chaos, 0.5)), 1.0f, 12.0f);
            break;
        case GlitchType::any:
            break;
    }
    return e;
}

double durationSeconds (const Event& e) noexcept
{
    switch (e.type)
    {
        case GlitchType::skip:  return e.lengthSeconds * e.repeats + skipReturnSeconds;
        case GlitchType::chew:  return e.lengthSeconds;
        case GlitchType::stall: return e.lengthSeconds + e.holdSeconds + e.restartSeconds + catchUpSeconds;
        case GlitchType::bend:  return e.lengthSeconds + catchUpSeconds;
        case GlitchType::any:   break;
    }
    return 0.0;
}

} // namespace inkcap::dsp::glitch
