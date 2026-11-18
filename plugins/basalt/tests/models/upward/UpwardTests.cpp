// Upward against src/dsp/models/upward/NOTES.md, every row of its gate table at model level: the
// static curve on sines and the carrier with the monotonic output and the exact zero under Floor,
// the attack and the lift-in against the knobs and the specification (Reference.h), Auto, Floor,
// idle, Link, the seed, block sizes, look-ahead and a sane key.

#include "Reference.h"
#include "dynamics/Harness.h"

#include "dsp/dynamics/KeyPath.h"
#include "dsp/dynamics/Stereo.h"
#include "dsp/models/upward/UpwardModel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <functional>
#include <limits>
#include <tuple>
#include <utility>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using basalt::models::UpwardModel;
using Catch::Matchers::WithinAbs;
using myzel::dsp::isExactly;

namespace ref = basalt::measure::reference::upward;

namespace
{
    constexpr double rate = 48000.0;

    ModelParams params (double t, double inverseRatio, double knee, double range, double floorDb, double attackMs, double releaseMs,
                        bool autoRelease = false)
    {
        ModelParams p;
        p.curve = { t, inverseRatio, knee, range };
        p.floorDb = floorDb;
        p.attackMs = attackMs;
        p.releaseMs = releaseMs;
        p.autoRelease = autoRelease;
        return p;
    }

    ref::Curve referenceCurve (const ModelParams& p)
    {
        return { p.curve.thresholdDb, p.curve.inverseRatio, p.curve.kneeDb, p.curve.rangeDb, p.floorDb };
    }

    UpwardModel prepared()
    {
        UpwardModel m;
        m.prepare (rate);
        return m;
    }

    /** The carrier at a reading on the RMS detector (3.01 dB above its amplitude). */
    Signal carrierAt (double readingDb, int n)
    {
        return carrier (amplitudeFor (ref::carrierAmplitudeDbFor (readingDb)), n);
    }

    /** The lift per sample: the reduction's negative. */
    Trace liftOf (const Trace& r)
    {
        Trace l (r.size());
        for (size_t i = 0; i < r.size(); ++i)
            l[i] = 0.0 - r[i];
        return l;
    }

    double inverseOf (double ratio) { return ratio > 0.0 ? 1.0 / ratio : 0.0; }

    bool allZero (const Trace& r)
    {
        return std::all_of (r.begin(), r.end(), [] (double v) { return isExactly (v, 0.0) && isExactly (gainFor (v), 1.0); });
    }

    /** The first time the lift crosses `level` upwards after `from`, interpolated as the core's definition. */
    double risingCrossing (const Trace& lift, size_t from, double level)
    {
        return ref::crossingSeconds (lift, from, rate, level, true, lift[from - 1]);
    }
}

TEST_CASE ("Upward: the engine's static curve is the specified one", "[upward][static]")
{
    // staticReduction() is what the transfer curve, Auto Gain and Learn's display read: it must be
    // −ℓ(x) of NOTES.md, here against Reference.h on a 0.01 dB grid over the gate's settings.
    auto worst = 0.0;
    for (const auto t : { -60.0, -50.0, -30.0, -10.0, 0.0 })
        for (const auto ratio : { 1.0, 1.5, 2.0, 4.0, 10.0, 20.0, 0.0 })
            for (const auto knee : { 0.0, 6.0, 24.0 })
                for (const auto range : { 0.0, 10.0, 40.0 })
                    for (const auto floorDb : { -90.0, -70.0, -50.0, -30.0 })
                    {
                        UpwardModel m;
                        const auto p = params (t, inverseOf (ratio), knee, range, floorDb, 10.0, 1000.0);
                        const auto c = referenceCurve (p);
                        for (auto x = -120.0; x <= 10.0; x += 0.01)
                        {
                            const auto got = m.staticReduction (p, x);
                            worst = std::max (worst, std::abs (got - ref::reduction (c, x)));
                            REQUIRE (got <= 0.0);
                            if (x <= floorDb || isExactly (ratio, 1.0) || isExactly (range, 0.0) || x >= t + 0.5 * knee)
                                REQUIRE (isExactly (got, 0.0));
                        }
                    }
    std::printf ("Upward staticReduction against the specification: worst %.2e dB\n", worst);
    REQUIRE (worst < 1.0e-12);
}

TEST_CASE ("Upward: the output never folds back and the Floor fade has no corner", "[upward][static]")
{
    // NOTES.md, Gain computer: on a 0.005 dB grid from −100 to 0 dB the output's smallest slope is
    // 1/R (0.25 at 4:1, 0.05 at 20:1, 0 at ∞ with Range 10), the largest inside the fade 3.54 at
    // 20:1, Range 40, Floor −70. Checked on the specification and on the engine's curve.
    UpwardModel m;
    std::array<double, 3> steepest {};     // T −50, −40, −30

    for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 20.0, 0.0 })
        for (const auto range : { 10.0, 40.0 })
            for (const auto knee : { 0.0, 6.0, 24.0 })
                for (const auto t : { -50.0, -30.0, -10.0 })
                    for (const auto floorDb : { -90.0, -70.0, -50.0 })
                    {
                        const auto p = params (t, inverseOf (ratio), knee, range, floorDb, 10.0, 1000.0);
                        auto smallest = std::numeric_limits<double>::infinity();
                        auto previous = -100.0 - m.staticReduction (p, -100.0);

                        for (auto x = -100.0 + 0.005; x <= 0.0; x += 0.005)
                        {
                            const auto out = x - m.staticReduction (p, x);
                            smallest = std::min (smallest, (out - previous) / 0.005);
                            previous = out;
                            if (isExactly (knee, 0.0) && isExactly (floorDb, -70.0) && isExactly (range, 40.0) && isExactly (ratio, 20.0))
                                for (size_t k = 0; k < 3; ++k)
                                    steepest[k] = std::max (steepest[k], ref::outputSlope (referenceCurve (params (-50.0 + 10.0 * (double) k, 0.05, 0.0, 40.0, -70.0, 10.0, 1000.0)), x));
                        }

                        INFO ("ratio " << ratio << ", range " << range << ", knee " << knee << ", T " << t << ", floor " << floorDb);
                        REQUIRE (smallest >= inverseOf (ratio) - 1.0e-9);
                    }

    std::printf ("Upward static curve: steepest output slope in the Floor fade at 20:1, Range 40, Floor -70: %.2f at T -50, %.2f at T -40, %.2f at T -30\n",
                 steepest[0], steepest[1], steepest[2]);

    // C¹ at Floor and at Floor + 12 dB: the slope's jump across each is 0.
    const auto p = params (-30.0, 0.25, 0.0, 40.0, -70.0, 10.0, 1000.0);
    const auto c = referenceCurve (p);
    for (const auto x : { -70.0, -58.0 })
        REQUIRE_THAT (ref::outputSlope (c, x - 1.0e-7) - ref::outputSlope (c, x + 1.0e-7), WithinAbs (0.0, 1.0e-5));
}

TEST_CASE ("Upward: the static curve on 1 kHz sines and the carrier", "[upward][static]")
{
    // NOTES.md, Gates: levels F − 10 … T + 10 dB in 1 dB steps; T −50, −30, −10; R 1.5, 2, 4, 10, ∞;
    // W 0, 6, 24; Range 40, 10; F −90, −70, −50; Attack 10 ms, Release 1 s, Auto off: mean lift
    // within ±0.25 dB of ℓ(x). On the carrier also the monotonic output and the exact zero at and
    // below Floor. The sweep descends, so each step's lift comes in (at most c dB at 10 dB/s) or
    // goes away (Attack 10 ms); 0.2 s per step, the last 50 ms measured.
    const auto settle = samplesFor (0.150, rate), measure = samplesFor (0.050, rate);
    auto worstSine = 0.0, worstCarrier = 0.0;
    char worstSineAt[128] {}, worstCarrierAt[128] {};

    for (const auto useSine : { true, false })
        for (const auto t : { -50.0, -30.0, -10.0 })
            for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 0.0 })
                for (const auto knee : { 0.0, 6.0, 24.0 })
                    for (const auto range : { 40.0, 10.0 })
                        for (const auto floorDb : { -90.0, -70.0, -50.0 })
                        {
                            const auto p = params (t, inverseOf (ratio), knee, range, floorDb, 10.0, 1000.0);
                            const auto c = referenceCurve (p);

                            Signal key;
                            std::vector<double> levels;
                            for (auto level = t + 10.0; level >= floorDb - 10.0 - 1.0e-9; level -= 1.0)
                            {
                                const auto n = settle + measure;
                                append (key, useSine ? sine (amplitudeFor (level), 1000.0, rate, n, 2.0 * std::numbers::pi * 1000.0 * (double) key.size() / rate)
                                                     : carrierAt (level, n));
                                levels.push_back (level);
                            }

                            auto m = prepared();
                            const auto lift = liftOf (runModel (m, p, key)[0]);
                            auto previousOutput = -std::numeric_limits<double>::infinity();

                            for (size_t s = 0; s < levels.size(); ++s)
                            {
                                const auto end = (s + 1) * (size_t) (settle + measure);
                                const auto got = mean (lift, end - (size_t) measure, end);
                                const auto expected = ref::lift (c, levels[s]);
                                const auto error = std::abs (got - expected);
                                auto& worst = useSine ? worstSine : worstCarrier;
                                if (error > worst)
                                {
                                    worst = error;
                                    std::snprintf (useSine ? worstSineAt : worstCarrierAt, sizeof (worstSineAt),
                                                   "T %g, R %g, knee %g, range %g, floor %g, level %g", t, ratio, knee, range, floorDb, levels[s]);
                                }

                                INFO ((useSine ? "sine" : "carrier") << ", T " << t << ", ratio " << ratio << ", knee " << knee << ", range " << range
                                                                     << ", floor " << floorDb << ", level " << levels[s] << ": " << got << " for " << expected);
                                REQUIRE (error <= 0.25);

                                if (! useSine)
                                {
                                    // Descending: each output must lie at or below the one before.
                                    const auto output = levels[s] + got;
                                    if (s > 0)
                                        REQUIRE (output <= previousOutput + 1.0e-6);
                                    previousOutput = output;

                                    if (levels[s] <= floorDb)
                                        for (auto i = end - (size_t) measure; i < end; ++i)
                                            REQUIRE (isExactly (lift[i], 0.0));
                                }
                            }
                        }

    std::printf ("Upward static curve, worst |error|: 1 kHz sine %.4f dB (%s), carrier %.6f dB (%s)\n", worstSine, worstSineAt,
                 worstCarrier, worstCarrierAt);
}

namespace
{
    /** A second-order Butterworth high-pass (bilinear, RBJ's form), for band-limiting the test noise. */
    void highPass (Signal& x, double hz)
    {
        const auto w = 2.0 * std::numbers::pi * hz / rate;
        const auto alpha = std::sin (w) / std::numbers::sqrt2, cw = std::cos (w), a0 = 1.0 + alpha;
        const auto b0 = 0.5 * (1.0 + cw) / a0, b1 = -(1.0 + cw) / a0, a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : x)
        {
            const auto y = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = v;
            y2 = y1;
            y1 = y;
            v = y;
        }
    }
}

TEST_CASE ("Upward: 50 and 100 Hz sines and pink noise, reported", "[upward][static]")
{
    // Not gated (NOTES.md, Gates): the 10 ms RMS ripples at 2f and the asymmetric ballistics ride
    // it. Lift 9 dB (T − 12 dB, Ratio 4), the mean lift over the last second against ℓ, at M0's
    // settings (NOTES.md, Ballistics). Pink noise (Kellett's filter) high-passed at 20 Hz, its
    // long-term reading set to T − 12 dB.
    const auto c = ref::Curve { -30.0, 0.25, 0.0, 40.0, -90.0 };
    for (const auto& [attackMs, releaseMs] : { std::pair { 1.0, 150.0 }, std::pair { 30.0, 150.0 }, std::pair { 30.0, 1000.0 } })
    {
        const auto p = params (c.thresholdDb, c.inverseRatio, c.kneeDb, c.rangeDb, c.floorDb, attackMs, releaseMs);
        std::printf ("Upward, Attack %g ms, Release %g ms, lift 9 dB:", attackMs, releaseMs);
        for (const auto hz : { 50.0, 100.0, 1000.0 })
        {
            auto m = prepared();
            const auto lift = liftOf (runModel (m, p, sine (amplitudeFor (-42.0), hz, rate, samplesFor (4.0, rate)))[0]);
            std::printf ("  %g Hz %+.3f dB", hz, mean (lift, (size_t) samplesFor (3.0, rate), lift.size()) - 9.0);
        }
        std::printf ("\n");
    }

    auto noise = pinkNoise (samplesFor (30.0, rate), 7u);
    highPass (noise, 20.0);
    auto power = 0.0;
    for (const auto v : noise)
        power += v * v;
    scale (noise, amplitudeFor (-42.0) / std::sqrt (2.0 * power / (double) noise.size()));

    for (const auto& [attackMs, releaseMs] : { std::pair { 5.0, 150.0 }, std::pair { 30.0, 500.0 }, std::pair { 30.0, 2000.0 } })
    {
        const auto p = params (c.thresholdDb, c.inverseRatio, c.kneeDb, c.rangeDb, c.floorDb, attackMs, releaseMs);
        auto m = prepared();
        const auto lift = liftOf (runModel (m, p, noise)[0]);
        std::printf ("Upward, pink noise at T - 12 dB, Attack %g ms, Release %g ms: mean lift %+.3f dB against 9 dB\n", attackMs, releaseMs,
                     mean (lift, (size_t) samplesFor (5.0, rate), lift.size()) - 9.0);
    }
}

TEST_CASE ("Upward: THD+N of a 1 kHz sine at 6 dB of lift, reported", "[upward][distortion]")
{
    // PLAN.md, Distortion, Character off: reported for every model, gated for none of Upward's
    // settings. The sine at T − 8 dB (Ratio 4, hard knee: a 6 dB lift), settled for 25 attack
    // constants and 2 s; the fundamental fitted over 50 whole periods, THD+N the rest against it.
    constexpr double t = -30.0;
    const auto x = sine (amplitudeFor (t - 8.0), 1000.0, rate, samplesFor (3.0, rate));
    std::printf ("Upward THD+N, 1 kHz at 6 dB of lift (Attack \\ Release 5, 50, 150, 1000 ms):\n");

    for (const auto attackMs : { 1.0, 10.0, 30.0, 100.0 })
    {
        std::printf ("  Attack %5g ms:", attackMs);
        for (const auto releaseMs : { 5.0, 50.0, 150.0, 1000.0 })
        {
            const auto p = params (t, 0.25, 0.0, 40.0, -90.0, attackMs, releaseMs);
            auto m = prepared();
            const auto r = runModel (m, p, x)[0];

            const auto from = (size_t) samplesFor (std::max (2.0, 25.0 * attackMs * 0.001), rate);
            const auto n = (size_t) samplesFor (0.050, rate);
            REQUIRE (from + n <= x.size());
            auto ss = 0.0, sc = 0.0, cc = 0.0, ys = 0.0, yc = 0.0;
            for (auto i = from; i < from + n; ++i)
            {
                const auto w = 2.0 * std::numbers::pi * 1000.0 * (double) i / rate;
                const auto y = x[i] * (double) gainFor (r[i]);
                ss += std::sin (w) * std::sin (w);
                sc += std::sin (w) * std::cos (w);
                cc += std::cos (w) * std::cos (w);
                ys += y * std::sin (w);
                yc += y * std::cos (w);
            }
            const auto det = ss * cc - sc * sc;
            const auto a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
            auto residual = 0.0, fundamental = 0.0;
            for (auto i = from; i < from + n; ++i)
            {
                const auto w = 2.0 * std::numbers::pi * 1000.0 * (double) i / rate;
                const auto f = a * std::sin (w) + b * std::cos (w);
                const auto e = x[i] * (double) gainFor (r[i]) - f;
                residual += e * e;
                fundamental += f * f;
            }
            REQUIRE_THAT (20.0 * std::log10 (std::sqrt (fundamental / (double) n) * std::numbers::sqrt2 / amplitudeFor (t - 8.0)), WithinAbs (6.0, 0.1));
            std::printf (" %7.1f", 10.0 * std::log10 (residual / fundamental));
        }
        std::printf (" dBc\n");
    }
}

TEST_CASE ("Upward: the attack means the knob", "[upward][ballistics]")
{
    // NOTES.md, Gates: the carrier from T − 20 to T + 10 dB (the settled lift c·20), time to 36.8 %
    // of the starting lift: ±10 % of Attack from 3 ms; below, ±10 % of the specified time
    // (Reference.h: the detector's rise adds to Attack). Every row also within 1 % of the
    // specification's own figure.
    using G = ref::AttackGate;
    auto worstKnob = 0.0, worstSpec = 0.0;

    for (const auto attackMs : { 0.02, 0.05, 0.1, 0.3, 0.5, 1.0, 3.0, 10.0, 30.0, 100.0, 300.0 })
    {
        const auto specified = ref::attackSeconds (attackMs, rate);
        const auto gated = ref::gatedAttackSeconds (attackMs, rate);
        std::printf ("Upward attack %g ms: specified %.4f ms (%.3f x; Attack + 0.25 ms would be %.3f ms):", attackMs, specified * 1000.0,
                     specified / (attackMs * 0.001), attackMs + 0.25);

        for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 0.0 })
        {
            const auto p = params (G::thresholdDb, inverseOf (ratio), G::kneeDb, G::rangeDb, G::floorDb, attackMs, G::releaseMs);
            const auto hold = samplesFor (G::holdSecondsFor (attackMs), rate);
            auto key = carrierAt (G::thresholdDb - G::belowDb, hold);
            append (key, carrierAt (G::thresholdDb + G::aboveDb, samplesFor (10.0 * attackMs * 0.001 + 0.05, rate)));

            auto m = prepared();
            const auto lift = liftOf (runModel (m, p, key)[0]);
            const auto start = lift[(size_t) hold - 1];
            const auto measured = releaseTime (lift, (size_t) hold, rate, start);

            INFO ("ratio " << ratio << ", attack " << attackMs << " ms: start " << start << " dB, measured " << measured * 1000.0 << " ms, gated against "
                           << gated * 1000.0 << ", specified " << specified * 1000.0);
            REQUIRE_THAT (start, WithinAbs ((1.0 - inverseOf (ratio)) * G::belowDb, 1.0e-6));
            REQUIRE (withinTimeGate (measured, gated, rate));
            REQUIRE (std::abs (measured / specified - 1.0) <= 0.01);

            std::printf (" R %g %.4f", ratio, measured * 1000.0);
            if (attackMs >= 3.0)
                worstKnob = std::max (worstKnob, std::abs (measured / (attackMs * 0.001) - 1.0));
            worstSpec = std::max (worstSpec, std::abs (measured / specified - 1.0));
        }
        std::printf (" ms\n");
    }

    std::printf ("Upward attack, worst |measured/knob - 1| from 3 ms %.2f %%, worst |measured/specified - 1| %.4f %%\n", 100.0 * worstKnob,
                 100.0 * worstSpec);

    SECTION ("A small step waits for the detector: 6 dB from T − 6 to T at Attack 0.5 ms")
    {
        // NOTES.md, Detector: M0 read 6.8 ms (τ_RMS 10 ms); the target only reaches 0 as the RMS
        // settles on T, so the detector, not Attack, sets this time.
        const auto p = params (G::thresholdDb, 0.25, 0.0, G::rangeDb, G::floorDb, 0.5, G::releaseMs);
        const auto hold = samplesFor (1.0, rate);
        auto key = carrierAt (G::thresholdDb - 6.0, hold);
        append (key, carrierAt (G::thresholdDb, samplesFor (0.1, rate)));
        auto m = prepared();
        const auto lift = liftOf (runModel (m, p, key)[0]);
        const auto measured = releaseTime (lift, (size_t) hold, rate, lift[(size_t) hold - 1]);
        const auto specified = ref::attackSeconds (0.5, rate, 0.25, 6.0, 0.0);
        std::printf ("Upward attack, 6 dB step to T at Attack 0.5 ms: %.3f ms (specified %.3f, M0 6.8)\n", measured * 1000.0, specified * 1000.0);
        REQUIRE (withinTimeGate (measured, 0.0068, rate));
        REQUIRE (std::abs (measured / specified - 1.0) <= 0.01);
    }
}

TEST_CASE ("Upward: the lift comes in at 10 dB per Release, and never faster than the detector", "[upward][ballistics]")
{
    // NOTES.md, Gates: the carrier from T + 10 to T − 60 dB, Range 20, Floor −90, the lift's slope
    // from 2 to 12 dB: 10 dB per Release ±10 %; below the detector's floor ln 10·τ_RMS/(1 − 1/R)
    // (23 ms at ∞, 31 ms at 4:1), that floor ±10 %. Every row also within 1 % of the specification.
    using G = ref::ReleaseGate;
    auto worstKnob = 0.0, worstSpec = 0.0;

    for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 0.0 })
        for (const auto attackMs : { 1.0, 30.0 })
            for (const auto releaseMs : { 5.0, 10.0, 20.0, 31.0, 50.0, 150.0, 500.0, 1000.0, 5000.0 })
            {
                const auto inverseRatio = inverseOf (ratio);
                const auto p = params (G::thresholdDb, inverseRatio, G::kneeDb, G::rangeDb, G::floorDb, attackMs, releaseMs);
                const auto expectedMs = ref::releasePer10DbMs (releaseMs, inverseRatio);
                const auto specified = ref::liftInSeconds (releaseMs, inverseRatio, rate, false, 0.5, attackMs);

                const auto held = samplesFor (0.5, rate);
                auto key = carrierAt (G::thresholdDb + G::aboveDb, held);
                append (key, carrierAt (G::thresholdDb - G::belowDb, samplesFor ((2.2 * expectedMs + 250.0) * 0.001, rate)));

                auto m = prepared();
                const auto lift = liftOf (runModel (m, p, key)[0]);
                const auto measured = (risingCrossing (lift, (size_t) held, G::toDb) - risingCrossing (lift, (size_t) held, G::fromDb))
                                      * 10.0 / (G::toDb - G::fromDb);

                INFO ("ratio " << ratio << ", attack " << attackMs << " ms, release " << releaseMs << " ms: " << measured * 1000.0
                               << " ms per 10 dB, expected " << expectedMs << ", specified " << specified * 1000.0);
                REQUIRE (withinTimeGate (measured, expectedMs * 0.001, rate));
                REQUIRE (std::abs (measured / specified - 1.0) <= 0.01);
                REQUIRE_THAT (lift.back(), WithinAbs (G::rangeDb, 1.0e-6));

                worstKnob = std::max (worstKnob, std::abs (measured / (expectedMs * 0.001) - 1.0));
                worstSpec = std::max (worstSpec, std::abs (measured / specified - 1.0));
            }

    std::printf ("Upward lift-in, worst |measured/expected - 1| %.2f %% (expected: Release, or the detector's floor), "
                 "worst |measured/specified - 1| %.4f %%\n", 100.0 * worstKnob, 100.0 * worstSpec);
    for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 0.0 })
        std::printf ("Upward lift-in floor at R %g: %.2f ms per 10 dB (specified %.2f ms at Release 5 ms)\n", ratio,
                     ref::detectorReleaseFloorMs (inverseOf (ratio)), 1000.0 * ref::liftInSeconds (5.0, inverseOf (ratio), rate));
}

TEST_CASE ("Upward: Auto slows the lift-in after time above Threshold", "[upward][auto]")
{
    // NOTES.md, Gates: Release 150 ms, Attack 5 ms: 209 and 606 ms per 10 dB after 0.2 s and 6 s
    // above T, ±10 %; each also within 1 % of the specification.
    using G = ref::ReleaseGate;

    const auto liftIn = [] (double secondsAbove, bool autoRelease, ModelParams* customised = nullptr, int block = 64) {
        auto p = params (G::thresholdDb, 0.25, G::kneeDb, G::rangeDb, G::floorDb, 5.0, 150.0, autoRelease);
        if (customised != nullptr)
            p = *customised;
        const auto held = samplesFor (secondsAbove, rate);
        auto key = silence (samplesFor (0.1, rate));
        const auto start = key.size() + (size_t) held;
        append (key, carrierAt (G::thresholdDb + G::aboveDb, held));
        append (key, carrierAt (G::thresholdDb - G::belowDb, samplesFor (1.5, rate)));
        auto m = prepared();
        const auto lift = liftOf (runModel (m, p, key, nullptr, block)[0]);
        return (risingCrossing (lift, start, G::toDb) - risingCrossing (lift, start, G::fromDb)) * 10.0 / (G::toDb - G::fromDb);
    };

    for (const auto& [seconds, m0Ms] : { std::pair { 0.2, 209.0 }, std::pair { 6.0, 606.0 } })
    {
        const auto measured = liftIn (seconds, true);
        const auto specified = ref::autoLiftInSeconds (seconds, rate);
        std::printf ("Upward Auto, Release 150 ms, after %g s above T: %.1f ms per 10 dB (specified %.1f, M0 %.0f)\n", seconds, measured * 1000.0,
                     specified * 1000.0, m0Ms);
        REQUIRE (withinTimeGate (measured, m0Ms * 0.001, rate));
        REQUIRE (std::abs (measured / specified - 1.0) <= 0.01);
    }

    SECTION ("Auto off: 10 dB per Release whatever came before")
    {
        for (const auto seconds : { 0.2, 6.0 })
            REQUIRE (withinTimeGate (liftIn (seconds, false), 0.150, rate));
    }

    SECTION ("A single hit inside a quiet passage leaves the lift-in nearly as set")
    {
        // 20 ms above Threshold: h charges to about 2 %, the lift-in 8 % slower at most.
        const auto measured = liftIn (0.020, true);
        std::printf ("Upward Auto, after one 20 ms hit: %.1f ms per 10 dB (specified %.1f)\n", measured * 1000.0,
                     1000.0 * ref::autoLiftInSeconds (0.020, rate));
        REQUIRE (withinTimeGate (measured, 0.150, rate));
    }

    SECTION ("The slowest is 5 × Release")
    {
        const auto measured = liftIn (60.0, true, nullptr, 4096);
        std::printf ("Upward Auto, after 60 s above T: %.1f ms per 10 dB (5 x Release = 750 ms; h decays during the lift-in)\n", measured * 1000.0);
        REQUIRE (measured < 0.750);
        REQUIRE (withinTimeGate (measured, ref::autoLiftInSeconds (60.0, rate), rate));
    }

    SECTION ("A linked pair shares one memory, from the louder channel")
    {
        // The left channel is loud for 6 s, the right one quiet throughout: at Link 100 % both
        // channels lift in as the loud one's memory says, and their reductions are identical.
        auto p = params (G::thresholdDb, 0.25, G::kneeDb, G::rangeDb, G::floorDb, 5.0, 150.0, true);
        p.linked = true;
        p.link = 1.0;
        const auto held = samplesFor (6.0, rate);
        auto left = carrierAt (G::thresholdDb + G::aboveDb, held);
        append (left, carrierAt (G::thresholdDb - G::belowDb, samplesFor (1.5, rate)));
        const auto right = carrierAt (G::thresholdDb - G::belowDb, (int) left.size());

        auto m = prepared();
        const auto r = runModel (m, p, left, &right);
        REQUIRE (r[0] == r[1]);
        REQUIRE (isExactly (m.autoMemory (0), m.autoMemory (1)));

        // Charged over the 6 s above T, then decaying from the step down plus the 23 ms the RMS takes to reach T.
        const auto expected = ref::autoMemoryAfter (6.023) * std::exp (-(1.5 - 0.023) * 1000.0 / ref::autoMemoryMs);
        REQUIRE_THAT (m.autoMemory (1), WithinAbs (expected, 0.002));
    }
}

TEST_CASE ("Upward: idle is exactly 0", "[upward][idle]")
{
    // NOTES.md, Gates, Identity: no lift at or above T + W/2, at or below Floor (silence included),
    // at Ratio 1 or Range 0: every reduction exactly 0 and every gain exactly 1. Under Floor, at
    // Ratio 1 and at Range 0 from the first sample; over the knee once the RMS has read the level:
    // from silence it rises through the lift region first (below).
    constexpr double t = -30.0, floorDb = -70.0;
    const auto n = samplesFor (0.5, rate);
    const auto settled = (size_t) samplesFor (0.2, rate);
    const auto tail = [&] (const Trace& r) { return Trace (r.begin() + (std::ptrdiff_t) settled, r.end()); };

    for (const auto autoRelease : { false, true })
        for (const auto knee : { 0.0, 6.0, 24.0 })
        {
            const auto p = params (t, 0.25, knee, 40.0, floorDb, 10.0, 150.0, autoRelease);
            auto m = prepared();

            REQUIRE (allZero (tail (runModel (m, p, carrierAt (t + 0.5 * knee, n))[0])));
            m.reset();
            REQUIRE (allZero (tail (runModel (m, p, carrierAt (t + 0.5 * knee + 20.0, n))[0])));
            m.reset();
            // A 1 kHz sine's RMS ripples by 0.034 dB around its peak reading: 0.5 dB of margin.
            REQUIRE (allZero (tail (runModel (m, p, sine (amplitudeFor (t + 0.5 * knee + 0.5), 1000.0, rate, n))[0])));
            m.reset();
            REQUIRE (allZero (runModel (m, p, carrierAt (floorDb, n))[0]));
            m.reset();
            REQUIRE (allZero (runModel (m, p, carrierAt (floorDb - 30.0, n))[0]));
            m.reset();
            REQUIRE (allZero (runModel (m, p, silence (n))[0]));
            m.reset();
            REQUIRE (allZero (runModel (m, p, sine (amplitudeFor (floorDb - 0.5), 1000.0, rate, n))[0]));

            for (const auto& q : { params (t, 1.0, knee, 40.0, floorDb, 10.0, 150.0, autoRelease), params (t, 0.25, knee, 0.0, floorDb, 10.0, 150.0, autoRelease) })
            {
                m.reset();
                auto key = carrierAt (t - 20.0, n);
                append (key, sine (amplitudeFor (t - 10.0), 300.0, rate, n));
                REQUIRE (allZero (runModel (m, q, key)[0]));
            }
        }

    SECTION ("An onset from silence over the knee, reported")
    {
        // The RMS starts at 0 and rises through Floor … T + W/2 before it reads the level, and the
        // lift comes in at the Release rate meanwhile: the specification's own onset (Reference.h
        // shows the same), measured here as the largest lift and when it is exactly 0 again.
        for (const auto& [knee, overDb, releaseMs] : { std::tuple { 0.0, 0.0, 150.0 }, std::tuple { 0.0, 1.0, 150.0 }, std::tuple { 6.0, 0.0, 150.0 },
                                                      std::tuple { 0.0, 6.0, 150.0 }, std::tuple { 0.0, 20.0, 150.0 }, std::tuple { 0.0, 20.0, 5.0 },
                                                      std::tuple { 6.0, 3.0, 5.0 } })
        {
            const auto p = params (t, 0.25, knee, 40.0, floorDb, 10.0, releaseMs);
            const auto level = t + 0.5 * knee + overDb;
            auto m = prepared();
            const auto lift = liftOf (runModel (m, p, carrierAt (level, n))[0]);
            ref::Settings s { referenceCurve (p), 10.0, releaseMs, false };
            const auto specified = ref::liftTrace (s, carrierAt (level, n), rate);

            auto last = (size_t) 0;
            for (size_t i = 0; i < lift.size(); ++i)
                if (! isExactly (lift[i], 0.0))
                    last = i + 1;
            const auto peak = *std::max_element (lift.begin(), lift.end());
            std::printf ("Upward onset from silence at T + W/2 + %g dB (knee %g, Release %g ms): largest lift %.2e dB (specified %.2e), exactly 0 from %.2f ms\n",
                         overDb, knee, releaseMs, peak, *std::max_element (specified.begin(), specified.end()), 1000.0 * (double) last / rate);
            REQUIRE (last < settled);
        }
    }

    SECTION ("After a quiet passage the lift lands on exactly 0 and stays there")
    {
        for (const auto& [attackMs, releaseMs] : { std::pair { 0.02, 5.0 }, std::pair { 10.0, 150.0 }, std::pair { 300.0, 5000.0 } })
        {
            const auto p = params (t, 0.25, 6.0, 40.0, floorDb, attackMs, releaseMs);
            auto key = carrierAt (t - 20.0, samplesFor (std::max (2.0, 3.0 * releaseMs * 0.001), rate));
            const auto end = key.size();
            append (key, carrierAt (t + 10.0, samplesFor (std::max (0.5, 30.0 * attackMs * 0.001), rate)));

            auto m = prepared();
            const auto r = runModel (m, p, key)[0];
            REQUIRE (r[end - 1] < -14.0);
            auto firstZero = r.size();
            for (auto i = end; i < r.size(); ++i)
                if (isExactly (r[i], 0.0))
                {
                    firstZero = i;
                    break;
                }
            REQUIRE (firstZero < r.size());
            REQUIRE (allZero (Trace (r.begin() + (std::ptrdiff_t) firstZero, r.end())));
            std::printf ("Upward idle snap, Attack %g ms: exactly 0 %.2f ms (%.1f x Attack) after the step over Threshold\n", attackMs,
                         1000.0 * (double) (firstZero - end) / rate, (double) (firstZero - end) / rate / (attackMs * 0.001));
        }
    }
}

namespace
{
    /** One or two channels of key through Upward in 64-sample blocks, the parameters per block from `at` (the block's first sample). */
    std::array<Trace, 2> runScheduled (UpwardModel& m, const Signal& left, const Signal* right, const std::function<ModelParams (int)>& at)
    {
        std::array<Trace, 2> r;
        const auto channels = right != nullptr ? 2 : 1;
        for (int start = 0; start < (int) left.size(); start += 64)
        {
            KeyBlock k;
            k.numChannels = channels;
            k.numSamples = std::min (64, (int) left.size() - start);
            k.channels[0] = left.data() + start;
            if (right != nullptr)
                k.channels[1] = right->data() + start;
            std::array<std::vector<double>, 2> out { std::vector<double> ((size_t) k.numSamples), std::vector<double> ((size_t) k.numSamples) };
            double* o[] { out[0].data(), out[1].data() };
            m.process (at (start), k, o);
            for (int c = 0; c < channels; ++c)
                r[(size_t) c].insert (r[(size_t) c].end(), out[(size_t) c].begin(), out[(size_t) c].end());
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

    /**
        Halite's click measure (tests/support, clickCrestDb) on a 100 Hz probe through the gains:
        the largest third difference over [from, to) against the RMS there, in dB. The probe's own
        third difference sits near −122 dB, so what it reads is the gain's.
    */
    double probeClickDb (const Trace& reduction, size_t from, size_t to)
    {
        Signal x (reduction.size());
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (double) gainFor (reduction[i]) * 0.5 * std::sin (2.0 * std::numbers::pi * 100.0 * (double) i / rate);
        auto peak = 0.0, sum = 0.0;
        for (auto i = std::max<size_t> (from, 3); i < to; ++i)
            peak = std::max (peak, std::abs (x[i] - 3.0 * x[i - 1] + 3.0 * x[i - 2] - x[i - 3]));
        for (auto i = from; i < to; ++i)
            sum += x[i] * x[i];
        return 20.0 * std::log10 (std::max (peak, 1.0e-30) / std::sqrt (sum / (double) (to - from)));
    }
}

TEST_CASE ("Upward: Floor fades the lift without a step", "[upward][floor]")
{
    SECTION ("A level sweeping 1 dB/s through F … F + 12: the gain's slope stays continuous")
    {
        // NOTES.md, Gates, Floor. Up and down through the fade at 20:1 (the steepest fade), the
        // reduction's second difference stays at the scale of the curve's own curvature: no step
        // in the slope anywhere, and no click on the probe.
        constexpr double floorDb = -70.0;
        for (const auto& [attackMs, releaseMs] : { std::pair { 0.02, 50.0 }, std::pair { 10.0, 150.0 } })
        {
            const auto p = params (-30.0, 0.05, 0.0, 40.0, floorDb, attackMs, releaseMs);
            const auto n = samplesFor (16.0, rate);
            Signal key ((size_t) (2 * n));
            for (int i = 0; i < 2 * n; ++i)
            {
                const auto level = floorDb - 2.0 + (i < n ? (double) i : (double) (2 * n - i)) / rate;
                key[(size_t) i] = (i % 2 == 0 ? 1.0 : -1.0) * amplitudeFor (ref::carrierAmplitudeDbFor (level));
            }

            auto m = prepared();
            const auto r = runModel (m, p, key)[0];

            // The fade spans F … F + 12 dB, 1 to 13 s into each half; the turns of the sweep (its own corners) stay out.
            const auto upFrom = (size_t) samplesFor (1.0, rate), upTo = (size_t) samplesFor (15.0, rate);
            const auto [upStep, upCorner] = roughness (r, upFrom, upTo);
            const auto [downStep, downCorner] = roughness (r, (size_t) n + upFrom, (size_t) n + upTo);
            const auto click = probeClickDb (r, (size_t) samplesFor (0.1, rate), r.size());
            std::printf ("Upward Floor sweep 1 dB/s through F ... F + 12 dB, Attack %g ms, Release %g ms: largest step %.2e / %.2e dB, "
                         "second difference %.2e / %.2e dB (up / down), probe click %.1f dB, lift at the top %.2f dB\n",
                         attackMs, releaseMs, upStep, downStep, upCorner, downCorner, click, -r[(size_t) n]);
            REQUIRE_THAT (-r[(size_t) n], WithinAbs (0.95 * 26.0, 0.05));     // the RMS reads a 1 dB/s sweep 10 ms late
            // The one corner is the core's snap: the lift leaves exactly 0 with a step of 10⁻⁶ dB
            // where it first exceeds it (dynamics/NOTES.md, Idle); the curve's own curvature here is about 7·10⁻¹⁰.
            REQUIRE (std::max (upCorner, downCorner) <= 1.1 * snapDb);
            REQUIRE (click < -60.0);
        }
    }

    SECTION ("Floor −90 <-> −30 at 10 dB of lift")
    {
        // NOTES.md, Gates, Clicks: Floor glides over 20 ms (Settled 67), the lift goes away on
        // Attack and comes back at the Release rate; no step artefact above −60 dB on the probe.
        const auto period = samplesFor (1.5, rate);
        const auto key = carrierAt (-40.0, samplesFor (6.0, rate));
        for (const auto attackMs : { 0.02, 10.0 })
            for (const auto releaseMs : { 50.0, 150.0, 1000.0 })
            {
                auto m = prepared();
                const auto r = runScheduled (m, key, nullptr, [&] (int pos) {
                    return params (-20.0, 0.5, 6.0, 40.0, (pos / period) % 2 == 1 ? -30.0 : -90.0, attackMs, releaseMs);
                })[0];
                REQUIRE_THAT (r[(size_t) period - 1], WithinAbs (-10.0, 1.0e-6));
                REQUIRE (isExactly (r[(size_t) (2 * period - 1)], 0.0));

                const auto [step, corner] = roughness (r, (size_t) period - 64);
                const auto click = probeClickDb (r, (size_t) period - 64, r.size());
                std::printf ("Upward Floor jump -90 <-> -30 dB at 10 dB of lift, Attack %g ms, Release %g ms: largest step %.4f dB, "
                             "second difference %.2e dB, probe click %.1f dB\n", attackMs, releaseMs, step, corner, click);
                CHECK (click < -60.0);
            }
    }
}

TEST_CASE ("Upward: a knob jump neither steps nor bends the lift", "[upward][clicks]")
{
    // As Clean's: a key under the default Threshold, a knob jumping between its ends every 0.31 s
    // while the lift is still moving, Attack 0.02 ms, from the first jump on. The curve fade keeps
    // every second difference at the scale of the raised cosine's own (≈ 2·10⁻⁴ for 40 dB over
    // 960 samples). The lift coming in at the Release rate lands on its target with a corner of
    // that rate, 10 dB / (Release·fs) per sample: the constant-rate law's own (NOTES.md,
    // Ballistics), allowed on top.
    const auto period = samplesFor (0.31, rate);

    struct Jump
    {
        const char* name;
        double keyDb;
        std::function<void (ModelParams&, bool)> set;
        double shortestReleaseMs = 150.0;
    };
    const Jump jumps[] {
        { "Threshold -60 <-> 0", -32.0, [] (ModelParams& p, bool b) { p.curve.thresholdDb = b ? 0.0 : -60.0; } },
        { "Ratio 1 <-> inf", -32.0, [] (ModelParams& p, bool b) { p.curve.inverseRatio = b ? 0.0 : 1.0; } },
        { "Knee 0 <-> 24", -21.0, [] (ModelParams& p, bool b) { p.curve.kneeDb = b ? 24.0 : 0.0; } },
        { "Range 0 <-> 40", -32.0, [] (ModelParams& p, bool b) { p.curve.rangeDb = b ? 40.0 : 0.0; } },
        { "Attack 0.02 <-> 300", -32.0, [] (ModelParams& p, bool b) { p.attackMs = b ? 300.0 : 0.02; } },
        { "Release 50 <-> 5000", -32.0, [] (ModelParams& p, bool b) { p.releaseMs = b ? 5000.0 : 50.0; }, 50.0 },
        { "Auto release on <-> off", -32.0, [] (ModelParams& p, bool b) { p.autoRelease = b; } },
    };

    for (const auto& jump : jumps)
    {
        const auto key = carrierAt (jump.keyDb, samplesFor (2.0, rate));
        auto m = prepared();
        const auto r = runScheduled (m, key, nullptr, [&] (int pos) {
            auto p = params (-18.0, 0.25, 6.0, 40.0, -70.0, 0.02, 150.0, true);
            jump.set (p, (pos / period) % 2 == 1);
            return p;
        })[0];
        const auto [step, corner] = roughness (r, (size_t) period - 2);
        const auto click = probeClickDb (r, (size_t) period - 64, r.size());
        std::printf ("Upward knob jump at Attack 0.02 ms, %s: largest step %.4f dB, second difference %.2e dB, probe click %.1f dB\n", jump.name, step,
                     corner, click);
        INFO (jump.name << ": largest step " << step << " dB, largest second difference " << corner << " dB, probe " << click << " dB");
        REQUIRE (step < 0.1);
        REQUIRE (corner < 5.0e-4 + 10.0 / (jump.shortestReleaseMs * 0.001 * rate));
    }
}

TEST_CASE ("Upward: Link 100 % gives both channels the louder one's lift, Link 0 % is Unlinked", "[upward][stereo]")
{
    // Ten seconds of two unrelated, bursty noises at lifting levels.
    const auto n = samplesFor (10.0, rate);
    auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
    for (int i = 0; i < n; ++i)
    {
        const auto envelope = 0.02 + 0.98 * std::abs (std::sin (2.0 * std::numbers::pi * 0.7 * i / rate));
        left[(size_t) i] *= 0.03 * envelope;
        right[(size_t) i] *= 0.03 * (1.02 - envelope);
    }

    for (const auto& [attackMs, autoRelease] : { std::pair { 0.1, false }, std::pair { 10.0, true }, std::pair { 30.0, false } })
    {
        auto p = params (-30.0, 0.25, 6.0, 40.0, -70.0, attackMs, 150.0, autoRelease);
        const Signal lookahead ((size_t) n, 96.0);

        p.linked = true;
        p.link = 1.0;
        auto m = prepared();
        const auto full = runModel (m, p, left, &right, 97, &lookahead);
        REQUIRE (full[0] == full[1]);
        REQUIRE (*std::min_element (full[0].begin(), full[0].end()) < -6.0);

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

    SECTION ("A source on one channel only is lifted on both")
    {
        // The left channel at T − 20, the right one silent (under Floor): the louder channel
        // wins, so both channels take the left's 15 dB lift. The smaller lift of the two would
        // be the silent channel's 0, and a hard-panned source would never be lifted.
        auto p = params (-30.0, 0.25, 0.0, 40.0, -70.0, 10.0, 150.0);
        p.linked = true;
        p.link = 1.0;
        const auto source = carrierAt (-50.0, samplesFor (1.0, rate));
        const auto nothing = silence ((int) source.size());
        auto m = prepared();
        const auto r = runModel (m, p, source, &nothing);
        REQUIRE (r[0] == r[1]);
        REQUIRE_THAT (r[0].back(), WithinAbs (-15.0, 1.0e-9));

        p.link = 0.5;
        m.reset();
        const auto half = runModel (m, p, source, &nothing);
        REQUIRE_THAT (half[0].back(), WithinAbs (-15.0, 1.0e-9));
        REQUIRE_THAT (half[1].back(), WithinAbs (-7.5, 1.0e-9));
    }

    SECTION ("Above the Floor fade the louder channel's lift is the smaller one")
    {
        auto p = params (-30.0, 0.25, 0.0, 40.0, -90.0, 10.0, 150.0);
        p.linked = true;
        p.link = 1.0;
        const auto louder = carrierAt (-40.0, samplesFor (1.0, rate));
        const auto quieter = carrierAt (-60.0, (int) louder.size());
        auto m = prepared();
        const auto r = runModel (m, p, louder, &quieter);
        REQUIRE_THAT (r[1].back(), WithinAbs (-7.5, 1.0e-9));
    }
}

TEST_CASE ("Upward: a seed starts the ballistics at the applied reduction", "[upward][switch]")
{
    // Settled 33 and the M1 switch: the incoming model follows the key unheard (follow()), is
    // seeded with the reduction applied, and carries on from it.
    constexpr double t = -30.0;
    const auto level = carrierAt (t - 20.0, samplesFor (3.0, rate));
    KeyBlock key;
    key.numChannels = 1;
    key.numSamples = samplesFor (0.05, rate);
    key.channels[0] = level.data();

    SECTION ("Seeded with its own lift it holds it: no swell, no re-attack")
    {
        const auto p = params (t, 0.25, 0.0, 40.0, -70.0, 300.0, 5000.0);
        auto m = prepared();
        m.follow (p, key);
        const double seed[] { -15.0 };
        m.seed (seed, 1);
        REQUIRE (isExactly (m.reduction (0), -15.0));
        const auto r = runModel (m, p, Signal (level.begin() + key.numSamples, level.end()))[0];
        REQUIRE_THAT (*std::min_element (r.begin(), r.end()), WithinAbs (-15.0, 1.0e-6));
        REQUIRE_THAT (*std::max_element (r.begin(), r.end()), WithinAbs (-15.0, 1.0e-6));
    }

    SECTION ("Seeded with a reduction, the gain rises at the Release rate")
    {
        // From another model's 6 dB of reduction to Upward's 15 dB of lift: 21 dB at 10 dB per
        // Release, a straight line in dB.
        const auto p = params (t, 0.25, 0.0, 40.0, -70.0, 10.0, 500.0);
        auto m = prepared();
        m.follow (p, key);
        const double seed[] { 6.0 };
        m.seed (seed, 1);
        const auto r = runModel (m, p, Signal (level.begin() + key.numSamples, level.end()))[0];
        const auto perSample = 10.0 / (0.5 * rate);
        REQUIRE_THAT (r[0], WithinAbs (6.0 - perSample, 1.0e-9));
        const auto reached = (size_t) std::ceil (21.0 / perSample);
        REQUIRE_THAT (r[reached / 2], WithinAbs (6.0 - perSample * (double) (reached / 2 + 1), 1.0e-6));
        REQUIRE_THAT (r[reached + 1], WithinAbs (-15.0, 1.0e-9));
    }

    SECTION ("follow() reads the key as if it had always run")
    {
        // After 50 ms of follow() (settleMs) the RMS reads the level: process() lifts in from
        // the seed's 0 at the Release rate to exactly the static lift, never past it.
        const auto p = params (t, 0.25, 0.0, 40.0, -70.0, 10.0, 150.0);
        auto m = prepared();
        REQUIRE_THAT (m.settleMs (p), WithinAbs (50.0, 1.0e-12));
        m.follow (p, key);
        const double seed[] { 0.0 };
        m.seed (seed, 1);
        const auto r = runModel (m, p, Signal (level.begin() + key.numSamples, level.end()))[0];
        REQUIRE (*std::min_element (r.begin(), r.end()) >= -15.0 - 1.0e-9);
        REQUIRE_THAT (r.back(), WithinAbs (-15.0, 1.0e-9));
        REQUIRE (withinTimeGate (risingCrossing (liftOf (r), 1, 12.0) - risingCrossing (liftOf (r), 1, 2.0), 0.150, rate));
    }

    SECTION ("reset() releases everything")
    {
        const auto p = params (t, 0.25, 0.0, 40.0, -70.0, 10.0, 150.0, true);
        auto m = prepared();
        (void) runModel (m, p, level);
        REQUIRE (m.reduction (0) < -14.0);
        m.reset();
        REQUIRE (isExactly (m.reduction (0), 0.0));
        REQUIRE (isExactly (m.autoMemory (0), 0.0));
        REQUIRE (allZero (runModel (m, p, silence (samplesFor (0.1, rate)))[0]));
    }
}

TEST_CASE ("Upward: the reduction does not depend on the block size", "[upward][blocks]")
{
    // Settings still, a stereo key with look-ahead: blocks of 1, 7, 64, 4096 and a mixed cycle
    // give the same bits.
    const auto n = samplesFor (3.0, rate);
    auto left = pinkNoise (n, 11u), right = pinkNoise (n, 12u);
    for (int i = 0; i < n; ++i)
    {
        const auto envelope = std::pow (10.0, -2.0 * std::abs (std::sin (2.0 * std::numbers::pi * 0.9 * i / rate)));
        left[(size_t) i] *= 0.1 * envelope;
        right[(size_t) i] *= 0.05 * envelope;
    }
    const Signal lookahead ((size_t) n, 120.0);

    for (const auto linked : { false, true })
    {
        auto p = params (-30.0, 0.25, 6.0, 40.0, -70.0, 2.0, 100.0, true);
        p.linked = linked;
        p.link = 0.7;

        auto reference = prepared();
        const auto one = runModel (reference, p, left, &right, 1, &lookahead);

        for (const auto block : { 7, 64, 4096 })
        {
            auto m = prepared();
            REQUIRE (runModel (m, p, left, &right, block, &lookahead) == one);
        }

        auto m = prepared();
        std::array<Trace, 2> mixed { Trace ((size_t) n), Trace ((size_t) n) };
        const int sizes[] { 1, 13, 512, 3, 4096, 77 };
        for (int start = 0, k = 0; start < n; ++k)
        {
            const auto len = std::min (sizes[k % 6], n - start);
            KeyBlock key;
            key.numChannels = 2;
            key.numSamples = len;
            key.channels = { left.data() + start, right.data() + start };
            key.lookahead = lookahead.data() + start;
            double* out[] { mixed[0].data() + start, mixed[1].data() + start };
            m.process (p, key, out);
            start += len;
        }
        REQUIRE (mixed == one);
    }
}

TEST_CASE ("Upward: look-ahead removes the lift early with the same shape", "[upward][lookahead]")
{
    // The core's rule: the window acts on the mean square before any state, so the attack side is
    // the LA 0 trace shifted by LA, and the lift-in starts when the audio's loud part has left.
    const auto latency = samplesFor (0.010, rate);
    constexpr double t = -30.0;

    for (const auto attackMs : { 0.1, 10.0 })
        for (const auto laMs : { 3.0, 10.0 })
        {
            const auto p = params (t, 0.25, 6.0, 40.0, -70.0, attackMs, 100.0);
            auto key = carrierAt (t - 15.0, samplesFor (0.5, rate));
            const auto onset = key.size();
            append (key, carrierAt (t + 10.0, samplesFor (0.3, rate)));
            const auto end = key.size();
            append (key, carrierAt (t - 15.0, samplesFor (0.6, rate)));

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
            for (auto n = onset + (size_t) latency - (size_t) la - 200; n + (size_t) la < end + (size_t) latency; ++n)
                attackSide = std::max (attackSide, std::abs (rl[n] - r0[n + (size_t) la]));
            for (auto n = end + (size_t) latency; n < key.size(); ++n)
                releaseSide = std::max (releaseSide, std::abs (rl[n] - r0[n]));

            std::printf ("Upward look-ahead %g ms, Attack %g ms: attack side %.1e dB from the shifted trace, release side %.1e dB\n", laMs, attackMs,
                         attackSide, releaseSide);
            REQUIRE (attackSide <= 1.0e-6);
            REQUIRE (releaseSide <= 1.0e-6);
        }
}

TEST_CASE ("Upward: a sane key keeps the reduction finite and recovers", "[upward][sanity]")
{
    // The chain takes the key sane (saneKey, Settled 59): a NaN or an infinity reads as silence,
    // anything else is clamped to ±10⁶. Through it, every reduction stays finite, and the model
    // lifts the next quiet passage as if nothing had happened.
    const auto p = params (-30.0, 0.0, 6.0, 40.0, -70.0, 1.0, 50.0, true);
    auto key = carrierAt (-50.0, samplesFor (0.5, rate));
    key[100] = std::numeric_limits<double>::quiet_NaN();
    key[200] = std::numeric_limits<double>::infinity();
    key[300] = -std::numeric_limits<double>::infinity();
    key[400] = 1.0e300;
    key[401] = -1.0e300;
    append (key, carrierAt (-50.0, samplesFor (2.0, rate)));
    for (auto& v : key)
        v = saneKey (v);

    for (const auto linked : { false, true })
    {
        auto q = p;
        q.linked = linked;
        auto m = prepared();
        const auto r = runModel (m, q, key, &key);
        for (const auto& channel : r)
            for (const auto v : channel)
                REQUIRE (std::isfinite (v));
        REQUIRE_THAT (r[0].back(), WithinAbs (-20.0, 1.0e-9));
        REQUIRE (std::isfinite (gainFor (*std::min_element (r[0].begin(), r[0].end()))));
    }

    SECTION ("A raw NaN, past the chain's guard, never reaches the gain")
    {
        // Without saneKey the mean square stays NaN for good, as in every model; the lift then
        // reads no level and goes away, so the gain stays finite (1 once released).
        auto raw = carrierAt (-50.0, samplesFor (0.5, rate));
        raw[1000] = std::numeric_limits<double>::quiet_NaN();
        auto m = prepared();
        const auto r = runModel (m, p, raw)[0];
        for (const auto v : r)
            REQUIRE (std::isfinite (v));
        REQUIRE (r[999] < -1.0);
        REQUIRE (isExactly (r.back(), 0.0));
    }

    SECTION ("The ends of every knob")
    {
        auto m = prepared();
        const auto loud = carrierAt (0.0, samplesFor (0.2, rate));
        auto keyAll = carrierAt (-95.0, samplesFor (0.2, rate));
        append (keyAll, loud);
        append (keyAll, carrierAt (-45.0, samplesFor (0.5, rate)));
        for (const auto t : { -60.0, 0.0 })
            for (const auto inverseRatio : { 1.0, 0.0 })
                for (const auto knee : { 0.0, 24.0 })
                    for (const auto range : { 0.0, 40.0 })
                        for (const auto floorDb : { -90.0, -30.0 })
                            for (const auto& [attackMs, releaseMs] : { std::pair { 0.02, 5.0 }, std::pair { 300.0, 5000.0 } })
                            {
                                m.reset();
                                const auto r = runModel (m, params (t, inverseRatio, knee, range, floorDb, attackMs, releaseMs, true), keyAll)[0];
                                for (const auto v : r)
                                {
                                    REQUIRE (std::isfinite (v));
                                    REQUIRE (v <= 0.0);
                                    REQUIRE (v >= -range - 1.0e-9);
                                }
                            }
    }
}

TEST_CASE ("Upward: nothing goes denormal, without flush-to-zero", "[upward][realtime]")
{
    // The slowest settings, a lift held, then 150 s of silence: the lift goes away on a 300 ms
    // one-pole and snaps, the mean square and Auto's memory decay and flush.
    auto p = params (-40.0, 0.25, 6.0, 40.0, -90.0, 300.0, 5000.0, true);
    p.linked = true;
    auto key = pinkNoise (samplesFor (3.0, rate), 4u);
    scale (key, 0.003);
    append (key, silence (samplesFor (150.0, rate)));

    UpwardModel m;
    m.prepare (rate);
    const auto r = runModel (m, p, key, &key, 4096);
    REQUIRE (*std::min_element (r[0].begin(), r[0].end()) < -3.0);
    for (const auto& channel : r)
        for (const auto v : channel)
            REQUIRE (std::fpclassify (v) != FP_SUBNORMAL);
    REQUIRE (isExactly (r[0].back(), 0.0));
    REQUIRE (isExactly (m.reduction (1), 0.0));
    REQUIRE (isExactly (m.autoMemory (0), 0.0));
}
