// The audio thread allocates nothing: every operator new on this thread is counted while
// RoomModule::process runs, through every type, seed, transport jump and change (CLAUDE.md, "Audio thread").

#include "dsp/modules/room/RoomModule.h"

#include "support/Analysis.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <new>

namespace
{
    thread_local bool counting = false;
    thread_local long allocations = 0;

    void* allocate (std::size_t size, bool mayThrow)
    {
        if (counting)
            ++allocations;
        if (void* p = std::malloc (size > 0 ? size : 1))
            return p;
        if (mayThrow)
            throw std::bad_alloc();
        return nullptr;
    }
} // namespace

// The unaligned forms, replaced as a matched set; the aligned ones keep their defaults.
void* operator new (std::size_t size) { return allocate (size, true); }
void* operator new[] (std::size_t size) { return allocate (size, true); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size, false); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size, false); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }

using namespace inkcap;
using namespace inkcap::dsp;

TEST_CASE ("Room: process allocates nothing", "[room][realtime]")
{
    constexpr double fs = 48000.0;
    constexpr int block = 256;
    RoomModule module;
    module.prepare ({ fs, block, 2 });
    GridClock clock;
    clock.prepare (fs);

    auto in = analysis::programme (fs, 8.0, 3);
    long counted = 0;
    for (int64_t start = 0; start + block <= (int64_t) in.size(); start += block)
    {
        const auto t = (double) start / fs;
        RoomSettings s;
        s.on = t < 7.0;
        s.amount = 1.0f;
        s.type = (decltype (s.type)) ((int) (t * 1.7) % 4);
        s.size = (float) (0.5 + 0.5 * std::sin (t));
        s.duck = t > 3.0 ? 0.7f : 0.0f;
        if (t > 5.0)
            std::fill (in.left.begin() + start, in.left.begin() + start + block, 0.0f);     // silence: the module may stop working
        if (t > 5.0)
            std::fill (in.right.begin() + start, in.right.begin() + start + block, 0.0f);

        // The host plays, then loops back to bar 3 at 4 s; the seed changes at 2.5 s; Damage dips to 0.
        const auto position = t < 4.0 ? start : start - (int64_t) (2.0 * fs);
        dsp::HostPosition host;
        host.playing = host.hasPpq = host.hasSeconds = true;
        host.bpm = 120.0;
        host.ppq = (double) position / fs * 2.0;
        host.seconds = (double) position / fs;
        clock.advance (host, 120.0, block);
        const BlockContext context { clock, t > 1.0 && t < 1.5 ? 0.0f : 0.9f, (float) (0.5 + 0.5 * std::sin (2.0 * t)), t < 2.5 ? 1u : 2u };

        float* channels[] = { in.left.data() + start, in.right.data() + start };
        counting = true;
        module.process ({ channels, 2, block, clock.blockStart() }, s, context);
        counting = false;
        counted += allocations;
        allocations = 0;
    }
    CHECK (counted == 0);
}
