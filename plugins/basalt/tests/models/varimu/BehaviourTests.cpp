// Vari-mu as the chain uses it (dynamics/NOTES.md, Model interface; NOTES.md here, Gate table):
// the idle zeros, the stereo link, the seed and the warm-up, knob changes, block sizes.

#include "VariMuHarness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <functional>
#include <random>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using namespace basalt::testing::varimu;
using Catch::Matchers::WithinAbs;
using myzel::dsp::isExactly;

namespace
{
    constexpr double rate = 48000.0;

    bool allZero (const Trace& r)
    {
        return std::all_of (r.begin(), r.end(), [] (double v) { return isExactly (v, 0.0) && isExactly (gainFor (v), 1.0); });
    }

    /** Two unrelated bursty noises, 10 s. */
    std::pair<Signal, Signal> stereoNoise (int n)
    {
        auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
        for (int i = 0; i < n; ++i)
        {
            const auto envelope = 0.2 + 0.8 * std::abs (std::sin (2.0 * std::numbers::pi * 0.7 * i / rate));
            left[(size_t) i] *= 0.3 * envelope;
            right[(size_t) i] *= 0.3 * (1.2 - envelope);
        }
        return { left, right };
    }
}

TEST_CASE ("Vari-mu idle: exactly 0 at Ratio 1, Range 0, and under T - W/2 and T - 13 dB", "[varimu][idle]")
{
    // NOTES.md, Gate table, Identity: the base curve is 0 there and the rise under 10⁻⁶ dB at every
    // Ratio, so the loop's snap makes the gain exactly 1.
    constexpr double t = -30.0;
    const auto loud = carrier (amplitudeFor (-3.0), samplesFor (0.5, rate));

    for (const auto autoRelease : { false, true })
    {
        auto m = prepared (rate);
        REQUIRE (allZero (reductionFor (m, params (t, 1.0, 6.0, 40.0, 10.0, 150.0, autoRelease), loud)));
        m.reset();
        REQUIRE (allZero (reductionFor (m, params (t, 0.25, 6.0, 0.0, 10.0, 150.0, autoRelease), loud)));

        for (auto ratio = 1.01; ratio <= 25.0; ratio *= 1.15)
            for (const auto knee : { 0.0, 6.0, 24.0 })
                for (const auto attackMs : { 0.02, 10.0, 300.0 })
                {
                    const auto p = params (t, ratio > 20.0 ? 0.0 : 1.0 / ratio, knee, 40.0, attackMs, 150.0, autoRelease);
                    const auto below = t + std::min (-0.5 * knee, -13.0) - 0.01;
                    m.reset();
                    INFO ("ratio " << ratio << ", knee " << knee << ", attack " << attackMs);
                    REQUIRE (allZero (reductionFor (m, p, carrier (amplitudeFor (below), samplesFor (0.3, rate)))));
                    m.reset();
                    REQUIRE (allZero (reductionFor (m, p, sine (amplitudeFor (below), 1000.0, rate, samplesFor (0.3, rate)))));
                }
    }

    SECTION ("After a burst the reduction and both memories land on exactly 0 and stay there")
    {
        for (const auto& [attackMs, releaseMs] : { std::pair { 0.02, 5.0 }, std::pair { 10.0, 150.0 }, std::pair { 300.0, 5000.0 } })
        {
            auto key = carrier (amplitudeFor (t + 20.0), samplesFor (1.0, rate));
            const auto end = key.size();
            const auto longest = std::min (ref::slowTier * releaseMs * 0.001, ref::tierCapS);
            append (key, silence (samplesFor (longest * 22.0, rate)));

            auto m = prepared (rate);
            const auto r = reductionFor (m, params (t, 0.25, 6.0, 40.0, attackMs, releaseMs, true), key, 4096);
            auto firstZero = r.size();
            for (auto i = end; i < r.size(); ++i)
                if (isExactly (r[i], 0.0))
                {
                    firstZero = i;
                    break;
                }
            INFO ("attack " << attackMs << " ms, release " << releaseMs << " ms");
            REQUIRE (firstZero < r.size());
            REQUIRE (std::all_of (r.begin() + (std::ptrdiff_t) firstZero, r.end(), [] (double v) { return isExactly (v, 0.0); }));
            REQUIRE (isExactly (m.memory (0, 0), 0.0));
            REQUIRE (isExactly (m.memory (0, 1), 0.0));
            std::printf ("Vari-mu idle snap, Attack %g ms, Release %g ms, Auto on: exactly 0 after %.1f s\n", attackMs, releaseMs,
                         (double) (firstZero - end) / rate);
        }
    }
}

TEST_CASE ("Vari-mu stereo: Link 100 % gives both channels the same reduction to the bit, Link 0 % is Unlinked", "[varimu][stereo]")
{
    const auto n = samplesFor (10.0, rate);
    const auto [left, right] = stereoNoise (n);
    const Signal lookahead ((size_t) n, 96.0);

    for (const auto& [attackMs, autoRelease] : { std::pair { 0.4, true }, std::pair { 10.0, false }, std::pair { 50.0, true } })
    {
        auto p = params (-40.0, 0.25, 6.0, 40.0, attackMs, 300.0, autoRelease);

        p.linked = true;
        p.link = 1.0;
        auto m = prepared (rate);
        const auto full = runModel (m, p, left, &right, 97, &lookahead);
        REQUIRE (full[0] == full[1]);
        REQUIRE (*std::max_element (full[0].begin(), full[0].end()) > 6.0);

        p.link = 0.0;
        m.reset();
        const auto none = runModel (m, p, left, &right, 64, &lookahead);

        p.linked = false;
        m.reset();
        const auto unlinked = runModel (m, p, left, &right, 512, &lookahead);
        REQUIRE (none[0] == unlinked[0]);
        REQUIRE (none[1] == unlinked[1]);
        REQUIRE_FALSE (none[0] == none[1]);
    }
}

TEST_CASE ("Vari-mu switch: a seed starts the loop at the applied reduction, the memories empty", "[varimu][switch]")
{
    // Settled 33: at Attack 300 ms the incoming model starts at the applied 10 dB instead of
    // re-attacking from 0; the pre-hold, run by follow(), already reads the key.
    constexpr double t = -30.0;
    for (const auto autoRelease : { false, true })
    {
        const auto p = params (t, 0.25, 6.0, 40.0, 300.0, 1000.0, autoRelease);
        const auto level = carrier (carrierAmplitudeFor (p, 10.0), samplesFor (3.0, rate));

        auto m = prepared (rate);
        KeyBlock key;
        key.numChannels = 1;
        key.numSamples = samplesFor (m.settleMs (p) * 0.001, rate) + 1;
        key.channels[0] = level.data();
        m.follow (p, key);

        const double seed[] { 10.0 };
        m.seed (seed, 1);
        REQUIRE (isExactly (m.reduction (0), 10.0));
        REQUIRE (isExactly (m.memory (0, 0), 0.0));
        const auto r = reductionFor (m, p, level);
        REQUIRE_THAT (*std::min_element (r.begin(), r.end()), WithinAbs (10.0, 1.0e-6));
        REQUIRE_THAT (*std::max_element (r.begin(), r.end()), WithinAbs (10.0, 1.0e-6));
    }
}

TEST_CASE ("Vari-mu switch: warmed up for settleMs and seeded, it carries on as a model that ran throughout", "[varimu][switch]")
{
    // M1's switch (dynamics/NOTES.md, The chain): from reset() the model follows the key unheard for
    // settleMs plus the look-ahead window, is seeded with the applied reduction, then heard. With
    // Auto off the loop and the pre-hold are all it has, so it must match one that ran throughout.
    const auto n = samplesFor (6.0, rate);
    const auto [left, right] = stereoNoise (n);
    const auto lookaheadSamples = 120;
    const Signal lookahead ((size_t) n, (double) lookaheadSamples);

    auto p = params (-40.0, 0.25, 6.0, 40.0, 30.0, 300.0, false);
    p.linked = true;
    p.link = 0.7;

    auto throughout = prepared (rate);
    const auto reference = runModel (throughout, p, left, &right, 64, &lookahead);

    const auto switchAt = samplesFor (3.0, rate);
    const auto warm = (int) std::ceil (throughout.settleMs (p) * 0.001 * rate) + lookaheadSamples + 1;

    auto incoming = prepared (rate);
    KeyBlock key;
    key.numChannels = 2;
    key.numSamples = warm;
    key.channels = { left.data() + switchAt - warm, right.data() + switchAt - warm };
    key.lookahead = lookahead.data();
    incoming.follow (p, key);
    const double seed[] { reference[0][(size_t) switchAt - 1], reference[1][(size_t) switchAt - 1] };
    incoming.seed (seed, 2);

    const Signal tailL (left.begin() + switchAt, left.end()), tailR (right.begin() + switchAt, right.end());
    const Signal tailLa (lookahead.begin() + switchAt, lookahead.end());
    const auto after = runModel (incoming, p, tailL, &tailR, 64, &tailLa);

    auto worst = 0.0;
    for (int c = 0; c < 2; ++c)
        for (size_t i = 0; i < after[(size_t) c].size(); ++i)
            worst = std::max (worst, std::abs (after[(size_t) c][i] - reference[(size_t) c][(size_t) switchAt + i]));
    std::printf ("Vari-mu switched in after its warm-up: within %.1e dB of the model that ran throughout\n", worst);
    REQUIRE (worst < 1.0e-6);
}

namespace
{
    /** One channel of key in 64-sample blocks, the parameters per block from `at` (the block's first sample). */
    Trace runScheduled (VariMuModel& m, const Signal& key, const std::function<ModelParams (int)>& at, int block = 64)
    {
        Trace r;
        std::vector<double> out ((size_t) block);
        for (int start = 0; start < (int) key.size(); start += block)
        {
            KeyBlock k;
            k.numSamples = std::min (block, (int) key.size() - start);
            k.channels[0] = key.data() + start;
            double* o[] { out.data(), nullptr };
            m.process (at (start), k, o);
            r.insert (r.end(), out.begin(), out.begin() + k.numSamples);
        }
        return r;
    }

    std::pair<double, double> roughness (const Trace& r, size_t from, size_t to = std::numeric_limits<size_t>::max())
    {
        auto step = 0.0, corner = 0.0;
        for (size_t i = std::max<size_t> (from, 2); i < std::min (r.size(), to); ++i)
        {
            step = std::max (step, std::abs (r[i] - r[i - 1]));
            corner = std::max (corner, std::abs (r[i] - 2.0 * r[i - 1] + r[i - 2]));
        }
        return { step, corner };
    }
}

TEST_CASE ("Vari-mu clicks: a curve knob jump neither steps nor bends the reduction", "[varimu][clicks]")
{
    // The core's click rows at model level, as Clean's: a key 14 dB over the default Threshold;
    // every 0.31 s a knob jumps between its ends while the reduction still moves from the last
    // jump, at the attack floor and at 10 ms. The fade keeps every second difference of the order
    // of the raised cosine's own. A Ratio or Knee jump also moves R_ref and Φ, as coefficients
    // only (NOTES.md, Gate table, Clicks). Auto off: a memory taking over a release is a corner of
    // the model's own, measured below, not the knob's; so is the state reaching Range, which
    // Threshold −60 dB would make it do on this key (also below), so Threshold jumps to −40.
    const auto period = samplesFor (0.31, rate);

    struct Jump
    {
        const char* name;
        double keyDb;
        std::function<void (ModelParams&, bool)> set;
    };
    const Jump jumps[] {
        { "Threshold -40 <-> 0", -4.0, [] (ModelParams& p, bool b) { p.curve.thresholdDb = b ? 0.0 : -40.0; } },
        { "Ratio 1 <-> inf", -4.0, [] (ModelParams& p, bool b) { p.curve.inverseRatio = b ? 0.0 : 1.0; } },
        { "Ratio 1.5 <-> 20", -4.0, [] (ModelParams& p, bool b) { p.curve.inverseRatio = b ? 0.05 : 1.0 / 1.5; } },
        { "Knee 0 <-> 24", -15.0, [] (ModelParams& p, bool b) { p.curve.kneeDb = b ? 24.0 : 0.0; } },
        { "Range 0 <-> 40", -4.0, [] (ModelParams& p, bool b) { p.curve.rangeDb = b ? 40.0 : 0.0; } },
    };

    for (const auto attackMs : { 0.1, 10.0 })
        for (const auto& jump : jumps)
        {
            const auto key = carrier (amplitudeFor (jump.keyDb), samplesFor (2.0, rate));
            auto m = prepared (rate);
            const auto r = runScheduled (m, key, [&] (int pos)
            {
                auto p = params (-18.0, 0.25, 6.0, 40.0, attackMs, 150.0, false);
                jump.set (p, (pos / period) % 2 == 1);
                return p;
            });
            const auto [step, corner] = roughness (r, 64);
            std::printf ("Vari-mu knob jump at Attack %g ms, %-20s: largest step %.4f dB, largest second difference %.2e dB\n", attackMs, jump.name,
                         step, corner);
            INFO (jump.name << " at Attack " << attackMs << ": step " << step << ", corner " << corner);
            REQUIRE (step > 1.0e-4);
            REQUIRE (step < 0.1);
            REQUIRE (corner < 5.0e-4);
        }
}

TEST_CASE ("Vari-mu clicks: time knobs glide mid-attack and mid-release", "[varimu][clicks]")
{
    // Changed at once, a time knob bends the reduction where it moves: Attack 300 → 0.1 ms in
    // mid-attack would close a gap of dB within a sample, Release 5 s → 5 ms in mid-release would
    // turn a 4·10⁻⁵ dB-per-sample slope into 0.04 within one. The 20 ms glides in log time spread
    // both. Auto release turned off while the 5 × memory carries the release glides its tiers.
    struct Change
    {
        const char* name;
        double sustainS, jumpAfterS;    // the key's length, and the jump relative to its end
        double attackMs, releaseMs;
        std::function<void (ModelParams&, bool)> set;
    };
    const Change changes[] {
        { "Attack 300 -> 0.1 ms mid-attack", 1.0, -0.9, 300.0, 300.0, [] (ModelParams& p, bool b) { p.attackMs = b ? 0.1 : 300.0; } },
        { "Release 5 s -> 5 ms mid-release", 1.0, 0.010, 0.4, 5000.0, [] (ModelParams& p, bool b) { p.releaseMs = b ? 5.0 : 5000.0; p.autoRelease = false; } },
        { "Auto off mid-release", 3.0, 0.050, 0.4, 300.0, [] (ModelParams& p, bool b) { p.autoRelease = ! b; } },
    };

    for (const auto& change : changes)
    {
        auto key = carrier (amplitudeFor (-4.0), samplesFor (change.sustainS, rate));
        const auto stop = (int) key.size();
        append (key, silence (samplesFor (1.0, rate)));
        const auto jumpAt = (stop + samplesFor (change.jumpAfterS, rate)) / 64 * 64;

        auto m = prepared (rate);
        const auto r = runScheduled (m, key, [&] (int pos)
        {
            auto p = params (-18.0, 0.25, 6.0, 40.0, change.attackMs, change.releaseMs, true);
            change.set (p, pos >= jumpAt);
            return p;
        });
        const auto [step, corner] = roughness (r, (size_t) jumpAt - 64, (size_t) (jumpAt + samplesFor (0.05, rate)));
        std::printf ("Vari-mu %-32s: at %.2f dB, largest step %.4f dB, largest second difference %.2e dB around it\n", change.name,
                     r[(size_t) jumpAt], step, corner);
        INFO (change.name << ": step " << step << ", corner " << corner);
        REQUIRE (step < 0.05);
        REQUIRE (corner < 1.0e-3);
    }
}

TEST_CASE ("Vari-mu: the state reaching Range bends the reduction, reported", "[varimu][clicks]")
{
    // Range sits on the loop's state (dynamics/NOTES.md, Range; Settled 29): an attack whose demand
    // lies far beyond Range arrives at it with the slope it has. On the rising curve the demand of
    // a key 56 dB over Threshold is hundreds of dB (the 20:1 top inverted), so the state reaches
    // 40 dB at speed. The core's choice, measured here for the integrator; FET's loop does the same.
    constexpr double rate48 = 48000.0;
    for (const auto attackMs : { 0.1, 10.0, 300.0 })
    {
        auto key = silence (16);
        append (key, carrier (amplitudeFor (-4.0), samplesFor (std::max (2.0, attackMs * 0.04), rate48)));
        auto m = prepared (rate48);
        const auto r = reductionFor (m, params (-60.0, 0.25, 6.0, 40.0, attackMs, 150.0), key);
        size_t at = 16;
        while (at < r.size() && r[at] < 40.0)
            ++at;
        const auto slope = at < r.size() ? r[at - 1] - r[at - 2] : 0.0;
        std::printf ("Vari-mu reaching Range 40 from a key 56 dB over Threshold, Attack %g ms: arrives at %.2e dB per sample, after %.2f ms\n", attackMs,
                     slope, 1000.0 * (double) (at - 16) / rate48);
        REQUIRE (at < r.size());
    }
}

TEST_CASE ("Vari-mu: a memory taking over the release bends it as the loop's own release start does, reported", "[varimu][clicks]")
{
    // r = max(s, m, ℓ) (NOTES.md, Auto release): when the loop's state falls under a charged
    // memory, the release slope changes within a sample. The loop itself does the same where its
    // release starts after the pre-hold (smooth branching, dynamics/NOTES.md, Feedback). Both on
    // one stimulus: 1 s at 14 dB over Threshold, then silence, Release 150 ms, Auto on.
    constexpr double rate48 = 48000.0;
    auto key = carrier (amplitudeFor (-4.0), samplesFor (1.0, rate48));
    const auto stop = key.size();
    append (key, silence (samplesFor (1.0, rate48)));
    auto m = prepared (rate48);
    const auto r = reductionFor (m, params (-18.0, 0.25, 6.0, 40.0, 0.4, 150.0, true), key);

    auto releaseStart = 0.0, takeover = 0.0;
    size_t takeoverAt = 0;
    for (auto i = stop; i + 1 < r.size(); ++i)
    {
        const auto corner = std::abs (r[i + 1] - 2.0 * r[i] + r[i - 1]);
        if (i < stop + (size_t) samplesFor (0.010, rate48))
            releaseStart = std::max (releaseStart, corner);
        else if (corner > takeover)
        {
            takeover = corner;
            takeoverAt = i;
        }
    }
    std::printf ("Vari-mu release corners, Release 150 ms, Auto on: the loop's release start %.2e dB, the 5 × memory taking over %.2e dB "
                 "(at %.2f dB, %.0f ms into the release)\n",
                 releaseStart, takeover, r[takeoverAt], 1000.0 * (double) (takeoverAt - stop) / rate48);
    REQUIRE (takeover <= releaseStart);
}

TEST_CASE ("Vari-mu: the same bits at every block size", "[varimu][blocks]")
{
    const auto n = samplesFor (4.0, rate);
    const auto [left, right] = stereoNoise (n);
    Signal lookahead ((size_t) n);
    for (int i = 0; i < n; ++i)
        lookahead[(size_t) i] = std::min (240.0, i / 400.0);

    // Settings that move at multiples of 448 samples, a block boundary for 1, 7, 64 and 448.
    const auto at = [] (int pos)
    {
        auto p = params (-40.0, 0.25, 6.0, 40.0, 0.4, 300.0, true);
        p.linked = true;
        p.link = 0.8;
        const auto segment = pos / (448 * 50);
        p.curve.thresholdDb = segment % 3 == 1 ? -30.0 : -40.0;
        p.curve.inverseRatio = segment % 4 == 2 ? 0.1 : 0.25;
        p.attackMs = segment % 5 == 3 ? 30.0 : 0.4;
        p.autoRelease = segment % 2 == 0;
        return p;
    };

    const auto render = [&] (const std::function<int (int)>& blockAt)
    {
        auto m = prepared (rate);
        std::array<Trace, 2> out { Trace ((size_t) n), Trace ((size_t) n) };
        for (int start = 0, b = 0; start < n; ++b)
        {
            const auto len = std::min (blockAt (b), n - start);
            KeyBlock key;
            key.numChannels = 2;
            key.numSamples = len;
            key.channels = { left.data() + start, right.data() + start };
            key.lookahead = lookahead.data() + start;
            double* o[] { out[0].data() + start, out[1].data() + start };
            m.process (at (start), key, o);
            start += len;
        }
        return out;
    };

    const auto reference = render ([] (int) { return 448; });
    for (const auto block : { 1, 7, 64 })
    {
        INFO ("block " << block);
        REQUIRE (render ([block] (int) { return block; }) == reference);
    }

    // With the settings still, any sequence of block sizes.
    const auto stillRender = [&] (const std::function<int (int)>& blockAt)
    {
        auto m = prepared (rate);
        const auto p = at (0);
        std::array<Trace, 2> out { Trace ((size_t) n), Trace ((size_t) n) };
        for (int start = 0, b = 0; start < n; ++b)
        {
            const auto len = std::min (blockAt (b), n - start);
            KeyBlock key;
            key.numChannels = 2;
            key.numSamples = len;
            key.channels = { left.data() + start, right.data() + start };
            key.lookahead = lookahead.data() + start;
            double* o[] { out[0].data() + start, out[1].data() + start };
            m.process (p, key, o);
            start += len;
        }
        return out;
    };
    const auto still = stillRender ([] (int) { return 4096; });
    std::mt19937 rng (5u);
    std::uniform_int_distribution<int> size (1, 4096);
    REQUIRE (stillRender ([&] (int) { return size (rng); }) == still);
    REQUIRE (stillRender ([] (int) { return 1; }) == still);
}
