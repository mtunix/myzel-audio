#include "dsp/common/EventRing.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

using halite::dsp::EssEvent;

namespace
{
    EssEvent eventNumber (std::int64_t k)
    {
        return { k * 100, k * 100 + 50, (float) k, -(float) k };
    }

    bool isEventNumber (const EssEvent& e, std::int64_t k)
    {
        const auto expected = eventNumber (k);
        return e.start == expected.start && e.end == expected.end
            && std::abs (e.peakReductionDb - expected.peakReductionDb) < 1.0e-3f
            && std::abs (e.peakSibilanceDb - expected.peakSibilanceDb) < 1.0e-3f;
    }
}

TEST_CASE ("EventRing: events arrive in order, and the reader catches up", "[detector][events]")
{
    halite::dsp::EventRingOf<8> ring;
    std::uint64_t cursor = 0;
    std::array<EssEvent, 16> out {};

    CHECK (ring.read (cursor, out.data(), (int) out.size()) == 0);

    for (int k = 0; k < 5; ++k)
        ring.push (eventNumber (k));
    CHECK (ring.count() == 5);

    // Fewer than asked for at once, then the rest.
    REQUIRE (ring.read (cursor, out.data(), 3) == 3);
    for (int k = 0; k < 3; ++k)
        CHECK (isEventNumber (out[(size_t) k], k));
    REQUIRE (ring.read (cursor, out.data(), (int) out.size()) == 2);
    CHECK (isEventNumber (out[0], 3));
    CHECK (isEventNumber (out[1], 4));
    CHECK (cursor == 5);

    // A reader that fell more than the capacity behind gets the newest events, oldest first.
    for (int k = 5; k < 30; ++k)
        ring.push (eventNumber (k));
    const auto n = ring.read (cursor, out.data(), (int) out.size());
    REQUIRE (n == 8);
    for (int k = 0; k < n; ++k)
        CHECK (isEventNumber (out[(size_t) k], 22 + k));
    CHECK (cursor == 30);
}

TEST_CASE ("EventRing: a reader on another thread never sees a torn event", "[detector][events]")
{
    halite::dsp::EventRingOf<16> ring;
    constexpr std::int64_t total = 200000;
    std::atomic<bool> done { false };

    std::thread writer ([&]
    {
        for (std::int64_t k = 0; k < total; ++k)
            ring.push (eventNumber (k));
        done.store (true);
    });

    std::uint64_t cursor = 0;
    std::array<EssEvent, 16> out {};
    long torn = 0, read = 0;
    std::int64_t last = -1;
    auto ordered = true;

    while (! done.load() || cursor < ring.count())
    {
        const auto n = ring.read (cursor, out.data(), (int) out.size());
        for (int i = 0; i < n; ++i)
        {
            const auto k = out[(size_t) i].start / 100;
            torn += isEventNumber (out[(size_t) i], k) ? 0 : 1;
            ordered = ordered && k > last;
            last = k;
            ++read;
        }
    }
    writer.join();

    CHECK (torn == 0);
    CHECK (ordered);
    CHECK (read > 0);
    CHECK (last == total - 1);
}
