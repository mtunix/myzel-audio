// The chain's wiring (PLAN.md, Signal flow, Latency, Auto Gain; dynamics/NOTES.md, Key path,
// Stereo, Look-ahead, Clicks): key source and key EQ, stereo modes, latency and look-ahead,
// Listen, Mix against the aligned dry path, Auto Gain, the switch crossfades, block-size
// invariance, the meters. Most read the gain exactly: the key carries the test signal and the
// main bus a ±0.25 constant-magnitude probe, so out / in is the float gain itself.

#include "dsp/chain/BasaltChain.h"
#include "dsp/models/clean/CleanModel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <numbers>
#include <random>

using namespace basalt;
using namespace basalt::dsp;
using Catch::Matchers::WithinAbs;
using myzel::dsp::isExactly;

namespace
{
    constexpr double rate = 48000.0;
    constexpr float probe = 0.25f;

    using Channels = std::vector<std::vector<float>>;
    using Automation = std::function<void (int position, ChainSettings&)>;

    int samples (double seconds) { return (int) std::lround (seconds * rate); }
    float gainOf (double db) { return (float) std::pow (10.0, db / 20.0); }

    std::vector<float> carrier (int n, float a)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = i % 2 == 0 ? a : -a;
        return x;
    }

    std::vector<float> sine (double hz, int n, float a, double phase = 0.0)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = (float) (a * std::sin (2.0 * std::numbers::pi * hz * i / rate + phase));
        return x;
    }

    std::vector<float> noise (int n, float a, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> d (-a, a);
        std::vector<float> x ((size_t) n);
        for (auto& v : x)
            v = d (rng);
        return x;
    }

    /** Silence, then `a` from `on` to `off`, then silence again. */
    std::vector<float> burst (int n, float a, int on, int off)
    {
        auto x = carrier (n, a);
        for (int i = 0; i < n; ++i)
            if (i < on || i >= off)
                x[(size_t) i] = 0.0f;
        return x;
    }

    struct Rendered
    {
        Channels out;
        int latency = 0;
    };

    /** A fresh chain over the whole input, in blocks of the given sizes (cycled). */
    Rendered render (ChainSettings s, Channels main, const Channels& key = {}, const std::vector<int>& blocks = { 256 },
                     const Automation& automate = {}, BasaltChain* keep = nullptr)
    {
        auto owned = std::make_unique<BasaltChain>();
        auto& chain = keep != nullptr ? *keep : *owned;
        chain.prepare (rate, 4096, (int) main.size(), s.latency);

        Rendered r;
        r.latency = BasaltChain::latencySamples (s.latency, rate);
        const auto n = (int) main[0].size();
        size_t k = 0;
        for (int pos = 0; pos < n;)
        {
            const auto len = std::min (blocks[k++ % blocks.size()], n - pos);
            if (automate)
                automate (pos, s);
            std::array<float*, 2> m {};
            for (size_t c = 0; c < main.size(); ++c)
                m[c] = main[c].data() + pos;
            std::array<const float*, 2> kp {};
            for (size_t c = 0; c < key.size(); ++c)
                kp[c] = key[c].data() + pos;
            chain.process (m.data(), (int) main.size(), len, s, key.empty() ? nullptr : kp.data(), (int) key.size());
            pos += len;
        }
        r.out = std::move (main);
        return r;
    }

    /** The reduction in dB the probe on channel c read, per input sample (out[n + latency] against in[n]). */
    std::vector<double> reductionOf (const Rendered& r, int c = 0)
    {
        const auto& out = r.out[(size_t) c];
        std::vector<double> db (out.size() - (size_t) r.latency);
        for (size_t i = 0; i < db.size(); ++i)
            db[i] = -20.0 * std::log10 (std::abs ((double) out[i + (size_t) r.latency]) / probe);
        return db;
    }

    Channels probes (int channels, int n)
    {
        return Channels ((size_t) channels, carrier (n, probe));
    }

    ChainSettings compressing()
    {
        ChainSettings s;
        s.thresholdDb = -30.0;
        s.inverseRatio = 0.25;
        s.kneeDb = 0.0;
        s.attackMs = 10.0;
        s.releaseMs = 100.0;
        s.autoRelease = false;
        return s;
    }

    bool sameBits (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; }

    /** The first sample where two renders differ in any channel, or -1. */
    long firstDifference (const Channels& a, const Channels& b)
    {
        auto first = -1L;
        for (size_t c = 0; c < std::min (a.size(), b.size()); ++c)
            for (size_t i = 0; i < std::min (a[c].size(), b[c].size()); ++i)
                if (! sameBits (a[c][i], b[c][i]) && (first < 0 || (long) i < first))
                {
                    first = (long) i;
                    break;
                }
        return first;
    }
}

//==============================================================================
TEST_CASE ("Chain: the audio is delayed by the reported latency, the dry path too", "[chain][latency]")
{
    for (const auto mode : { LatencyMode::normal, LatencyMode::live })
    {
        INFO ((mode == LatencyMode::live ? "Live" : "Normal"));
        ChainSettings s = compressing();
        s.latency = mode;
        s.mix = 0.0;    // the dry path alone, while the wet one compresses

        const auto n = samples (0.2);
        auto in = noise (n, 0.9f, 3u);
        const auto r = render (s, { in, in });
        const auto latency = BasaltChain::latencySamples (mode, rate);
        CHECK (r.latency == (mode == LatencyMode::live ? 0 : 480));
        for (int i = 0; i < n; ++i)
            REQUIRE (sameBits (r.out[0][(size_t) i], i >= latency ? in[(size_t) (i - latency)] : 0.0f));
    }
}

TEST_CASE ("Chain: look-ahead moves the attack early and leaves the release in audio time", "[chain][latency]")
{
    // dynamics/NOTES.md, Look-ahead: the attack side is the LA 0 trace shifted by LA, the release side identical.
    const auto n = samples (1.5), on = samples (0.2), off = samples (0.9);
    const Channels key { burst (n, gainOf (-20.0), on, off) };

    auto s = compressing();
    s.key = KeySource::external;
    const auto r0 = reductionOf (render (s, probes (1, n), key));
    REQUIRE (r0[(size_t) on + 2000] > 5.0);

    for (const auto la : { 3.0, 10.0 })
    {
        INFO ("look-ahead " << la << " ms");
        s.lookaheadMs = la;
        const auto r = reductionOf (render (s, probes (1, n), key));
        const auto shift = (int) std::lround (la * 0.001 * rate);

        auto attack = 0.0, release = 0.0;
        for (int i = 0; i + shift < off; ++i)
            attack = std::max (attack, std::abs (r[(size_t) i] - r0[(size_t) (i + shift)]));
        for (int i = off; i < (int) r.size(); ++i)
            release = std::max (release, std::abs (r[(size_t) i] - r0[(size_t) i]));
        CHECK (attack < 1.0e-6);
        CHECK (release < 1.0e-6);
        CHECK (r[(size_t) (on - shift)] > 0.0);     // it starts LA early
        CHECK (isExactly (r[(size_t) (on - shift - 1)], 0.0));
    }

    SECTION ("Live has none, whatever Look-ahead says")
    {
        s.latency = LatencyMode::live;
        s.lookaheadMs = 10.0;
        const auto live = reductionOf (render (s, probes (1, n), key));
        CHECK (isExactly (live[(size_t) on - 1], 0.0));
        CHECK (live[(size_t) on] > 0.0);
    }
}

TEST_CASE ("Chain: a change of latency fades out, switches and fades in", "[chain][latency]")
{
    ChainSettings s;
    s.inverseRatio = 1.0;   // idle
    const auto n = samples (0.5), at = samples (0.2);
    auto in = sine (440.0, n, 0.5f);
    const auto r = render (s, { in }, {}, { 64 }, [at] (int pos, ChainSettings& x) { x.latency = pos >= at ? LatencyMode::live : LatencyMode::normal; });

    // Out by the switch, back in Live's alignment within 10 + 10 ms, every sample finite.
    auto quietest = 1.0f;
    for (int i = at; i < at + samples (0.012); ++i)
        quietest = std::min (quietest, std::abs (r.out[0][(size_t) i]));
    CHECK (isExactly (quietest, 0.0f));
    for (int i = 0; i < n; ++i)
        REQUIRE (std::isfinite (r.out[0][(size_t) i]));
    for (int i = at + samples (0.03); i < n; ++i)
        REQUIRE (sameBits (r.out[0][(size_t) i], in[(size_t) i]));
}

//==============================================================================
TEST_CASE ("Chain: the key is the input, the side chain, or the input when the side chain is off", "[chain][key]")
{
    const auto n = samples (0.6);
    const auto loud = carrier (n, gainOf (-10.0));
    auto s = compressing();

    SECTION ("External: the side chain decides, the main bus is only processed")
    {
        s.key = KeySource::external;
        CHECK (reductionOf (render (s, probes (2, n), { loud, loud })).back() > 10.0);
        CHECK (isExactly (reductionOf (render (s, probes (2, n), { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) })).back(), 0.0));

        // Main and key swapped: the loud main bus passes untouched under a silent key.
        auto quiet = render (s, { loud, loud }, { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) });
        for (int i = 480; i < n; ++i)
            REQUIRE (sameBits (quiet.out[0][(size_t) i], loud[(size_t) (i - 480)]));
    }

    SECTION ("Internal: the side chain is ignored")
    {
        // The probe (−12 dBFS, −9 dB on the RMS half) under Threshold, a key far over it.
        s.key = KeySource::internal;
        s.thresholdDb = -5.0;
        const auto full = carrier (n, 0.9f);
        CHECK (isExactly (reductionOf (render (s, probes (2, n), { full, full })).back(), 0.0));
        s.key = KeySource::external;
        CHECK (reductionOf (render (s, probes (2, n), { full, full })).back() > 2.0);
    }

    SECTION ("External with the side chain off hears the input (Halite's Settled 143)")
    {
        s.key = KeySource::external;
        const auto internal = render (s, { loud });
        s.key = KeySource::internal;
        const auto reference = render (s, { loud });
        CHECK (internal.out == reference.out);
    }

    SECTION ("A mono key feeds both channels; a stereo key on a mono bus is (L + R)/2")
    {
        s.key = KeySource::external;
        const auto mono = reductionOf (render (s, probes (2, n), { loud }));
        const auto stereo = reductionOf (render (s, probes (2, n), { loud, loud }));
        CHECK (mono == stereo);

        const auto half = carrier (n, gainOf (-10.0) * 0.5f);
        const auto cancel = reductionOf (render (s, probes (1, n), { loud, carrier (n, -gainOf (-10.0)) }));
        CHECK (isExactly (cancel.back(), 0.0));
        CHECK_THAT (reductionOf (render (s, probes (1, n), { loud, std::vector<float> ((size_t) n) })).back(),
                    WithinAbs (reductionOf (render (s, probes (1, n), { half })).back(), 1.0e-9));
    }
}

TEST_CASE ("Chain: the key EQ shapes what the detector hears, never the audio", "[chain][key]")
{
    // Release 300 ms over 3 s: the filters' onset transients have long gone by the end.
    const auto n = samples (3.0);
    auto s = compressing();
    s.key = KeySource::external;
    s.releaseMs = 300.0;
    const Channels low { sine (100.0, n, gainOf (-10.0)) };
    const auto settled = [&] (const ChainSettings& x) { return reductionOf (render (x, probes (1, n), low)).back(); };

    const auto flat = settled (s);
    CHECK (flat > 10.0);

    auto hp = s;
    hp.keyEq.highPass = true;
    hp.keyEq.highPassHz = 2000.0;   // 4.3 octaves above: about 52 dB down
    CHECK (settled (hp) < 0.01);

    auto lp = s;
    lp.keyEq.lowPass = true;
    lp.keyEq.lowPassHz = 2000.0;    // far above 100 Hz: next to nothing
    CHECK_THAT (settled (lp), WithinAbs (flat, 0.05));

    auto cut = s;
    cut.keyEq.bellHz = 100.0;
    cut.keyEq.bellDb = -18.0;
    CHECK_THAT (settled (cut), WithinAbs (flat - 0.75 * 18.0, 0.3));   // 18 dB less over Threshold at Ratio 4

    // Off is off: the stages switched off give the flat key to the bit.
    auto off = s;
    off.keyEq.highPassHz = 2000.0;
    off.keyEq.lowPassHz = 2000.0;
    off.keyEq.bellHz = 100.0;
    CHECK (reductionOf (render (off, probes (1, n), low)) == reductionOf (render (s, probes (1, n), low)));
}

//==============================================================================
TEST_CASE ("Chain: stereo modes apply their gains where they belong", "[chain][stereo]")
{
    const auto n = samples (0.6);
    const auto loud = carrier (n, gainOf (-10.0)), quiet = carrier (n, gainOf (-60.0));
    auto s = compressing();
    s.key = KeySource::external;

    SECTION ("Linked at 100 %: one gain for both channels, to the bit")
    {
        const auto r = render (s, probes (2, n), { loud, quiet });
        CHECK (r.out[0] == r.out[1]);
        CHECK (reductionOf (r, 1).back() > 10.0);
    }

    SECTION ("Linked at 0 % is Unlinked: the quiet channel is untouched")
    {
        s.link = 0.0;
        const auto linked0 = render (s, probes (2, n), { loud, quiet });
        s.stereo = StereoMode::unlinked;
        const auto unlinked = render (s, probes (2, n), { loud, quiet });
        CHECK (linked0.out == unlinked.out);
        CHECK (reductionOf (unlinked, 0).back() > 10.0);
        for (int i = 480; i < n; ++i)
            REQUIRE (sameBits (unlinked.out[1][(size_t) i], probe * (i % 2 == 0 ? 1.0f : -1.0f)));
    }

    SECTION ("Mid and Side: each hears and turns down only its own")
    {
        // Main all side (L = −R): Mid leaves it to the bit, Side turns it down.
        auto sideMain = probes (2, n);
        for (auto& v : sideMain[1])
            v = -v;
        const Channels sideKey { loud, carrier (n, -gainOf (-10.0)) };

        s.stereo = StereoMode::mid;
        const auto mid = render (s, sideMain, sideKey);
        for (int i = 480; i < n; ++i)
            REQUIRE (sameBits (mid.out[0][(size_t) i], sideMain[0][(size_t) (i - 480)]));

        s.stereo = StereoMode::side;
        const auto side = render (s, sideMain, sideKey);
        CHECK (reductionOf (side, 0).back() > 10.0);
        CHECK (isExactly (side.out[0][(size_t) n - 1], -side.out[1][(size_t) n - 1]));

        s.stereo = StereoMode::midSide;
        const auto both = render (s, sideMain, sideKey);
        CHECK (both.out == side.out);
    }

    SECTION ("A mono bus has one detector whatever the mode")
    {
        for (int m = 0; m < numStereoModes; ++m)
        {
            s.stereo = static_cast<StereoMode> (m);
            CHECK (reductionOf (render (s, probes (1, n), { loud })).back() > 10.0);
        }
    }
}

//==============================================================================
TEST_CASE ("Chain: Listen plays the output, the key after its EQ, or what is taken out", "[chain][listen]")
{
    const auto n = samples (0.4);
    const auto settle = samples (0.05);     // past Listen's 30 ms fade
    auto s = compressing();
    s.key = KeySource::external;
    const Channels key { noise (n, 0.8f, 11u), noise (n, 0.6f, 12u) };
    const Channels main { noise (n, 0.5f, 13u), noise (n, 0.5f, 14u) };

    SECTION ("Key, lined up with the audio; Mid, Side and Mid/Side as M and S")
    {
        s.listen = Listen::key;
        for (int m = 0; m < numStereoModes; ++m)
        {
            s.stereo = static_cast<StereoMode> (m);
            INFO ("stereo mode " << m);
            const auto r = render (s, main, key);
            const auto ms = s.stereo != StereoMode::linked && s.stereo != StereoMode::unlinked;
            for (int i = settle; i < n; ++i)
            {
                const auto l = (double) key[0][(size_t) (i - 480)], rr = (double) key[1][(size_t) (i - 480)];
                REQUIRE (sameBits (r.out[0][(size_t) i], (float) (ms ? 0.5 * (l + rr) : l)));
                REQUIRE (sameBits (r.out[1][(size_t) i], (float) (ms ? 0.5 * (l - rr) : rr)));
            }
        }
    }

    SECTION ("Delta: with the output, it adds up to the aligned input")
    {
        const auto output = render (s, main, key);
        s.listen = Listen::delta;
        const auto delta = render (s, main, key);
        auto worst = 0.0, taken = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = settle; i < n; ++i)
            {
                const auto d = (double) delta.out[(size_t) c][(size_t) i];
                worst = std::max (worst, std::abs (output.out[(size_t) c][(size_t) i] + d - main[(size_t) c][(size_t) (i - 480)]));
                taken = std::max (taken, std::abs (d));
            }
        CHECK (taken > 0.1);
        CHECK (worst < 1.0e-7);
    }
}

TEST_CASE ("Chain: Mix blends the latency-aligned dry path", "[chain][mix]")
{
    const auto n = samples (0.5);
    auto s = compressing();
    const Channels in { noise (n, 0.9f, 21u), noise (n, 0.7f, 22u) };

    const auto wet = render (s, in);
    s.mix = 0.0;
    const auto dryOnly = render (s, in);
    s.mix = 0.5;
    const auto half = render (s, in);

    auto worst = 0.0, apart = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = 480; i < n; ++i)
        {
            const auto d = (double) in[(size_t) c][(size_t) (i - 480)];
            REQUIRE (sameBits (dryOnly.out[(size_t) c][(size_t) i], (float) d));
            worst = std::max (worst, std::abs (half.out[(size_t) c][(size_t) i] - 0.5 * (d + wet.out[(size_t) c][(size_t) i])));
            apart = std::max (apart, std::abs (wet.out[(size_t) c][(size_t) i] - d));
        }
    CHECK (apart > 0.1);    // it compresses
    CHECK (worst < 1.0e-6);
}

TEST_CASE ("Chain: Auto Gain gives back the static reduction at -18 dB, on the wet path", "[chain][autogain]")
{
    // models/clean/NOTES.md, Make-up: Threshold −30, Ratio 4, Knee 6 → M = 9.0 dB.
    const auto n = samples (1.5);
    auto s = compressing();
    s.kneeDb = 6.0;
    s.releaseMs = 1000.0;
    s.autoGain = true;
    const auto level = gainOf (-18.0);
    const Channels in { sine (1000.0, n, level) };

    auto chain = std::make_unique<BasaltChain>();
    auto r = render (s, in, {}, { 256 }, {}, chain.get());
    CHECK_THAT (chain->status().autoGainDb.load(), WithinAbs (9.0, 1.0e-5));

    const auto peakDb = [&] (const Rendered& x)
    {
        auto p = 0.0f;
        for (int i = n - samples (0.1); i < n; ++i)
            p = std::max (p, std::abs (x.out[0][(size_t) i]));
        return 20.0 * std::log10 (p);
    };
    CHECK_THAT (peakDb (r), WithinAbs (-18.0, 0.1));

    s.gainDb = 3.0f;
    CHECK_THAT (peakDb (render (s, in)), WithinAbs (-15.0, 0.1));

    // Mix 0 is the dry path alone: Auto Gain never reaches it.
    s.mix = 0.0;
    const auto dry = render (s, in);
    for (int i = 480; i < n; ++i)
        REQUIRE (sameBits (dry.out[0][(size_t) i], in[0][(size_t) (i - 480)]));
}

TEST_CASE ("Chain: Upward's lift passes Mix, Auto Gain, Listen and the meters as a gain above 1", "[chain][upward]")
{
    // upward/NOTES.md, 4. Make-up: the chain applies the reduction −ℓ, so the gain rises above 1;
    // Auto Gain's make-up is the static reduction at −18 dB, here a cut (−ℓ(−18)); the Lift meter
    // reads ℓ. T −10, Ratio 4, hard knee, Floor −70: a key reading −30 dB is lifted 15 dB, and a
    // sine at −18 dB 6 dB.
    const auto n = samples (1.0);
    ChainSettings s;
    s.model = Model::upward;
    s.thresholdDb = -10.0;
    s.inverseRatio = 0.25;
    s.kneeDb = 0.0;
    s.autoRelease = false;
    s.key = KeySource::external;
    const Channels key { carrier (n, gainOf (-30.0 - 10.0 * std::log10 (2.0))) };
    const auto settled = [&] (const Rendered& r) { return reductionOf (r)[(size_t) (n - r.latency - 1)]; };

    auto chain = std::make_unique<BasaltChain>();
    const auto lifted = render (s, probes (2, n), key, { 256 }, {}, chain.get());
    CHECK_THAT (settled (lifted), WithinAbs (-15.0, 0.01));
    CHECK_THAT ((double) chain->meters().liftDb[0].read(), WithinAbs (15.0, 0.01));
    CHECK (isExactly (chain->meters().reductionDb[0].read(), 0.0f));
    CHECK (chain->status().model.load() == static_cast<int> (Model::upward));

    // Mix 50 %: halfway between the dry probe and the lifted one, in amplitude.
    s.mix = 0.5;
    const auto half = render (s, probes (2, n), key);
    const auto g = std::pow (10.0, 15.0 / 20.0);
    CHECK_THAT (std::abs ((double) half.out[0][(size_t) (n - 1)]) / probe, WithinAbs (0.5 * (1.0 + g), 1.0e-3));
    s.mix = 1.0;

    // Listen › Delta: the aligned input less the gain cell's output, negative for what a lift adds.
    s.listen = Listen::delta;
    const auto delta = render (s, probes (2, n), key);
    const auto in = (double) probes (1, n)[0][(size_t) (n - 1 - delta.latency)];
    CHECK_THAT ((double) delta.out[0][(size_t) (n - 1)], WithinAbs (in * (1.0 - g), 1.0e-3));
    s.listen = Listen::output;

    // Auto Gain: the make-up for a lift is a cut, −ℓ(−18) = −6 dB, so the lift and the make-up add up to 9 dB.
    s.autoGain = true;
    auto withMakeup = std::make_unique<BasaltChain>();
    const auto made = render (s, probes (2, n), key, { 256 }, {}, withMakeup.get());
    CHECK_THAT (withMakeup->status().autoGainDb.load(), WithinAbs (-6.0, 1.0e-4));
    CHECK_THAT (settled (made), WithinAbs (-9.0, 0.01));
}

TEST_CASE ("Chain: Bypass is the aligned input while the dynamics run", "[chain]")
{
    const auto n = samples (0.3);
    auto s = compressing();
    s.bypass = true;
    s.gainDb = 6.0f;
    s.mix = 0.3;
    const Channels in { noise (n, 0.9f, 31u) };
    const auto r = render (s, in);
    for (int i = 480; i < n; ++i)
        REQUIRE (sameBits (r.out[0][(size_t) i], in[0][(size_t) (i - 480)]));
}

//==============================================================================
TEST_CASE ("Chain: the output does not depend on the block size", "[chain][blocks]")
{
    // Everything runs per sample and the settings hold still, so 1 … 4096 and variable blocks give the same bits.
    const auto n = samples (0.6);
    ChainSettings s;    // the defaults: Clean, Attack 10 ms, Auto release on
    s.thresholdDb = -30.0;
    s.lookaheadMs = 5.0;
    s.autoGain = true;
    s.mix = 0.7;
    const Channels in { noise (n, 0.9f, 41u), sine (220.0, n, 0.6f) };

    std::mt19937 rng (5u);
    std::uniform_int_distribution<int> size (1, 4096);
    std::vector<int> variable;
    for (int i = 0; i < 40; ++i)
        variable.push_back (size (rng));

    const auto reference = render (s, in, {}, { 256 });
    for (const auto& blocks : std::vector<std::vector<int>> { { 1 }, { 7 }, { 255 }, { 257 }, { 4096 }, variable })
    {
        INFO ("first block " << blocks.front());
        CHECK (render (s, in, {}, blocks).out == reference.out);
    }
}

TEST_CASE ("Chain: across switches the output does not depend on the block size either", "[chain][blocks]")
{
    // Every decision (a warm-up's end, a fade's end, a switch waiting for one) falls at a chunk's
    // start, and a chunk ends where the next is due, so a change the host makes at the same sample
    // gives the same bits whatever its blocks. The changes sit on multiples of 1344, which every
    // block size here divides; some land in a warm-up or a fade, and wait or restart.
    constexpr int grid = 1344;
    const auto n = 60 * grid;
    ChainSettings s;
    s.thresholdDb = -30.0;
    s.attackMs = 100.0;
    s.lookaheadMs = 5.0;
    s.mix = 0.7;
    const Channels in { noise (n, 0.9f, 43u), sine (220.0, n, 0.6f) };
    const Channels key { noise (n, 0.5f, 44u), noise (n, 0.3f, 45u) };

    const Automation automate = [] (int pos, ChainSettings& x)
    {
        const auto at = pos / grid;
        x.stereo = at >= 27 ? StereoMode::mid : (at >= 22 ? StereoMode::unlinked : (at >= 21 ? StereoMode::midSide : StereoMode::linked));
        x.key = at >= 32 ? KeySource::external : KeySource::internal;
        x.listen = at == 40 ? Listen::delta : Listen::output;
        x.latency = at == 50 ? LatencyMode::live : LatencyMode::normal;
        x.thresholdDb = at >= 45 ? -36.0 : -30.0;
        x.attackMs = at >= 46 ? 3.0 : 100.0;
        x.link = at >= 47 ? 0.4 : 1.0;
    };

    const auto reference = render (s, in, key, { 64 }, automate);
    for (const auto& blocks : std::vector<std::vector<int>> { { 1 }, { 7 }, { 448 }, { grid }, { 64, 448, 7, 1, grid - 520 } })
    {
        INFO ("blocks of " << blocks.front());
        CHECK (firstDifference (render (s, in, key, blocks, automate).out, reference.out) == -1);
    }
}

TEST_CASE ("Chain: a switch to Linked starts both channels on the linked reduction", "[chain][switch]")
{
    // Settled 32: at Link 100 % the gains are identical to the bit. A switch from Unlinked seeds
    // the incoming pair with the linked value of the outgoing reductions, so they are from the
    // moment it is heard, through a release below Threshold too.
    const auto n = samples (4.0), at = samples (1.0), quiet = samples (2.0);
    auto s = compressing();
    s.stereo = StereoMode::unlinked;
    s.key = KeySource::external;
    s.autoRelease = true;
    Channels key { carrier (n, gainOf (-10.0)), carrier (n, gainOf (-16.0)) };
    for (auto& ch : key)
        for (int i = quiet; i < n; ++i)
            ch[(size_t) i] *= gainOf (-30.0);

    const auto r = render (s, probes (2, n), key, { 256 }, [at] (int pos, ChainSettings& x) { x.stereo = pos >= at ? StereoMode::linked : StereoMode::unlinked; });

    // The Linked lane follows for Clean's warm-up (50 ms with Auto release), then fades in over 30 ms.
    const auto heard = at + samples (models::CleanModel::crestSettleMs * 0.001) + 1 + samples (DynamicsLane::fadeSeconds) + 256;
    const auto left = reductionOf (r, 0), right = reductionOf (r, 1);
    REQUIRE (left[(size_t) at - 1] > right[(size_t) at - 1] + 3.0);
    long differ = 0;
    for (auto i = (size_t) heard; i < left.size(); ++i)
        differ += sameBits (r.out[0][i + (size_t) r.latency], r.out[1][i + (size_t) r.latency]) ? 0 : 1;
    CHECK (differ == 0);
    CHECK (left[(size_t) (quiet + samples (0.5))] < 1.0);
}

TEST_CASE ("Chain: Character's delay lengthens the latency and moves nothing else", "[chain][latency]")
{
    // M3's Character adds its integer delay to Normal's latency (PLAN.md, Latency). The audio, the
    // dry path, the key's tap and Listen › Key all move by it: the output is the one without it,
    // delayed. Live reports no Character delay. 76 samples is Normal's at 48 kHz.
    constexpr int characterDelay = 76;
    const auto n = samples (1.0);
    const Channels key { burst (n, gainOf (-12.0), samples (0.2), samples (0.5)), burst (n, gainOf (-20.0), samples (0.3), samples (0.6)) };

    for (const auto latency : { LatencyMode::normal, LatencyMode::live })
        for (const auto lookahead : { 0.0, 5.0 })
            for (const auto listen : { Listen::output, Listen::key, Listen::delta })
            {
                INFO ((latency == LatencyMode::live ? "Live" : "Normal") << ", look-ahead " << lookahead << " ms, Listen " << (int) listen);
                auto s = compressing();
                s.key = KeySource::external;
                s.latency = latency;
                s.lookaheadMs = lookahead;
                s.listen = listen;

                const auto run = [&] (int delay)
                {
                    BasaltChain chain;
                    chain.prepare (rate, 4096, 2, latency, delay);
                    auto main = probes (2, n);
                    for (int pos = 0; pos < n; pos += 256)
                    {
                        std::array<float*, 2> m { main[0].data() + pos, main[1].data() + pos };
                        std::array<const float*, 2> k { key[0].data() + pos, key[1].data() + pos };
                        chain.process (m.data(), 2, std::min (256, n - pos), s, k.data(), 2);
                    }
                    return main;
                };

                const auto plain = run (0), delayed = run (characterDelay);
                const auto shift = latency == LatencyMode::normal ? characterDelay : 0;
                long differ = 0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        differ += sameBits (delayed[(size_t) ch][(size_t) i], i >= shift ? plain[(size_t) ch][(size_t) (i - shift)] : 0.0f) ? 0 : 1;
                CHECK (differ == 0);
            }
}

//==============================================================================
TEST_CASE ("Chain: a key or stereo switch crossfades, the incoming lane settled and seeded", "[chain][switch]")
{
    // Settled 33 at Attack 300 ms: a carrier on both key channels reads the same in Linked,
    // Unlinked and Mid, so a perfect switch keeps the reduction where it is.
    const auto n = samples (8.0);
    auto s = compressing();
    s.attackMs = 300.0;
    s.releaseMs = 1000.0;
    s.autoRelease = true;
    s.key = KeySource::external;
    const auto loud = carrier (n, gainOf (-16.0));
    const Channels key { loud, loud };

    const auto at1 = samples (3.0), at2 = samples (5.0), at3 = samples (6.5);
    auto chain = std::make_unique<BasaltChain>();
    std::vector<StereoMode> heardAt;
    const auto* c = chain.get();
    const auto r = reductionOf (render (s, probes (2, n), key, { 512 }, [=, &heardAt] (int pos, ChainSettings& x)
    {
        if (pos / 512 == (at1 + samples (0.5)) / 512 || pos / 512 == (at2 + samples (0.5)) / 512 || pos / 512 == (at3 + samples (0.5)) / 512)
            heardAt.push_back (c->heardLane().config().stereo);
        x.stereo = pos >= at3 ? StereoMode::linked : (pos >= at2 ? StereoMode::mid : (pos >= at1 ? StereoMode::unlinked : StereoMode::linked));
    }, chain.get()));

    const auto settled = r[(size_t) at1 - 1];
    REQUIRE (settled > 8.0);
    auto excursion = 0.0;
    for (int i = at1; i < (int) r.size(); ++i)
        excursion = std::max (excursion, std::abs (r[(size_t) i] - settled));
    CHECK (excursion < 0.05);
    CHECK (heardAt == std::vector<StereoMode> { StereoMode::unlinked, StereoMode::mid, StereoMode::linked });

    SECTION ("Internal <-> External with the same signal on both")
    {
        auto x = s;
        x.stereo = StereoMode::linked;
        const auto sw = render (x, { loud, loud }, key, { 512 }, [=] (int pos, ChainSettings& y)
        {
            y.key = pos >= at1 && pos < at2 ? KeySource::internal : KeySource::external;
        });
        auto worst = 0.0;
        const auto ref = -20.0 * std::log10 (std::abs (sw.out[0][(size_t) at1 - 1 + 480]) / gainOf (-16.0));
        for (int i = at1; i + 480 < n; ++i)
            worst = std::max (worst, std::abs (-20.0 * std::log10 (std::abs (sw.out[0][(size_t) i + 480]) / gainOf (-16.0)) - ref));
        CHECK (worst < 0.05);
    }
}

TEST_CASE ("Chain: a switch of stereo mode never steps the audio", "[chain][switch]")
{
    // A 30 ms crossfade between two different gains on two different matrices: the slope stays bounded.
    const auto n = samples (2.0);
    auto s = compressing();
    s.key = KeySource::internal;
    const Channels in { sine (330.0, n, 0.7f), sine (330.0, n, 0.2f, 1.0) };
    const auto at = samples (1.0);
    const auto r = render (s, in, {}, { 256 }, [=] (int pos, ChainSettings& x) { x.stereo = pos >= at ? StereoMode::side : StereoMode::linked; });

    auto steepest = 0.0f;
    for (int i = 1; i < n; ++i)
        steepest = std::max (steepest, std::abs (r.out[0][(size_t) i] - r.out[0][(size_t) i - 1]));
    const auto sineSlope = (float) (0.7 * 2.0 * std::numbers::pi * 330.0 / rate);
    CHECK (steepest <= sineSlope * 1.05f);
}

TEST_CASE ("Lane: a new engine warms up unheard, then crossfades over 30 ms, seeded with the applied reduction", "[chain][switch]")
{
    // Two Clean engines stand in for two models (M1 builds one): the incoming one starts from
    // rest, follows the key for its settleMs, so seeded mid-attack at Attack 300 ms it carries on
    // where the outgoing one is, its mean squares reading the key as if they had always run.
    DynamicsLane lane, reference;
    for (auto* l : { &lane, &reference })
    {
        l->install (0, std::make_unique<models::CleanModel>());
        l->install (1, std::make_unique<models::CleanModel>());
        l->prepare (rate);
        l->start ({ 0, StereoMode::linked, KeySource::internal, 2 });
        const std::array<double, 2> zero {};
        l->seed (zero.data());
    }

    dynamics::ModelParams p;
    p.curve = { -30.0, 0.25, 0.0, 40.0 };
    p.attackMs = 300.0;
    p.releaseMs = 1000.0;
    p.linked = true;
    std::array<double, DynamicsLane::maxSamples> la {};

    const auto blocks = samples (2.0) / DynamicsLane::maxSamples;
    const auto switchAt = samples (0.4) / DynamicsLane::maxSamples;
    const auto a = gainOf (-14.0);
    auto worst = 0.0;
    auto switchedAfter = -1;

    for (int b = 0; b < blocks; ++b)
    {
        for (auto* l : { &lane, &reference })
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < DynamicsLane::maxSamples; ++i)
                    l->key (c)[i] = i % 2 == 0 ? a : -a;

        lane.process (p, la.data(), DynamicsLane::maxSamples, b >= switchAt ? 1 : 0);
        reference.process (p, la.data(), DynamicsLane::maxSamples, 0);

        if (b >= switchAt && switchedAfter < 0 && ! lane.switching())
            switchedAfter = (b - switchAt + 1) * DynamicsLane::maxSamples;
        for (int i = 0; i < DynamicsLane::maxSamples; ++i)
            worst = std::max (worst, std::abs (lane.reductions (0)[i] - reference.reductions (0)[i]));
    }

    // The warm-up is Clean's settleMs, 5·τ_RMS = 375 ms at Attack 300 ms, plus the one-sample window.
    const auto warmUp = (int) std::ceil (models::CleanModel().settleMs (p) * 0.001 * rate) + 1;
    REQUIRE (warmUp == samples (0.375) + 1);
    std::printf ("Lane switch at Attack 300 ms: heard after %d samples (warm-up %d, fade %d); worst %.2e dB from the engine that ran throughout\n",
                 switchedAfter, warmUp, samples (DynamicsLane::fadeSeconds), worst);
    CHECK (reference.reductions (0)[0] > 3.0);
    CHECK (lane.heardEngine() == 1);
    CHECK (switchedAfter >= warmUp + samples (DynamicsLane::fadeSeconds));
    CHECK (switchedAfter <= warmUp + samples (DynamicsLane::fadeSeconds) + 2 * DynamicsLane::maxSamples);
    CHECK (worst < 1.0e-4);
}

TEST_CASE ("Lane: only the engine heard runs, until another is asked for", "[chain][switch]")
{
    // The cost of a model is its own (PLAN.md, Performance budget): an engine not heard does no
    // work at all, and a new one follows the key only for its warm-up, then is heard.
    struct Counting final : dynamics::DynamicsModel
    {
        int processed = 0, followed = 0, resets = 0;
        void prepare (double) override { reset(); }
        void reset() noexcept override { ++resets; }
        void seed (const double*, int) noexcept override {}
        void process (const dynamics::ModelParams&, const dynamics::KeyBlock& k, double* const* r) noexcept override
        {
            processed += k.numSamples;
            for (int c = 0; c < k.numChannels; ++c)
                std::fill (r[c], r[c] + k.numSamples, 0.0);
        }
        void follow (const dynamics::ModelParams&, const dynamics::KeyBlock& k) noexcept override { followed += k.numSamples; }
        double staticReduction (const dynamics::ModelParams&, double) const noexcept override { return 0.0; }
        double reduction (int) const noexcept override { return 0.0; }
        double settleMs (const dynamics::ModelParams&) const noexcept override { return 10.0; }
    };

    DynamicsLane lane;
    auto counting = std::make_unique<Counting>();
    auto* idle = counting.get();
    lane.install (0, std::make_unique<models::CleanModel>());
    lane.install (1, std::move (counting));
    lane.prepare (rate);
    lane.start ({ 0, StereoMode::linked, KeySource::internal, 2 });
    const std::array<double, 2> zero {};
    lane.seed (zero.data());

    dynamics::ModelParams p;
    std::array<double, DynamicsLane::maxSamples> la {};
    const auto block = [&] (int engine)
    {
        for (int c = 0; c < 2; ++c)
            std::fill (lane.key (c), lane.key (c) + DynamicsLane::maxSamples, 0.1);
        const auto n = std::min (DynamicsLane::maxSamples, lane.samplesToDecision());
        lane.request (engine, p, 0.0);
        lane.process (p, la.data(), std::min (n, lane.samplesToDecision()));
    };

    for (int b = 0; b < 100; ++b)
        block (0);
    CHECK (idle->processed == 0);
    CHECK (idle->followed == 0);

    while (lane.heardEngine() != 1)
        block (1);
    CHECK (idle->followed == samples (0.010) + 1);
    CHECK (idle->processed >= samples (DynamicsLane::fadeSeconds));
    CHECK (idle->processed <= samples (DynamicsLane::fadeSeconds) + 1);
}

//==============================================================================
TEST_CASE ("Chain: the meters read In, Out and Reduction", "[chain][meters]")
{
    const auto n = samples (1.0);
    auto s = compressing();
    s.key = KeySource::external;
    s.releaseMs = 1000.0;
    const Channels key { carrier (n, gainOf (-10.0)), carrier (n, gainOf (-60.0)) };
    auto chain = std::make_unique<BasaltChain>();
    render (s, probes (2, n), key, { 256 }, {}, chain.get());

    auto& m = chain->meters();
    CHECK (isExactly (m.input[0].take(), probe));
    CHECK (isExactly (m.input[0].take(), 0.0f));      // taken
    for (auto* meter : { &m.input[1], &m.output[0], &m.output[1], &m.reductionDb[0], &m.reductionDb[1], &m.liftDb[0], &m.liftDb[1] })
        meter->take();

    // Settled, then 100 ms more.
    auto main = probes (2, samples (0.1));
    for (int pos = 0; pos < samples (0.1); pos += 256)
    {
        std::array<float*, 2> mp { main[0].data() + pos, main[1].data() + pos };
        std::array<const float*, 2> kp { key[0].data() + pos, key[1].data() + pos };
        chain->process (mp.data(), 2, std::min (256, samples (0.1) - pos), s, kp.data(), 2);
    }

    // 20 dB over at Ratio 4: 15 dB on the peak half, 17.26 on the RMS half (the carrier reads 3.01 dB more there).
    const auto reduction = m.reductionDb[0].read();
    CHECK_THAT ((double) reduction, WithinAbs (0.5 * (15.0 + 0.75 * 23.0103), 0.01));
    CHECK (isExactly (m.reductionDb[1].read(), reduction));   // Linked
    CHECK_THAT ((double) m.output[0].read(), WithinAbs (probe * std::pow (10.0, -reduction / 20.0), 1.0e-4));
    CHECK (isExactly (m.liftDb[0].read(), 0.0f));
    CHECK (m.detectorChannels.load() == 2);
    CHECK (chain->status().latencySamples.load() == 480);
    CHECK (chain->status().model.load() == static_cast<int> (Model::clean));
}
