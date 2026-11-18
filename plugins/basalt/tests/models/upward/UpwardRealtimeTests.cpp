// Upward on the audio thread's terms (CLAUDE.md, Audio thread): nothing allocates once prepared,
// whatever the knobs do. Plus the [.perf] report against PLAN.md's budget (≤ 0.3 % of a core).

#include "dynamics/Harness.h"

#include "dsp/dynamics/Stereo.h"
#include "dsp/models/upward/UpwardModel.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
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

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using basalt::models::UpwardModel;

namespace
{
    constexpr double rate = 48000.0;
}

TEST_CASE ("Upward: allocates nothing once prepared", "[upward][realtime]")
{
    // Random knobs every block (curve fades, Floor and time glides, Auto on and off, Link), random
    // block sizes and look-ahead, follow() and seed() as a switch would call them, the gain
    // conversion and the matrix.
    const auto n = samplesFor (4.0, rate);
    auto left = pinkNoise (n, 8u), right = pinkNoise (n, 9u);
    scale (left, 0.02);
    scale (right, 0.01);
    std::vector<double> outL ((size_t) n), outR ((size_t) n), la ((size_t) n);
    std::vector<float> gainL ((size_t) n), gainR ((size_t) n), audioL ((size_t) n, 0.1f), audioR ((size_t) n, -0.1f);

    UpwardModel upward;
    upward.prepare (rate);

    std::mt19937 rng (3u);
    std::uniform_real_distribution<double> unit (0.0, 1.0);

    long counted = 0;
    for (int start = 0, block = 0; start < n; ++block)
    {
        const auto len = std::min (1 + (int) (unit (rng) * 1024.0), n - start);

        ModelParams p;
        p.curve = { -60.0 * unit (rng), unit (rng) > 0.9 ? 0.0 : unit (rng), 24.0 * unit (rng), 40.0 * unit (rng) };
        p.floorDb = -90.0 + 60.0 * unit (rng);
        p.attackMs = 0.02 * std::pow (15000.0, unit (rng));
        p.releaseMs = 5.0 * std::pow (1000.0, unit (rng));
        p.autoRelease = unit (rng) > 0.3;
        p.linked = unit (rng) > 0.5;
        p.link = unit (rng);
        const auto lookahead = unit (rng) > 0.5 ? 0.0 : 480.0 * unit (rng);
        std::fill (la.begin() + start, la.begin() + start + len, lookahead);

        allocations = 0;
        counting = true;

        KeyBlock key;
        key.numChannels = 2;
        key.numSamples = len;
        key.channels = { left.data() + start, right.data() + start };
        key.lookahead = la.data() + start;
        double* out[] { outL.data() + start, outR.data() + start };

        if (block % 7 == 3)
        {
            upward.follow (p, key);
            const double seed[] { -6.0, 3.0 };
            upward.seed (seed, 2);
        }
        else
        {
            upward.process (p, key, out);
        }

        for (int i = start; i < start + len; ++i)
        {
            gainL[(size_t) i] = gainFor (outL[(size_t) i]);
            gainR[(size_t) i] = gainFor (outR[(size_t) i]);
        }
        float* audio[] { audioL.data() + start, audioR.data() + start };
        const float* gains[] { gainL.data() + start, gainR.data() + start };
        applyGains (StereoMode::linked, audio, 2, gains, len);
        (void) autoGainDb (upward, p);
        (void) upward.reduction (1);

        counting = false;
        counted += allocations;
        start += len;
    }

    REQUIRE (counted == 0);
}

TEST_CASE ("Upward: CPU, stereo at 48 kHz", "[.perf]")
{
    // PLAN.md, Performance budget: Upward ≤ 0.3 % of a core at 48 kHz stereo, Character off. As
    // Clean's figure: the model plus the gain conversion and the matrix, not the chain; the fastest
    // of five passes, in process CPU time. Pink noise around the lift region, so the curve's log
    // runs on most samples (the costly case); and a loud key, where most samples skip it.
    const auto n = samplesFor (20.0, rate);
    std::vector<double> outL ((size_t) n), outR ((size_t) n);
    std::vector<float> gainL (512), gainR (512), audioL ((size_t) n, 0.1f), audioR ((size_t) n, 0.1f);

    struct Case
    {
        const char* name;
        double keyGain, lookaheadSamples;
        bool autoRelease, linked;
    };

    for (const auto& c : { Case { "defaults (Linked, Auto on), key in the lift region", 0.01, 0.0, true, true },
                           Case { "look-ahead 5 ms, key in the lift region", 0.01, 240.0, true, true },
                           Case { "Unlinked, Auto off, key in the lift region", 0.01, 0.0, false, false },
                           Case { "defaults, key over Threshold", 0.5, 0.0, true, true } })
    {
        auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
        scale (left, c.keyGain);
        scale (right, c.keyGain);
        const std::vector<double> lookahead ((size_t) n, c.lookaheadSamples);

        ModelParams p;
        p.curve = { -18.0, 0.25, 6.0, 40.0 };
        p.floorDb = -70.0;
        p.attackMs = 10.0;
        p.releaseMs = 150.0;
        p.autoRelease = c.autoRelease;
        p.linked = c.linked;

        auto best = 1.0e9;
        auto lifted = 0.0;
        for (int pass = 0; pass < 5; ++pass)
        {
            UpwardModel upward;
            upward.prepare (rate);
            lifted = 0.0;
            const auto t0 = std::clock();
            for (int start = 0; start < n; start += 512)
            {
                const auto len = std::min (512, n - start);
                KeyBlock key;
                key.numChannels = 2;
                key.numSamples = len;
                key.channels = { left.data() + start, right.data() + start };
                key.lookahead = lookahead.data() + start;
                double* out[] { outL.data() + start, outR.data() + start };
                upward.process (p, key, out);

                for (int i = 0; i < len; ++i)
                {
                    gainL[(size_t) i] = gainFor (outL[(size_t) (start + i)]);
                    gainR[(size_t) i] = gainFor (outR[(size_t) (start + i)]);
                }
                float* audio[] { audioL.data() + start, audioR.data() + start };
                const float* gains[] { gainL.data(), gainR.data() };
                applyGains (p.linked ? StereoMode::linked : StereoMode::unlinked, audio, 2, gains, len);
                lifted = std::min (lifted, outL[(size_t) (start + len - 1)]);
            }
            best = std::min (best, (double) (std::clock() - t0) / CLOCKS_PER_SEC);
        }

        std::printf ("Upward, stereo, 48 kHz, %s: %.3f %% of a core (largest lift at a block end %.1f dB)\n", c.name, 100.0 * best / 20.0, -lifted);
    }
}
