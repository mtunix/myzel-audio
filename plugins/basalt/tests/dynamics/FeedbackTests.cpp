// The feedback loop as a building block (dynamics/NOTES.md, Feedback, Settled 29-30), driven with
// a peak pre-hold as M2's FET and Vari-mu will drive it: the static curve to the cap, the
// measured attack and release, Floru's law without the compensation, Range on the state, the
// look-ahead shift.

#include "Harness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;

    ModelParams params (double t, double inverseRatio, double knee, double range, double attackMs, double releaseMs)
    {
        ModelParams p;
        p.curve = { t, inverseRatio, knee, range };
        p.attackMs = attackMs;
        p.releaseMs = releaseMs;
        p.autoRelease = false;
        return p;
    }

    /** Ascending 1 dB steps, each settled; the worst |mean reduction − expected| over the staircase. */
    double staircaseError (FeedbackHarness& h, const ModelParams& p, bool useSine, double hz, double from, double to, double settleS)
    {
        const auto settle = samplesFor (settleS, rate), measure = samplesFor (0.020, rate);
        Signal key;
        std::vector<double> levels;
        for (auto level = from; level <= to + 1.0e-9; level += 1.0)
        {
            const auto n = settle + measure;
            const auto a = amplitudeFor (level);
            append (key, useSine ? sine (a, hz, rate, n, 2.0 * std::numbers::pi * hz * (double) key.size() / rate) : carrier (a, n));
            levels.push_back (level);
        }

        const auto r = h.run (p, key)[0];
        auto curve = p.curve;
        curve.inverseRatio = cappedInverseRatio (curve.inverseRatio, h.ratioCap);

        auto worst = 0.0;
        for (size_t s = 0; s < levels.size(); ++s)
        {
            const auto end = (s + 1) * (size_t) (settle + measure);
            const auto got = mean (r, end - (size_t) measure, end);
            worst = std::max (worst, std::abs (got - staticReduction (curve, levels[s])));
        }
        return worst;
    }
}

TEST_CASE ("Feedback: the loop settles on eq. 4 up to its cap", "[dynamics][feedback][static]")
{
    FeedbackHarness h;
    auto worstCarrier = 0.0;

    for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 20.0, 50.0, 0.0 })
        for (const auto knee : { 0.0, 6.0, 24.0 })
            for (const auto range : { 40.0, 10.0 })
            {
                const auto p = params (-30.0, ratio > 0.0 ? 1.0 / ratio : 0.0, knee, range, 10.0, 1000.0);
                // Inside the knee the loop runs slower than Attack by R/(local ratio) (NOTES.md, Knee region).
                const auto settleS = knee > 0.0 && (ratio >= 20.0 || ratio <= 0.0) ? 3.0 : 0.6;
                const auto e = staircaseError (h, p, false, 0.0, -50.0, 0.0, settleS);
                worstCarrier = std::max (worstCarrier, e);
                INFO ("ratio " << ratio << ", knee " << knee << ", range " << range);
                REQUIRE (e <= 0.25);
            }

    auto worst1k = 0.0, worst100 = 0.0;
    for (const auto ratio : { 4.0, 20.0, 50.0 })
        for (const auto knee : { 0.0, 6.0 })
        {
            const auto p = params (-30.0, 1.0 / ratio, knee, 40.0, 10.0, 1000.0);
            const auto e1k = staircaseError (h, p, true, 1000.0, -40.0, 0.0, 2.0);
            const auto e100 = staircaseError (h, p, true, 100.0, -40.0, 0.0, 2.0);
            worst1k = std::max (worst1k, e1k);
            worst100 = std::max (worst100, e100);
            INFO ("ratio " << ratio << ", knee " << knee);
            REQUIRE (e1k <= 0.25);
            REQUIRE (e100 <= 0.25);
        }

    std::printf ("Feedback static curve, worst |error|: carrier %.4f dB (R to 50, knee 0/6/24, Range 40/10); "
                 "1 kHz %.4f dB, 100 Hz %.4f dB (R 4/20/50, knee 0/6)\n", worstCarrier, worst1k, worst100);
}

TEST_CASE ("Feedback: attack and release mean the knobs at every ratio", "[dynamics][feedback][ballistics]")
{
    FeedbackHarness h;
    constexpr double t = -30.0, depth = 10.0;
    auto worstAttack = 0.0, worstRelease = 0.0;

    for (const auto ratio : { 2.0, 4.0, 20.0, 50.0 })
        for (const auto attackMs : { 0.02, 0.1, 1.0, 10.0, 100.0, 300.0 })
            for (const auto releaseMs : { 5.0, 50.0, 150.0, 500.0, 5000.0 })
            {
                const auto p = params (t, 1.0 / ratio, 0.0, 40.0, attackMs, releaseMs);
                const auto level = t + depth / (1.0 - 1.0 / ratio);
                const auto pre = 16;
                const auto held = samplesFor (std::max (5.0 * (attackMs + releaseMs), 20.0 * attackMs) * 0.001, rate);

                auto key = silence (pre);
                append (key, carrier (amplitudeFor (level), held));
                append (key, silence (samplesFor (3.0 * releaseMs * 0.001, rate) + 64));

                const auto r = h.run (p, key)[0];
                const auto settled = r[(size_t) (pre + held - 1)];
                const auto attack = attackTime (r, (size_t) pre, rate, settled);
                const auto release = releaseTime (r, (size_t) (pre + held), rate, settled);

                INFO ("ratio " << ratio << ", attack " << attackMs << " ms, release " << releaseMs << " ms: measured "
                               << attack * 1000.0 << " / " << release * 1000.0 << " ms, settled " << settled << " dB");
                REQUIRE_THAT (settled, WithinAbs (depth, 1.0e-6));
                REQUIRE (withinTimeGate (attack, attackMs * 0.001, rate));
                REQUIRE (withinTimeGate (release, releaseMs * 0.001, rate));

                if (attackMs >= 0.1)
                    worstAttack = std::max (worstAttack, std::abs (attack / (attackMs * 0.001) - 1.0));
                worstRelease = std::max (worstRelease, std::abs (release / (releaseMs * 0.001) - 1.0));
            }

    std::printf ("Feedback ballistics, worst |measured/set − 1|: attack %.2f %% (from 0.1 ms), release %.2f %%\n",
                 100.0 * worstAttack, 100.0 * worstRelease);
}

TEST_CASE ("Feedback: without the compensation the loop divides the attack by the ratio (Floru)", "[dynamics][feedback][ballistics]")
{
    FeedbackHarness h;
    h.compensate = false;
    constexpr double t = -30.0, depth = 10.0;

    // dynamics/NOTES.md, Results: 0.4990, 0.2484, 0.1232, 0.0480 of the detector's constant.
    for (const auto& [ratio, notes] : { std::pair { 2.0, 0.4990 }, std::pair { 4.0, 0.2484 }, std::pair { 8.0, 0.1232 }, std::pair { 20.0, 0.0480 } })
    {
        const auto p = params (t, 1.0 / ratio, 0.0, 40.0, 5.0, 200.0);
        auto key = silence (16);
        append (key, carrier (amplitudeFor (t + depth / (1.0 - 1.0 / ratio)), samplesFor (0.2, rate)));
        const auto r = h.run (p, key)[0];
        const auto measured = attackTime (r, 16, rate, r.back()) / 0.005;
        std::printf ("Feedback uncompensated, R %g: attack %.4f of the detector's constant (1/R = %.4f)\n", ratio, measured, 1.0 / ratio);
        REQUIRE_THAT (measured, WithinAbs (notes, 0.02 * notes));
    }
}

TEST_CASE ("Feedback: Range sits on the state, so it caps without slowing the attack", "[dynamics][feedback][range]")
{
    FeedbackHarness h;
    constexpr double t = -30.0;

    SECTION ("∞ (the cap), 30 dB over, Range 10: 10 dB")
    {
        const auto p = params (t, 0.0, 0.0, 10.0, 5.0, 150.0);
        const auto r = h.run (p, carrier (amplitudeFor (t + 30.0), samplesFor (0.5, rate)))[0];
        REQUIRE_THAT (r.back(), WithinAbs (10.0, 0.05));
    }

    SECTION ("A demand beyond Range 40 still attacks at the knob (NOTES.md: 1.000 × with the state clamped)")
    {
        for (const auto& [ratio, over] : { std::pair { 20.0, 12.0 }, std::pair { 50.0, 12.0 }, std::pair { 50.0, 30.0 } })
        {
            const auto p = params (t, 1.0 / ratio, 0.0, 40.0, 5.0, 150.0);
            auto key = silence (16);
            append (key, carrier (amplitudeFor (t + over), samplesFor (0.3, rate)));
            const auto r = h.run (p, key)[0];
            const auto attack = attackTime (r, 16, rate, r.back());
            INFO ("ratio " << ratio << ", " << over << " dB over: " << attack * 1000.0 << " ms");
            REQUIRE (withinTimeGate (attack, 0.005, rate));
        }
    }
}

TEST_CASE ("Feedback: look-ahead shifts the attack and leaves the release in audio time", "[dynamics][feedback][lookahead]")
{
    // dynamics/NOTES.md, Look-ahead: a 1 kHz burst 15 dB over, 200 ms, Ratio 4, Attack 5 ms,
    // Release 100 ms, the audio delayed by 10 ms; LA = 3 ms against LA = 0.
    const auto latency = samplesFor (0.010, rate);
    constexpr double t = -30.0;
    const auto p = params (t, 0.25, 0.0, 40.0, 5.0, 100.0);

    auto key = silence (480);
    append (key, sine (amplitudeFor (t + 15.0), 1000.0, rate, samplesFor (0.2, rate)));
    const auto end = key.size();
    append (key, silence (samplesFor (0.6, rate)));

    const auto tap = [&] (int la) {
        KeyDelay delay;
        delay.prepare (1, latency);
        Signal out;
        for (const auto x : key)
        {
            delay.push (&x);
            out.push_back (delay.read (0, latency - la));
        }
        return out;
    };

    FeedbackHarness h;
    const auto la = samplesFor (0.003, rate);
    const auto r0 = h.run (p, tap (0))[0];
    const auto rl = h.run (p, tap (la), nullptr, la)[0];

    auto attackSide = 0.0, releaseSide = 0.0;
    for (size_t n = 0; n + (size_t) la < end + (size_t) latency; ++n)
        attackSide = std::max (attackSide, std::abs (rl[n] - r0[n + (size_t) la]));
    for (auto n = end + (size_t) latency; n < key.size(); ++n)
        releaseSide = std::max (releaseSide, std::abs (rl[n] - r0[n]));

    std::printf ("Feedback look-ahead 3 ms, 1 kHz burst: attack side %.1e dB from the shifted trace, release side %.1e dB\n", attackSide, releaseSide);
    REQUIRE (attackSide <= 1.0e-6);
    REQUIRE (releaseSide <= 1.0e-6);
}

TEST_CASE ("Feedback: a model may bring its own demand and estimate, and read the linked demand", "[dynamics][feedback]")
{
    // What M2's Vari-mu needs of the loop (Feedback.h): its rising curve's inverse as the demand,
    // its r = max(s, m, ℓ) as what the estimate subtracts, its memories charging on the linked
    // demand; and a curve fade's second loop as a copy of the running one.
    const Curve curve { -30.0, 0.25, 0.0, 40.0 };
    const auto k = FeedbackLoop::coefficientsFor (5.0, 200.0, 5.0, curve.inverseRatio, rate);

    // Two channels 6 dB apart, swelling in and out of compression.
    const auto n = samplesFor (1.0, rate);
    std::vector<std::array<double, 2>> levels ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const auto l = -36.0 + 24.0 * std::sin (2.0 * std::numbers::pi * 1.5 * i / rate);
        levels[(size_t) i] = { l, l - 6.0 };
    }

    // eq. 18's hard-knee demand written out, its own estimate passed in, against the defaults.
    const auto hardKnee = [] (const Curve& c, double y) { return y > c.thresholdDb ? (y - c.thresholdDb) * (1.0 / c.inverseRatio - 1.0) : 0.0; };
    FeedbackLoop plain, custom;
    auto worst = 0.0, linkedWorst = 0.0;
    for (const auto& level : levels)
    {
        std::array<double, 2> a {}, b {}, estimate { custom.reduction (0), custom.reduction (1) }, linked {};
        const std::array<double, 2> previous { plain.reduction (0), plain.reduction (1) };
        plain.process (level.data(), a.data(), 2, curve, k, 1.0);
        custom.process (level.data(), b.data(), 2, curve, k, 1.0, hardKnee, estimate.data(), linked.data());
        worst = std::max ({ worst, std::abs (a[0] - b[0]), std::abs (a[1] - b[1]) });

        // At Link 100 % both channels take the louder one's demand.
        const auto expected = std::max (feedbackDemand (curve, level[0] - previous[0]), feedbackDemand (curve, level[1] - previous[1]));
        linkedWorst = std::max ({ linkedWorst, std::abs (linked[0] - expected), std::abs (linked[1] - expected) });
    }
    CHECK (worst < 1.0e-9);
    CHECK (linkedWorst < 1.0e-9);

    // A copy carries on as the original, to the bit.
    FeedbackLoop running;
    std::array<double, 2> r {}, s {};
    for (int i = 0; i < n / 2; ++i)
        running.process (levels[(size_t) i].data(), r.data(), 2, curve, k, 0.5);
    auto copy = running;
    auto differ = 0;
    for (int i = n / 2; i < n; ++i)
    {
        running.process (levels[(size_t) i].data(), r.data(), 2, curve, k, 0.5);
        copy.process (levels[(size_t) i].data(), s.data(), 2, curve, k, 0.5);
        differ += r == s ? 0 : 1;
    }
    CHECK (differ == 0);
}
