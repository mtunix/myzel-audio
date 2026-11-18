// Clean against models/clean/NOTES.md and the core's gate table, at unit level: the static curve
// on sines and the carrier, attack and release over the knob ranges, Auto release, look-ahead,
// Range, the idle zeros, the stereo link and the seed.

#include "Harness.h"

#include "dsp/dynamics/Stereo.h"
#include "dsp/models/clean/CleanModel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <functional>
#include <utility>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using basalt::models::CleanModel;
using Catch::Matchers::WithinAbs;
using myzel::dsp::isExactly;

namespace
{
    constexpr double rate = 48000.0;

    ModelParams params (double t, double inverseRatio, double knee, double range, double attackMs, double releaseMs, bool autoRelease = false)
    {
        ModelParams p;
        p.curve = { t, inverseRatio, knee, range };
        p.attackMs = attackMs;
        p.releaseMs = releaseMs;
        p.autoRelease = autoRelease;
        return p;
    }

    /** What Clean settles on for the carrier at peak level L (models/clean/NOTES.md, Detector). */
    double carrierReduction (const Curve& c, double level, double blend)
    {
        return std::min ((1.0 - blend) * curveReduction (c, level) + blend * curveReduction (c, level + carrierRmsOffsetDb), c.rangeDb);
    }

    /** The carrier's peak level for a settled reduction `depth` at the hard knee, the blend included. */
    double carrierLevelFor (double t, double inverseRatio, double depth, double blend)
    {
        return t + depth / (1.0 - inverseRatio) - blend * carrierRmsOffsetDb;
    }

    CleanModel prepared()
    {
        CleanModel m;
        m.prepare (rate);
        return m;
    }

    struct Release
    {
        double from = 0.0, seconds = 0.0;
    };

    /** Holds a stimulus until settled, then silence: the release from the reduction held at the step. */
    Release releaseAfter (CleanModel& m, const ModelParams& p, const Signal& held, double releaseS)
    {
        auto key = held;
        append (key, silence (samplesFor (releaseS, rate)));
        const auto r = runModel (m, p, key)[0];
        const auto from = r[held.size() - 1];
        return { from, releaseTime (r, held.size(), rate, from) };
    }
}

TEST_CASE ("Clean: the static curve on 1 kHz sines and the carrier", "[clean][static]")
{
    // The core's grid (dynamics/NOTES.md, Gate table) at Attack 10 ms (b = 0.5) and 0.1 ms (b = 0),
    // Release 1 s: ascending 1 dB steps from T − 20 to T + 30, each settled for 100 ms.
    const auto settle = samplesFor (0.100, rate), measure = samplesFor (0.020, rate);
    auto worstSine = 0.0, worstCarrier = 0.0;
    char worstSineAt[128] {}, worstCarrierAt[128] {};

    for (const auto attackMs : { 10.0, 0.1 })
        for (const auto useSine : { true, false })
            for (const auto t : { -60.0, -30.0, -10.0 })
                for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 20.0, 0.0 })
                    for (const auto knee : { 0.0, 6.0, 24.0 })
                        for (const auto range : { 40.0, 10.0 })
                        {
                            const auto p = params (t, ratio > 0.0 ? 1.0 / ratio : 0.0, knee, range, attackMs, 1000.0);
                            const auto b = CleanModel::blendFor (attackMs);

                            Signal key;
                            std::vector<double> levels;
                            for (auto level = t - 20.0; level <= t + 30.0 + 1.0e-9; level += 1.0)
                            {
                                const auto n = settle + measure;
                                const auto a = amplitudeFor (level);
                                append (key, useSine ? sine (a, 1000.0, rate, n, 2.0 * std::numbers::pi * 1000.0 * (double) key.size() / rate) : carrier (a, n));
                                levels.push_back (level);
                            }

                            auto m = prepared();
                            const auto r = runModel (m, p, key)[0];

                            for (size_t s = 0; s < levels.size(); ++s)
                            {
                                const auto end = (s + 1) * (size_t) (settle + measure);
                                const auto got = mean (r, end - (size_t) measure, end);
                                const auto expected = useSine ? staticReduction (p.curve, levels[s]) : carrierReduction (p.curve, levels[s], b);
                                const auto error = std::abs (got - expected);
                                auto& worst = useSine ? worstSine : worstCarrier;
                                if (error > worst)
                                {
                                    worst = error;
                                    std::snprintf (useSine ? worstSineAt : worstCarrierAt, sizeof (worstSineAt), "attack %g, T %g, R %g, knee %g, range %g, level %g",
                                                   attackMs, t, ratio, knee, range, levels[s]);
                                }

                                INFO ((useSine ? "sine" : "carrier") << ", attack " << attackMs << ", T " << t << ", ratio " << ratio
                                      << ", knee " << knee << ", range " << range << ", level " << levels[s] << ": " << got << " for " << expected);
                                REQUIRE (error <= 0.25);
                            }
                        }

    std::printf ("Clean static curve, worst |error|: 1 kHz sine %.4f dB (%s), carrier %.4f dB (%s)\n", worstSine, worstSineAt,
                 worstCarrier, worstCarrierAt);
}

TEST_CASE ("Clean: 100 Hz and pink noise, reported", "[clean][static]")
{
    // Not gated (models/clean/NOTES.md, Gates): the RMS window's ripple at 100 Hz, and pink noise
    // as the steady mean reduction, printed beside the sine's eq. 4 figure.
    for (const auto attackMs : { 2.0, 5.0, 10.0, 30.0, 100.0 })
    {
        const auto p = params (-30.0, 0.25, 0.0, 40.0, attackMs, 1000.0);
        auto m = prepared();
        const auto r = runModel (m, p, sine (amplitudeFor (-20.0), 100.0, rate, samplesFor (2.0, rate)))[0];
        const auto got = mean (r, (size_t) samplesFor (1.0, rate), r.size());
        std::printf ("Clean 100 Hz, Attack %g ms (b %.2f): %+.3f dB against eq. 4\n", attackMs, CleanModel::blendFor (attackMs), got - 7.5);
        CHECK (std::abs (got - 7.5) <= 0.35);
    }
}

TEST_CASE ("Clean: attack and release mean the knobs", "[clean][ballistics]")
{
    // The core's grid: Attack 0.02 … 300 ms × Release 5 ms … 5 s × Ratio 2, 4, 20, ∞, Knee 0, on
    // the carrier stepping to D = 10 dB (on the blend's own reading) and back to silence. Release
    // under the floor is measured against 1.01·Attack/κ(b).
    constexpr double t = -30.0, depth = 10.0;
    auto worstAttack = 0.0, worstRelease = 0.0;
    char worstAttackAt[96] {}, worstReleaseAt[96] {};

    for (const auto ratio : { 2.0, 4.0, 20.0, 0.0 })
        for (const auto attackMs : { 0.02, 0.1, 1.0, 10.0, 100.0, 300.0 })
            for (const auto releaseMs : { 5.0, 50.0, 150.0, 500.0, 5000.0 })
            {
                const auto inverseRatio = ratio > 0.0 ? 1.0 / ratio : 0.0;
                const auto p = params (t, inverseRatio, 0.0, 40.0, attackMs, releaseMs);
                const auto b = CleanModel::blendFor (attackMs);
                const auto expectedRelease = std::max (releaseMs, CleanModel::minimumReleaseMs (attackMs, inverseRatio));

                const auto pre = 16;
                const auto held = samplesFor (std::max (5.0 * (attackMs + releaseMs), 20.0 * attackMs) * 0.001, rate);
                auto key = silence (pre);
                append (key, carrier (amplitudeFor (carrierLevelFor (t, inverseRatio, depth, b)), held));
                append (key, silence (samplesFor (3.0 * expectedRelease * 0.001, rate) + 64));

                auto m = prepared();
                const auto r = runModel (m, p, key)[0];
                const auto settled = r[(size_t) (pre + held - 1)];
                const auto attack = attackTime (r, (size_t) pre, rate, settled);
                const auto release = releaseTime (r, (size_t) (pre + held), rate, settled);

                INFO ("ratio " << ratio << ", attack " << attackMs << " ms, release " << releaseMs << " ms: measured "
                               << attack * 1000.0 << " / " << release * 1000.0 << " ms (release expected " << expectedRelease << ")");
                REQUIRE_THAT (settled, WithinAbs (depth, 1.0e-6));
                REQUIRE (withinTimeGate (attack, attackMs * 0.001, rate));
                REQUIRE (withinTimeGate (release, expectedRelease * 0.001, rate));

                if (attackMs >= 0.1 && std::abs (attack / (attackMs * 0.001) - 1.0) > worstAttack)
                {
                    worstAttack = std::abs (attack / (attackMs * 0.001) - 1.0);
                    std::snprintf (worstAttackAt, sizeof (worstAttackAt), "ratio %g, attack %g ms", ratio, attackMs);
                }
                if (std::abs (release / (expectedRelease * 0.001) - 1.0) > worstRelease)
                {
                    worstRelease = std::abs (release / (expectedRelease * 0.001) - 1.0);
                    std::snprintf (worstReleaseAt, sizeof (worstReleaseAt), "ratio %g, attack %g ms, release %g ms", ratio, attackMs, releaseMs);
                }
            }

    std::printf ("Clean ballistics, worst |measured/set − 1|: attack %.2f %% (from 0.1 ms; %s), release %.2f %% (%s)\n",
                 100.0 * worstAttack, worstAttackAt, 100.0 * worstRelease, worstReleaseAt);
}

TEST_CASE ("Clean: the release against depth, reported", "[clean][ballistics]")
{
    // The drag calibration is exact at the reference step only: the RMS half's fall, and with it
    // the drag, grows with the level over Threshold. Pure peak (Attack ≤ 1 ms) has none.
    constexpr double t = -40.0;
    for (const auto ratio : { 2.0, 4.0, 0.0 })
        for (const auto& [attackMs, releaseMs] : { std::pair { 10.0, 50.0 }, std::pair { 10.0, 150.0 }, std::pair { 100.0, 500.0 } })
        {
            const auto inverseRatio = ratio > 0.0 ? 1.0 / ratio : 0.0;
            const auto p = params (t, inverseRatio, 0.0, 40.0, attackMs, releaseMs);
            std::printf ("Clean release/knob, Ratio %g, Attack %g ms, Release %g ms, at 3 / 6 / 10 / 20 dB:", ratio, attackMs, releaseMs);
            for (const auto depth : { 3.0, 6.0, 10.0, 20.0 })
            {
                auto m = prepared();
                const auto held = carrier (amplitudeFor (carrierLevelFor (t, inverseRatio, depth, CleanModel::blendFor (attackMs))),
                                           samplesFor (std::max (5.0 * (attackMs + releaseMs), 20.0 * attackMs) * 0.001, rate));
                std::printf (" %.3f", releaseAfter (m, p, held, 4.0 * releaseMs * 0.001).seconds / (releaseMs * 0.001));
            }
            std::printf ("\n");
        }
}

TEST_CASE ("Clean: κ holds the attack across depths and ratios", "[clean][ballistics]")
{
    // models/clean/NOTES.md, Ballistics: steps to D = 6, 10, 20, 10, 30 dB at R 2, 4, 4, 20, 20,
    // hard knee; after κ within ±2.5 % (expected), gated at ±10 %.
    const std::array<std::pair<double, double>, 5> steps { { { 6.0, 2.0 }, { 10.0, 4.0 }, { 20.0, 4.0 }, { 10.0, 20.0 }, { 30.0, 20.0 } } };
    constexpr double t = -40.0;

    for (const auto attackMs : { 2.0, 5.0, 10.0, 30.0, 300.0 })
    {
        auto lo = 10.0, hi = 0.0;
        for (const auto& [depth, ratio] : steps)
        {
            const auto p = params (t, 1.0 / ratio, 0.0, 40.0, attackMs, 1000.0);
            auto key = silence (16);
            append (key, carrier (amplitudeFor (carrierLevelFor (t, 1.0 / ratio, depth, CleanModel::blendFor (attackMs))), samplesFor (20.0 * attackMs * 0.001, rate)));
            auto m = prepared();
            const auto r = runModel (m, p, key)[0];
            const auto ratioToKnob = attackTime (r, 16, rate, r.back()) / (attackMs * 0.001);
            lo = std::min (lo, ratioToKnob);
            hi = std::max (hi, ratioToKnob);
            REQUIRE (withinTimeGate (ratioToKnob * attackMs * 0.001, attackMs * 0.001, rate));
        }
        std::printf ("Clean attack after κ, Attack %g ms: %.3f … %.3f of the knob\n", attackMs, lo, hi);
    }
}

TEST_CASE ("Clean: Auto release follows the crest factor at half strength", "[clean][auto]")
{
    constexpr double t = -40.0;

    SECTION ("A sine releases in 1.00 × Release, the carrier in 1.41 ×")
    {
        for (const auto& [attackMs, releaseMs] : { std::pair { 1.0, 50.0 }, std::pair { 1.0, 150.0 }, std::pair { 1.0, 500.0 },
                                                   std::pair { 10.0, 150.0 }, std::pair { 10.0, 500.0 } })
        {
            const auto p = params (t, 0.0, 0.0, 40.0, attackMs, releaseMs, true);
            const auto hold = samplesFor (1.5, rate);

            auto m = prepared();
            const auto sineRelease = releaseAfter (m, p, sine (amplitudeFor (t + 10.0), 1000.0, rate, hold), 4.0 * releaseMs * 0.001);
            m.reset();
            const auto carrierRelease = releaseAfter (m, p, carrier (amplitudeFor (t + 10.0), hold), 6.0 * releaseMs * 0.001);

            std::printf ("Clean Auto, Attack %g ms, Release %g ms: sine %.3f ×, carrier %.3f × Release\n", attackMs, releaseMs,
                         sineRelease.seconds / (releaseMs * 0.001), carrierRelease.seconds / (releaseMs * 0.001));
            REQUIRE (withinTimeGate (sineRelease.seconds, releaseMs * 0.001, rate));
            REQUIRE (withinTimeGate (carrierRelease.seconds, std::numbers::sqrt2 * releaseMs * 0.001, rate));
        }
    }

    SECTION ("Pink noise releases in √(2/C²) × Release, C² from a reference crest detector")
    {
        for (const auto releaseMs : { 150.0, 500.0 })
        {
            const auto p = params (-50.0, 0.0, 0.0, 40.0, 1.0, releaseMs, true);
            auto noise = pinkNoise (samplesFor (2.0, rate), 21u);
            scale (noise, 0.05);

            // The reference: eq. 7 transcribed, p and q over the same noise.
            const auto alpha = std::exp (-1.0 / (0.2 * rate));
            auto pk = 0.0, q = 0.0;
            for (const auto x : noise)
            {
                pk = std::max (x * x, alpha * pk + (1.0 - alpha) * x * x);
                q = alpha * q + (1.0 - alpha) * x * x;
            }
            const auto expected = releaseMs * 0.001 * CleanModel::autoReleaseFactor (pk / q);

            auto m = prepared();
            const auto rel = releaseAfter (m, p, noise, 6.0 * releaseMs * 0.001);
            std::printf ("Clean Auto, pink noise, Release %g ms: C² %.2f, released in %.3f × Release (expected %.3f ×)\n",
                         releaseMs, pk / q, rel.seconds / (releaseMs * 0.001), expected / (releaseMs * 0.001));
            REQUIRE (withinTimeGate (rel.seconds, expected, rate));
        }
    }

    SECTION ("An impulse train never releases faster than Release/8")
    {
        constexpr double releaseMs = 500.0;
        const auto p = params (t, 0.0, 0.0, 40.0, 0.02, releaseMs, true);
        Signal key;
        for (int k = 0; k < 20; ++k)
        {
            auto click = silence (samplesFor (0.1, rate));
            click[0] = amplitudeFor (t + 10.0);
            append (key, click);
        }
        key.resize (key.size() - (size_t) samplesFor (0.1, rate) + 1);

        // Measured from the reduction's peak after the last click: one sample of level at 0.02 ms
        // leaves the attack stage short of the hold's 10 dB on the click itself.
        const auto last = key.size() - 1;
        append (key, silence (samplesFor (1.0, rate)));
        auto m = prepared();
        const auto r = runModel (m, p, key)[0];
        const auto peak = (size_t) (std::max_element (r.begin() + (std::ptrdiff_t) last, r.end()) - r.begin());
        const auto rel = releaseTime (r, peak + 1, rate, r[peak]);
        std::printf ("Clean Auto, 10 Hz impulses: C² %.0f, released in %.4f × Release\n", m.crestSquared (0), rel / (releaseMs * 0.001));
        REQUIRE (withinTimeGate (rel, releaseMs * 0.001 / 8.0, rate));
    }

    SECTION ("Release 5 s on the carrier: 7.07 s, and the tail keeps its rate past the guard")
    {
        const auto p = params (t, 0.0, 0.0, 40.0, 10.0, 5000.0, true);
        const auto hold = samplesFor (30.0, rate);
        auto key = carrier (amplitudeFor (t + 10.0), hold);
        append (key, silence (samplesFor (25.0, rate)));

        auto m = prepared();
        const auto r = runModel (m, p, key, nullptr, 512)[0];
        const auto from = r[(size_t) hold - 1];
        const auto rel = releaseTime (r, (size_t) hold, rate, from);
        REQUIRE (withinTimeGate (rel, 5.0 * std::numbers::sqrt2, rate));

        // ln r falls at 1/τ_R once the attack stage has died away: the same at 5 s and at 20 s.
        const auto slopeAt = [&] (double s) {
            const auto i = (size_t) (hold + samplesFor (s, rate));
            const auto d = (size_t) samplesFor (0.5, rate);
            return (std::log (r[i + d]) - std::log (r[i])) / 0.5;
        };
        std::printf ("Clean Auto, carrier, Release 5 s: %.3f s; tail rate %.5f /s at 5 s, %.5f /s at 20 s\n", rel, slopeAt (5.0), slopeAt (20.0));
        REQUIRE_THAT (slopeAt (20.0), WithinAbs (slopeAt (5.0), 0.01 * std::abs (slopeAt (5.0))));
        REQUIRE_THAT (m.crestSquared (0), WithinAbs (1.0, 1.0e-6));
    }

    SECTION ("The first release after a reset uses Release (C² = 2)")
    {
        const auto p = params (t, 0.0, 0.0, 40.0, 1.0, 150.0, true);
        auto m = prepared();
        const double seed[] { 10.0 };
        m.seed (seed, 1);
        const auto r = runModel (m, p, silence (samplesFor (1.0, rate)))[0];
        REQUIRE (withinTimeGate (releaseTime (r, 0, rate, 10.0), 0.150, rate));
    }
}

TEST_CASE ("Clean: look-ahead starts the attack early with the same shape, and the release when the audio ends", "[clean][lookahead]")
{
    const auto latency = samplesFor (0.010, rate);
    constexpr double t = -30.0;

    // On the carrier, as the Ballistics gate measures: on a sine the window also holds each
    // crest for the look-ahead's length, which the ballistics' first stage then sees.
    for (const auto attackMs : { 0.1, 10.0 })
        for (const auto laMs : { 3.0, 10.0 })
        {
            const auto p = params (t, 0.25, 6.0, 40.0, attackMs, 100.0);
            auto key = silence (480);
            append (key, carrier (amplitudeFor (t + 15.0), samplesFor (0.3, rate)));
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

            std::printf ("Clean look-ahead %g ms, Attack %g ms: attack side %.1e dB from the shifted trace, release side %.1e dB\n", laMs, attackMs,
                         attackSide, releaseSide);
            REQUIRE (attackSide <= 1.0e-6);
            REQUIRE (releaseSide <= 1.0e-6);
        }
}

TEST_CASE ("Clean: a slow attack reaches a small reduction instead of snapping to 0", "[clean][idle]")
{
    // At Attack 300 ms one step towards 0.01 dB is 7·10⁻⁷ dB, under the snap: the snap must wait
    // for the target to be under it too.
    constexpr double t = -30.0;
    const auto p = params (t, 0.25, 0.0, 40.0, 300.0, 1000.0);
    const auto level = t + 0.01 / 0.75;

    auto m = prepared();
    const auto r = runModel (m, p, sine (amplitudeFor (level), 1000.0, rate, samplesFor (6.0, rate)))[0];
    REQUIRE_THAT (r.back(), WithinAbs (0.01, 0.002));

    FeedbackHarness h;
    const auto fr = h.run (p, carrier (amplitudeFor (level), samplesFor (6.0, rate)))[0];
    REQUIRE_THAT (fr.back(), WithinAbs (0.01, 1.0e-6));
}

TEST_CASE ("Clean: Range caps before the ballistics", "[clean][range]")
{
    constexpr double t = -30.0;
    const auto p = params (t, 0.0, 0.0, 10.0, 5.0, 150.0);
    auto key = silence (16);
    append (key, carrier (amplitudeFor (t + 30.0), samplesFor (0.5, rate)));

    auto m = prepared();
    const auto r = runModel (m, p, key)[0];
    REQUIRE_THAT (r.back(), WithinAbs (10.0, 0.05));
    REQUIRE (withinTimeGate (attackTime (r, 16, rate, r.back()), 0.005, rate));
}

TEST_CASE ("Clean: idle is exactly 0", "[clean][idle]")
{
    constexpr double t = -30.0;
    const auto loud = carrier (amplitudeFor (-3.0), samplesFor (0.5, rate));

    const auto allZero = [] (const Trace& r) {
        return std::all_of (r.begin(), r.end(), [] (double v) { return isExactly (v, 0.0) && isExactly (gainFor (v), 1.0); });
    };

    for (const auto autoRelease : { false, true })
    {
        auto m = prepared();
        REQUIRE (allZero (runModel (m, params (t, 1.0, 6.0, 40.0, 10.0, 150.0, autoRelease), loud)[0]));
        m.reset();
        REQUIRE (allZero (runModel (m, params (t, 0.25, 6.0, 0.0, 10.0, 150.0, autoRelease), loud)[0]));
        m.reset();
        // 0.5 dB under the knee's foot: the 2.5 ms RMS window reads a 1 kHz sine up to 0.14 dB over its peak.
        auto quiet = sine (amplitudeFor (t - 3.0 - 0.5), 1000.0, rate, samplesFor (0.5, rate));
        REQUIRE (allZero (runModel (m, params (t, 0.0, 6.0, 40.0, 10.0, 150.0, autoRelease), quiet)[0]));
    }

    SECTION ("After a burst the reduction lands on exactly 0 and stays there")
    {
        for (const auto& [attackMs, releaseMs] : { std::pair { 0.02, 5.0 }, std::pair { 10.0, 150.0 }, std::pair { 300.0, 5000.0 } })
        {
            auto key = carrier (amplitudeFor (t + 20.0), samplesFor (0.5, rate));
            const auto end = key.size();
            append (key, silence (samplesFor (std::max (releaseMs, attackMs) * 0.001 * 30.0, rate)));

            auto m = prepared();
            const auto r = runModel (m, params (t, 0.25, 6.0, 40.0, attackMs, releaseMs), key)[0];
            auto firstZero = r.size();
            for (auto i = end; i < r.size(); ++i)
                if (isExactly (r[i], 0.0))
                {
                    firstZero = i;
                    break;
                }
            REQUIRE (firstZero < r.size());
            REQUIRE (std::all_of (r.begin() + (std::ptrdiff_t) firstZero, r.end(), [] (double v) { return isExactly (v, 0.0); }));
            std::printf ("Clean idle snap, Attack %g ms, Release %g ms: exactly 0 after %.1f × Release\n", attackMs, releaseMs,
                         (double) (firstZero - end) / rate / (releaseMs * 0.001));
        }
    }
}

TEST_CASE ("Clean: Link 100 % gives both channels the same reduction to the bit, Link 0 % is Unlinked", "[clean][stereo]")
{
    // Ten seconds of two unrelated, bursty noises.
    const auto n = samplesFor (10.0, rate);
    auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
    for (int i = 0; i < n; ++i)
    {
        const auto envelope = 0.2 + 0.8 * std::abs (std::sin (2.0 * std::numbers::pi * 0.7 * i / rate));
        left[(size_t) i] *= 0.3 * envelope;
        right[(size_t) i] *= 0.3 * (1.2 - envelope);
    }

    for (const auto& [attackMs, autoRelease] : { std::pair { 0.1, false }, std::pair { 10.0, true }, std::pair { 30.0, false } })
    {
        auto p = params (-40.0, 0.25, 6.0, 40.0, attackMs, 150.0, autoRelease);
        const Signal lookahead ((size_t) n, 96.0);

        p.linked = true;
        p.link = 1.0;
        auto m = prepared();
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

TEST_CASE ("Clean: a seed starts the ballistics at the applied reduction", "[clean][switch]")
{
    // Settled 33: at Attack 300 ms the incoming model starts at the applied 10 dB instead of
    // re-attacking from 0. The level states, run by follow(), are settled already.
    constexpr double t = -30.0;
    const auto p = params (t, 0.0, 0.0, 40.0, 300.0, 1000.0);
    const auto level = carrier (amplitudeFor (carrierLevelFor (t, 0.0, 10.0, 0.5)), samplesFor (3.0, rate));

    auto m = prepared();
    KeyBlock key;
    key.numChannels = 1;
    key.numSamples = (int) level.size();
    key.channels[0] = level.data();
    m.follow (p, key);

    const double seed[] { 10.0 };
    m.seed (seed, 1);
    const auto r = runModel (m, p, level)[0];
    REQUIRE_THAT (*std::min_element (r.begin(), r.end()), WithinAbs (10.0, 1.0e-6));
    REQUIRE_THAT (*std::max_element (r.begin(), r.end()), WithinAbs (10.0, 1.0e-6));
}

namespace
{
    /** One channel of key through Clean in 64-sample blocks, the parameters per block from `at` (the block's first sample). */
    Trace runScheduled (CleanModel& m, const Signal& key, const std::function<ModelParams (int)>& at)
    {
        Trace r;
        for (int start = 0; start < (int) key.size(); start += 64)
        {
            KeyBlock k;
            k.numSamples = std::min (64, (int) key.size() - start);
            k.channels[0] = key.data() + start;
            std::vector<double> out ((size_t) k.numSamples);
            double* o[] { out.data(), nullptr };
            m.process (at (start), k, o);
            r.insert (r.end(), out.begin(), out.end());
        }
        return r;
    }

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

TEST_CASE ("Clean: a knob jump neither steps nor bends the reduction", "[clean][clicks]")
{
    // A key that sits 14 dB over the default Threshold. Every 0.31 s a knob jumps between its
    // ends while the reduction is still releasing from the last jump, at Attack 0.02 ms: on a
    // parameter ramp the instant attack would meet the falling hold at a corner (a slope step
    // of the order of the ramp's 0.06 dB per sample); the fade keeps every second difference
    // of the order of the raised cosine's own, 40 dB·(π/960)²/2 ≈ 2·10⁻⁴.
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
    };

    for (const auto& jump : jumps)
    {
        const auto key = carrier (amplitudeFor (jump.keyDb), samplesFor (2.0, rate));
        auto m = prepared();
        const auto r = runScheduled (m, key, [&] (int pos)
        {
            auto p = params (-18.0, 0.25, 6.0, 40.0, 0.02, 150.0);
            p.autoRelease = true;
            jump.set (p, (pos / period) % 2 == 1);
            return p;
        });
        const auto [step, corner] = roughness (r, 64);
        std::printf ("Clean knob jump at Attack 0.02 ms, %s: largest step %.4f dB, largest second difference %.2e dB\n", jump.name, step, corner);
        INFO (jump.name << ": largest step " << step << " dB, largest second difference " << corner << " dB");
        REQUIRE (step > 1.0e-4);
        REQUIRE (step < 0.1);
        REQUIRE (corner < 5.0e-4);
    }
}

TEST_CASE ("Clean: an Attack jump mid-attack glides instead of completing the attack at once", "[clean][clicks]")
{
    // Attack 300 ms leaves the attack far from done when it jumps to 0.02 ms. Changing the
    // coefficient at once closed the gap within a sample (0.3 dB here); the 20 ms glide in log
    // time closes it smoothly.
    const auto key = carrier (amplitudeFor (-4.0), samplesFor (1.0, rate));
    const auto jumpAt = samplesFor (0.1, rate) / 64 * 64;
    auto m = prepared();
    const auto r = runScheduled (m, key, [&] (int pos) { return params (-18.0, 0.25, 6.0, 40.0, pos >= jumpAt ? 0.02 : 300.0, 150.0); });

    const auto [step, corner] = roughness (r, (size_t) jumpAt - 64);
    INFO ("largest step " << step << " dB, largest second difference " << corner << " dB; reduction at the jump " << r[(size_t) jumpAt]);
    REQUIRE (r[(size_t) jumpAt] < 5.0);
    REQUIRE_THAT (r.back(), WithinAbs (10.5, 0.01));
    REQUIRE (step < 0.05);
    REQUIRE (corner < 1.0e-3);
}
