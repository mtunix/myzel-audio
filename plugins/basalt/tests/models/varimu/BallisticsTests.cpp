// Vari-mu's ballistics (NOTES.md, Ballistics, Gate table): the calibrated attack and its floor,
// overshoot, the release with Auto off, and the three-tier Auto release against the reference
// transcription on the same stimulus.

#include "VariMuHarness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using namespace basalt::testing::varimu;
using Catch::Matchers::WithinAbs;

namespace
{
    /** Long enough for D_final: the loop's slowest constant on the way (τ·R_ref at the knee's foot) many times over. */
    double holdFor (const ModelParams& p)
    {
        const auto tau = std::max (p.attackMs, ref::attackFloorMs) * 0.001 / ref::attackCalibration (p.curve.inverseRatio, p.curve.kneeDb);
        return std::max ({ 5.0 * (p.attackMs + p.releaseMs) * 0.001, 40.0 * tau, 0.05 });
    }
}

TEST_CASE ("Vari-mu attack: the reference step reads the knob, floored at 0.1 ms; no overshoot", "[varimu][ballistics][attack]")
{
    // NOTES.md, Gate table: carrier, silence → 10 dB, Knee 0, 6, 24, R 1.5, 4, 8, 20: Attack
    // ±10 % (±15 % at 0.1 ms, below it 0.1 ms); overshoot ≤ 0.05 dB for steps to 10, 20, 30 dB at
    // every Attack, 44.1 and 48 kHz. Deeper steps attack faster: reported.
    for (const auto rate : { 48000.0, 44100.0 })
    {
        auto worstAttack = 0.0, worstOvershoot = 0.0;
        double deepLow = 10.0, deepHigh = 0.0;

        for (const auto ratio : { 1.5, 4.0, 8.0, 20.0 })
            for (const auto knee : { 0.0, 6.0, 24.0 })
                for (const auto attackMs : { 0.02, 0.05, 0.1, 0.2, 0.4, 1.0, 10.0, 25.0, 50.0, 70.0, 300.0 })
                {
                    auto m = prepared (rate);
                    const auto p = params (-30.0, 1.0 / ratio, knee, 40.0, attackMs, 150.0);
                    const auto hold = holdFor (p);

                    const auto reference = attackStep (m, p, 10.0, hold, rate);
                    const auto expected = ref::expectedAttackMs (attackMs) * 0.001;
                    const auto tolerance = ref::attackTolerance (attackMs);
                    const auto error = reference.attackS / expected - 1.0;

                    INFO (rate << " Hz, ratio " << ratio << ", knee " << knee << ", attack " << attackMs << " ms: measured "
                                << reference.attackS * 1000.0 << " ms, settled " << reference.settled << " dB, overshoot " << reference.overshoot);
                    REQUIRE_THAT (reference.settled, WithinAbs (10.0, 1.0e-3));
                    REQUIRE (std::abs (error) <= tolerance);
                    if (attackMs > ref::attackFloorMs)
                        worstAttack = std::max (worstAttack, std::abs (error));

                    for (const auto depth : { 10.0, 20.0, 30.0 })
                    {
                        const auto step = depth < 15.0 ? reference : attackStep (m, p, depth, hold, rate);
                        INFO ("step to " << depth << " dB: overshoot " << step.overshoot << " dB, settled " << step.settled);
                        REQUIRE (step.overshoot <= ref::overshootLimitDb);
                        worstOvershoot = std::max (worstOvershoot, step.overshoot);
                        if (depth > 10.0 && attackMs >= 1.0)
                        {
                            deepLow = std::min (deepLow, step.attackS / expected);
                            deepHigh = std::max (deepHigh, step.attackS / expected);
                        }
                    }
                }

        std::printf ("Vari-mu attack at %.1f kHz: worst |measured/knob − 1| %.2f %% above the floor; overshoot at most %.4f dB; "
                     "steps to 20 and 30 dB read %.2f … %.2f × Attack (from 1 ms)\n",
                     rate / 1000.0, 100.0 * worstAttack, worstOvershoot, deepLow, deepHigh);
    }
}

TEST_CASE ("Vari-mu attack: M0's calibrated table, repeated on the engine", "[varimu][ballistics][attack][report]")
{
    // NOTES.md, Attack, "Calibrated, at 44.1 kHz": measured/Attack for steps to 10, 20 and 30 dB,
    // and the largest overshoot over the settled reduction.
    for (const auto rate : { 44100.0, 48000.0 })
    {
        std::printf ("Vari-mu attack at %.1f kHz, measured/Attack at 10 / 20 / 30 dB (overshoot, dB):\n", rate / 1000.0);
        for (const auto attackMs : { 0.02, 0.05, 0.1, 0.2, 10.0 })
        {
            std::printf ("  %5.2f ms:", attackMs);
            for (const auto& [ratio, knee] : { std::pair { 1.5, 12.0 }, std::pair { 4.0, 6.0 }, std::pair { 8.0, 6.0 }, std::pair { 20.0, 0.0 } })
            {
                auto m = prepared (rate);
                const auto p = params (-30.0, 1.0 / ratio, knee, 40.0, attackMs, 150.0);
                double t[3] {}, overshoot = 0.0;
                for (int k = 0; k < 3; ++k)
                {
                    const auto step = attackStep (m, p, 10.0 * (k + 1), holdFor (p), rate);
                    t[k] = step.attackS / (attackMs * 0.001);
                    overshoot = std::max (overshoot, step.overshoot);
                }
                std::printf ("  R %g W %g: %.2f / %.2f / %.2f (%.3f)", ratio, knee, t[0], t[1], t[2], overshoot);
            }
            std::printf ("\n");
        }
    }
}

TEST_CASE ("Vari-mu attack: between the table's nodes, across Ratio and Knee", "[varimu][ballistics][attack]")
{
    // Φ is read off its table bilinearly; this sweeps ratios between its rows and knees through the
    // band where the reference point enters the knee (W* to 24 dB, from Ratio 6 up), at Attack
    // 30 ms where the table was measured and at 1 ms.
    constexpr double rate = 48000.0;
    auto m = prepared (rate);
    auto worst = 0.0, worstRatio = 0.0, worstKnee = 0.0, worstAttack = 0.0;

    // Under Ratio 1.05 the reference point lies above +120 dBFS, the loudest key the chain passes.
    std::vector<double> ratios { 1.05, 1.1 };
    for (int i = 0; i <= 24; ++i)
        ratios.push_back (1.2 * std::pow (20.0 / 1.2, i / 24.0));
    ratios.push_back (std::numeric_limits<double>::infinity());

    for (const auto ratio : ratios)
        for (const auto knee : { 0.0, 3.0, 9.0, 15.0, 19.0, 20.5, 21.0, 21.25, 21.5, 21.75, 22.0, 22.5, 23.0, 23.5, 24.0 })
            for (const auto attackMs : { 1.0, 30.0 })
            {
                const auto p = params (-30.0, ref::inverseRatioOf (ratio), knee, 40.0, attackMs, 150.0);
                const auto error = attackStep (m, p, 10.0, holdFor (p), rate).attackS / (attackMs * 0.001) - 1.0;
                INFO ("ratio " << ratio << ", knee " << knee << ", attack " << attackMs << " ms: measured/knob " << 1.0 + error);
                REQUIRE (std::abs (error) <= ref::timeTolerance);
                if (std::abs (error) > worst)
                {
                    worst = std::abs (error);
                    worstRatio = ratio;
                    worstKnee = knee;
                    worstAttack = attackMs;
                }
            }

    std::printf ("Vari-mu attack between the table's nodes (Ratio 1.05 … 20, ∞; Knee 0 … 24 dB; Attack 1 and 30 ms): worst |measured/knob − 1| "
                 "%.2f %% (Ratio %.2f, Knee %g dB, Attack %g ms)\n",
                 100.0 * worst, worstRatio, worstKnee, worstAttack);
}

TEST_CASE ("Vari-mu release: Auto off, the knob 1:1", "[varimu][ballistics][release]")
{
    // NOTES.md, Gate table: the core's definition (36.8 % after the step to silence from 10 dB),
    // Release 5 ms … 5 s, ±10 %.
    constexpr double rate = 48000.0;
    auto worst = 0.0;

    for (const auto ratio : { 1.5, 4.0, 20.0 })
        for (const auto attackMs : { 0.1, 10.0, 300.0 })
            for (const auto releaseMs : { 5.0, 15.0, 50.0, 150.0, 500.0, 1500.0, 5000.0 })
            {
                auto m = prepared (rate);
                const auto p = params (-30.0, 1.0 / ratio, 0.0, 40.0, attackMs, releaseMs);
                auto key = carrier (carrierAmplitudeFor (p, 10.0), samplesFor (holdFor (p), rate));
                const auto step = key.size();
                append (key, silence (samplesFor (3.0 * releaseMs * 0.001 + 0.01, rate)));
                const auto r = reductionFor (m, p, key);
                const auto release = releaseTime (r, step, rate, r[step - 1]);
                const auto expected = ref::expectedReleaseMs (releaseMs) * 0.001;
                INFO ("ratio " << ratio << ", attack " << attackMs << " ms, release " << releaseMs << " ms: measured " << release * 1000.0 << " ms");
                REQUIRE (std::abs (release / expected - 1.0) <= ref::timeTolerance);
                worst = std::max (worst, std::abs (release / expected - 1.0));
            }

    std::printf ("Vari-mu release, Auto off: worst |measured/knob − 1| %.2f %%\n", 100.0 * worst);
}

namespace
{
    double engineAutoRelease (const ref::History& h, const ModelParams& p, double depth, double rate)
    {
        constexpr double preS = 0.01;
        const auto level = ref::levelForReduction (depth, p.curve.thresholdDb, p.curve.inverseRatio, p.curve.kneeDb);
        const auto tailS = std::min (ref::slowTier * p.releaseMs * 0.001, ref::tierCapS) * 2.0 + 1.0;
        const auto m = ref::magnitudes (h, amplitudeFor (level), preS, tailS, rate);

        Signal key (m.size());
        for (size_t i = 0; i < m.size(); ++i)
            key[i] = i % 2 == 0 ? m[i] : -m[i];

        auto model = prepared (rate);
        const auto r = reductionFor (model, p, key, 512);
        return ref::releaseSeconds (r, (size_t) std::lround ((preS + h.endS()) * rate), rate);
    }
}

TEST_CASE ("Vari-mu Auto release: three tiers charged by time under reduction", "[varimu][ballistics][auto]")
{
    // NOTES.md, Gate table: Auto on, 10 dB, Release 0.3 and 2 s, Ratio 4, Knee 6, Attack 0.4 ms
    // (M0's set-up). One 50 ms peak releases in Release ±10 %; 3, 5 and 8 peaks, 2 s and 30 s
    // sustained within ±10 % of the transcription on the same stimulus, the release growing with
    // the number of peaks. M0's variant's figures beside them.
    constexpr double rate = 48000.0, depth = 10.0;

    for (const auto releaseMs : { 300.0, 2000.0 })
    {
        const auto p = params (-30.0, 0.25, 6.0, 40.0, 0.4, releaseMs, true);
        const ref::Settings s { -30.0, 0.25, 6.0, 40.0, 0.4, releaseMs, true };
        auto previous = 0.0;

        for (const auto& row : ref::autoRows())
        {
            const auto measured = engineAutoRelease (row.history, p, depth, rate);
            const auto expected = ref::expectedAutoReleaseS (row.history, s, depth, rate);
            const auto m0 = ref::m0ExpectedReleaseS (row, releaseMs);
            std::printf ("Vari-mu Auto, Release %g s, %-30s: %.3f s = %.2f × Release (transcription %.3f s, M0's variant %.2f s)\n", releaseMs * 0.001,
                         row.history.name, measured, measured / (releaseMs * 0.001), expected, m0);
            INFO (row.history.name << ": measured " << measured << " s, transcription " << expected << " s");
            REQUIRE (std::abs (measured / expected - 1.0) <= ref::timeTolerance);

            if (row.history.bursts == 1 && row.history.burstS < 0.1)
                REQUIRE (std::abs (measured / (releaseMs * 0.001) - 1.0) <= ref::timeTolerance);

            if (row.history.burstS < 0.1)
            {
                REQUIRE (measured >= previous);
                previous = measured;
            }
        }

        const ref::History note { "one 200 ms note", 1, 0.2, 0.2 };
        const auto measured = engineAutoRelease (note, p, depth, rate);
        std::printf ("Vari-mu Auto, Release %g s, %-30s: %.3f s = %.2f × Release (transcription %.3f s; M0 3.55 s at Release 2 s)\n", releaseMs * 0.001,
                     note.name, measured, measured / (releaseMs * 0.001), ref::expectedAutoReleaseS (note, s, depth, rate));
    }
}

TEST_CASE ("Vari-mu Auto release: the memories charge only while the side chain calls for reduction", "[varimu][ballistics][auto]")
{
    // NOTES.md, Auto release, the 25 ms rule: after a single 50 ms peak the 5 × memory holds only
    // what 75 ms of charging gives, and a release tail alone never fills it.
    constexpr double rate = 48000.0;
    const auto p = params (-30.0, 0.25, 6.0, 40.0, 0.4, 2000.0, true);
    auto m = prepared (rate);

    auto key = silence (16);
    append (key, carrier (carrierAmplitudeFor (p, 10.0), samplesFor (0.05, rate)));
    const auto end = key.size();
    append (key, silence (samplesFor (1.0, rate)));
    const auto r = reductionFor (m, p, key);
    std::printf ("Vari-mu Auto after one 50 ms peak: memories %.3f and %.3f dB one second on, the loop's reduction %.3f dB\n", m.memory (0, 0),
                 m.memory (0, 1), r.back());

    // Charging at 0.3 s for at most 50 ms + h + 25 ms, then a second of the 10 s release.
    REQUIRE (m.memory (0, 0) < 10.0 * (1.0 - std::exp (-0.080 / 0.3)));
    REQUIRE (m.memory (0, 0) > 0.0);
    REQUIRE (r.back() >= m.memory (0, 0));
    REQUIRE (r[end - 1] > 9.9);

    SECTION ("Auto off: no memory, and the release is Release")
    {
        auto off = p;
        off.autoRelease = false;
        auto n = prepared (rate);
        auto sustained = carrier (carrierAmplitudeFor (off, 10.0), samplesFor (3.0, rate));
        const auto step = sustained.size();
        append (sustained, silence (samplesFor (6.0, rate)));
        const auto q = reductionFor (n, off, sustained);
        REQUIRE (myzel::dsp::isExactly (n.memory (0, 0), 0.0));
        REQUIRE (myzel::dsp::isExactly (n.memory (0, 1), 0.0));
        REQUIRE (std::abs (releaseTime (q, step, rate, q[step - 1]) / 2.0 - 1.0) <= ref::timeTolerance);
    }
}
