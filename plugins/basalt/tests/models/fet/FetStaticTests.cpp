// FET's gain computer against models/fet/NOTES.md (Gain computer, All, Gate table): the static
// curve at T_eff on 1 kHz sines and the carrier over the gate's grid, 100 and 50 Hz, the threshold
// moving with the ratio, Auto Gain, Range on the loop's state.

#include "FetHarness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using namespace basalt::testing::fet;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;

    const char* ratioName (double ratio)
    {
        static char text[16];
        if (std::isinf (ratio))
            return "All";
        std::snprintf (text, sizeof (text), "%g", ratio);
        return text;
    }

    /**
        How long a step of the ascending staircase takes to settle within 1 % of its 1 dB: six of
        the closed loop's local time constants, Attack·R·G'(x) (Reference.h), which inside the knee
        at 20:1 is up to twenty times the knob.
    */
    double settleFor (double level, double t, double ratio, double knee, double attackMs)
    {
        return 0.1 + 6.0e-3 * ref::kneeRegionAttackMs (attackMs, level, t, ratio, knee);
    }

    struct Worst
    {
        double error = 0.0;
        char at[160] {};

        void take (double e, const char* where)
        {
            if (e > error)
            {
                error = e;
                std::snprintf (at, sizeof (at), "%s", where);
            }
        }
    };

    /** The worst |mean reduction − the specified curve| over one staircase, T − 20 … T + 30 in 1 dB steps. */
    double staircaseError (double t, double ratio, double knee, double range, double hz, double attackMs, double releaseMs,
                           double from = ref::staticBelowDb, double to = ref::staticAboveDb, double step = 1.0, double* signedWorst = nullptr)
    {
        const auto p = params (t, ratio, knee, range, attackMs, releaseMs);
        const auto s = staircase (t + from, t + to, step, hz, rate, 0.040, [&] (double level) { return settleFor (level, t, ratio, knee, attackMs); });
        auto m = prepared (rate);
        const auto r = runModel (m, p, s.key)[0];

        auto worst = 0.0;
        for (size_t i = 0; i < s.levels.size(); ++i)
        {
            const auto got = mean (r, s.ends[i] - s.measure, s.ends[i]);
            const auto error = got - ref::staticReductionDb (s.levels[i], t, ratio, knee, range);
            if (std::abs (error) > std::abs (worst))
                worst = error;
        }
        if (signedWorst != nullptr)
            *signedWorst = worst;
        return std::abs (worst);
    }
}

TEST_CASE ("FET: the static curve at T_eff on 1 kHz sines and the carrier", "[fet][static]")
{
    // NOTES.md, Gate table: R 1.5 … 20 and All, W 0/6/24, Range 40/10, Attack 10 ms, Release 1 s,
    // T − 20 … T + 30 dB: within ±0.25 dB of min(x − G(x; T_eff), Range).
    Worst sineWorst, carrierWorst;

    for (const auto hz : { 1000.0, 0.0 })
        for (const auto t : ref::staticThresholds)
            for (const auto ratio : ref::staticRatios)
                for (const auto knee : ref::staticKnees)
                    for (const auto range : ref::staticRanges)
                    {
                        const auto error = staircaseError (t, ratio, knee, range, hz, ref::staticAttackMs, ref::staticReleaseMs);
                        char where[160];
                        std::snprintf (where, sizeof (where), "T %g, R %s, knee %g, range %g", t, ratioName (ratio), knee, range);
                        (hz > 0.0 ? sineWorst : carrierWorst).take (error, where);
                        INFO ((hz > 0.0 ? "1 kHz sine, " : "carrier, ") << where << ": worst " << error << " dB");
                        REQUIRE (error <= ref::staticToleranceDb);
                    }

    std::printf ("FET static curve, worst |error|: 1 kHz sine %.4f dB (%s), carrier %.4f dB (%s)\n", sineWorst.error, sineWorst.at,
                 carrierWorst.error, carrierWorst.at);
}

TEST_CASE ("FET: 100 and 50 Hz reported, M0's figures repeated", "[fet][static]")
{
    // NOTES.md, Gate table: 100 and 50 Hz are reported, not gated. The 1 ms hold (Settled 38)
    // leaves the held level falling within a 100 Hz half-cycle, the closed loop releases towards
    // it R times faster than the knob (a partial release), and the attack wins it back at the
    // knob's speed: the steady reduction reads low, more so at slower Attack and higher Ratio
    // (NOTES.md, Measured in M2). PLAN.md's Static row gates feedback models at 100 Hz; FET's
    // NOTES set that aside with the hold, and this row says by how much.
    for (const auto hz : { 100.0, 50.0 })
        for (const auto attackMs : { 0.8, ref::staticAttackMs })
        {
            std::printf ("FET static curve at %g Hz, T -30, Attack %g ms, Release 1 s, worst over knees and ranges:", hz, attackMs);
            for (const auto ratio : ref::staticRatios)
            {
                auto worst = 0.0;
                for (const auto knee : ref::staticKnees)
                    for (const auto range : ref::staticRanges)
                    {
                        double signedError = 0.0;
                        staircaseError (-30.0, ratio, knee, range, hz, attackMs, ref::staticReleaseMs, ref::staticBelowDb, ref::staticAboveDb, 1.0,
                                        &signedError);
                        if (std::abs (signedError) > std::abs (worst))
                            worst = signedError;
                    }
                std::printf (" R %s %+.3f;", ratioName (ratio), worst);
            }
            std::printf (" dB\n");
        }

    // NOTES.md's M0 simulation, repeated on the engine: Attack 0.8 ms, Release 1.1 s, Knee 6,
    // T −20, −30 … 0 dB in 3 dB steps: 0.000 dB at 1 kHz; −0.020, −0.025, −0.027, −0.029 dB at
    // 100 Hz for 4, 8, 12, 20:1 (the worst level of each staircase).
    const std::pair<double, double> m0At100Hz[] { { 4.0, -0.020 }, { 8.0, -0.025 }, { 12.0, -0.027 }, { 20.0, -0.029 } };
    for (const auto hz : { 1000.0, 100.0, 50.0 })
    {
        std::printf ("FET static curve, M0's set-up at %g Hz:", hz);
        for (const auto& [ratio, m0] : m0At100Hz)
        {
            double signedError = 0.0;
            staircaseError (-20.0, ratio, 6.0, 40.0, hz, 0.8, 1100.0, -10.0, 20.0, 3.0, &signedError);
            std::printf (" R %g %+.4f dB", ratio, signedError);
            INFO (hz << " Hz, R " << ratio << ": " << signedError << " dB");
            if (myzel::dsp::isExactly (hz, 1000.0))
                CHECK (std::abs (signedError) <= 0.0005);
            else if (myzel::dsp::isExactly (hz, 100.0))
                CHECK (std::abs (signedError - m0) <= 0.0015);
        }
        std::printf ("\n");
    }
}

TEST_CASE ("FET: the threshold moves with the ratio, as the reference's table", "[fet][static][coupling]")
{
    // NOTES.md, Gate table: hard knee, the carrier on a 0.01 dB level sweep; the knee found by
    // extrapolating the reduction above it linearly to 0 dB sits at T + 0, 4, 5, 6 dB (±0.05) for
    // 4, 8, 12, 20:1, at T + 2 dB for 5.66:1, at T for All. Below 4:1 the offset stays 0.
    constexpr double t = -30.0;
    auto rows = std::vector<std::pair<double, double>> (ref::couplingRows.begin(), ref::couplingRows.end());
    rows.emplace_back (2.0, 0.0);
    rows.emplace_back (1.5, 0.0);

    for (const auto& [ratio, offset] : rows)
    {
        const auto expected = ref::kneeFoundAtDb (t, ratio);
        REQUIRE_THAT (expected, WithinAbs (t + offset, ref::kneeToleranceDb));

        const auto p = params (t, ratio, 0.0, 40.0, 0.2, 300.0);
        const auto s = staircase (expected - 3.0, expected + 12.0, 0.01, 0.0, rate, 0.0, [] (double) { return 0.002; });
        auto m = prepared (rate);
        const auto r = runModel (m, p, s.key)[0];

        // Least squares through the settled reductions between 1 and 8 dB, extrapolated to 0.
        auto n = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (size_t i = 0; i < s.levels.size(); ++i)
        {
            const auto y = r[s.ends[i] - 1];
            if (y < 1.0 || y > 8.0)
                continue;
            const auto x = s.levels[i];
            n += 1.0;
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
        }
        const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        const auto knee = (sx - sy / slope) / n;

        std::printf ("FET threshold coupling, R %s: knee at T %+.4f dB (specified %+.2f), slope %.5f (1 - 1/R %.5f)\n", ratioName (ratio),
                     knee - t, offset, slope, 1.0 - 1.0 / ref::loopRatio (ratio));
        INFO ("ratio " << ratioName (ratio) << ": knee at T " << knee - t << " dB");
        REQUIRE_THAT (knee, WithinAbs (t + offset, ref::kneeToleranceDb));
        REQUIRE_THAT (slope, WithinAbs (1.0 - 1.0 / ref::loopRatio (ratio), 1.0e-3));
    }
}

TEST_CASE ("FET: the static reduction the display and Auto Gain read is eq. 4 at T_eff", "[fet][static][autogain]")
{
    // NOTES.md, Make-up: Auto Gain is r_s(L_ref) with T_eff, ±0.01 dB against the formula.
    auto m = prepared (rate);
    auto worst = 0.0;
    for (const auto t : { -60.0, -40.0, -30.0, -24.0, -18.0, -10.0, 0.0 })
        for (const auto ratio : { 1.0, 1.5, 2.0, 4.0, 5.66, 8.0, 10.0, 12.0, 16.0, 20.0, ref::infinity })
            for (const auto knee : { 0.0, 6.0, 12.0, 24.0 })
                for (const auto range : { 0.0, 10.0, 40.0 })
                {
                    const auto p = params (t, ratio, knee, range, 10.0, 150.0);
                    worst = std::max (worst, std::abs (autoGainDb (m, p) - ref::autoGainDb (t, ratio, knee, range)));
                    for (auto level = -80.0; level <= 20.0; level += 0.5)
                        worst = std::max (worst, std::abs (m.staticReduction (p, level) - ref::staticReductionDb (level, t, ratio, knee, range)));
                }
    std::printf ("FET staticReduction and Auto Gain against Reference.h: worst %.2e dB\n", worst);
    REQUIRE (worst <= 1.0e-9);
}

TEST_CASE ("FET: Range sits on the loop's state", "[fet][static][range]")
{
    // The core's Range row at FET's cap: 30 dB over T_eff at 20:1 and All, Range 10, steady
    // 10 dB ±0.05; a demand past Range 40 still attacks at the knob.
    constexpr double t = -30.0;
    for (const auto ratio : { 20.0, ref::infinity })
    {
        const auto p = params (t, ratio, 0.0, 10.0, 5.0, 150.0);
        auto m = prepared (rate);
        const auto r = runModel (m, p, carrier (amplitudeFor (ref::effectiveThresholdDb (t, ratio) + 30.0), samplesFor (0.5, rate)))[0];
        REQUIRE_THAT (r.back(), WithinAbs (10.0, ref::rangeToleranceDb));
        REQUIRE (*std::max_element (r.begin(), r.end()) <= 10.0);

        for (const auto over : { 12.0, 30.0 })
        {
            const auto q = params (t, ratio, 0.0, 40.0, 5.0, 150.0);
            const auto lag = (size_t) samplesFor (ref::lagMs (ratio) * 0.001, rate);
            auto key = silence (16);
            append (key, carrier (amplitudeFor (ref::effectiveThresholdDb (t, ratio) + over), samplesFor (0.3, rate)));
            auto fresh = prepared (rate);
            const auto rr = runModel (fresh, q, key)[0];
            const auto attack = attackTime (rr, 16 + lag, rate, rr.back());
            INFO ("ratio " << ratioName (ratio) << ", " << over << " dB over: " << attack * 1000.0 << " ms");
            REQUIRE (withinTimeGate (attack, 0.005, rate));
        }
    }
}
