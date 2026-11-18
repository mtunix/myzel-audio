// FET's ballistics against models/fet/NOTES.md (Ballistics, All, Gate table): attack and release
// as the core's measured times over the whole knobs at 4:1, 20:1 and All, All's lag, Auto release
// dimmed, look-ahead; the knee region and the in-cycle modulation reported.

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
    const char* ratioName (double ratio) { return std::isinf (ratio) ? "All" : (myzel::dsp::isExactly (ratio, 4.0) ? "4" : "20"); }

    struct StepResponse
    {
        double attackS = 0.0, releaseS = 0.0, settledDb = 0.0;
    };

    /**
        The core's measurement definition (dynamics/NOTES.md, What the knobs mean): the carrier from
        silence to the level for `depth` dB, held max(5·(Attack + Release), 10·Attack) (Settled 70),
        then silence. Both times from the step, or from the end of All's lag (NOTES.md, Gate table).
    */
    StepResponse stepResponse (double rate, double ratio, double attackMs, double releaseMs, double depth = ref::ballisticsDepthDb,
                               double knee = 0.0, double level = std::numeric_limits<double>::quiet_NaN())
    {
        constexpr double t = -30.0;
        const auto p = params (t, ratio, knee, 40.0, attackMs, releaseMs);
        const auto lag = (size_t) std::lround (ref::lagMs (ratio) * 0.001 * rate);
        const auto onset = (size_t) samplesFor (0.002, rate);
        const auto held = samplesFor (std::max (5.0 * (attackMs + releaseMs), 10.0 * attackMs) * 0.001, rate);
        const auto x = std::isnan (level) ? ref::levelForReductionDb (t, ratio, depth) : level;

        auto key = silence ((int) onset);
        append (key, carrier (amplitudeFor (x), held));
        const auto off = key.size();
        append (key, silence (samplesFor (5.0 * releaseMs * 0.001 + 0.01, rate)));

        auto m = prepared (rate);
        const auto r = runModel (m, p, key, nullptr, 512)[0];
        const auto settled = r[off - 1];
        return { attackTime (r, onset + lag, rate, settled), releaseTime (r, off + lag, rate, settled), settled };
    }
}

TEST_CASE ("FET: attack and release are the knobs at 4:1, 20:1 and All", "[fet][ballistics]")
{
    // NOTES.md, Gate table: Knee 0, Attack 0.02 … 300 ms × Release 5 ms … 5 s, R 4, 20 and All:
    // ±10 % (±0.25 sample under 2.5 samples), All from the end of its lag. At 44.1, 48 and 96 kHz.
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto worstAttack = 1.0, worstRelease = 1.0;
        for (const auto ratio : ref::ballisticsRatios)
            for (const auto attackMs : ref::ballisticsAttacksMs)
                for (const auto releaseMs : ref::ballisticsReleasesMs)
                {
                    const auto s = stepResponse (rate, ratio, attackMs, releaseMs);
                    const auto expectedA = ref::attackMs (attackMs), expectedR = ref::releaseMs (releaseMs);
                    const auto a = s.attackS * 1000.0, r = s.releaseS * 1000.0;
                    if (std::abs (a / expectedA - 1.0) > std::abs (worstAttack - 1.0))
                        worstAttack = a / expectedA;
                    if (std::abs (r / expectedR - 1.0) > std::abs (worstRelease - 1.0))
                        worstRelease = r / expectedR;

                    INFO (rate << " Hz, R " << ratioName (ratio) << ", Attack " << attackMs << " ms, Release " << releaseMs << " ms: attack "
                          << a << " ms, release " << r << " ms, settled " << s.settledDb << " dB");
                    REQUIRE_THAT (s.settledDb, WithinAbs (ref::ballisticsDepthDb, 1.0e-3));
                    REQUIRE (std::abs (a - expectedA) <= ref::timeToleranceMs (expectedA, rate));
                    REQUIRE (std::abs (r - expectedR) <= ref::timeToleranceMs (expectedR, rate));
                }

        std::printf ("FET ballistics at %g Hz: worst attack %.4f x the knob, worst release %.4f x\n", rate, worstAttack, worstRelease);
    }
}

TEST_CASE ("FET: the M0 simulation's ballistics table, repeated", "[fet][ballistics]")
{
    // NOTES.md, Ballistics, Measured (simulation): Ratio 4, hard knee, 10 dB; attack at Release
    // 300 ms, release at Attack 0.8 ms; 48 kHz.
    constexpr double rate = 48000.0;
    std::printf ("FET attack, measured/set at Release 300 ms:");
    for (const auto a : { 0.02, 0.05, 0.2, 0.8, 10.0, 100.0 })
        std::printf (" %g ms %.4f", a, stepResponse (rate, 4.0, a, 300.0).attackS * 1000.0);
    std::printf ("\nFET release, measured at Attack 0.8 ms:");
    for (const auto r : { 5.0, 50.0, 300.0, 1100.0 })
        std::printf (" %g ms %.3f", r, stepResponse (rate, 4.0, 0.8, r).releaseS * 1000.0);
    std::printf ("\n");
}

TEST_CASE ("FET: All's key lag lets the first millisecond through, then attacks at the knob", "[fet][ballistics][all]")
{
    // NOTES.md, Gate table: the carrier from silence to 10 dB of reduction: below 0.1 dB for the
    // first L (1 ms ± 1 sample), then 63 % of the settled value Attack later, ±10 %.
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto attackMs : { 0.02, 0.2, 0.8, 10.0 })
        {
            constexpr double t = -30.0;
            const auto p = params (t, ref::infinity, 0.0, 40.0, attackMs, 300.0);
            const auto onset = (size_t) samplesFor (0.002, rate);
            auto key = silence ((int) onset);
            append (key, carrier (amplitudeFor (ref::levelForReductionDb (t, ref::infinity, 10.0)), samplesFor (0.05 + 10.0 * attackMs * 0.001, rate)));

            auto m = prepared (rate);
            const auto r = runModel (m, p, key)[0];
            const auto lagS = ref::allLagMs * 0.001;
            const auto lag = (size_t) std::lround (lagS * rate);
            const auto quiet = timeToReach (r, onset, rate, ref::lagQuietDb);
            const auto first = timeToReach (r, onset, rate, 0.0);
            const auto toAttack = attackTime (r, onset + lag, rate, r.back());

            std::printf ("FET All at %g Hz, Attack %g ms: first reduction at %.4f ms, 0.1 dB at %.4f ms, 63 %% %.4f ms after the lag\n", rate,
                         attackMs, first * 1000.0, quiet * 1000.0, toAttack * 1000.0);
            INFO (rate << " Hz, Attack " << attackMs << " ms");
            REQUIRE (quiet >= lagS - ref::lagToleranceSamples / rate);
            REQUIRE (first <= lagS + ref::lagToleranceSamples / rate);
            REQUIRE (std::abs (toAttack * 1000.0 - attackMs) <= ref::timeToleranceMs (attackMs, rate));
        }
}

TEST_CASE ("FET: Auto release is dimmed and changes nothing", "[fet][ballistics][auto]")
{
    // NOTES.md, Ballistics: no source documents a program-dependent FET release.
    constexpr double rate = 48000.0;
    auto key = pinkNoise (samplesFor (3.0, rate), 11u);
    scale (key, 0.4);
    append (key, silence (samplesFor (1.0, rate)));

    for (const auto ratio : { 4.0, 20.0, ref::infinity })
    {
        auto off = prepared (rate), on = prepared (rate);
        const auto a = runModel (off, params (-30.0, ratio, 6.0, 40.0, 0.8, 150.0, false), key)[0];
        const auto b = runModel (on, params (-30.0, ratio, 6.0, 40.0, 0.8, 150.0, true), key)[0];
        REQUIRE_FALSE (ref::autoReleaseChangesAnything);
        REQUIRE (a == b);
    }
}

TEST_CASE ("FET: look-ahead starts the attack early with the same shape and leaves the release in audio time", "[fet][ballistics][lookahead]")
{
    // The core's look-ahead row (dynamics/NOTES.md, Look-ahead in feedback models) on FET's
    // loop, All's lag included: a 1 kHz burst 15 dB over, 200 ms, the audio delayed by 10 ms;
    // LA = 3 ms against LA = 0.
    constexpr double rate = 48000.0;
    const auto latency = samplesFor (0.010, rate);
    constexpr double t = -30.0;

    auto key = silence (480);
    append (key, sine (amplitudeFor (t + 21.0), 1000.0, rate, samplesFor (0.2, rate)));
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

    for (const auto ratio : { 4.0, 20.0, ref::infinity })
    {
        const auto p = params (t, ratio, 0.0, 40.0, 5.0, 100.0);
        const auto la = samplesFor (0.003, rate);
        const Signal laTrace (key.size(), (double) la);

        auto m0 = prepared (rate), ml = prepared (rate);
        const auto r0 = runModel (m0, p, tap (0))[0];
        const auto rl = runModel (ml, p, tap (la), nullptr, 64, &laTrace)[0];

        auto attackSide = 0.0, releaseSide = 0.0;
        for (size_t n = 0; n + (size_t) la < end + (size_t) latency; ++n)
            attackSide = std::max (attackSide, std::abs (rl[n] - r0[n + (size_t) la]));
        for (auto n = end + (size_t) latency; n < key.size(); ++n)
            releaseSide = std::max (releaseSide, std::abs (rl[n] - r0[n]));

        std::printf ("FET look-ahead 3 ms, R %s: attack side %.1e dB from the shifted trace, release side %.1e dB\n", ratioName (ratio),
                     attackSide, releaseSide);
        INFO ("R " << ratioName (ratio));
        REQUIRE (attackSide <= 1.0e-6);
        REQUIRE (releaseSide <= 1.0e-6);
    }
}

TEST_CASE ("FET: the knee region attacks slower than the knob, reported", "[fet][ballistics][knee]")
{
    // NOTES.md, Gate table, Knee region: Knee 12, R 4, steps ending inside the knee; measured/set
    // beside the linearised closed loop's constant at the settled level, Attack·R·G'(x).
    constexpr double rate = 48000.0, t = -30.0;
    for (const auto attackMs : { 0.8, 10.0 })
    {
        std::printf ("FET knee region, Knee 12, R 4, Attack %g ms:", attackMs);
        for (const auto over : { -4.0, -2.0, 0.0, 2.0, 4.0, 6.0, 10.0 })
        {
            const auto s = stepResponse (rate, 4.0, attackMs, 300.0, 0.0, 12.0, t + over);
            std::printf (" T%+g: %.3f dB, %.2f x (linearised %.2f x)", over, s.settledDb, s.attackS * 1000.0 / attackMs,
                         ref::kneeRegionAttackMs (attackMs, t + over, t, 4.0, 12.0) / attackMs);
        }
        std::printf ("\n");
    }
}

namespace
{
    /**
        THD+N of a sine through the model's gain, in dBc: the fundamental fitted by least squares over
        `periods` whole periods after the settling, the residual against it (Settled 71's method).
    */
    double thdPlusN (double rate, double hz, double ratio, double attackMs, double releaseMs)
    {
        constexpr double t = -30.0;
        const auto p = params (t, ratio, 0.0, 40.0, attackMs, releaseMs);
        const auto level = ref::levelForReductionDb (t, ratio, 6.0);
        const auto settle = samplesFor (25.0 * attackMs * 0.001 + 2.0 * releaseMs * 0.001 + 0.5, rate);
        const auto periods = 50, perPeriod = (int) std::lround (rate / hz);
        const auto x = sine (amplitudeFor (level), hz, rate, settle + periods * perPeriod);

        auto m = prepared (rate);
        const auto r = runModel (m, p, x)[0];

        auto ss = 0.0, cc = 0.0, sc = 0.0, sy = 0.0, cy = 0.0;
        std::vector<double> y;
        for (auto n = (size_t) settle; n < x.size(); ++n)
        {
            const auto out = x[n] * std::pow (10.0, -r[n] / 20.0);
            const auto ph = 2.0 * std::numbers::pi * hz * (double) n / rate;
            const auto s = std::sin (ph), c = std::cos (ph);
            ss += s * s;
            cc += c * c;
            sc += s * c;
            sy += s * out;
            cy += c * out;
            y.push_back (out);
        }
        const auto det = ss * cc - sc * sc;
        const auto a = (sy * cc - cy * sc) / det, b = (cy * ss - sy * sc) / det;

        auto residual = 0.0, fundamental = 0.0;
        for (size_t i = 0; i < y.size(); ++i)
        {
            const auto ph = 2.0 * std::numbers::pi * hz * (double) (i + (size_t) settle) / rate;
            const auto f = a * std::sin (ph) + b * std::cos (ph);
            fundamental += f * f;
            residual += (y[i] - f) * (y[i] - f);
        }
        return 10.0 * std::log10 (residual / fundamental);
    }
}

TEST_CASE ("FET: the in-cycle modulation at 6 dB of reduction, reported", "[fet][distortion]")
{
    // NOTES.md, Gate table, Distortion: reported, not gated (PLAN.md: fast models' in-cycle
    // modulation is their sound). 1 kHz at Attack 0.02, 0.8, 10 ms × Release 50 ms, 1.1 s, R 4,
    // 20 and All; 100 Hz at Release 50 ms. Hard knee, T −30. At 48 kHz a 1 kHz period is 48
    // samples, so the 1 ms hold always holds the same sampled crest; 997 Hz at 48 kHz and 1 kHz
    // at 44.1 kHz show what a crest moving from period to period gives.
    struct Row
    {
        const char* name;
        double rate, hz;
        bool shortReleaseOnly;
    };
    const Row rows[] { { "1 kHz", 48000.0, 1000.0, false }, { "997 Hz", 48000.0, 997.0, false },
                       { "1 kHz at 44.1", 44100.0, 1000.0, false }, { "100 Hz", 48000.0, 100.0, true } };

    for (const auto ratio : ref::ballisticsRatios)
        for (const auto& row : rows)
        {
            std::printf ("FET THD+N, R %s, %s:", ratioName (ratio), row.name);
            for (const auto attackMs : { 0.02, 0.8, 10.0 })
                for (const auto releaseMs : { 50.0, 1100.0 })
                    if (! row.shortReleaseOnly || releaseMs < 100.0)
                        std::printf (" A %g/R %g %.1f;", attackMs, releaseMs, thdPlusN (row.rate, row.hz, ratio, attackMs, releaseMs));
            std::printf (" dBc\n");
        }
}
