/*
    GridClock: the song position of every sample, the same to the bit for any block size; jumps
    found where the host's timeline breaks; slots with swing, exact on their boundaries.
*/

#include "dsp/common/GridClock.h"

#include "common/dsp/FloatCompare.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace inkcap::dsp;
using myzel::dsp::isExactly;

namespace
{
    constexpr double fs = 48000.0;

    HostPosition playingAt (double ppq, double bpm = 120.0)
    {
        HostPosition h;
        h.hasPpq = h.playing = true;
        h.ppq = ppq;
        h.bpm = bpm;
        h.hasSeconds = true;
        h.seconds = ppq * 60.0 / bpm;
        return h;
    }

    /** Positions of samples 0 .. n-1 with a host that plays from ppq 0 at 120 BPM, in blocks of `block`. */
    std::vector<double> positions (int n, int block)
    {
        GridClock clock;
        clock.prepare (fs);
        std::vector<double> ppq;
        for (int start = 0; start < n; start += block)
        {
            const auto len = std::min (block, n - start);
            clock.advance (playingAt ((double) start * 120.0 / 60.0 / fs), 120.0, len);
            for (int i = 0; i < len; ++i)
                ppq.push_back (clock.ppqAt (clock.blockStart() + i));
        }
        return ppq;
    }
}

TEST_CASE ("GridClock: positions are the same to the bit for any block size", "[clock]")
{
    const auto reference = positions (20000, 20000);
    for (const auto block : { 1, 7, 64, 333, 4096 })
    {
        const auto p = positions (20000, block);
        bool same = true;
        for (size_t i = 0; i < p.size(); ++i)
            same = same && isExactly (p[i], reference[i]);
        INFO ("block " << block);
        CHECK (same);
    }
    CHECK (std::abs (reference[4000] - 4000.0 * 120.0 / 60.0 / fs) < 1.0e-15);
}

TEST_CASE ("GridClock: the first block, play, stop, a locate and a loop are jumps; a tempo change is not", "[clock]")
{
    GridClock clock;
    clock.prepare (fs);

    clock.advance (playingAt (16.0), 120.0, 480);
    CHECK (clock.jumped());
    CHECK (clock.isFollowingHost());
    CHECK (isExactly (clock.ppqAt (0), 16.0));

    clock.advance (playingAt (16.0 + 480.0 / 24000.0), 120.0, 480);
    CHECK_FALSE (clock.jumped());

    // The host reports a position within a sample of ours: still the same timeline, our anchor.
    clock.advance (playingAt (16.0 + 960.0 / 24000.0 + 1.0e-6), 120.0, 480);
    CHECK_FALSE (clock.jumped());
    CHECK (isExactly (clock.ppqAt (960), 16.0 + 960.0 / 24000.0));

    // A loop back to bar 5.
    clock.advance (playingAt (16.0), 120.0, 480);
    CHECK (clock.jumped());
    CHECK (isExactly (clock.ppqAt (clock.blockStart()), 16.0));

    // A tempo change: the position carries on, at the new tempo from here.
    const auto before = clock.ppqAt (clock.blockStart() + 480);
    clock.advance (playingAt (before, 90.0), 120.0, 480);
    CHECK_FALSE (clock.jumped());
    CHECK (isExactly (clock.getBpm(), 90.0));
    CHECK (std::abs (clock.ppqAt (clock.blockStart() + 480) - (before + 480.0 * 90.0 / 60.0 / fs)) < 1.0e-12);

    // Stop: free-running from 0 at the host's tempo.
    HostPosition stopped;
    stopped.bpm = 100.0;
    clock.advance (stopped, 120.0, 480);
    CHECK (clock.jumped());
    CHECK_FALSE (clock.isFollowingHost());
    CHECK (isExactly (clock.ppqAt (clock.blockStart()), 0.0));
    CHECK (isExactly (clock.getBpm(), 100.0));
    clock.advance (stopped, 120.0, 480);
    CHECK_FALSE (clock.jumped());

    // No tempo from the host: the fallback BPM; a new fallback re-anchors without a jump.
    clock.advance (HostPosition {}, 140.0, 480);
    CHECK_FALSE (clock.jumped());
    CHECK (isExactly (clock.getBpm(), 140.0));
    const auto runningAt = clock.ppqAt (clock.blockStart());
    CHECK (std::abs (runningAt - 960.0 * 100.0 / 60.0 / fs) < 1.0e-12);

    // Play again.
    clock.advance (playingAt (32.0), 120.0, 480);
    CHECK (clock.jumped());
    CHECK (clock.isFollowingHost());

    // reset(): the next block jumps, and samples count from 0 again.
    clock.reset();
    clock.advance (playingAt (32.0 + 480.0 / 24000.0), 120.0, 64);
    CHECK (clock.jumped());
    CHECK (clock.blockStart() == 0);
}

TEST_CASE ("GridClock: garbage from the host never breaks the position", "[clock]")
{
    GridClock clock;
    clock.prepare (44100.0);

    auto h = playingAt (8.0, 0.0);
    h.bpm = -5.0;
    clock.advance (h, 0.0, 128);
    CHECK (clock.getBpm() >= GridClock::minBpm);

    h.ppq = std::nan ("");
    clock.advance (h, 120.0, 128);
    CHECK (clock.jumped());
    CHECK_FALSE (clock.isFollowingHost());
    CHECK (std::isfinite (clock.ppqAt (clock.blockStart() + 64)));
}

TEST_CASE ("Grid: slots with swing, exact on their boundaries, before bar 1 too", "[clock]")
{
    Grid g { 0.25, 0.5 };
    CHECK (isExactly (g.slotStart (0), 0.0));
    CHECK (isExactly (g.slotStart (5), 1.25));
    CHECK (isExactly (g.slotStart (-1), -0.25));
    CHECK (g.slotAt (1.25) == 5);
    CHECK (g.slotAt (1.25 - 1.0e-12) == 5);    // double rounding below a boundary still counts as on it
    CHECK (g.slotAt (1.2499) == 4);
    CHECK (g.slotAt (-0.1) == -1);
    CHECK (g.firstSlotFrom (1.25) == 5);
    CHECK (g.firstSlotFrom (1.2501) == 6);

    // 66 % swing: the off-beat slot starts at 2/3 of the pair.
    Grid swung { 0.25, 2.0 / 3.0 };
    CHECK (std::abs (swung.slotStart (1) - 1.0 / 3.0) < 1.0e-15);
    CHECK (std::abs (swung.slotLength (0) - 1.0 / 3.0) < 1.0e-15);
    CHECK (std::abs (swung.slotLength (1) - 1.0 / 6.0) < 1.0e-15);
    CHECK (swung.slotAt (0.3) == 0);
    CHECK (swung.slotAt (0.34) == 1);
    CHECK (swung.slotAt (0.5) == 2);
    CHECK (swung.slotAt (-0.1) == -1);
    CHECK (swung.slotAt (-0.2) == -2);

    for (int64_t k = -40; k < 40; ++k)
    {
        INFO ("slot " << k);
        CHECK (swung.slotAt (swung.slotStart (k)) == k);
        CHECK (swung.firstSlotFrom (swung.slotStart (k)) == k);
    }
}

TEST_CASE ("GridClock: a slot starts on the same sample whether the render began at bar 1 or bar 17", "[clock]")
{
    // Bar 17 at 120 BPM is sample 1 536 000. 1/16T slots fall between samples; the boundary
    // sample must not depend on where the timeline was anchored.
    const Grid g { 1.0 / 6.0, 0.5 };
    GridClock fromStart, fromBar17;
    fromStart.prepare (fs);
    fromBar17.prepare (fs);
    fromStart.advance (playingAt (0.0), 120.0, 64);
    fromBar17.advance (playingAt (64.0), 120.0, 64);

    const int64_t offset = 1536000;
    for (int64_t k = 384; k < 384 + 200; ++k)
    {
        const auto a = fromStart.sampleAt (g.slotStart (k));
        const auto b = fromBar17.sampleAt (g.slotStart (k)) + offset;
        INFO ("slot " << k);
        CHECK (a == b);
    }
}
