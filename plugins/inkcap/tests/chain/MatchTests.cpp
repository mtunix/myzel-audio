#include "dsp/chain/Match.h"
#include "dsp/chain/MatchTables.h"
#include "support/MatchGates.h"

#include "common/dsp/FloatCompare.h"

#include <catch2/catch_test_macros.hpp>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::gates;
using myzel::dsp::isExactly;

namespace
{
    /** One module alone at Amount 100 %, its type, its controls at their defaults. */
    ChainSettings alone (Module m, float damage, bool matchOn, int type = 0)
    {
        const auto c = defaultControls (m);
        return gates::alone (m, std::min (type, (int) moduleInfo (m).types.size() - 1), c.a, c.b, 1.0f, damage, matchOn);
    }
}

TEST_CASE ("Match: each module keeps pink noise's loudness within 1 dB over the Damage range", "[chain][match][gate]")
{
    // The plan's gate, at each module's default controls; InkcapCalibrate --report sweeps every
    // type in 5 % steps and the controls between the table's points.
    for (const auto m : { Module::crush, Module::bits, Module::hiss, Module::room })
        for (int type = 0; type < (int) moduleInfo (m).types.size(); ++type)
            for (const auto damage : { 0.3f, 0.65f, 1.0f })
            {
                const auto matched = gainDb (alone (m, damage, true, type));
                INFO (moduleInfo (m).name << " " << moduleInfo (m).types[(size_t) type] << " at Damage " << damage << ": " << matched
                                          << " dB with Match");
                CHECK (std::abs (matched) <= 1.0);
            }

    // Where a module changes the level, Match is what keeps it.
    CHECK (gainDb (alone (Module::crush, 1.0f, false, (int) CrushType::fuzz)) < -3.0);
    CHECK (gainDb (alone (Module::bits, 1.0f, false, (int) BitsType::phone)) < -3.0);
}

TEST_CASE ("Match: Glitch's table follows the share of slots that fire", "[chain][match]")
{
    // At Chaos 0 the share is exactly 1 / N, a row of the table: no interpolation between periods.
    GlitchSettings g;
    g.on = true;
    g.type = GlitchType::chew;
    for (const auto damage : { 0.3f, 0.5f, 0.64f, 0.66f, 1.0f })
    {
        const auto p = glitch::patternFor (g, damage, 0.0f);
        const auto n = glitch::periodFor (p.density);
        REQUIRE (n > 0);
        CHECK (isExactly (glitch::meanDensity (p), 1.0f / (float) n));
    }

    // Chaos blends the steady pattern's share into the density.
    const auto p = glitch::patternFor (g, 0.8f, 0.5f);
    CHECK (std::abs (glitch::meanDensity (p) - (0.5f / (float) glitch::periodFor (p.density) + 0.5f * p.density)) < 1.0e-7f);

    for (const auto type : { GlitchType::chew, GlitchType::stall })
    {
        auto s = alone (Module::glitch, 1.0f, false, (int) type);
        const auto raw = gainDb (s);
        s.match = true;
        const auto matched = gainDb (s);
        INFO ((type == GlitchType::chew ? "Chew" : "Stall") << ": " << raw << " dB without Match, " << matched << " dB with it");
        CHECK (raw < -1.5);
        CHECK (std::abs (matched) <= 1.0);
    }
}

TEST_CASE ("Match: Damage 0 and Match off are exactly unity; boosts stop at the limit", "[chain][match]")
{
    for (int m = 0; m < numModules; ++m)
        CHECK (isExactly (match::compensationDb (alone ((Module) m, 0.0f, true)), 0.0f));

    auto s = everyModule (1.0f, 0.4f);
    CHECK (std::abs (match::compensationDb (s)) > 0.1f);
    s.match = false;
    CHECK (isExactly (match::compensationDb (s), 0.0f));

    // Every module where it would boost most: the sum stops at the limit.
    auto loud = alone (Module::bits, 1.0f, true, (int) BitsType::phone);
    loud.bits.rate = 0.0f;
    loud.bits.depth = 0.0f;
    loud.crush = { true, 1.0f, CrushType::cassette, 1.0f, -1.0f };
    loud.glitch = { true, 1.0f, GlitchType::stall, numGlitchRates - 1, 1.0f, 0.5f };
    CHECK (match::compensationDb (loud) <= match::maxBoostDb);
    for (const auto v : match::bitsTable.db)
        CHECK (v <= match::maxBoostDb);
    for (const auto v : match::glitchTable.db)
        CHECK (v <= match::maxBoostDb);

    // Between the grid's points the table interpolates: at a point, it is that point.
    const auto& t = match::crushTable;
    using T = match::ModuleTable;
    CHECK (isExactly (match::lookup (t, 1, { match::positionAt (2, T::aPoints), match::positionAt (5, T::bPoints) }, 1.0f),
                      t.db[(size_t) T::indexOf (1, 2, 5, T::intensityPoints - 1)]));
}

TEST_CASE ("Match: a level change glides, never steps", "[chain][match]")
{
    constexpr auto fs = loudness::calibrationRate;
    // A 330 Hz sine through Crush's Fuzz, Damage jumping between 0.2 and 1: Match moves by several
    // dB, along its 50 ms glide (the chain's click gate on its own terms).
    const auto x = analysis::sine (330.0, fs, 3.0, 0.3);
    auto s = alone (Module::crush, 0.2f, true, (int) CrushType::fuzz);
    const auto out = render (s, { x, x }, 256, fs, {}, [] (int64_t start, ChainSettings& c) { c.damage = (start / 24000) % 2 == 0 ? 0.2f : 1.0f; });
    CHECK (analysis::clickCrestDb (out.left, 4800) <= -60.0);
}
