// Vari-mu on the audio thread's terms (CLAUDE.md, Audio thread): nothing allocates once prepared,
// no state goes denormal without flush-to-zero, a non-finite key does no lasting harm. Plus the
// [.perf] report against PLAN.md's budget and the distortion the gate table reports.

#include "VariMuHarness.h"

#include "dsp/dynamics/Stereo.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <random>

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
using namespace basalt::testing::varimu;

namespace
{
    constexpr double rate = 48000.0;
}

TEST_CASE ("Vari-mu real-time: nothing allocates once prepared", "[varimu][realtime]")
{
    // Random settings per block (curve fades, time glides, Auto on and off, Link), random block
    // sizes, follow() and seed() between, two channels and one.
    const auto n = samplesFor (4.0, rate);
    auto left = pinkNoise (n, 8u), right = pinkNoise (n, 9u);
    scale (left, 0.4);
    scale (right, 0.3);
    std::vector<double> la ((size_t) n), outL ((size_t) n), outR ((size_t) n);
    std::mt19937 rng (3u);
    std::uniform_real_distribution<double> unit (0.0, 1.0);
    for (auto& v : la)
        v = 240.0 * unit (rng);

    auto m = prepared (rate);
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

        KeyBlock key;
        key.numChannels = block % 5 == 4 ? 1 : 2;
        key.numSamples = len;
        key.channels = { left.data() + start, right.data() + start };
        key.lookahead = la.data() + start;
        double* out[] { outL.data() + start, outR.data() + start };

        allocations = 0;
        counting = true;
        if (block % 7 == 3)
        {
            m.follow (p, key);
            const double seed[] { 6.0, 3.0 };
            m.seed (seed, 2);
        }
        else
        {
            m.process (p, key, out);
        }
        (void) m.staticReduction (p, -10.0);
        (void) autoGainDb (m, p);
        counting = false;

        counted += allocations;
        start += len;
    }

    REQUIRE (counted == 0);
}

TEST_CASE ("Vari-mu real-time: nothing goes denormal after a release, without flush-to-zero", "[varimu][realtime]")
{
    // Silence after Auto release's longest tier (25 s) has charged to 23 dB: the snap at 10⁻⁶ dB
    // comes ln(2.3·10⁷) × 25 s = 424 s later. The loop and both memories end exactly 0, and no
    // state lingers in the denormal range on the way.
    const auto p = params (-30.0, 0.25, 6.0, 40.0, 300.0, 5000.0, true);
    auto key = carrier (amplitudeFor (-5.0), samplesFor (35.0, rate));
    append (key, silence (samplesFor (450.0, rate)));

    auto m = prepared (rate);
    const auto r = reductionFor (m, p, key, 4096);
    auto denormals = 0;
    for (const auto v : r)
        denormals += std::fpclassify (v) == FP_SUBNORMAL ? 1 : 0;
    REQUIRE (denormals == 0);
    REQUIRE (myzel::dsp::isExactly (r.back(), 0.0));
    REQUIRE (myzel::dsp::isExactly (m.memory (0, 0), 0.0));
    REQUIRE (myzel::dsp::isExactly (m.memory (0, 1), 0.0));
}

TEST_CASE ("Vari-mu real-time: a NaN or an infinity in the key does no lasting harm", "[varimu][realtime]")
{
    // The chain takes its key sane (dynamics/NOTES.md, Key path); the model reads a NaN as silence
    // and an infinity as +120 dBFS on its own too. Every reduction stays finite and within Range,
    // and a second after the bad samples the trace is the clean one.
    const auto p = params (-30.0, 0.25, 6.0, 40.0, 1.0, 150.0, true);
    const auto clean = sine (amplitudeFor (-15.0), 1000.0, rate, samplesFor (2.0, rate));

    for (const auto bad : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() })
    {
        auto key = clean;
        for (int i = 0; i < 50; ++i)
            key[(size_t) samplesFor (0.5, rate) + (size_t) i * 7] = bad;

        auto a = prepared (rate), b = prepared (rate);
        const auto r = reductionFor (a, p, key);
        const auto reference = reductionFor (b, p, clean);
        REQUIRE (std::all_of (r.begin(), r.end(), [] (double v) { return std::isfinite (v) && v >= 0.0 && v <= 40.0; }));

        auto worst = 0.0;
        for (auto i = (size_t) samplesFor (1.8, rate); i < r.size(); ++i)
            worst = std::max (worst, std::abs (r[i] - reference[i]));
        INFO ("bad sample " << bad << ": " << worst << " dB from the clean trace at 1.8 s");
        REQUIRE (worst < 1.0e-6);
    }
}

TEST_CASE ("Vari-mu: THD+N of a 1 kHz sine at 6 dB of reduction, reported", "[varimu][distortion]")
{
    // NOTES.md, Gate table, Distortion (reported): Attack 0.4 and 50 ms, Release 0.3 and 2 s, Auto
    // off and on. The sine is multiplied by the model's gain; the fundamental is fitted over 50
    // whole periods after settling and the rest is THD+N. Ratio 4, hard knee, as the core's row.
    for (const auto sampleRate : { 48000.0, 44100.0 })
    {
        for (const auto autoRelease : { false, true })
            for (const auto attackMs : { 0.4, 50.0 })
                for (const auto releaseMs : { 300.0, 2000.0 })
                {
                    auto p = params (-30.0, 0.25, 0.0, 40.0, attackMs, releaseMs, autoRelease);
                    const auto x = sine (carrierAmplitudeFor (p, 6.0), 1000.0, sampleRate, samplesFor (6.0, sampleRate));
                    auto m = prepared (sampleRate);
                    const auto r = reductionFor (m, p, x);

                    // 50 whole periods of 1 kHz: 2400 samples at 48 kHz, 2205 at 44.1.
                    const auto length = (size_t) std::lround (0.05 * sampleRate);
                    const auto from = x.size() - length;
                    double c = 0.0, s = 0.0;
                    for (size_t i = from; i < x.size(); ++i)
                    {
                        const auto y = x[i] * (double) gainFor (r[i]);
                        const auto phase = 2.0 * std::numbers::pi * 1000.0 * (double) i / sampleRate;
                        c += y * std::cos (phase);
                        s += y * std::sin (phase);
                    }
                    c *= 2.0 / (double) length;
                    s *= 2.0 / (double) length;
                    double residual = 0.0, total = 0.0;
                    for (size_t i = from; i < x.size(); ++i)
                    {
                        const auto y = x[i] * (double) gainFor (r[i]);
                        const auto phase = 2.0 * std::numbers::pi * 1000.0 * (double) i / sampleRate;
                        const auto e = y - c * std::cos (phase) - s * std::sin (phase);
                        residual += e * e;
                        total += y * y;
                    }
                    // Floored at −200 dBc: a gain that holds still leaves only rounding.
                    std::printf ("Vari-mu THD+N, 1 kHz at 6 dB, %.1f kHz, Attack %g ms, Release %g s, Auto %s: %.1f dBc (reduction %.2f dB)\n",
                                 sampleRate / 1000.0, attackMs, releaseMs * 0.001, autoRelease ? "on" : "off",
                                 std::max (-200.0, 10.0 * std::log10 (residual / (total - residual) + 1.0e-30)), mean (r, from, r.size()));
                }
    }
}

TEST_CASE ("Vari-mu real-time: CPU, stereo at 48 kHz", "[.perf]")
{
    // PLAN.md, Performance budget: Vari-mu ≤ 0.5 % of a core at 48 kHz stereo, Character off. The
    // model plus the gain conversion and the matrix, not the chain, as Clean's figure; the fastest
    // of five passes, in process CPU time. Pink noise around Threshold, so the solve runs.
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
        double attackMs, ratio;
        bool autoRelease, linked;
    };
    for (const auto& c : { Case { "defaults (Attack 10 ms, Ratio 4, Auto)", 10.0, 4.0, true, true }, Case { "Attack 0.4 ms, Auto", 0.4, 4.0, true, true },
                           Case { "Attack 50 ms, Ratio 1.5, Auto off", 50.0, 1.5, false, true }, Case { "Ratio 20 (no rise)", 10.0, 20.0, true, true },
                           Case { "defaults, Unlinked", 10.0, 4.0, true, false } })
    {
        ModelParams p;
        p.curve = { -30.0, 1.0 / c.ratio, 6.0, 40.0 };
        p.attackMs = c.attackMs;
        p.autoRelease = c.autoRelease;
        p.linked = c.linked;

        auto best = 1.0e9;
        double meanReduction = 0.0;
        for (int pass = 0; pass < 5; ++pass)
        {
            auto m = prepared (rate);
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
            meanReduction = mean (outL, 0, outL.size());
        }

        std::printf ("Vari-mu, stereo, 48 kHz, look-ahead 5 ms, %-40s: %.3f %% of a core (mean reduction %.1f dB)\n", c.name, 100.0 * best / 20.0,
                     meanReduction);
    }
}
