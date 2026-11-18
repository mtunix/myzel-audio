// FET as a model the chain runs (dynamics/NOTES.md, Model interface; models/fet/NOTES.md, Gate
// table): the idle zeros, the stereo link, the seed and the warm-up, knob jumps and the All
// switch, block-size independence and a key that is not a number.

#include "FetHarness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <limits>
#include <string>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using namespace basalt::testing::fet;
using Catch::Matchers::WithinAbs;
using myzel::dsp::isExactly;

namespace
{
    constexpr double rate = 48000.0;

    bool allExactlyZero (const Trace& r)
    {
        return std::all_of (r.begin(), r.end(), [] (double v) { return isExactly (v, 0.0); });
    }
}

TEST_CASE ("FET: idle is exactly 0", "[fet][idle]")
{
    // NOTES.md, Gate table, Identity: Ratio 1; or the input below T_eff − W/2 (All: below
    // T − W/2); Range 0. Exactly 0 on every sample, so the gain is exactly 1, at look-ahead 0, 5
    // and 10 ms and any block size, with and without All's lag.
    constexpr double t = -30.0;
    struct Case
    {
        const char* name;
        double ratio, knee, range, levelDb;
    };
    const Case cases[] {
        { "Ratio 1, 40 dB over", 1.0, 6.0, 40.0, t + 40.0 },
        { "Range 0, 40 dB over", 20.0, 6.0, 0.0, t + 40.0 },
        { "Range 0, All", ref::infinity, 0.0, 0.0, t + 40.0 },
        { "20:1 between T - W/2 and T_eff - W/2", 20.0, 6.0, 40.0, t + 2.9 },
        { "12:1 just under T_eff, hard knee", 12.0, 0.0, 40.0, t + 4.99 },
        { "8:1 under T_eff - W/2, knee 24", 8.0, 24.0, 40.0, t - 8.01 },
        { "All under T - W/2", ref::infinity, 6.0, 40.0, t - 3.01 },
    };

    for (const auto& c : cases)
        for (const auto laMs : { 0.0, 5.0, 10.0 })
            for (const auto block : { 1, 64, 4096 })
                for (const auto useSine : { false, true })
                {
                    const auto n = samplesFor (0.3, rate);
                    const auto a = amplitudeFor (c.levelDb);
                    auto key = useSine ? sine (a, 997.0, rate, n) : carrier (a, n);
                    const Signal la (key.size(), laMs * 0.001 * rate);
                    auto m = prepared (rate);
                    const auto r = runModel (m, params (t, c.ratio, c.knee, c.range, 0.02, 50.0), key, &key, block, &la);
                    INFO (c.name << ", look-ahead " << laMs << " ms, block " << block << (useSine ? ", sine" : ", carrier"));
                    REQUIRE (allExactlyZero (r[0]));
                    REQUIRE (allExactlyZero (r[1]));
                    REQUIRE (isExactly (gainFor (r[0].back()), 1.0));
                }
}

TEST_CASE ("FET: Link 100 % gives both channels the same reduction to the bit, Link 0 % is Unlinked", "[fet][stereo]")
{
    // dynamics/NOTES.md, Stereo: the loop links the demands; at 100 % both states stay identical.
    const auto n = samplesFor (10.0, rate);
    auto left = pinkNoise (n, 21u), right = pinkNoise (n, 22u);
    scale (left, 0.5);
    scale (right, 0.2);
    const Signal la ((size_t) n, 96.0);

    for (const auto ratio : { 4.0, 20.0, ref::infinity })
    {
        auto p = params (-30.0, ratio, 6.0, 40.0, 0.05, 150.0);
        p.linked = true;
        p.link = 1.0;
        auto m = prepared (rate);
        const auto full = runModel (m, p, left, &right, 64, &la);
        REQUIRE (full[0] == full[1]);
        REQUIRE (*std::max_element (full[0].begin(), full[0].end()) > 10.0);

        p.link = 0.0;
        m.reset();
        const auto none = runModel (m, p, left, &right, 64, &la);
        p.linked = false;
        m.reset();
        const auto unlinked = runModel (m, p, left, &right, 512, &la);
        REQUIRE (none[0] == unlinked[0]);
        REQUIRE (none[1] == unlinked[1]);
        REQUIRE_FALSE (none[0] == none[1]);

        // Halfway, the quieter channel's demand is halfway to the louder one's: its mean reduction
        // lies between its own and the linked pair's.
        p.linked = true;
        p.link = 0.5;
        m.reset();
        const auto half = runModel (m, p, left, &right, 64, &la);
        const auto own = mean (unlinked[1], 0, unlinked[1].size()), shared = mean (full[1], 0, full[1].size());
        const auto halfway = mean (half[1], 0, half[1].size());
        REQUIRE (own < halfway);
        REQUIRE (halfway < shared);
    }
}

TEST_CASE ("FET: a seed starts the loop at the applied reduction", "[fet][switch]")
{
    // Settled 33: at Attack 300 ms the incoming model starts at the applied 10 dB instead of
    // re-attacking from 0, and it holds there on the key that gives it.
    constexpr double t = -30.0;
    for (const auto ratio : { 4.0, 20.0, ref::infinity })
    {
        const auto p = params (t, ratio, 0.0, 40.0, 300.0, 1000.0);
        const auto level = carrier (amplitudeFor (ref::levelForReductionDb (t, ratio, 10.0)), samplesFor (3.0, rate));

        auto m = prepared (rate);
        KeyBlock key;
        key.numChannels = 1;
        key.numSamples = samplesFor (m.settleMs (p) * 0.001, rate) + 1;
        key.channels[0] = level.data();
        m.follow (p, key);

        const double seed[] { 10.0 };
        m.seed (seed, 1);
        REQUIRE (isExactly (m.reduction (0), 10.0));
        const auto r = runModel (m, p, level)[0];
        INFO ("R " << ratio);
        REQUIRE_THAT (*std::min_element (r.begin(), r.end()), WithinAbs (10.0, 1.0e-6));
        REQUIRE_THAT (*std::max_element (r.begin(), r.end()), WithinAbs (10.0, 1.0e-6));
    }
}

TEST_CASE ("FET: warmed up for settleMs and seeded, it carries on as an engine that ran throughout", "[fet][switch]")
{
    // Settled 56: the chain follows the key unheard for settleMs plus the look-ahead window, then
    // seeds the applied reduction. FET's key-only states are the pre-hold window and All's lag
    // line, so the incoming engine then matches one that ran throughout to the bit.
    const auto n = samplesFor (4.0, rate);
    auto left = pinkNoise (n, 31u), right = pinkNoise (n, 32u);
    scale (left, 0.5);
    scale (right, 0.25);
    const Signal la ((size_t) n, 240.0);

    for (const auto ratio : { 4.0, 20.0, ref::infinity })
        for (const auto releaseMs : { 5.0, 150.0 })
        {
            auto p = params (-30.0, ratio, 6.0, 40.0, 0.8, releaseMs);
            p.linked = true;
            p.link = 0.7;

            auto throughout = prepared (rate);
            const auto reference = runModel (throughout, p, left, &right, 64, &la);

            const auto switchAt = samplesFor (2.0, rate) / 64 * 64;
            const auto warm = samplesFor ((throughout.settleMs (p) + 5.0) * 0.001, rate) + 1;
            auto incoming = prepared (rate);
            KeyBlock key;
            key.numChannels = 2;
            key.numSamples = warm;
            key.channels = { left.data() + switchAt - warm, right.data() + switchAt - warm };
            key.lookahead = la.data();
            incoming.follow (p, key);

            const double seed[] { reference[0][(size_t) switchAt - 1], reference[1][(size_t) switchAt - 1] };
            incoming.seed (seed, 2);

            const Signal restL (left.begin() + switchAt, left.end()), restR (right.begin() + switchAt, right.end());
            const auto after = runModel (incoming, p, restL, &restR, 64, &la);
            auto worst = 0.0;
            for (size_t i = 0; i < restL.size(); ++i)
                for (int c = 0; c < 2; ++c)
                    worst = std::max (worst, std::abs (after[(size_t) c][i] - reference[(size_t) c][(size_t) switchAt + i]));
            INFO ("R " << ratio << ", Release " << releaseMs << " ms: worst " << worst << " dB");
            REQUIRE (isExactly (worst, 0.0));
        }
}

TEST_CASE ("FET: settleMs covers the pre-hold and All's lag", "[fet][switch]")
{
    auto m = prepared (rate);
    REQUIRE_THAT (m.settleMs (params (-30.0, 4.0, 6.0, 40.0, 10.0, 150.0)), WithinAbs (1.0 + 1000.0 / rate, 1.0e-9));
    REQUIRE_THAT (m.settleMs (params (-30.0, 4.0, 6.0, 40.0, 10.0, 5.0)), WithinAbs (0.5 + 1000.0 / rate, 1.0e-9));
    REQUIRE_THAT (m.settleMs (params (-30.0, ref::infinity, 6.0, 40.0, 10.0, 150.0)), WithinAbs (2.0 + 1000.0 / rate, 1.0e-9));
}

namespace
{
    /** The largest |first| and |second difference| of a trace from `from`, dB per sample (squared). */
    std::pair<double, double> roughness (const Trace& r, size_t from)
    {
        auto step = 0.0, corner = 0.0;
        for (size_t i = std::max<size_t> (from, 2); i < r.size(); ++i)
        {
            step = std::max (step, std::abs (r[i] - r[i - 1]));
            corner = std::max (corner, std::abs (r[i] - 2.0 * r[i - 1] + r[i - 2]));
        }
        return { step, corner };
    }
}

TEST_CASE ("FET: a knob jump fades instead of stepping, and so does the switch into and out of All", "[fet][clicks]")
{
    // A key 14 dB over the default Threshold; every 0.31 s a knob jumps between its ends while the
    // reduction still moves from the last jump, at Attack 0.02 and 10 ms. The fade blends two
    // loops on a raised cosine, so the reduction never steps, and within the fade its second
    // difference stays of the order of the raised cosine's own, 40 dB·(π/960)²/2 ≈ 2·10⁻⁴.
    // After the fade the loop moves on its own; where it attacks into Range with its demand still
    // past it, the state's clamp leaves a corner of slope (demand − Range)/τ_A, which a plain step
    // gives as well (NOTES.md, Measured in M2), so it is reported, not counted as the fade's.
    const auto period = samplesFor (0.31, rate);
    const auto fadeSamples = (size_t) samplesFor (CurveFade::fadeMs * 0.001, rate);

    struct Jump
    {
        const char* name;
        double keyDb;
        std::function<void (ModelParams&, bool)> set;
    };
    const Jump jumps[] {
        { "Threshold -60 <-> 0", -4.0, [] (ModelParams& p, bool b) { p.curve.thresholdDb = b ? 0.0 : -60.0; } },
        { "Ratio 1 <-> All", -4.0, [] (ModelParams& p, bool b) { p.curve.inverseRatio = b ? 0.0 : 1.0; } },
        { "Ratio 20 <-> All", -4.0, [] (ModelParams& p, bool b) { p.curve.inverseRatio = b ? 0.0 : 0.05; } },
        { "Ratio 4 <-> 20", -4.0, [] (ModelParams& p, bool b) { p.curve.inverseRatio = b ? 0.05 : 0.25; } },
        { "Knee 0 <-> 24", -15.0, [] (ModelParams& p, bool b) { p.curve.kneeDb = b ? 24.0 : 0.0; } },
        { "Range 0 <-> 40", -4.0, [] (ModelParams& p, bool b) { p.curve.rangeDb = b ? 40.0 : 0.0; } },
    };

    for (const auto attackMs : { 0.02, 10.0 })
        for (const auto& jump : jumps)
        {
            const auto key = carrier (amplitudeFor (jump.keyDb), samplesFor (2.0, rate));
            const auto at = [&] (int pos)
            {
                auto p = params (-18.0, 4.0, 6.0, 40.0, attackMs, 150.0);
                jump.set (p, (pos / period) % 2 == 1);
                return p;
            };
            auto m = prepared (rate);
            const auto r = runScheduled (m, key, nullptr, 64, at)[0];

            auto step = 0.0, inFade = 0.0, after = 0.0;
            size_t fadeEnds = 0;
            for (size_t i = 64; i < r.size(); ++i)
            {
                if (i % 64 == 0 && ! CurveFade::same (at ((int) i).curve, at ((int) i - 64).curve))
                    fadeEnds = i + fadeSamples + 1;
                step = std::max (step, std::abs (r[i] - r[i - 1]));
                const auto corner = std::abs (r[i] - 2.0 * r[i - 1] + r[i - 2]);
                (i <= fadeEnds ? inFade : after) = std::max (i <= fadeEnds ? inFade : after, corner);
            }

            std::printf ("FET knob jump at Attack %g ms, %s: largest step %.4f dB, largest second difference %.2e dB in the fades, %.2e dB after them\n",
                         attackMs, jump.name, step, inFade, after);
            INFO (jump.name << " at Attack " << attackMs << " ms: largest step " << step << " dB, largest second difference " << inFade
                            << " dB in the fades, " << after << " dB after");
            REQUIRE (step < 0.1);
            REQUIRE (inFade < 5.0e-4);
        }

    // The loop's own corner at Range, without a knob moving: a step whose demand passes Range 40 by 2 dB.
    auto key = silence (480);
    append (key, carrier (amplitudeFor (-4.0), samplesFor (0.3, rate)));
    auto m = prepared (rate);
    const auto r = runModel (m, params (-60.0, 4.0, 6.0, 40.0, 10.0, 150.0), key)[0];
    const auto [step, corner] = roughness (r, 482);
    std::printf ("FET plain step into Range 40 (demand 42 dB), Attack 10 ms: largest step %.4f dB, largest second difference %.2e dB\n", step, corner);
}

TEST_CASE ("FET: the switch into and out of All glides the key's lag without a step", "[fet][clicks][all]")
{
    // NOTES.md, All: Ratio 20 ↔ All ramps T_eff over 20 ms and glides the key's lag by L over
    // 50 ms. On the carrier the lag cannot show; on a 997 Hz sine 14 dB over and pink noise the
    // glide reads the key's magnitude between samples. The reduction's steps and second
    // differences from 0.1 s on (past the onset), against the same key held at 20:1 and at All.
    const auto period = samplesFor (0.31, rate);
    auto noise = pinkNoise (samplesFor (2.0, rate), 61u);
    scale (noise, 0.6);
    const std::pair<const char*, Signal> keys[] { { "997 Hz sine", sine (amplitudeFor (-4.0), 997.0, rate, samplesFor (2.0, rate)) },
                                                  { "pink noise", noise } };

    for (const auto attackMs : { 0.02, 10.0 })
        for (const auto& [name, key] : keys)
        {
            const auto run = [&] (std::function<bool (int)> all) {
                auto m = prepared (rate);
                return runScheduled (m, key, nullptr, 64, [&] (int pos) { return params (-18.0, all (pos) ? ref::infinity : 20.0, 6.0, 40.0, attackMs, 150.0); })[0];
            };
            const auto settled = (size_t) samplesFor (0.1, rate);
            const auto switching = run ([&] (int pos) { return (pos / period) % 2 == 1; });
            const auto at20 = run ([] (int) { return false; });
            const auto atAll = run ([] (int) { return true; });

            const auto [step, corner] = roughness (switching, settled);
            const auto [step20, corner20] = roughness (at20, settled);
            const auto [stepAll, cornerAll] = roughness (atAll, settled);
            std::printf ("FET Ratio 20 <-> All at Attack %g ms, %s: largest step %.4f dB, second difference %.2e dB; held at 20:1 %.4f / %.2e, at All %.4f / %.2e\n",
                         attackMs, name, step, corner, step20, corner20, stepAll, cornerAll);
            INFO (name << " at Attack " << attackMs << " ms");
            REQUIRE (step <= 1.05 * std::max (step20, stepAll) + 0.01);
            REQUIRE (corner <= 1.05 * std::max (corner20, cornerAll) + 5.0e-4);
        }
}

TEST_CASE ("FET: an Attack or Release jump glides", "[fet][clicks]")
{
    // Attack 300 ms leaves the attack far from done when it jumps to 0.02 ms: the 20 ms glide in
    // log time closes the gap smoothly. Release 5 s → 5 ms mid-release likewise.
    const auto key = carrier (amplitudeFor (-4.0), samplesFor (1.0, rate));
    const auto jumpAt = samplesFor (0.1, rate) / 64 * 64;
    auto m = prepared (rate);
    const auto r = runScheduled (m, key, nullptr, 64, [&] (int pos) { return params (-18.0, 4.0, 6.0, 40.0, pos >= jumpAt ? 0.02 : 300.0, 150.0); })[0];
    const auto [step, corner] = roughness (r, (size_t) jumpAt - 64);
    INFO ("Attack: largest step " << step << " dB, largest second difference " << corner << " dB; reduction at the jump " << r[(size_t) jumpAt]);
    REQUIRE (r[(size_t) jumpAt] < 5.0);
    REQUIRE_THAT (r.back(), WithinAbs (ref::staticReductionDb (-4.0, -18.0, 4.0, 6.0, 40.0), 0.01));
    REQUIRE (step < 0.05);
    REQUIRE (corner < 1.0e-3);

    auto burst = carrier (amplitudeFor (-4.0), samplesFor (0.5, rate));
    append (burst, silence (samplesFor (1.5, rate)));
    const auto releaseJump = samplesFor (0.8, rate) / 64 * 64;
    auto mr = prepared (rate);
    const auto rr = runScheduled (mr, burst, nullptr, 64, [&] (int pos) { return params (-18.0, 4.0, 6.0, 40.0, 0.02, pos >= releaseJump ? 5.0 : 5000.0); })[0];
    const auto [rStep, rCorner] = roughness (rr, (size_t) releaseJump - 64);
    std::printf ("FET Release 5 s -> 5 ms mid-release: largest step %.4f dB, largest second difference %.2e dB\n", rStep, rCorner);
    REQUIRE (rStep < 0.05);
    REQUIRE (rCorner < 1.0e-3);
    REQUIRE (isExactly (rr.back(), 0.0));
}

TEST_CASE ("FET: the output does not depend on the block size", "[fet][blocks]")
{
    // Settings still: blocks of 1, 7, 64, 448 and 4096 give the same bits. Settings changed at
    // samples every block size starts a block on: the same bits too.
    const auto n = samplesFor (3.0, rate);
    auto left = pinkNoise (n, 41u), right = pinkNoise (n, 42u);
    scale (left, 0.5);
    scale (right, 0.3);
    const Signal la ((size_t) n, 120.0);

    const auto still = [] (int) {
        auto p = params (-30.0, ref::infinity, 6.0, 40.0, 0.2, 80.0);
        p.linked = true;
        p.link = 0.8;
        return p;
    };
    const auto moving = [] (int pos) {
        const auto phase = pos / 28672;     // 7 · 4096
        auto p = params (-30.0 + 4.0 * (phase % 3), phase % 2 == 0 ? 20.0 : ref::infinity, 6.0, 40.0, phase % 2 == 0 ? 0.2 : 3.0,
                         phase % 3 == 0 ? 80.0 : 400.0);
        p.linked = true;
        p.link = 0.8;
        return p;
    };

    for (const auto& [name, at] : { std::pair<const char*, std::function<ModelParams (int)>> { "still", still }, { "moving", moving } })
    {
        auto reference = prepared (rate);
        const auto expected = runScheduled (reference, left, &right, 1, at, &la);
        for (const auto block : { 7, 64, 448, 4096 })
        {
            if (std::string (name) == "moving" && 28672 % block != 0)
                continue;
            auto m = prepared (rate);
            const auto got = runScheduled (m, left, &right, block, at, &la);
            INFO (name << ", block " << block);
            REQUIRE (got[0] == expected[0]);
            REQUIRE (got[1] == expected[1]);
        }
    }
}

TEST_CASE ("FET: a key that is not a number reads as silence, an infinite one as +120 dBFS", "[fet][nan]")
{
    // The chain sanitises the key (saneKey); the model takes its magnitude the same way, so a NaN
    // or an infinity can neither poison the loop nor the pre-hold's window.
    const auto n = samplesFor (1.0, rate);
    auto clean = sine (amplitudeFor (-10.0), 440.0, rate, n);
    auto dirty = clean;
    auto expected = clean;
    for (const auto at : { 1000, 1001, 20000, 30000 })
    {
        dirty[(size_t) at] = std::numeric_limits<double>::quiet_NaN();
        expected[(size_t) at] = 0.0;
    }
    dirty[25000] = std::numeric_limits<double>::infinity();
    expected[25000] = keyLimit;
    dirty[26000] = -std::numeric_limits<double>::infinity();
    expected[26000] = -keyLimit;

    for (const auto ratio : { 4.0, ref::infinity })
    {
        const auto p = params (-30.0, ratio, 6.0, 40.0, 0.2, 100.0);
        auto a = prepared (rate), b = prepared (rate);
        const auto got = runModel (a, p, dirty)[0];
        const auto want = runModel (b, p, expected)[0];
        REQUIRE (got == want);
        REQUIRE (std::all_of (got.begin(), got.end(), [] (double v) { return std::isfinite (v) && v >= 0.0 && v <= 40.0; }));
    }
}
