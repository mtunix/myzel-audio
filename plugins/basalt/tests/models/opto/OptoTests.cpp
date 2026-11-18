// Opto against models/opto/NOTES.md's gate table, at model level: the cell's static curve on sines
// and the carrier, Attack and Release by their measurement definitions, the release's depth and
// the cell's memory against Reference.h's transcription, identity, Auto Gain, the stereo link,
// the seed, knob jumps and block-size independence.

#include "OptoSupport.h"

#include "dsp/dynamics/KeyPath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <functional>
#include <utility>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using namespace basalt::testing::opto;
using Catch::Matchers::WithinAbs;
using myzel::dsp::isExactly;

namespace
{
    double inverseOf (double ratio) { return ratio > 0.0 ? 1.0 / ratio : 0.0; }

    /** Settled reduction of the same model after `seconds` on a carrier at `depthDb`. */
    double settledOn (const ModelParams& p, double depthDb, double seconds)
    {
        auto m = prepared();
        return runModel (m, p, carrierFor (p, depthDb, samplesFor (seconds, rate)), nullptr, 512)[0].back();
    }
}

TEST_CASE ("Opto: the static curve on 1 kHz sines and the carrier", "[opto][static]")
{
    // NOTES' grid: T − 20 … T + 30 dB in 1 dB steps, T −60/−30/−10, R 1.5 … ∞, Knee 0/6/24, Range
    // 40/10, Attack 50 ms, Release 1 s, Auto off, within ±0.25 dB of r_s. The carrier's level is its
    // mean-square reading. The steps run in the direction whose constant is shorter (low ratios
    // calibrate τ_on far above Attack, the log law); the steady state is the same either way. A
    // descending run starts seeded at its first level after the light's warm-up.
    constexpr double attackMs = 50.0, releaseMs = 1000.0;
    const auto measure = samplesFor (0.020, rate);
    double worst[2] {};
    char worstAt[2][160] {};

    for (const auto useSine : { true, false })
        for (const auto t : { -60.0, -30.0, -10.0 })
            for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 20.0, 0.0 })
                for (const auto knee : { 0.0, 6.0, 24.0 })
                    for (const auto range : { 40.0, 10.0 })
                    {
                        const auto inverseRatio = inverseOf (ratio);
                        const auto p = params (t, inverseRatio, knee, range, attackMs, releaseMs);
                        const auto tauOn = OptoModel::attackConstantMs (attackMs, inverseRatio, knee);
                        const auto tauFall = OptoModel::releaseConstantMs (releaseMs, attackMs, inverseRatio, knee);
                        const auto ascending = tauOn <= tauFall;
                        const auto settle = samplesFor (std::max (ref::stepSettleS (std::min (tauOn, tauFall), attackMs, inverseRatio, knee), 0.05), rate);

                        std::vector<double> levels;
                        for (int s = 0; s <= 50; ++s)
                            levels.push_back (ascending ? t - 20.0 + s : t + 30.0 - s);

                        auto signalAt = [&] (double level, int n, double offset)
                        {
                            return useSine ? sine (amplitudeFor (level), 1000.0, rate, n, 2.0 * std::numbers::pi * 1000.0 * offset / rate)
                                           : carrierAt (level, n);
                        };

                        Signal key;
                        for (const auto level : levels)
                            append (key, signalAt (level, settle + measure, (double) key.size()));

                        auto m = prepared();
                        if (! ascending)
                        {
                            const auto warm = signalAt (levels.front(), samplesFor (0.05, rate), -(double) samplesFor (0.05, rate));
                            KeyBlock k;
                            k.numSamples = (int) warm.size();
                            k.channels[0] = warm.data();
                            m.follow (p, k);
                            const double seed[] { OptoModel::curveReduction (p.curve, levels.front()) };
                            m.seed (seed, 1);
                        }

                        const auto r = runModel (m, p, key, nullptr, 512)[0];

                        for (size_t s = 0; s < levels.size(); ++s)
                        {
                            const auto end = (s + 1) * (size_t) (settle + measure);
                            const auto got = mean (r, end - (size_t) measure, end);
                            const auto expected = ref::staticReduction (levels[s], t, inverseRatio, knee, range);
                            const auto error = std::abs (got - expected);
                            if (error > worst[useSine ? 0 : 1])
                            {
                                worst[useSine ? 0 : 1] = error;
                                std::snprintf (worstAt[useSine ? 0 : 1], sizeof (worstAt[0]), "T %g, R %g, knee %g, range %g, level %g: %.4f for %.4f",
                                               t, ratio, knee, range, levels[s], got, expected);
                            }

                            INFO ((useSine ? "sine" : "carrier") << ", T " << t << ", ratio " << ratio << ", knee " << knee << ", range " << range
                                  << ", level " << levels[s] << ": " << got << " for " << expected);
                            REQUIRE (error <= 0.25);
                        }
                    }

    std::printf ("Opto static curve, worst |error|: 1 kHz sine %.4f dB (%s); carrier %.4f dB (%s)\n", worst[0], worstAt[0], worst[1], worstAt[1]);
}

TEST_CASE ("Opto: 100 Hz and the NOTES' own point, reported", "[opto][static]")
{
    // NOTES: 1 kHz, R 4, Knee 6, T −30, Attack ≈ 10 ms, Release 1 s: within 0.016 dB (simulation).
    // 100 Hz is reported: the 5 ms light ripples at 200 Hz.
    for (const auto hz : { 1000.0, 100.0 })
    {
        auto worst = 0.0, signedWorst = 0.0;
        for (auto level = -50.0; level <= 0.0; level += 5.0)
        {
            const auto p = params (-30.0, 0.25, 6.0, 40.0, 10.0, 1000.0);
            auto m = prepared();
            const auto r = runModel (m, p, sine (amplitudeFor (level), hz, rate, samplesFor (3.0, rate)), nullptr, 512)[0];
            const auto got = mean (r, r.size() - (size_t) samplesFor (0.1, rate), r.size());
            const auto error = got - ref::staticReduction (level, -30.0, 0.25, 6.0, 40.0);
            if (std::abs (error) > worst)
            {
                worst = std::abs (error);
                signedWorst = error;
            }
        }
        std::printf ("Opto %g Hz, R 4, Knee 6, Attack 10 ms, T − 20 … T + 30: worst %+.4f dB against r_s\n", hz, signedWorst);
        if (hz > 500.0)
            CHECK (worst <= 0.05);
    }
}

TEST_CASE ("Opto: Attack means the 63 % time from silence to 10 dB, floored at 2 ms", "[opto][ballistics]")
{
    // NOTES: carrier, silence → 10 dB, Knee 0 and 6, R 2, 4, 20: Attack 2 … 300 ms ±10 %; below
    // 2 ms it reads 2 ms. D_final is r_s exactly on the carrier (the cell's fixed point).
    auto worst = 0.0;
    char worstAt[96] {};

    for (const auto knee : { 0.0, 6.0 })
        for (const auto ratio : { 2.0, 4.0, 20.0 })
            for (const auto attackMs : { 0.02, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 100.0, 300.0 })
            {
                const auto p = params (-30.0, inverseOf (ratio), knee, 40.0, attackMs, 1000.0);
                const auto expected = ref::expectedAttackMs (attackMs) * 0.001;
                auto key = silence (16);
                append (key, carrierFor (p, 10.0, samplesFor (3.0 * expected + 0.02, rate)));

                auto m = prepared();
                const auto r = runModel (m, p, key)[0];
                const auto measured = attackTime (r, 16, rate, 10.0);

                INFO ("knee " << knee << ", ratio " << ratio << ", attack " << attackMs << " ms: " << measured * 1000.0 << " ms");
                REQUIRE (withinTimeGate (measured, expected, rate));
                if (std::abs (measured / expected - 1.0) > worst)
                {
                    worst = std::abs (measured / expected - 1.0);
                    std::snprintf (worstAt, sizeof (worstAt), "knee %g, ratio %g, attack %g ms: %.3f ms", knee, ratio, attackMs, measured * 1000.0);
                }
            }

    std::printf ("Opto attack, worst |measured/set − 1|: %.2f %% (%s)\n", 100.0 * worst, worstAt);

    SECTION ("D_final is 10 dB")
    {
        for (const auto ratio : { 2.0, 4.0, 20.0 })
            REQUIRE_THAT (settledOn (params (-30.0, inverseOf (ratio), 6.0, 40.0, 10.0, 1000.0), 10.0, 4.0), WithinAbs (10.0, 1.0e-4));
    }
}

TEST_CASE ("Opto: one table serves every host rate", "[opto][ballistics]")
{
    // The light and the cell are continuous-time one-poles, so τ_on and τ_ff read the same at
    // 44.1 and 96 kHz as at the 192 kHz the table was solved at.
    for (const auto sampleRate : { 44100.0, 96000.0 })
    {
        auto worst = 0.0;
        for (const auto knee : { 0.0, 6.0 })
            for (const auto ratio : { 4.0, 20.0 })
            {
                for (const auto attackMs : { 2.0, 10.0, 100.0 })
                {
                    const auto p = params (-30.0, inverseOf (ratio), knee, 40.0, attackMs, 1000.0);
                    auto key = silence (16);
                    append (key, carrierFor (p, 10.0, samplesFor (3.0 * attackMs * 0.001 + 0.02, sampleRate)));
                    auto m = prepared (sampleRate);
                    const auto measured = attackTime (runModel (m, p, key)[0], 16, sampleRate, 10.0);
                    INFO (sampleRate << " Hz, knee " << knee << ", ratio " << ratio << ", attack " << attackMs << " ms: " << measured * 1000.0);
                    REQUIRE (withinTimeGate (measured, attackMs * 0.001, sampleRate));
                    worst = std::max (worst, std::abs (measured / (attackMs * 0.001) - 1.0));
                }

                for (const auto releaseMs : { 30.0, 150.0 })
                {
                    const auto p = params (-30.0, inverseOf (ratio), knee, 40.0, 10.0, releaseMs);
                    auto key = carrierFor (p, 10.0, samplesFor (1.0, sampleRate));
                    const auto end = key.size();
                    append (key, silence (samplesFor (0.5, sampleRate)));
                    auto m = prepared (sampleRate);
                    const auto r = runModel (m, p, key)[0];
                    const auto measured = fallTime (r, end, sampleRate, r[end - 1], 0.5);
                    const auto expected = ref::expectedReleaseMs (releaseMs, 10.0, knee) * 0.001;
                    INFO (sampleRate << " Hz, knee " << knee << ", ratio " << ratio << ", release " << releaseMs << " ms: " << measured * 1000.0);
                    REQUIRE (std::abs (measured / expected - 1.0) <= 0.10);
                    worst = std::max (worst, std::abs (measured / expected - 1.0));
                }
            }
        std::printf ("Opto at %g Hz: attack and release within %.2f %% of the knobs\n", sampleRate, 100.0 * worst);
    }
}

TEST_CASE ("Opto: bigger steps attack faster, reported", "[opto][ballistics]")
{
    // NOTES, Attack: the log of a conductance rising linearly reaches 63 % of a deeper target sooner.
    for (const auto attackMs : { 10.0, 100.0 })
    {
        std::printf ("Opto attack against depth, R 4, Knee 6, Attack %g ms:", attackMs);
        for (const auto depth : { 5.0, 10.0, 20.0, 30.0 })
        {
            const auto p = params (-40.0, 0.25, 6.0, 40.0, attackMs, 1000.0);
            auto key = silence (16);
            append (key, carrierFor (p, depth, samplesFor (2.0 * attackMs * 0.001 + 0.05, rate)));
            auto m = prepared();
            const auto r = runModel (m, p, key)[0];
            std::printf (" %g dB %.2f ms", depth, 1000.0 * attackTime (r, 16, rate, depth));
        }
        std::printf ("\n");
    }
}

TEST_CASE ("Opto: Release means the time to 50 % after 10 dB, Auto off", "[opto][ballistics]")
{
    // NOTES: t50 = Release ±10 % for Release 30 ms … 5 s at R 2, 4, 20 and Knee 0, 6, 24.
    auto worst = 0.0, worstReference = 0.0;
    char worstAt[96] {};

    for (const auto knee : { 0.0, 6.0, 24.0 })
        for (const auto ratio : { 2.0, 4.0, 20.0 })
            for (const auto releaseMs : { 30.0, 60.0, 150.0, 500.0, 2000.0, 5000.0 })
            {
                const auto p = params (-30.0, inverseOf (ratio), knee, 40.0, 10.0, releaseMs);
                const auto tauOn = OptoModel::attackConstantMs (10.0, p.curve.inverseRatio, knee);
                const auto expected = ref::expectedReleaseMs (releaseMs, 10.0, knee) * 0.001;

                auto m = prepared();
                const auto held = releaseAfterHold (m, p, 10.0, std::max (1.0, 0.016 * tauOn), 0.5, 3.0 * expected + 0.1);
                const auto transcribed = ref::releaseAfterHold (referenceFor (p), 10.0, std::max (1.0, 0.016 * tauOn), 0.5).seconds;

                INFO ("knee " << knee << ", ratio " << ratio << ", release " << releaseMs << " ms: t50 " << held.seconds * 1000.0
                              << " ms from " << held.from << " dB; transcription " << transcribed * 1000.0 << " ms");
                REQUIRE_THAT (held.from, WithinAbs (10.0, 0.01));
                REQUIRE (std::abs (held.seconds / expected - 1.0) <= 0.10);
                REQUIRE (std::abs (held.seconds / transcribed - 1.0) <= 1.0e-3);

                worstReference = std::max (worstReference, std::abs (held.seconds / transcribed - 1.0));
                if (std::abs (held.seconds / expected - 1.0) > worst)
                {
                    worst = std::abs (held.seconds / expected - 1.0);
                    std::snprintf (worstAt, sizeof (worstAt), "knee %g, ratio %g, release %g ms: %.2f ms", knee, ratio, releaseMs, held.seconds * 1000.0);
                }
            }

    std::printf ("Opto release (t50), worst |measured/set − 1|: %.2f %% (%s); against the transcription %.4f %%\n", 100.0 * worst, worstAt,
                 100.0 * worstReference);
}

TEST_CASE ("Opto: depth slows the release by the log law alone", "[opto][ballistics]")
{
    // NOTES: Auto off, Release 60 ms, R 4, Knee 6: t50 after 20 dB over t50 after 10 dB within ±10 %
    // of the transcription's ratio (M0 expected about 1.84).
    const auto p = params (-40.0, 0.25, 6.0, 40.0, 10.0, 60.0);
    auto m = prepared();
    const auto ten = releaseAfterHold (m, p, 10.0, 1.0, 0.5, 1.0).seconds;
    m.reset();
    const auto twenty = releaseAfterHold (m, p, 20.0, 1.0, 0.5, 1.0).seconds;
    const auto expected = ref::depthRatio (referenceFor (p));

    std::printf ("Opto depth, Release 60 ms, R 4, Knee 6: t50 %.2f ms after 10 dB, %.2f ms after 20 dB, ratio %.3f (transcription %.3f, M0 about 1.84)\n",
                 1000.0 * ten, 1000.0 * twenty, twenty / ten, expected);
    REQUIRE (std::abs ((twenty / ten) / expected - 1.0) <= 0.10);
}

namespace
{
    struct History
    {
        double shortHold = 0.0, longHold = 0.0, referenceShort = 0.0, referenceLong = 0.0;
    };

    History historyAt (double releaseMs, double ratio)
    {
        const auto p = params (-40.0, inverseOf (ratio), 6.0, 40.0, 10.0, releaseMs, true);
        const auto tail = 10.0 * ref::dischargeMs (releaseMs) * 0.001 + 2.0;
        History h;
        auto m = prepared();
        h.shortHold = releaseAfterHold (m, p, 10.0, 0.5, 0.1, tail).seconds;
        m.reset();
        h.longHold = releaseAfterHold (m, p, 10.0, 10.0, 0.1, tail).seconds;
        h.referenceShort = ref::fullReleaseS (referenceFor (p), 0.5);
        h.referenceLong = ref::fullReleaseS (referenceFor (p), 10.0);
        return h;
    }
}

TEST_CASE ("Opto: the cell's memory lengthens the full release after long reduction", "[opto][auto]")
{
    // NOTES, history row: Auto on, Knee 6. At R 4, Release 60 ms the full release (to 10 %) after
    // 0.5 s and after 10 s at 10 dB lies within ±10 % of the transcription, M0's 0.55 and 2.31 s
    // reported beside it; the long hold's release is at least twice the short one's at every
    // Release 30 ms … 1 s. R 2 is the next test case: it misses the 2 × by the NOTES' own equations.
    {
        const auto h = historyAt (60.0, 4.0);
        std::printf ("Opto history, R 4, Knee 6, Release 60 ms: full release %.3f s after 0.5 s, %.3f s after 10 s (×%.2f); "
                     "transcription %.3f / %.3f s; M0 expected %.2f / %.2f s\n",
                     h.shortHold, h.longHold, h.longHold / h.shortHold, h.referenceShort, h.referenceLong, ref::m0FullReleaseAfterHalfSecondS,
                     ref::m0FullReleaseAfterTenSecondsS);
        REQUIRE (std::abs (h.shortHold / h.referenceShort - 1.0) <= 0.10);
        REQUIRE (std::abs (h.longHold / h.referenceLong - 1.0) <= 0.10);
    }

    for (const auto ratio : { 2.5, 3.0, 4.0, 10.0, 20.0 })
        for (const auto releaseMs : { 30.0, 60.0, 150.0, 500.0, 1000.0 })
        {
            const auto h = historyAt (releaseMs, ratio);
            std::printf ("Opto history, R %g, Release %g ms: %.3f s / %.3f s = %.2f (transcription %.2f)\n", ratio, releaseMs, h.longHold,
                         h.shortHold, h.longHold / h.shortHold, h.referenceLong / h.referenceShort);
            INFO ("ratio " << ratio << ", release " << releaseMs << " ms");
            REQUIRE (std::abs (h.shortHold / h.referenceShort - 1.0) <= 0.10);
            REQUIRE (std::abs (h.longHold / h.referenceLong - 1.0) <= 0.10);
            REQUIRE (h.longHold / h.shortHold >= ref::historyRatioFloor);
        }
}

TEST_CASE ("Opto: at Ratio 2 the memory's history ratio stays under 2 (NOTES' gate, not met)", "[opto][auto][!shouldfail]")
{
    // The NOTES' equations give 1.86 … 1.88 here (transcription and engine agree): 10 dB at R 2,
    // Knee 6 needs 20 dB of light over Threshold, so half a second already charges the memory to
    // 3.4 dB. Recorded in NOTES.md, Measured in M2; this case fails until the design changes.
    for (const auto releaseMs : { 30.0, 60.0, 150.0, 500.0, 1000.0 })
    {
        const auto h = historyAt (releaseMs, 2.0);
        std::printf ("Opto history, R 2, Release %g ms: %.3f s / %.3f s = %.2f (transcription %.2f)\n", releaseMs, h.longHold, h.shortHold,
                     h.longHold / h.shortHold, h.referenceLong / h.referenceShort);
        CHECK (std::abs (h.longHold / h.referenceLong - 1.0) <= 0.10);
        CHECK (h.longHold / h.shortHold >= ref::historyRatioFloor);
    }
}

TEST_CASE ("Opto: the release table of NOTES' Auto release, reported", "[opto][auto]")
{
    // NOTES, Auto release: R 4, Knee 6, Attack 10 ms, Release 60 ms (M0, without Δ: t50 67.0 / 99.4 /
    // 159 / 1690 ms, full 0.55 / 2.31 / 3.60 / 5.36 s).
    const auto p = params (-40.0, 0.25, 6.0, 40.0, 10.0, 60.0, true);
    for (const auto& [depth, hold] : { std::pair { 10.0, 0.5 }, std::pair { 10.0, 10.0 }, std::pair { 20.0, 0.5 }, std::pair { 20.0, 10.0 } })
    {
        auto m = prepared();
        const auto half = releaseAfterHold (m, p, depth, hold, 0.5, 10.0).seconds;
        m.reset();
        const auto full = releaseAfterHold (m, p, depth, hold, 0.1, 10.0).seconds;
        std::printf ("Opto Auto, %g dB for %g s: t50 %.1f ms, full %.3f s\n", depth, hold, 1000.0 * half, full);
    }
}

TEST_CASE ("Opto: look-ahead starts the attack early with the same shape, and the release when the audio ends", "[opto][lookahead]")
{
    // The window acts on the light's mean square (NOTES.md, Detector): it rises with the key, so
    // the window's maximum is the advanced value, and after the key it holds the value from the
    // look-ahead ago, which is the light without look-ahead in audio time. Exact with Auto off.
    const auto latency = samplesFor (0.010, rate);
    constexpr double t = -30.0;

    for (const auto& [attackMs, autoRelease] : { std::pair { 2.0, false }, std::pair { 10.0, true } })
        for (const auto laMs : { 3.0, 10.0 })
        {
            const auto p = params (t, 0.25, 6.0, 40.0, attackMs, 100.0, autoRelease);
            auto key = silence (480);
            append (key, carrierAt (t + 15.0, samplesFor (0.3, rate)));
            const auto end = key.size();
            append (key, silence (samplesFor (0.6, rate)));

            const auto run = [&] (int la) {
                KeyDelay delay;
                delay.prepare (1, latency);
                Signal tapped;
                for (const auto x : key)
                {
                    delay.push (&x);
                    tapped.push_back (delay.read (0, latency - la));
                }
                const Signal lookahead (key.size(), (double) la);
                auto m = prepared();
                return runModel (m, p, tapped, nullptr, 64, &lookahead)[0];
            };

            const auto la = samplesFor (laMs * 0.001, rate);
            const auto r0 = run (0), rl = run (la);

            auto attackSide = 0.0, releaseSide = 0.0;
            for (size_t n = 0; n + (size_t) la < end + (size_t) latency; ++n)
                attackSide = std::max (attackSide, std::abs (rl[n] - r0[n + (size_t) la]));
            for (auto n = end + (size_t) latency; n < key.size(); ++n)
                releaseSide = std::max (releaseSide, std::abs (rl[n] - r0[n]));

            std::printf ("Opto look-ahead %g ms, Attack %g ms, Auto %s: attack side %.1e dB from the shifted trace, release side %.1e dB\n", laMs,
                         attackMs, autoRelease ? "on" : "off", attackSide, releaseSide);
            REQUIRE (attackSide <= 1.0e-9);
            // With Auto on the cell is lit the look-ahead longer, so its memory holds a little more.
            REQUIRE (releaseSide <= (autoRelease ? 0.05 : 1.0e-9));
        }
}

TEST_CASE ("Opto: idle is exactly 0", "[opto][idle]")
{
    // NOTES' identity row: Ratio 1, Range 0, or a key below T − 6w, both channels linked.
    constexpr double t = -30.0;
    const auto n = samplesFor (0.5, rate);
    const auto loud = carrier (amplitudeFor (-3.0), n);
    const Signal lookahead ((size_t) n, 240.0);

    const auto allZero = [] (const Trace& r) {
        return std::all_of (r.begin(), r.end(), [] (double v) { return isExactly (v, 0.0) && isExactly (gainFor (v), 1.0); });
    };

    for (const auto autoRelease : { false, true })
        for (const auto knee : { 0.0, 6.0, 24.0 })
        {
            auto p = params (t, 1.0, knee, 40.0, 10.0, 150.0, autoRelease);
            p.linked = true;
            auto m = prepared();
            auto r = runModel (m, p, loud, &loud, 64, &lookahead);
            REQUIRE (allZero (r[0]));
            REQUIRE (allZero (r[1]));

            m.reset();
            p.curve = { t, 0.0, knee, 0.0 };
            r = runModel (m, p, loud, &loud, 64, &lookahead);
            REQUIRE (allZero (r[0]));

            // The 5 ms light reads a 1 kHz sine up to 0.07 dB over its peak, so 0.25 dB of margin.
            m.reset();
            p.curve = { t, 0.0, knee, 40.0 };
            const auto quiet = sine (amplitudeFor (ref::onsetDb (t, knee) - 0.25), 1000.0, rate, n);
            const auto quietCarrier = carrierAt (ref::onsetDb (t, knee) - 0.01, n);
            r = runModel (m, p, quiet, &quietCarrier, 64, &lookahead);
            REQUIRE (allZero (r[0]));
            REQUIRE (allZero (r[1]));
        }

    SECTION ("After a burst the reduction lands on exactly 0 and stays there")
    {
        for (const auto& [releaseMs, autoRelease] : { std::pair { 30.0, false }, std::pair { 150.0, true }, std::pair { 1000.0, true } })
        {
            const auto p = params (t, 0.25, 6.0, 40.0, 10.0, releaseMs, autoRelease);
            auto key = carrierFor (p, 15.0, samplesFor (2.0, rate));
            const auto end = key.size();
            append (key, silence (samplesFor (0.001 * (autoRelease ? 25.0 * ref::dischargeMs (releaseMs) : 30.0 * releaseMs) + 1.0, rate)));

            auto m = prepared();
            const auto r = runModel (m, p, key, nullptr, 512)[0];
            const auto firstZero = (size_t) (std::find_if (r.begin() + (std::ptrdiff_t) end, r.end(), [] (double v) { return isExactly (v, 0.0); }) - r.begin());
            REQUIRE (firstZero < r.size());
            REQUIRE (std::all_of (r.begin() + (std::ptrdiff_t) firstZero, r.end(), [] (double v) { return isExactly (v, 0.0); }));
            REQUIRE (isExactly (m.fastOf (0), 0.0));
            REQUIRE (isExactly (m.memoryOf (0), 0.0));
            std::printf ("Opto idle snap, Release %g ms, Auto %s: exactly 0 after %.2f s\n", releaseMs, autoRelease ? "on" : "off",
                         (double) (firstZero - end) / rate);
        }
    }
}

TEST_CASE ("Opto: Range caps the cell, and its release starts from the cap", "[opto][range]")
{
    for (const auto autoRelease : { false, true })
    {
        const auto p = params (-30.0, 0.0, 0.0, 10.0, 10.0, 60.0, autoRelease);
        auto key = carrierAt (0.0, samplesFor (3.0, rate));
        const auto end = key.size();
        append (key, silence (samplesFor (1.0, rate)));
        auto m = prepared();
        const auto r = runModel (m, p, key)[0];
        REQUIRE (*std::max_element (r.begin(), r.end()) <= 10.0 + 1.0e-9);
        REQUIRE_THAT (r[end - 1], WithinAbs (10.0, 1.0e-9));

        // From the cap, not from the 30 dB the light asks for: the cell starts releasing as soon
        // as the light has fallen the 20 dB to the cap, 20·τ_m/4.343 after the key stops.
        const auto lightFall = 20.0 * OptoModel::lightMs (10.0) / ref::dbPerNeper;
        const auto t50 = fallTime (r, end, rate, 10.0, 0.5) * 1000.0;
        std::printf ("Opto from Range 10 dB with the light 30 dB over, Auto %s: t50 %.2f ms (Auto off expects Release 60 ms + the light's %.2f ms)\n",
                     autoRelease ? "on" : "off", t50, lightFall);
        if (! autoRelease)
            REQUIRE (std::abs (t50 / (60.0 + lightFall) - 1.0) <= 0.10);
    }
}

TEST_CASE ("Opto: Auto Gain reads r_s at the reference", "[opto][autogain]")
{
    for (const auto t : { -40.0, -30.0, -18.0, -10.0, 0.0 })
        for (const auto ratio : { 1.5, 4.0, 0.0 })
            for (const auto knee : { 0.0, 6.0, 24.0 })
                for (const auto range : { 40.0, 3.0 })
                {
                    const auto p = params (t, inverseOf (ratio), knee, range, 10.0, 150.0);
                    OptoModel m;
                    const auto got = autoGainDb (m, p);
                    REQUIRE_THAT (got, WithinAbs (ref::staticReduction (autoGainReferenceDb, t, p.curve.inverseRatio, knee, range), 0.01));
                    if (t - 6.0 * ref::kneeWidthDb (knee) >= autoGainReferenceDb)
                        REQUIRE (isExactly (got, 0.0));
                }
}

TEST_CASE ("Opto: Link 100 % gives both channels the same cell to the bit, Link 0 % is Unlinked", "[opto][stereo]")
{
    // NOTES: at Link 100 % the two channels' F and S are bit-identical over a 10 s excerpt.
    const auto n = samplesFor (10.0, rate);
    auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
    for (int i = 0; i < n; ++i)
    {
        const auto envelope = 0.2 + 0.8 * std::abs (std::sin (2.0 * std::numbers::pi * 0.7 * i / rate));
        left[(size_t) i] *= 0.3 * envelope;
        right[(size_t) i] *= 0.3 * (1.2 - envelope);
    }
    const Signal lookahead ((size_t) n, 96.0);

    for (const auto& [attackMs, autoRelease] : { std::pair { 2.0, false }, std::pair { 10.0, true }, std::pair { 30.0, true } })
    {
        auto p = params (-40.0, 0.25, 6.0, 40.0, attackMs, 150.0, autoRelease);
        p.linked = true;
        p.link = 1.0;

        auto m = prepared();
        Trace fastDifference, memoryDifference;
        std::array<Trace, 2> full { Trace ((size_t) n), Trace ((size_t) n) };
        auto cellsDiffer = 0;
        for (int start = 0; start < n; start += 97)
        {
            const auto len = std::min (97, n - start);
            KeyBlock key;
            key.numChannels = 2;
            key.numSamples = len;
            key.channels = { left.data() + start, right.data() + start };
            key.lookahead = lookahead.data() + start;
            double* out[] { full[0].data() + start, full[1].data() + start };
            m.process (p, key, out);
            if (! isExactly (m.fastOf (0), m.fastOf (1)) || ! isExactly (m.memoryOf (0), m.memoryOf (1)))
                ++cellsDiffer;
        }
        REQUIRE (full[0] == full[1]);
        REQUIRE (cellsDiffer == 0);
        REQUIRE (*std::max_element (full[0].begin(), full[0].end()) > 6.0);
        if (autoRelease)
            REQUIRE (m.memoryOf (0) > 0.0);

        p.link = 0.0;
        m.reset();
        const auto none = runModel (m, p, left, &right, 64, &lookahead);
        p.linked = false;
        m.reset();
        const auto unlinked = runModel (m, p, left, &right, 512, &lookahead);
        REQUIRE (none[0] == unlinked[0]);
        REQUIRE (none[1] == unlinked[1]);
        REQUIRE_FALSE (none[0] == none[1]);

        p.linked = true;
        p.link = 0.5;
        m.reset();
        const auto half = runModel (m, p, left, &right, 64, &lookahead);
        for (const auto& channel : half)
            REQUIRE (std::all_of (channel.begin(), channel.end(), [] (double v) { return std::isfinite (v) && v >= 0.0; }));
    }
}

TEST_CASE ("Opto: a seed starts the cell at the applied reduction", "[opto][switch]")
{
    // Settled 33: at Attack 300 ms the incoming model starts at the applied 10 dB instead of
    // re-attacking from 0; the light, run by follow() and normalised for its start from rest,
    // reads the key already.
    for (const auto autoRelease : { false, true })
    {
        const auto p = params (-30.0, 0.25, 6.0, 40.0, 300.0, 1000.0, autoRelease);
        const auto level = carrierFor (p, 10.0, samplesFor (3.0, rate));
        auto m = prepared();

        const auto warm = samplesFor (m.settleMs (p) * 0.001, rate);
        KeyBlock key;
        key.numChannels = 1;
        key.numSamples = warm;
        key.channels[0] = level.data();
        m.follow (p, key);

        const double seed[] { 10.0 };
        m.seed (seed, 1);
        REQUIRE (isExactly (m.memoryOf (0), 0.0));
        const auto r = runModel (m, p, level)[0];
        const auto [low, high] = std::minmax_element (r.begin(), r.end());
        std::printf ("Opto seed at 10 dB, Attack 300 ms, Auto %s: %.6f … %.6f dB over 3 s\n", autoRelease ? "on" : "off", *low, *high);
        // With Auto on the empty memory charges, and the fast part follows it down only at τ_ff:
        // F + S runs τ_ff·dS/dt over the light's conductance meanwhile (NOTES.md, Measured in M2).
        REQUIRE_THAT (*low, WithinAbs (10.0, autoRelease ? 0.05 : 1.0e-6));
        REQUIRE_THAT (*high, WithinAbs (10.0, autoRelease ? 0.05 : 1.0e-6));
    }

    SECTION ("A switch mid-attack carries on as an engine that ran throughout")
    {
        // M1's switch: the incoming engine follows the key from reset for settleMs, is seeded with
        // the reduction applied, and carries on. With Auto release on its memory starts empty.
        for (const auto autoRelease : { false, true })
        {
            const auto p = params (-30.0, 0.25, 6.0, 40.0, 300.0, 500.0, autoRelease);
            const auto key = carrierFor (p, 16.0, samplesFor (4.0, rate));
            const auto at = samplesFor (0.6, rate) / 64 * 64;

            auto throughout = prepared();
            const auto reference = runModel (throughout, p, key, nullptr, 64)[0];

            auto incoming = prepared();
            const auto warm = samplesFor (incoming.settleMs (p) * 0.001, rate);
            KeyBlock k;
            k.numSamples = warm;
            k.channels[0] = key.data() + at - warm;
            incoming.follow (p, k);
            const double seed[] { reference[(size_t) at - 1] };
            incoming.seed (seed, 1);
            const Signal rest (key.begin() + at, key.end());
            const auto r = runModel (incoming, p, rest, nullptr, 64)[0];

            auto worst = 0.0;
            for (size_t i = 0; i < r.size(); ++i)
                worst = std::max (worst, std::abs (r[i] - reference[(size_t) at + i]));
            std::printf ("Opto switch mid-attack, Attack 300 ms, Auto %s: within %.2e dB of an engine that ran throughout\n",
                         autoRelease ? "on" : "off", worst);
            REQUIRE (worst < (autoRelease ? 0.05 : 1.0e-6));
        }
    }
}

namespace
{
    /** One or two channels of key in 64-sample blocks, the parameters per block from `at` (the block's first sample). */
    Trace runScheduled (OptoModel& m, const Signal& key, const std::function<ModelParams (int)>& at, const Signal* right = nullptr)
    {
        Trace r;
        std::vector<double> out (64), out2 (64);
        for (int start = 0; start < (int) key.size(); start += 64)
        {
            KeyBlock k;
            k.numSamples = std::min (64, (int) key.size() - start);
            k.numChannels = right != nullptr ? 2 : 1;
            k.channels[0] = key.data() + start;
            if (right != nullptr)
                k.channels[1] = right->data() + start;
            double* o[] { out.data(), out2.data() };
            m.process (at (start), k, o);
            r.insert (r.end(), out.begin(), out.begin() + k.numSamples);
        }
        return r;
    }

    /** The largest |first| and |second difference| of a trace from `from`, dB per sample (squared). */
    std::pair<double, double> roughness (const Trace& r, size_t from, size_t to = std::numeric_limits<size_t>::max())
    {
        auto step = 0.0, corner = 0.0;
        for (size_t i = std::max<size_t> (from, 2); i < std::min (to, r.size()); ++i)
        {
            step = std::max (step, std::abs (r[i] - r[i - 1]));
            corner = std::max (corner, std::abs (r[i] - 2.0 * r[i - 1] + r[i - 2]));
        }
        return { step, corner };
    }
}

TEST_CASE ("Opto: a knob jump neither steps nor bends the reduction", "[opto][clicks]")
{
    // As Clean's test, at the Opto's fastest Attack (2 ms) and at 10 ms. NOTES' Clicks row: Ratio
    // and Knee jumps re-solve τ_on and τ_ff without moving F or S; the 20 ms curve fade blends
    // the old and the new law's reductions, so the reduction's slope never steps.
    const auto period = samplesFor (0.31, rate);

    struct Jump
    {
        const char* name;
        double keyDb;
        std::function<void (ModelParams&, bool)> set;
    };
    const Jump jumps[] {
        { "Threshold -60 <-> 0", -4.0, [] (ModelParams& p, bool b) { p.curve.thresholdDb = b ? 0.0 : -60.0; } },
        { "Ratio 1 <-> inf", -4.0, [] (ModelParams& p, bool b) { p.curve.inverseRatio = b ? 0.0 : 1.0; } },
        { "Knee 0 <-> 24", -15.0, [] (ModelParams& p, bool b) { p.curve.kneeDb = b ? 24.0 : 0.0; } },
        { "Range 0 <-> 40", -4.0, [] (ModelParams& p, bool b) { p.curve.rangeDb = b ? 40.0 : 0.0; } },
        { "Attack 2 <-> 300", -4.0, [] (ModelParams& p, bool b) { p.attackMs = b ? 300.0 : 2.0; } },
        { "Release 5 ms <-> 5 s", -4.0, [] (ModelParams& p, bool b) { p.releaseMs = b ? 5000.0 : 5.0; } },
        { "Auto release off <-> on", -4.0, [] (ModelParams& p, bool b) { p.autoRelease = b; } },
    };

    for (const auto attackMs : { 2.0, 10.0 })
        for (const auto& jump : jumps)
        {
            // The key drops by 30 dB and comes back every 0.31 s, half-way between the jumps, so the
            // jumps meet attacks, holds and releases but never the key's own edges.
            auto key = carrierAt (jump.keyDb, period / 2);
            for (int s = 0; s < 8; ++s)
                append (key, carrierAt (s % 2 == 0 ? jump.keyDb - 30.0 : jump.keyDb, period));

            auto m = prepared();
            const auto r = runScheduled (m, key, [&] (int pos)
            {
                auto p = params (-18.0, 0.25, 6.0, 40.0, attackMs, 150.0, true);
                jump.set (p, (pos / period) % 2 == 1);
                return p;
            });

            // The key's own steps reach the gain through the attack: measure only the 20 ms after each jump.
            auto step = 0.0, corner = 0.0;
            auto keyStep = 0.0, keyCorner = 0.0;
            for (int j = period; j < (int) key.size(); j += period)
            {
                const auto from = (size_t) (j / 64 * 64);
                const auto [s, c] = roughness (r, from, from + (size_t) samplesFor (0.02, rate));
                step = std::max (step, s);
                corner = std::max (corner, c);
            }

            // The same render without the jumps, as the baseline of what the key itself does.
            auto still = prepared();
            const auto base = runScheduled (still, key, [&] (int) { return params (-18.0, 0.25, 6.0, 40.0, attackMs, 150.0, true); });
            for (int j = period; j < (int) key.size(); j += period)
            {
                const auto from = (size_t) (j / 64 * 64);
                const auto [s, c] = roughness (base, from, from + (size_t) samplesFor (0.02, rate));
                keyStep = std::max (keyStep, s);
                keyCorner = std::max (keyCorner, c);
            }

            std::printf ("Opto knob jump at Attack %g ms, %s: largest step %.4f dB, second difference %.2e dB (still: %.4f, %.2e)\n", attackMs,
                         jump.name, step, corner, keyStep, keyCorner);
            INFO (jump.name << " at Attack " << attackMs << ": step " << step << ", second difference " << corner);
            REQUIRE (step < 0.1);
            REQUIRE (corner < std::max (5.0e-4, 1.5 * keyCorner));
        }
}

TEST_CASE ("Opto: block sizes change nothing", "[opto][blocks]")
{
    // Settings still, then a change of every knob at one sample, where the host's blocks break:
    // the same bits at blocks of 1, 7, 64, 4096 and a mixed cycle.
    const auto n = samplesFor (2.0, rate);
    auto left = pinkNoise (n, 5u), right = pinkNoise (n, 6u);
    scale (left, 0.5);
    scale (right, 0.3);
    const Signal lookahead ((size_t) n, 48.0);
    constexpr int changeAt = 28672;

    auto render = [&] (const std::vector<int>& sizes)
    {
        auto m = prepared();
        std::array<Trace, 2> out { Trace ((size_t) n), Trace ((size_t) n) };
        size_t which = 0;
        for (int start = 0; start < n;)
        {
            auto len = std::min (sizes[which++ % sizes.size()], n - start);
            if (start < changeAt)
                len = std::min (len, changeAt - start);
            auto p = params (-35.0, 0.25, 6.0, 40.0, 10.0, 150.0, true);
            p.linked = true;
            p.link = 0.7;
            if (start >= changeAt)
            {
                p = params (-25.0, 0.1, 12.0, 20.0, 40.0, 500.0, false);
                p.linked = true;
                p.link = 1.0;
            }
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

    const auto reference = render ({ 64 });
    REQUIRE (*std::max_element (reference[0].begin(), reference[0].end()) > 3.0);
    for (const auto& sizes : { std::vector<int> { 1 }, std::vector<int> { 7 }, std::vector<int> { 4096 }, std::vector<int> { 7, 64, 1, 512, 448 } })
    {
        const auto other = render (sizes);
        REQUIRE (other[0] == reference[0]);
        REQUIRE (other[1] == reference[1]);
    }
}
