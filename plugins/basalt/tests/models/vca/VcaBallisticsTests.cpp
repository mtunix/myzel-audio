// VCA's ballistics rows (models/vca/NOTES.md, Gates): Peak by the core's measurement definition
// over the knob ranges, Peak's Auto network against its double-precision transcription and M0's
// table, RMS's program-dependent attack from Threshold, its constant release rate and the
// detector's floor, and the look-ahead shift on both detectors.

#include "VcaTesting.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using namespace basalt::testing::vca;
namespace ref = basalt::reference::vca;
using Catch::Matchers::WithinAbs;
using models::VcaModel;

TEST_CASE ("VCA Peak: attack and release mean the knobs", "[vca][peak][ballistics]")
{
    // The core's grid: Attack 0.02 … 300 ms × Release 5 ms … 5 s × Ratio 2, 4, 20, ∞, Knee 0, the
    // carrier stepping from silence to D = 10 dB and back. Release under 1.01 × Attack is
    // measured against that floor.
    constexpr double t = -30.0, depth = 10.0;
    auto worstAttack = 0.0, worstRelease = 0.0;
    char worstAttackAt[96] {}, worstReleaseAt[96] {};

    for (const auto ratio : { 2.0, 4.0, 20.0, 0.0 })
        for (const auto attackMs : { 0.02, 0.1, 1.0, 10.0, 100.0, 300.0 })
            for (const auto releaseMs : { 5.0, 50.0, 150.0, 500.0, 5000.0 })
            {
                const auto inverseRatio = ratio > 0.0 ? 1.0 / ratio : 0.0;
                const auto p = params (t, inverseRatio, 0.0, 40.0, attackMs, releaseMs);
                const auto expectedAttack = ref::peakAttackMs (attackMs), expectedRelease = ref::peakReleaseMs (attackMs, releaseMs);

                const auto pre = 16;
                const auto held = samplesFor (std::max (5.0 * (attackMs + releaseMs), 20.0 * attackMs) * 0.001, rate);
                auto key = silence (pre);
                append (key, carrier (amplitudeFor (ref::levelForReductionDb (depth, t, inverseRatio)), held));
                append (key, silence (samplesFor (3.0 * expectedRelease * 0.001, rate) + 64));

                auto m = prepared (Detector::peak);
                const auto r = runModel (m, p, key)[0];
                const auto settled = r[(size_t) (pre + held - 1)];
                const auto attack = attackTime (r, (size_t) pre, rate, settled);
                const auto release = releaseTime (r, (size_t) (pre + held), rate, settled);

                INFO ("ratio " << ratio << ", attack " << attackMs << " ms, release " << releaseMs << " ms: measured " << attack * 1000.0
                               << " / " << release * 1000.0 << " ms (release expected " << expectedRelease << ")");
                REQUIRE_THAT (settled, WithinAbs (depth, 1.0e-6));
                REQUIRE (withinTimeGate (attack, expectedAttack * 0.001, rate));
                REQUIRE (withinTimeGate (release, expectedRelease * 0.001, rate));

                if (attackMs >= 0.1 && std::abs (attack / (expectedAttack * 0.001) - 1.0) > worstAttack)
                {
                    worstAttack = std::abs (attack / (expectedAttack * 0.001) - 1.0);
                    std::snprintf (worstAttackAt, sizeof (worstAttackAt), "ratio %g, attack %g ms", ratio, attackMs);
                }
                if (std::abs (release / (expectedRelease * 0.001) - 1.0) > worstRelease)
                {
                    worstRelease = std::abs (release / (expectedRelease * 0.001) - 1.0);
                    std::snprintf (worstReleaseAt, sizeof (worstReleaseAt), "ratio %g, attack %g ms, release %g ms", ratio, attackMs, releaseMs);
                }
            }

    std::printf ("VCA Peak ballistics, worst |measured/set - 1|: attack %.2f %% (from 0.1 ms; %s), release %.2f %% (%s)\n", 100.0 * worstAttack,
                 worstAttackAt, 100.0 * worstRelease, worstReleaseAt);
}

TEST_CASE ("VCA Peak: depth does not change the times", "[vca][peak][ballistics]")
{
    // Log-domain placement (NOTES.md, Peak: manual release): 3, 10 and 30 dB steps at Ratio 4.
    constexpr double t = -40.0;
    for (const auto depth : { 3.0, 10.0, 30.0 })
    {
        const auto p = params (t, 0.25, 0.0, 40.0, 10.0, 150.0);
        auto key = silence (16);
        append (key, carrier (amplitudeFor (ref::levelForReductionDb (depth, t, 0.25)), samplesFor (1.0, rate)));
        append (key, silence (samplesFor (1.0, rate)));
        auto m = prepared (Detector::peak);
        const auto r = runModel (m, p, key)[0];
        const auto settled = r[(size_t) (16 + samplesFor (1.0, rate) - 1)];
        const auto attack = attackTime (r, 16, rate, settled), release = releaseTime (r, (size_t) (16 + samplesFor (1.0, rate)), rate, settled);
        INFO ("depth " << depth << ": " << attack * 1000.0 << " / " << release * 1000.0 << " ms");
        REQUIRE_THAT (attack * 1000.0, WithinAbs (10.0, 0.05));
        REQUIRE_THAT (release * 1000.0, WithinAbs (150.0, 0.5));
    }
}

TEST_CASE ("VCA Peak Auto: the two-capacitor release, against its transcription and M0's table", "[vca][peak][auto]")
{
    // NOTES.md, Gates, Ballistics, Peak Auto: the carrier at 10 dB of static reduction from
    // silence, held 20 ms, 1 s or 5 s, then silence; the reduction at +0.1, +1 and +10 s within
    // ±0.3 dB, and the release to 36.8 % of the reduction applied when the signal stops within
    // ±10 %, against the double-precision transcription and against M0's figures.
    constexpr double t = -30.0, depth = 10.0;

    for (const auto& row : ref::autoRows)
    {
        const auto p = params (t, 0.25, 0.0, 40.0, row.attackMs, row.releaseMs, true);
        const auto held = samplesFor (row.heldS, rate);
        auto key = carrier (amplitudeFor (ref::levelForReductionDb (depth, t, 0.25)), held);
        append (key, silence (samplesFor (31.0, rate)));

        auto m = prepared (Detector::peak);
        const auto r = runModel (m, p, key, nullptr, 512)[0];
        const auto applied = r[(size_t) held - 1];
        const auto release = releaseTime (r, (size_t) held, rate, applied);
        const auto at = [&] (double s) { return r[(size_t) held - 1 + (size_t) samplesFor (s, rate)]; };

        const auto expected = ref::autoReleaseAfter (row.attackMs, row.releaseMs, row.heldS, rate, depth);

        std::printf ("VCA Peak Auto, Attack %g ms, Release %g ms, held %g s: applied %.3f dB; +0.1/+1/+10 s %.2f / %.2f / %.2f dB "
                     "(transcription %.2f / %.2f / %.2f); release %.4g s (transcription %.4g, M0 %.4g)\n",
                     row.attackMs, row.releaseMs, row.heldS, applied, at (0.1), at (1.0), at (10.0), expected.after[0], expected.after[2],
                     expected.after[4], release, expected.releaseS, row.releaseS);

        INFO ("attack " << row.attackMs << ", release " << row.releaseMs << ", held " << row.heldS);
        REQUIRE_THAT (applied, WithinAbs (expected.applied, 1.0e-6));
        REQUIRE_THAT (at (0.1), WithinAbs (expected.after[0], 0.3));
        REQUIRE_THAT (at (1.0), WithinAbs (expected.after[2], 0.3));
        REQUIRE_THAT (at (10.0), WithinAbs (expected.after[4], 0.3));
        REQUIRE (withinTimeGate (release, expected.releaseS, rate));

        // M0's figures, from the same equations in its own simulation.
        REQUIRE (withinTimeGate (release, row.releaseS, rate));
        if (! std::isnan (row.after01))
        {
            REQUIRE_THAT (at (0.1), WithinAbs (row.after01, 0.3));
            REQUIRE_THAT (at (1.0), WithinAbs (row.after1, 0.3));
            REQUIRE_THAT (at (10.0), WithinAbs (row.after10, 0.3));
            REQUIRE_THAT (expected.slowStage, WithinAbs (row.slowAtStep, 0.01));
        }

        // The engine and the transcription agree far closer than the gate asks.
        const auto trace = ref::autoReleaseTrace ([&] {
            std::vector<double> s (key.size(), 0.0);
            std::fill (s.begin(), s.begin() + held, depth);
            return s;
        }(), row.attackMs, row.releaseMs, rate);
        auto worst = 0.0;
        for (size_t i = 0; i < r.size(); ++i)
            worst = std::max (worst, std::abs (r[i] - trace[i]));
        REQUIRE (worst < 1.0e-6);
    }
}

TEST_CASE ("VCA Peak Auto: peaks above a held floor release fast down to it", "[vca][peak][auto]")
{
    // "Louder elements fast, quieter elements slowly": 2 s at 6 dB charges the slow stage, then a
    // 50 ms burst at 12 dB. After the burst the reduction falls back to the 6 dB floor at about
    // the fast stage's rate, not the slow one.
    constexpr double t = -30.0;
    const auto p = params (t, 0.25, 0.0, 40.0, 1.0, 100.0, true);
    const auto bed = carrier (amplitudeFor (ref::levelForReductionDb (6.0, t, 0.25)), samplesFor (2.0, rate));
    auto key = bed;
    append (key, carrier (amplitudeFor (ref::levelForReductionDb (12.0, t, 0.25)), samplesFor (0.05, rate)));
    const auto burstEnd = key.size();
    append (key, carrier (amplitudeFor (ref::levelForReductionDb (6.0, t, 0.25)), samplesFor (2.0, rate)));

    auto m = prepared (Detector::peak);
    const auto r = runModel (m, p, key)[0];
    const auto peak = r[burstEnd - 1];
    const auto halfway = 6.0 + 0.5 * (peak - 6.0);
    size_t j = burstEnd;
    while (j < r.size() && r[j] > halfway)
        ++j;
    const auto halfS = (double) (j - burstEnd) / rate;
    std::printf ("VCA Peak Auto, burst over a 6 dB floor: %.2f dB, halfway back in %.1f ms; at +1 s %.3f dB\n", peak, 1000.0 * halfS,
                 r[burstEnd + (size_t) samplesFor (1.0, rate)]);
    REQUIRE (peak > 11.5);
    REQUIRE (halfS < 0.2);
    REQUIRE_THAT (r[burstEnd + (size_t) samplesFor (1.0, rate)], WithinAbs (6.0, 0.05));
}

TEST_CASE ("VCA RMS: the attack is program-dependent, Attack the 63 % time of 10 dB from Threshold", "[vca][rms][ballistics]")
{
    // NOTES.md, Gates, Ballistics, RMS attack: the carrier settled at Threshold on the RMS reading
    // (reduction 0), then a step to T + 10, 20, 30 dB on the same reading at Ratio ∞, Knee 0: the
    // time to 63.2 % of the settled reduction is 0.45442, 0.19299, 0.08105 × Attack/0.45442, ±10 %.
    constexpr double t = -40.0;

    for (const auto attackMs : { 1.0, ref::markAttackMs, 100.0 })
        for (const auto depth : { 10.0, 20.0, 30.0 })
        {
            const auto p = params (t, 0.0, 0.0, 40.0, attackMs, ref::markReleaseMs);
            const auto tau = ref::rmsTimeConstantMs (attackMs) * 0.001;
            const auto pre = samplesFor (12.0 * tau, rate);
            auto key = carrier (carrierAmplitude (Detector::rms, t), pre);
            append (key, carrier (carrierAmplitude (Detector::rms, t + depth), samplesFor (20.0 * tau, rate)));

            auto m = prepared (Detector::rms);
            const auto r = runModel (m, p, key)[0];
            REQUIRE (myzel::dsp::isExactly (r[(size_t) pre - 1], 0.0));
            const auto settled = r.back();
            const auto attack = attackTime (r, (size_t) pre, rate, settled);
            const auto expected = ref::rmsAttackMs (attackMs, depth);

            std::printf ("VCA RMS attack, Attack %g ms, %g dB from Threshold: %.3f ms (single pole %.3f ms%s)\n", attackMs, depth, attack * 1000.0,
                         expected, myzel::dsp::isExactly (attackMs, ref::markAttackMs) ? (depth <= 10.0 ? "; sheet 15 ms" : (depth <= 20.0 ? "; sheet 5 ms" : "; sheet 3 ms")) : "");
            INFO ("attack " << attackMs << ", depth " << depth);
            REQUIRE_THAT (settled, WithinAbs (depth, 1.0e-6));
            REQUIRE (withinTimeGate (attack, expected * 0.001, rate));
        }

    // The derivation's three factors.
    REQUIRE_THAT (ref::rmsAttackPerTimeConstant (10.0), WithinAbs (0.45442, 5.0e-5));
    REQUIRE_THAT (ref::rmsAttackPerTimeConstant (20.0), WithinAbs (0.19299, 5.0e-5));
    REQUIRE_THAT (ref::rmsAttackPerTimeConstant (30.0), WithinAbs (0.08105, 5.0e-5));
}

TEST_CASE ("VCA RMS: the release is a constant rate, 10 dB per Release, floored by the detector", "[vca][rms][ballistics]")
{
    // NOTES.md, Gates, Ballistics, RMS release: the carrier at T + 6, 12, 24 dB on the RMS
    // reading, settled, then silence, Ratio ∞: the slope between 90 % and 10 % of the depth is
    // 10 dB per Release ±10 % at every depth, Release 20 ms … 5 s at Attack 1 ms; and below the
    // detector's floor, 10 dB in τ·ln 10, the floor ±10 %.
    constexpr double t = -40.0;
    auto worst = 0.0;

    for (const auto& [attackMs, releaseMs] : { std::pair { 1.0, 20.0 }, std::pair { 1.0, 50.0 }, std::pair { 1.0, 150.0 }, std::pair { 1.0, 500.0 },
                                               std::pair { 1.0, 1500.0 }, std::pair { 1.0, 5000.0 }, std::pair { ref::markAttackMs, ref::markReleaseMs },
                                               std::pair { 10.0, 20.0 }, std::pair { 100.0, 50.0 }, std::pair { 100.0, 150.0 }, std::pair { 300.0, 500.0 } })
        for (const auto depth : { 6.0, 12.0, 24.0 })
        {
            const auto p = params (t, 0.0, 0.0, 40.0, attackMs, releaseMs);
            const auto tau = ref::rmsTimeConstantMs (attackMs) * 0.001;
            const auto expectedPer10 = ref::rmsReleasePer10DbMs (attackMs, releaseMs, 0.0);
            const auto held = samplesFor (std::max (20.0 * tau, 0.05), rate);
            auto key = carrier (carrierAmplitude (Detector::rms, t + depth), held);
            append (key, silence (samplesFor (depth / 10.0 * expectedPer10 * 0.001 * 1.5 + 0.01, rate)));

            auto m = prepared (Detector::rms);
            const auto r = runModel (m, p, key)[0];
            REQUIRE_THAT (r[(size_t) held - 1], WithinAbs (depth, 1.0e-6));

            const auto crossing = [&] (double level) {
                for (size_t j = (size_t) held; j < r.size(); ++j)
                    if (r[j] <= level)
                        return (double) (j - 1) + (r[j - 1] - level) / (r[j - 1] - r[j]);
                return std::numeric_limits<double>::quiet_NaN();
            };
            const auto seconds = (crossing (0.1 * depth) - crossing (0.9 * depth)) / rate;
            const auto per10 = 10.0 * seconds / (0.8 * depth) * 1000.0;
            worst = std::max (worst, std::abs (per10 / expectedPer10 - 1.0));

            std::printf ("VCA RMS release, Attack %g ms, Release %g ms, depth %g dB: %.2f ms per 10 dB (expected %.2f%s)\n", attackMs, releaseMs, depth,
                         per10, expectedPer10, expectedPer10 > releaseMs ? ", the detector's floor" : "");
            INFO ("attack " << attackMs << ", release " << releaseMs << ", depth " << depth);
            REQUIRE (withinTimeGate (per10 * 0.001, expectedPer10 * 0.001, rate));
        }

    std::printf ("VCA RMS release, worst |measured/expected - 1|: %.2f %%\n", 100.0 * worst);

    // At the mark the floor at ∞:1 is the 160A's 125 dB/s itself.
    REQUIRE_THAT (VcaModel::rmsReleaseFloorMs (ref::markAttackMs, 0.0), WithinAbs (80.0, 0.05));
}

TEST_CASE ("VCA RMS: Auto release changes nothing", "[vca][rms][auto]")
{
    // Auto dims for RMS (PLAN.md, Controls): the constant rate already lengthens with depth.
    auto key = carrier (amplitudeFor (-10.0), samplesFor (0.5, rate));
    append (key, silence (samplesFor (1.0, rate)));
    auto a = prepared (Detector::rms), b = prepared (Detector::rms);
    REQUIRE (runModel (a, params (-30.0, 0.25, 6.0, 40.0, 5.0, 150.0, false), key)[0]
             == runModel (b, params (-30.0, 0.25, 6.0, 40.0, 5.0, 150.0, true), key)[0]);
}

TEST_CASE ("VCA: look-ahead starts the attack early with the same shape, and the release when the audio ends", "[vca][lookahead]")
{
    // The core's row: the chain hands the model the key LA samples early; with the window the
    // attack side is the LA = 0 trace shifted by LA, the release side the same in audio time.
    constexpr double t = -30.0;

    for (const auto d : { Detector::peak, Detector::rms })
        for (const auto la : { 144, 480 })
        {
            const auto p = params (t, 0.25, 0.0, 40.0, 1.0, 100.0);
            const auto pre = 2000, burst = samplesFor (1.0, rate), post = samplesFor (1.0, rate);
            auto key = silence (pre);
            append (key, carrier (carrierAmplitude (d, t + 15.0), burst));
            append (key, silence (post));

            Signal advanced (key.size(), 0.0);
            for (size_t i = 0; i + (size_t) la < key.size(); ++i)
                advanced[i] = key[i + (size_t) la];
            const Signal lookahead (key.size(), (double) la);

            auto a = prepared (d), b = prepared (d);
            const auto plain = runModel (a, p, key)[0];
            const auto early = runModel (b, p, advanced, nullptr, 64, &lookahead)[0];

            auto attackSide = 0.0, releaseSide = 0.0;
            for (size_t i = (size_t) (pre - la); i < (size_t) (pre + burst - la); ++i)
                attackSide = std::max (attackSide, std::abs (early[i] - plain[i + (size_t) la]));
            for (size_t i = (size_t) (pre + burst); i + (size_t) la < key.size(); ++i)
                releaseSide = std::max (releaseSide, std::abs (early[i] - plain[i]));

            INFO (nameOf (d) << ", look-ahead " << la << " samples: attack side " << attackSide << ", release side " << releaseSide);
            REQUIRE (myzel::dsp::isExactly (early[(size_t) (pre - la - 1)], 0.0));
            if (d == Detector::peak)
                REQUIRE (early[(size_t) (pre - la)] > 0.0);
            REQUIRE (attackSide < 1.0e-6);
            REQUIRE (releaseSide < 1.0e-6);
        }
}
