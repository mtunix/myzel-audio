// Opto on the audio thread's terms (CLAUDE.md, Audio thread): nothing allocates once prepared, no
// state goes denormal without flush-to-zero, no key or setting the chain can hand it gives a NaN.
// Plus the [.perf] report against PLAN.md's budget (Opto ≤ 0.5 % of a core) and the distortion
// the NOTES report.

#include "OptoSupport.h"

#include "dsp/dynamics/KeyPath.h"
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
using namespace basalt::testing::opto;

TEST_CASE ("Opto: allocates nothing once prepared", "[opto][realtime]")
{
    const auto n = samplesFor (6.0, rate);
    auto left = pinkNoise (n, 8u), right = pinkNoise (n, 9u);
    scale (left, 0.4);
    scale (right, 0.3);
    std::vector<double> la ((size_t) n), outL ((size_t) n), outR ((size_t) n);

    OptoModel opto;
    opto.prepare (rate);

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
        const auto channels = block % 3 == 0 ? 1 : 2;
        const auto lookahead = unit (rng) * 480.0;
        for (int i = start; i < start + len; ++i)
            la[(size_t) i] = lookahead;

        KeyBlock key;
        key.numChannels = channels;
        key.numSamples = len;
        key.channels = { left.data() + start, right.data() + start };
        key.lookahead = la.data() + start;
        double* out[] { outL.data() + start, outR.data() + start };

        allocations = 0;
        counting = true;

        if (block % 7 == 3)
        {
            opto.reset();
            opto.follow (p, key);
            const double seed[] { 6.0, 3.0 };
            opto.seed (seed, 2);
        }
        else
        {
            opto.process (p, key, out);
        }
        (void) opto.staticReduction (p, -18.0);
        (void) opto.settleMs (p);
        (void) autoGainDb (opto, p);

        counting = false;
        counted += allocations;
        start += len;
    }

    REQUIRE (counted == 0);
}

TEST_CASE ("Opto: nothing goes denormal, and the cell lands on exactly 0, without flush-to-zero", "[opto][realtime]")
{
    // The slowest settings: the memory discharges over 25 s and only snaps once under 10⁻⁶ dB.
    for (const auto autoRelease : { false, true })
    {
        auto p = params (-40.0, 0.25, 6.0, 40.0, 300.0, 5000.0, autoRelease);
        p.linked = true;

        auto key = pinkNoise (samplesFor (3.0, rate), 4u);
        scale (key, 0.5);
        append (key, silence (samplesFor (autoRelease ? 700.0 : 150.0, rate)));

        auto m = prepared();
        const auto r = runModel (m, p, key, &key, 4096);

        for (const auto& channel : r)
            for (const auto v : channel)
                REQUIRE (std::fpclassify (v) != FP_SUBNORMAL);
        REQUIRE (myzel::dsp::isExactly (r[0].back(), 0.0));
        REQUIRE (myzel::dsp::isExactly (m.reduction (1), 0.0));
        REQUIRE (myzel::dsp::isExactly (m.fastOf (0), 0.0));
        REQUIRE (myzel::dsp::isExactly (m.memoryOf (0), 0.0));
        REQUIRE (std::fpclassify (m.lightOf (0)) != FP_SUBNORMAL);
    }
}

TEST_CASE ("Opto: no key or setting the chain can hand it gives a NaN", "[opto][realtime]")
{
    // The chain clamps the key to ±10⁶ and reads NaN and infinity as 0 (saneKey). Every corner of
    // the knobs on such keys: the reduction stays finite and within Range.
    Signal key;
    append (key, carrier (keyLimit, samplesFor (0.5, rate)));
    append (key, silence (samplesFor (0.2, rate)));
    for (const auto x : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -1.0e300, 1.0e-320 })
        append (key, Signal ((size_t) samplesFor (0.1, rate), saneKey (x)));
    append (key, carrier (1.0e-6, samplesFor (0.2, rate)));
    const Signal lookahead (key.size(), 480.0);

    for (const auto t : { -60.0, 0.0 })
        for (const auto inverseRatio : { 1.0, 0.9999, 0.25, 0.0 })
            for (const auto knee : { 0.0, 24.0 })
                for (const auto range : { 0.0, 0.01, 40.0 })
                    for (const auto& [attackMs, releaseMs] : { std::pair { 0.02, 5.0 }, std::pair { 300.0, 5000.0 } })
                        for (const auto autoRelease : { false, true })
                        {
                            auto p = params (t, inverseRatio, knee, range, attackMs, releaseMs, autoRelease);
                            p.linked = true;
                            p.link = 0.5;
                            auto m = prepared();
                            const auto r = runModel (m, p, key, &key, 333, &lookahead);
                            for (const auto& channel : r)
                                for (const auto v : channel)
                                {
                                    INFO ("T " << t << ", 1/R " << inverseRatio << ", knee " << knee << ", range " << range << ", attack " << attackMs);
                                    REQUIRE (std::isfinite (v));
                                    REQUIRE (v >= 0.0);
                                    REQUIRE (v <= range + 1.0e-9);
                                }
                        }
}

TEST_CASE ("Opto: THD+N of a 1 kHz sine at 6 dB reduction, reported", "[opto][distortion]")
{
    // NOTES' distortion row: Attack 2, 10, 50 ms, Release 60 ms and 0.5 s. The key is the audio;
    // the fundamental is fitted over 50 whole periods once the cell has settled.
    for (const auto attackMs : { 2.0, 10.0, 50.0 })
        for (const auto releaseMs : { 60.0, 500.0 })
        {
            const auto p = params (-30.0, 0.25, 0.0, 40.0, attackMs, releaseMs);
            const auto level = ref::levelForReduction (6.0, -30.0, 0.25, 0.0);
            const auto tau = std::min (OptoModel::attackConstantMs (attackMs, 0.25, 0.0), OptoModel::releaseConstantMs (releaseMs, attackMs, 0.25, 0.0));
            const auto n = samplesFor (std::max (2.0, 0.025 * std::max (tau, OptoModel::attackConstantMs (attackMs, 0.25, 0.0))), rate);
            const auto x = sine (amplitudeFor (level), 1000.0, rate, n);
            auto m = prepared();
            const auto r = runModel (m, p, x, nullptr, 512)[0];

            const auto periods = samplesFor (0.05, rate);
            double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
            std::vector<double> y ((size_t) periods);
            for (int i = 0; i < periods; ++i)
            {
                const auto j = (size_t) (n - periods + i);
                y[(size_t) i] = x[j] * gainFor (r[j]);
                const auto s = std::sin (2.0 * std::numbers::pi * 1000.0 * (double) j / rate), c = std::cos (2.0 * std::numbers::pi * 1000.0 * (double) j / rate);
                ss += s * s;
                sc += s * c;
                cc += c * c;
                ys += y[(size_t) i] * s;
                yc += y[(size_t) i] * c;
            }
            const auto det = ss * cc - sc * sc;
            const auto a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
            double residual = 0, fundamental = 0;
            for (int i = 0; i < periods; ++i)
            {
                const auto j = (double) (n - periods + i);
                const auto f = a * std::sin (2.0 * std::numbers::pi * 1000.0 * j / rate) + b * std::cos (2.0 * std::numbers::pi * 1000.0 * j / rate);
                fundamental += f * f;
                residual += (y[(size_t) i] - f) * (y[(size_t) i] - f);
            }
            std::printf ("Opto THD+N, 1 kHz at 6 dB, R 4, hard knee, Attack %g ms, Release %g ms: %.1f dBc\n", attackMs, releaseMs,
                         10.0 * std::log10 (residual / fundamental));
        }
}

TEST_CASE ("Opto: CPU, stereo at 48 kHz", "[.perf]")
{
    // PLAN.md, Performance budget: Opto ≤ 0.5 % of a core at 48 kHz stereo, Character off. The
    // model plus the gain conversion and the matrix, as Clean's report; the fastest of five
    // passes, in process CPU time, look-ahead 5 ms, pink noise over Threshold.
    const auto n = samplesFor (20.0, rate);
    auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
    scale (left, 0.5);
    scale (right, 0.5);
    std::vector<double> outL ((size_t) n), outR ((size_t) n);
    std::vector<float> gainL (512), gainR (512), audioL ((size_t) n, 0.1f), audioR ((size_t) n, 0.1f);
    const std::vector<double> lookahead ((size_t) n, 240.0);

    struct Case
    {
        const char* name;
        StereoMode mode;
        double link;
        bool autoRelease;
        double attackMs;
        bool automated = false;     // Threshold moves every block: a curve fade runs two cells most of the time
    };

    for (const auto& c : { Case { "defaults (Linked 100 %, Auto on, Attack 10 ms)", StereoMode::linked, 1.0, true, 10.0 },
                           Case { "Linked 100 %, Auto off, Attack 2 ms", StereoMode::linked, 1.0, false, 2.0 },
                           Case { "Linked 50 %, Auto on", StereoMode::linked, 0.5, true, 10.0 },
                           Case { "Unlinked, Auto on", StereoMode::unlinked, 1.0, true, 10.0 },
                           Case { "Mid/Side, Auto on", StereoMode::midSide, 1.0, true, 10.0 },
                           Case { "defaults, Threshold automated every block", StereoMode::linked, 1.0, true, 10.0, true } })
    {
        ModelParams p;
        p.curve = { -30.0, 0.25, 6.0, 40.0 };
        p.attackMs = c.attackMs;
        p.autoRelease = c.autoRelease;
        p.linked = c.mode == StereoMode::linked;
        p.link = c.link;

        auto best = 1.0e9;
        for (int pass = 0; pass < 5; ++pass)
        {
            OptoModel opto;
            opto.prepare (rate);
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
                if (c.automated)
                    p.curve.thresholdDb = -30.0 + 6.0 * std::sin (start * 1.0e-3);
                opto.process (p, key, out);

                for (int i = 0; i < len; ++i)
                {
                    gainL[(size_t) i] = gainFor (outL[(size_t) (start + i)]);
                    gainR[(size_t) i] = gainFor (outR[(size_t) (start + i)]);
                }
                float* audio[] { audioL.data() + start, audioR.data() + start };
                const float* gains[] { gainL.data(), gainR.data() };
                applyGains (c.mode, audio, 2, gains, len);
            }
            best = std::min (best, (double) (std::clock() - t0) / CLOCKS_PER_SEC);
        }

        std::printf ("Opto, stereo, 48 kHz, look-ahead 5 ms, %s: %.3f %% of a core\n", c.name, 100.0 * best / 20.0);
        CHECK (100.0 * best / 20.0 <= 0.5);
    }
}
