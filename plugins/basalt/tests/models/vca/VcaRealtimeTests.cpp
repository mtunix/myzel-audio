// VCA on the audio thread's terms (CLAUDE.md, Audio thread): nothing allocates once prepared, no
// state goes denormal without flush-to-zero, and the [.perf] report against PLAN.md's budget.

#include "VcaTesting.h"

#include "dsp/dynamics/Stereo.h"

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
using namespace basalt::testing::vca;

TEST_CASE ("VCA: nothing allocates once prepared", "[vca][realtime]")
{
    // Random settings per block of random length, every stereo layout, look-ahead gliding, Auto
    // toggling, follow() and seed() as a switch makes them, and the static curve read.
    const auto n = samplesFor (4.0, rate);
    auto left = pinkNoise (n, 8u), right = pinkNoise (n, 9u);
    scale (left, 0.4);
    scale (right, 0.3);
    std::vector<double> la ((size_t) n), outL ((size_t) n), outR ((size_t) n);

    std::mt19937 rng (3u);
    std::uniform_real_distribution<double> unit (0.0, 1.0);

    for (const auto d : { Detector::peak, Detector::rms })
    {
        auto m = prepared (d);
        long counted = 0;
        auto lookahead = 0.0;

        for (int start = 0, block = 0; start < n; ++block)
        {
            const auto len = std::min (1 + (int) (unit (rng) * 1024.0), n - start);

            ModelParams p;
            p.curve = { -60.0 * unit (rng), unit (rng) > 0.9 ? 0.0 : unit (rng), 24.0 * unit (rng), 40.0 * unit (rng) };
            p.attackMs = 0.02 * std::pow (15000.0, unit (rng));
            p.releaseMs = 5.0 * std::pow (1000.0, unit (rng));
            p.autoRelease = unit (rng) > 0.3;
            p.linked = block % 3 != 0;
            p.link = unit (rng);
            const auto channels = block % 5 == 0 ? 1 : 2;
            if (unit (rng) > 0.8)
                lookahead = std::round (480.0 * unit (rng));

            allocations = 0;
            counting = true;

            for (int i = start; i < start + len; ++i)
                la[(size_t) i] = lookahead;

            KeyBlock key;
            key.numChannels = channels;
            key.numSamples = len;
            key.channels = { left.data() + start, right.data() + start };
            key.lookahead = la.data() + start;
            double* out[] { outL.data() + start, outR.data() + start };

            if (block % 7 == 3)
            {
                m.reset();
                m.follow (p, key);
                const double seed[] { 6.0, 3.0 };
                m.seed (seed, channels);
            }
            else
            {
                m.process (p, key, out);
            }

            (void) m.staticReduction (p, -18.0);
            (void) m.settleMs (p);

            counting = false;
            counted += allocations;
            start += len;
        }

        INFO (nameOf (d));
        REQUIRE (counted == 0);
    }
}

TEST_CASE ("VCA: nothing goes denormal after a release, without flush-to-zero", "[vca][realtime]")
{
    // The slowest settings: every state decays as long as it can before it snaps. Peak Auto's slow
    // stage (τ2 20 s) takes minutes to fall under 10⁻⁶ dB.
    for (const auto d : { Detector::peak, Detector::rms })
    {
        auto p = params (-40.0, 0.25, 6.0, 40.0, 300.0, 5000.0, true);
        p.linked = true;

        auto key = pinkNoise (samplesFor (6.0, rate), 4u);
        scale (key, 0.5);
        const auto stop = key.size();
        append (key, silence (samplesFor (d == Detector::peak ? 600.0 : 150.0, rate)));

        auto m = prepared (d);
        const auto r = runModel (m, p, key, &key, 4096);

        for (const auto& channel : r)
            for (const auto v : channel)
                REQUIRE (std::fpclassify (v) != FP_SUBNORMAL);

        auto lastNonZero = stop;
        for (auto i = stop; i < r[0].size(); ++i)
            if (! myzel::dsp::isExactly (r[0][i], 0.0))
                lastNonZero = i;
        std::printf ("VCA %s after 6 s of noise at Attack 300 ms, Release 5 s, Auto on: exactly 0 after %.1f s of silence\n", nameOf (d),
                     (double) (lastNonZero + 1 - stop) / rate);

        INFO (nameOf (d));
        REQUIRE (myzel::dsp::isExactly (r[0].back(), 0.0));
        REQUIRE (myzel::dsp::isExactly (m.reduction (1), 0.0));
    }
}

TEST_CASE ("VCA: CPU, stereo at 48 kHz", "[.perf]")
{
    // PLAN.md, Performance budget: VCA ≤ 0.3 % of a core for the whole chain at 48 kHz stereo,
    // Character off; Clean's model alone measured 0.23–0.29 % this way in M1 (clean/NOTES.md).
    // The model plus the gain conversion and the matrix, as Clean's [.perf]: the fastest of five
    // passes over 20 s of pink noise, in process CPU time.
    const auto n = samplesFor (20.0, rate);
    auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
    scale (left, 0.5);
    scale (right, 0.5);
    std::vector<double> outL ((size_t) n), outR ((size_t) n);
    std::vector<float> gainL (512), gainR (512), audioL ((size_t) n, 0.1f), audioR ((size_t) n, 0.1f);
    const std::vector<double> lookahead ((size_t) n, 240.0);

    struct Case
    {
        Detector detector;
        double attackMs;
        bool autoRelease;
        bool linked;
        double link = 1.0;
    };

    for (const auto& c : { Case { Detector::peak, 10.0, true, true }, Case { Detector::peak, 10.0, false, true }, Case { Detector::peak, 0.5, false, true },
                           Case { Detector::peak, 10.0, true, false }, Case { Detector::rms, 10.0, false, true }, Case { Detector::rms, 0.5, false, true },
                           Case { Detector::rms, 10.0, false, false },
                           Case { Detector::rms, 10.0, false, true, 0.5 } })
    {
        ModelParams p;
        p.curve = { -30.0, 0.25, 6.0, 40.0 };
        p.attackMs = c.attackMs;
        p.autoRelease = c.autoRelease;
        p.linked = c.linked;
        p.link = c.link;

        auto best = 1.0e9;
        for (int pass = 0; pass < 5; ++pass)
        {
            auto m = prepared (c.detector);
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
                m.process (p, key, out);

                for (int i = 0; i < len; ++i)
                {
                    gainL[(size_t) i] = gainFor (outL[(size_t) (start + i)]);
                    gainR[(size_t) i] = gainFor (outR[(size_t) (start + i)]);
                }
                float* audio[] { audioL.data() + start, audioR.data() + start };
                const float* gains[] { gainL.data(), gainR.data() };
                applyGains (c.linked ? StereoMode::linked : StereoMode::unlinked, audio, 2, gains, len);
            }
            best = std::min (best, (double) (std::clock() - t0) / CLOCKS_PER_SEC);
        }

        std::printf ("VCA %s, stereo %s, Link %g %%, 48 kHz, look-ahead 5 ms, Attack %g ms, Auto %s: %.3f %% of a core\n", nameOf (c.detector),
                     c.linked ? "linked" : "unlinked", 100.0 * c.link, c.attackMs, c.autoRelease ? "on" : "off", 100.0 * best / 20.0);
    }
}
