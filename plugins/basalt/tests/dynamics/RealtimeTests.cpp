// The core on the audio thread's terms (CLAUDE.md, Audio thread): nothing allocates once prepared,
// and no state goes denormal without flush-to-zero. Plus the [.perf] report.

#include "Harness.h"

#include "dsp/dynamics/Ballistics.h"
#include "dsp/dynamics/Stereo.h"
#include "dsp/models/clean/CleanModel.h"

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
using basalt::models::CleanModel;

namespace
{
    constexpr double rate = 48000.0;
}

TEST_CASE ("Real-time: the core and Clean allocate nothing once prepared", "[dynamics][realtime]")
{
    const auto n = samplesFor (4.0, rate);
    auto left = pinkNoise (n, 8u), right = pinkNoise (n, 9u);
    scale (left, 0.4);
    scale (right, 0.3);
    std::vector<double> la ((size_t) n), outL ((size_t) n), outR ((size_t) n), tapped ((size_t) n * 2);
    std::vector<float> gainL ((size_t) n), gainR ((size_t) n), audioL ((size_t) n, 0.1f), audioR ((size_t) n, -0.1f);

    CleanModel clean;
    clean.prepare (rate);
    KeyEq eq;
    eq.prepare (rate);
    KeyDelay delay;
    delay.prepare (2, samplesFor (0.010, rate));
    LinearGlide glide;
    glide.prepare (lookaheadGlideMs, rate);
    RunningMax hold;
    hold.prepare (windowCapacity (rate));
    FeedbackLoop loop;
    CurveFade fade;
    fade.prepare (rate);

    std::mt19937 rng (3u);
    std::uniform_real_distribution<double> unit (0.0, 1.0);

    long counted = 0;
    for (int start = 0, block = 0; start < n; ++block)
    {
        const auto len = std::min (1 + (int) (unit (rng) * 1024.0), n - start);

        ModelParams p;
        p.curve = { -60.0 * unit (rng), unit (rng) > 0.9 ? 0.0 : unit (rng), 24.0 * unit (rng), 40.0 * unit (rng) };
        p.attackMs = 0.02 * std::pow (15000.0, unit (rng));
        p.releaseMs = 5.0 * std::pow (1000.0, unit (rng));
        p.autoRelease = unit (rng) > 0.3;
        p.linked = unit (rng) > 0.5;
        p.link = unit (rng);
        const auto mode = (StereoMode) (block % numStereoModes);
        const auto layout = layoutFor (mode, 2);

        KeyEqSettings s;
        s.highPass = unit (rng) > 0.5;
        s.highPassHz = 20.0 + 1000.0 * unit (rng);
        s.lowPass = unit (rng) > 0.5;
        s.lowPassHz = 2000.0 + 18000.0 * unit (rng);
        s.bellDb = unit (rng) > 0.5 ? 0.0 : 36.0 * unit (rng) - 18.0;

        allocations = 0;
        counting = true;

        eq.set (s);
        glide.setTarget (unit (rng) > 0.5 ? 0.0 : 480.0 * unit (rng));
        fade.setTarget (p.curve);
        const auto k = FeedbackLoop::coefficientsFor (p.attackMs, p.releaseMs, feedbackHoldMs (p.releaseMs), cappedInverseRatio (p.curve.inverseRatio, feedbackRatioCap), rate);

        for (int i = start; i < start + len; ++i)
        {
            double frame[2] {}, eqFrame[] { left[(size_t) i], right[(size_t) i] };
            eq.process (eqFrame, 2);
            encodeKey (mode, eqFrame[0], eqFrame[1], frame);
            delay.push (frame);
            la[(size_t) i] = glide.next();
            for (int c = 0; c < layout.detectorChannels; ++c)
                tapped[(size_t) (c * n + i)] = delay.read (c, 480.0 - la[(size_t) i]);

            hold.setLength (feedbackHoldSamples (feedbackHoldMs (p.releaseMs), rate) + (int) la[(size_t) i] + 1);
            const double level = peakDb (hold.push (std::abs (left[(size_t) i])));
            double r = 0.0;
            (void) fade.next();
            loop.process (&level, &r, 1, fade.target(), k, 0.0);
            (void) releaseRatio (1.0 + unit (rng) * 100.0);
        }

        KeyBlock key;
        key.numChannels = layout.detectorChannels;
        key.numSamples = len;
        key.channels = { tapped.data() + start, tapped.data() + n + start };
        key.lookahead = la.data() + start;
        double* out[] { outL.data() + start, outR.data() + start };

        if (block % 7 == 3)
        {
            clean.follow (p, key);
            const double seed[] { 6.0, 3.0 };
            clean.seed (seed, 2);
        }
        else
        {
            clean.process (p, key, out);
        }

        for (int i = start; i < start + len; ++i)
        {
            gainL[(size_t) i] = gainFor (outL[(size_t) i]);
            gainR[(size_t) i] = gainFor (outR[(size_t) i]);
        }
        float* audio[] { audioL.data() + start, audioR.data() + start };
        const float* gains[] { gainL.data() + start, gainR.data() + start };
        applyGains (mode, audio, 2, gains, len);
        (void) clean.staticReduction (p, -18.0);

        counting = false;
        counted += allocations;
        start += len;
    }

    REQUIRE (counted == 0);
}

TEST_CASE ("Real-time: nothing goes denormal after a release, without flush-to-zero", "[dynamics][realtime]")
{
    // The slowest settings: every state decays as long as it can before it snaps or flushes.
    ModelParams p;
    p.curve = { -40.0, 0.25, 6.0, 40.0 };
    p.attackMs = 300.0;
    p.releaseMs = 5000.0;
    p.autoRelease = true;
    p.linked = true;

    auto key = pinkNoise (samplesFor (3.0, rate), 4u);
    scale (key, 0.5);
    append (key, silence (samplesFor (150.0, rate)));

    CleanModel clean;
    clean.prepare (rate);
    const auto r = runModel (clean, p, key, &key, 4096);

    for (const auto& channel : r)
        for (const auto v : channel)
            REQUIRE (std::fpclassify (v) != FP_SUBNORMAL);
    REQUIRE (myzel::dsp::isExactly (r[0].back(), 0.0));
    REQUIRE (myzel::dsp::isExactly (clean.reduction (1), 0.0));

    FeedbackHarness h;
    const auto fr = h.run (p, key)[0];
    for (const auto v : fr)
        REQUIRE (std::fpclassify (v) != FP_SUBNORMAL);
    REQUIRE (myzel::dsp::isExactly (fr.back(), 0.0));
}

TEST_CASE ("Real-time: CPU of Clean, stereo at 48 kHz", "[.perf]")
{
    // PLAN.md, Performance budget: Clean ≤ 0.3 % of a core at 48 kHz stereo, Character off. This
    // is the model plus the gain conversion and the matrix, not the chain; the fastest of five
    // passes, in process CPU time.
    const auto n = samplesFor (20.0, rate);
    auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
    scale (left, 0.5);
    scale (right, 0.5);
    std::vector<double> outL ((size_t) n), outR ((size_t) n);
    std::vector<float> gainL (512), gainR (512), audioL ((size_t) n, 0.1f), audioR ((size_t) n, 0.1f);
    const std::vector<double> lookahead ((size_t) n, 240.0);

    for (const auto& [attackMs, autoRelease] : { std::pair { 10.0, true }, std::pair { 0.5, false } })
    {
        ModelParams p;
        p.curve = { -30.0, 0.25, 6.0, 40.0 };
        p.attackMs = attackMs;
        p.autoRelease = autoRelease;
        p.linked = true;

        auto best = 1.0e9;
        for (int pass = 0; pass < 5; ++pass)
        {
            CleanModel clean;
            clean.prepare (rate);
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
                clean.process (p, key, out);

                for (int i = 0; i < len; ++i)
                {
                    gainL[(size_t) i] = gainFor (outL[(size_t) (start + i)]);
                    gainR[(size_t) i] = gainFor (outR[(size_t) (start + i)]);
                }
                float* audio[] { audioL.data() + start, audioR.data() + start };
                const float* gains[] { gainL.data(), gainR.data() };
                applyGains (StereoMode::linked, audio, 2, gains, len);
            }
            best = std::min (best, (double) (std::clock() - t0) / CLOCKS_PER_SEC);
        }

        std::printf ("Clean, stereo, 48 kHz, look-ahead 5 ms, Attack %g ms, Auto %s: %.3f %% of a core\n", attackMs,
                     autoRelease ? "on" : "off", 100.0 * best / 20.0);
    }
}
