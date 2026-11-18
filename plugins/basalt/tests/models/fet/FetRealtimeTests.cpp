// FET on the audio thread's terms (CLAUDE.md, Audio thread): nothing allocates once prepared, no
// state goes denormal without flush-to-zero; and the [.perf] report against PLAN.md's budget.

#include "FetHarness.h"

#include "dsp/dynamics/Stereo.h"

#include <catch2/catch_test_macros.hpp>

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
using namespace basalt::testing::fet;

namespace
{
    constexpr double rate = 48000.0;
}

TEST_CASE ("FET: allocates nothing once prepared", "[fet][realtime]")
{
    // Every path: steady blocks, curve fades, time glides, All's lag glide, follow, seed, reset,
    // mono and linked stereo, look-ahead moving, random block sizes and settings.
    const auto n = samplesFor (6.0, rate);
    auto left = pinkNoise (n, 51u), right = pinkNoise (n, 52u);
    scale (left, 0.5);
    scale (right, 0.3);
    std::vector<double> la ((size_t) n), outL ((size_t) n), outR ((size_t) n);
    for (int i = 0; i < n; ++i)
        la[(size_t) i] = 240.0 + 200.0 * std::sin (2.0 * std::numbers::pi * 0.5 * i / rate);

    FetModel m;
    m.prepare (rate);

    std::mt19937 rng (5u);
    std::uniform_real_distribution<double> unit (0.0, 1.0);
    const double ratios[] { 1.0, 2.0, 4.0, 8.0, 12.0, 20.0, ref::infinity };

    long counted = 0;
    for (int start = 0; start < n;)
    {
        const auto len = std::min (n - start, 1 + (int) (unit (rng) * 700.0));
        auto p = params (-50.0 + 40.0 * unit (rng), ratios[(int) (unit (rng) * 7.0) % 7], 24.0 * unit (rng), 40.0 * unit (rng),
                         0.02 * std::pow (15000.0, unit (rng)), 5.0 * std::pow (1000.0, unit (rng)), unit (rng) > 0.5);
        p.linked = unit (rng) > 0.3;
        p.link = unit (rng);

        KeyBlock key;
        key.numChannels = unit (rng) > 0.2 ? 2 : 1;
        key.numSamples = len;
        key.channels = { left.data() + start, right.data() + start };
        key.lookahead = la.data() + start;
        double* out[] { outL.data() + start, outR.data() + start };
        const double seedDb[] { 3.0, 4.0 };
        const auto what = unit (rng);

        counting = true;
        allocations = 0;
        if (what < 0.05)
            m.reset();
        else if (what < 0.1)
            m.follow (p, key);
        else if (what < 0.12)
            m.seed (seedDb, 2);
        m.process (p, key, out);
        (void) m.staticReduction (p, -20.0);
        (void) m.settleMs (p);
        counting = false;
        counted += allocations;

        start += len;
    }

    REQUIRE (counted == 0);
}

TEST_CASE ("FET: nothing goes denormal after a release, without flush-to-zero", "[fet][realtime]")
{
    // The slowest settings and All: every state decays as long as it can before it snaps.
    auto key = pinkNoise (samplesFor (3.0, rate), 4u);
    scale (key, 0.5);
    append (key, silence (samplesFor (150.0, rate)));

    for (const auto ratio : { 4.0, ref::infinity })
    {
        auto p = params (-40.0, ratio, 6.0, 40.0, 300.0, 5000.0);
        p.linked = true;
        auto m = prepared (rate);
        const auto r = runModel (m, p, key, &key, 4096);
        for (const auto& channel : r)
            for (const auto v : channel)
                REQUIRE (std::fpclassify (v) != FP_SUBNORMAL);
        REQUIRE (myzel::dsp::isExactly (r[0].back(), 0.0));
        REQUIRE (myzel::dsp::isExactly (m.reduction (1), 0.0));
    }
}

TEST_CASE ("FET: CPU, stereo at 48 kHz", "[.perf]")
{
    // PLAN.md, Performance budget: FET ≤ 0.5 % of a core at 48 kHz stereo, Character off. The
    // model plus the gain conversion and the matrix, not the chain; the fastest of five passes,
    // in process CPU time, on 20 s of pink noise into compression.
    const auto n = samplesFor (20.0, rate);
    auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
    scale (left, 0.5);
    scale (right, 0.5);
    std::vector<double> outL ((size_t) n), outR ((size_t) n);
    std::vector<float> gainL (512), gainR (512), audioL ((size_t) n, 0.1f), audioR ((size_t) n, 0.1f);

    struct Setting
    {
        const char* name;
        double ratio, attackMs, releaseMs, laSamples;
        bool linked, fading = false;
    };
    const Setting settings[] {
        { "defaults (4:1, Attack 10 ms, Release 150 ms), Linked", 4.0, 10.0, 150.0, 0.0, true },
        { "defaults, Linked, look-ahead 5 ms", 4.0, 10.0, 150.0, 240.0, true },
        { "20:1, Attack 0.02 ms, Release 50 ms, Unlinked", 20.0, 0.02, 50.0, 0.0, false },
        { "All, Attack 0.02 ms, Release 50 ms, Linked, look-ahead 10 ms", ref::infinity, 0.02, 50.0, 480.0, true },
        { "defaults, Linked, Threshold moving every 20 ms (always two loops)", 4.0, 10.0, 150.0, 0.0, true, true },
    };

    for (const auto& s : settings)
    {
        auto p = params (-30.0, s.ratio, 6.0, 40.0, s.attackMs, s.releaseMs);
        p.linked = s.linked;
        const std::vector<double> lookahead ((size_t) n, s.laSamples);

        auto best = 1.0e9;
        for (int pass = 0; pass < 5; ++pass)
        {
            FetModel m;
            m.prepare (rate);
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
                if (s.fading)
                    p.curve.thresholdDb = (start / 960) % 2 == 0 ? -30.0 : -24.0;
                m.process (p, key, out);

                for (int i = 0; i < len; ++i)
                {
                    gainL[(size_t) i] = gainFor (outL[(size_t) (start + i)]);
                    gainR[(size_t) i] = gainFor (outR[(size_t) (start + i)]);
                }
                float* audio[] { audioL.data() + start, audioR.data() + start };
                const float* gains[] { gainL.data(), gainR.data() };
                applyGains (s.linked ? StereoMode::linked : StereoMode::unlinked, audio, 2, gains, len);
            }
            best = std::min (best, (double) (std::clock() - t0) / CLOCKS_PER_SEC);
        }

        std::printf ("FET, stereo, 48 kHz, %s: %.3f %% of a core (budget 0.5 %%), %.1f ns a stereo frame\n", s.name, 100.0 * best / 20.0,
                     1.0e9 * best / n);
    }
}
