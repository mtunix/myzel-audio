/*
    Glitch's pattern: which slots fire and what each event plays, a pure function of the seed,
    the parameters and the slot. Chaos 0 is a fixed pattern that repeats with its period; Chaos 1
    a probability per slot; Damage 0 never fires.
*/

#include "dsp/modules/glitch/GlitchControls.h"
#include "dsp/modules/glitch/GlitchPattern.h"

#include "common/dsp/FloatCompare.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <vector>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::dsp::glitch;
using myzel::dsp::isExactly;

namespace
{
    constexpr uint32_t seed = 0x4393'0A3Du;

    PatternParams pattern (GlitchType type, float damage, float chaos, float amount = 1.0f, float length = 0.5f)
    {
        GlitchSettings s;
        s.on = true;
        s.amount = amount;
        s.type = type;
        s.length = length;
        return patternFor (s, damage, chaos);
    }

    bool sameEvent (const Event& a, const Event& b)
    {
        return a.slot == b.slot && a.type == b.type && isExactly (a.lengthSeconds, b.lengthSeconds) && a.repeats == b.repeats
               && isExactly (a.holdSeconds, b.holdSeconds) && isExactly (a.restartSeconds, b.restartSeconds) && isExactly (a.depth, b.depth)
               && isExactly (a.startBeat, b.startBeat);
    }
}

TEST_CASE ("Glitch pattern: Damage 0 and Amount 0 never fire", "[glitch][pattern]")
{
    for (const auto chaos : { 0.0f, 0.5f, 1.0f })
        for (int type = 0; type < 5; ++type)
        {
            const auto none = pattern ((GlitchType) type, 0.0f, chaos);
            const auto silent = pattern ((GlitchType) type, 1.0f, chaos, 0.0f);
            CHECK (periodFor (none.density) == 0);
            bool fired = false;
            for (int64_t k = -500; k < 5000; ++k)
                fired = fired || eventAt (seed, none, k).has_value() || eventAt (seed, silent, k).has_value();
            CHECK_FALSE (fired);
        }
}

TEST_CASE ("Glitch pattern: Chaos 0 fires every Nth slot and repeats exactly with its period", "[glitch][pattern]")
{
    for (const auto damage : { 0.2f, 0.5f, 0.8f, 1.0f })
        for (int type = 0; type < 5; ++type)
        {
            const auto p = pattern ((GlitchType) type, damage, 0.0f);
            const auto n = periodFor (p.density);
            REQUIRE (n >= 2);

            std::vector<int64_t> fired;
            for (int64_t k = -64; k < 4096; ++k)
                if (eventAt (seed, p, k))
                    fired.push_back (k);

            INFO ("damage " << damage << ", type " << type << ", period " << n);
            REQUIRE (fired.size() > 10);
            bool regular = true;
            for (size_t i = 1; i < fired.size(); ++i)
                regular = regular && fired[i] - fired[i - 1] == n;
            CHECK (regular);

            // The event list repeats with the period (Any: the seed's order of four, so 4N).
            const auto cycle = (GlitchType) type == GlitchType::any ? 4 * n : n;
            bool repeats = true;
            for (const auto k : fired)
            {
                const auto a = eventAt (seed, p, k), b = eventAt (seed, p, k + cycle);
                REQUIRE (b.has_value());
                auto shifted = *a;
                shifted.slot = b->slot;
                shifted.startBeat = b->startBeat;
                repeats = repeats && sameEvent (shifted, *b);
            }
            CHECK (repeats);
        }
}

TEST_CASE ("Glitch pattern: the steady period follows the density in musical steps", "[glitch][pattern]")
{
    CHECK (periodFor (0.5f) == 2);
    CHECK (periodFor (0.25f) == 4);
    CHECK (periodFor (1.0f / 16.0f) == 16);
    CHECK (periodFor (1.0f / 20.0f) == 24);
    CHECK (periodFor (1.0e-4f) == 128);

    // More Damage, more glitches: the period never grows with the density.
    int last = 1 << 20;
    for (float d = 0.05f; d <= 1.0f; d += 0.05f)
    {
        const auto n = periodFor (pattern (GlitchType::skip, d, 0.0f).density);
        CHECK (n <= last);
        last = n;
    }
    CHECK (periodFor (pattern (GlitchType::skip, 1.0f, 0.0f).density) == 2);
}

TEST_CASE ("Glitch pattern: Chaos 1 fires each slot with the density's probability", "[glitch][pattern]")
{
    const auto p = pattern (GlitchType::skip, 0.6f, 1.0f);
    int fired = 0;
    constexpr int slots = 40000;
    std::set<int64_t> gaps;
    int64_t last = 0;
    for (int64_t k = 0; k < slots; ++k)
        if (eventAt (seed, p, k))
        {
            ++fired;
            gaps.insert (k - last);
            last = k;
        }

    const auto expected = p.density * slots;
    INFO ("fired " << fired << ", expected " << expected);
    CHECK (std::abs (fired - expected) < 4.0 * std::sqrt (expected));
    CHECK (gaps.size() > 10);     // irregular
}

TEST_CASE ("Glitch pattern: Any plays the seed's order of the four at Chaos 0, and draws them at Chaos 1", "[glitch][pattern]")
{
    const auto steady = pattern (GlitchType::any, 1.0f, 0.0f);
    std::vector<GlitchType> order;
    for (int64_t k = 0; order.size() < 8; ++k)
        if (const auto e = eventAt (seed, steady, k))
            order.push_back (e->type);

    std::set<GlitchType> four (order.begin(), order.begin() + 4);
    CHECK (four.size() == 4);
    CHECK_FALSE (four.contains (GlitchType::any));
    for (size_t i = 4; i < order.size(); ++i)
        CHECK (order[i] == order[i - 4]);

    // Another seed, another order (for most seeds).
    int differ = 0;
    for (uint32_t other = 1; other < 40; ++other)
    {
        std::vector<GlitchType> o;
        for (int64_t k = 0; o.size() < 4; ++k)
            if (const auto e = eventAt (other, steady, k))
                o.push_back (e->type);
        differ += o != std::vector<GlitchType> (order.begin(), order.begin() + 4) ? 1 : 0;
    }
    CHECK (differ > 25);

    std::map<GlitchType, int> counts;
    const auto wild = pattern (GlitchType::any, 1.0f, 1.0f);
    for (int64_t k = 0; k < 20000; ++k)
        if (const auto e = eventAt (seed, wild, k))
            ++counts[e->type];
    CHECK (counts.size() == 4);
    CHECK (counts[GlitchType::stall] > counts[GlitchType::skip]);     // the wilder failures weigh more at full Chaos
}

TEST_CASE ("Glitch pattern: every event stays within its type's ranges and the longest event", "[glitch][pattern]")
{
    for (int type = 0; type < 4; ++type)
        for (const auto length : { 0.0f, 0.5f, 1.0f })
            for (const auto chaos : { 0.0f, 1.0f })
            {
                const auto p = pattern ((GlitchType) type, 1.0f, chaos, 1.0f, length);
                for (int64_t k = 0; k < 4000; ++k)
                    if (const auto e = eventAt (seed, p, k))
                    {
                        INFO ("type " << type << ", length " << length << ", chaos " << chaos << ", slot " << k);
                        CHECK (durationSeconds (*e) <= longestEventSeconds);
                        CHECK (e->lengthSeconds > 0.0);
                        switch (e->type)
                        {
                            case GlitchType::skip:
                                CHECK ((e->lengthSeconds >= 0.02 && e->lengthSeconds <= 0.12));
                                CHECK ((e->repeats >= 2 && e->repeats <= 8));
                                break;
                            case GlitchType::chew:  CHECK ((e->depth >= 6.0f && e->depth <= 30.0f)); break;
                            case GlitchType::bend:  CHECK ((e->depth >= 1.0f && e->depth <= 12.0f)); break;
                            case GlitchType::stall: CHECK (e->restartSeconds > 0.0); break;
                            case GlitchType::any:   FAIL ("Any is never an event's type"); break;
                        }
                    }
            }

    // Chaos 0: Skip repeats 4 times, Length alone sets the piece.
    const auto p = pattern (GlitchType::skip, 1.0f, 0.0f, 1.0f, 0.0f);
    for (int64_t k = 0; k < 100; ++k)
        if (const auto e = eventAt (seed, p, k))
        {
            CHECK (e->repeats == 4);
            CHECK (std::abs (e->lengthSeconds - 0.02) < 1.0e-12);
        }
}

TEST_CASE ("Glitch pattern: a pure function of seed, parameters and slot; the grid's swing moves the start", "[glitch][pattern]")
{
    auto p = pattern (GlitchType::any, 0.9f, 0.6f);
    for (int64_t k = 0; k < 2000; ++k)
    {
        const auto a = eventAt (seed, p, k), b = eventAt (seed, p, k);
        REQUIRE (a.has_value() == b.has_value());
        if (a)
            CHECK (sameEvent (*a, *b));
    }

    int differ = 0;
    for (int64_t k = 0; k < 2000; ++k)
        differ += eventAt (seed, p, k).has_value() != eventAt (seed + 1, p, k).has_value() ? 1 : 0;
    CHECK (differ > 100);

    p.grid.swing = 0.7;
    for (int64_t k = 1; k < 200; k += 2)
        if (const auto e = eventAt (seed, p, k))
            CHECK (std::abs (e->startBeat - ((double) (k - 1) * p.grid.slotBeats + 1.4 * p.grid.slotBeats)) < 1.0e-12);
}

TEST_CASE ("Glitch Length read-outs: each type's own time, typed back to where it was", "[glitch][readout]")
{
    CHECK (glitch::lengthText ((int) GlitchType::skip, 0.0f) == "20 ms");
    CHECK (glitch::lengthText ((int) GlitchType::skip, 100.0f) == "120 ms");
    CHECK (glitch::lengthText ((int) GlitchType::chew, 100.0f) == "600 ms");
    CHECK (glitch::lengthText ((int) GlitchType::stall, 0.0f) == "50 ms");
    CHECK (glitch::lengthText ((int) GlitchType::bend, 50.0f) == "253 ms");
    CHECK (glitch::lengthText ((int) GlitchType::any, 42.0f) == "42 %");

    CHECK (std::abs (glitch::lengthFromText ((int) GlitchType::skip, "120 ms") - 100.0f) < 0.01f);
    CHECK (std::abs (glitch::lengthFromText ((int) GlitchType::skip, "0.02 s") - 0.0f) < 0.01f);
    CHECK (std::abs (glitch::lengthFromText ((int) GlitchType::chew, "190") - 50.0f) < 0.2f);
    CHECK (std::abs (glitch::lengthFromText ((int) GlitchType::stall, "5 s") - 100.0f) < 0.01f);
    CHECK (glitch::lengthFromText ((int) GlitchType::bend, "slow") < 0.0f);
    CHECK (glitch::lengthFromText ((int) GlitchType::bend, "3 bars") < 0.0f);
    CHECK (std::abs (glitch::lengthFromText ((int) GlitchType::any, "30 %") - 30.0f) < 0.01f);

    for (int type = 0; type < 5; ++type)
        for (float p = 0.0f; p <= 100.0f; p += 7.0f)
        {
            const auto text = glitch::lengthText (type, p);
            INFO (type << " at " << p << ": " << text);
            CHECK (std::abs (glitch::lengthFromText (type, text) - p) < 2.1f);
        }
}
