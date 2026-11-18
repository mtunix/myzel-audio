// Vari-mu's curve (NOTES.md, Gain computer, Gate table): the formula against the reference
// transcription and against the NOTES' own fits, the per-sample inverse, the static curve through
// the loop on the carrier and sines, the rising ratio, Range and Auto Gain.

#include "VariMuHarness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <random>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using namespace basalt::testing::varimu;
using Catch::Matchers::WithinAbs;
namespace curve = basalt::models::varimu;

namespace
{
    constexpr double rate = 48000.0;
    const double ratios[] { 1.5, 2.0, 4.0, 6.0, 10.0, 20.0, std::numeric_limits<double>::infinity() };
    const double knees[] { 0.0, 6.0, 12.0, 24.0 };

    Curve curveOf (double t, double ratio, double knee, double range = 40.0) { return { t, ref::inverseRatioOf (ratio), knee, range }; }
}

TEST_CASE ("Vari-mu curve: the model's d(x) is the NOTES' formula", "[varimu][curve]")
{
    SECTION ("d(x) and d'(x) against the transcription")
    {
        // The model reads softplus from a table (VariMuCurve.h), within 10⁻⁸ dB of the formula.
        auto worst = 0.0, worstSlope = 0.0;
        for (const auto ratio : { 1.0, 1.1, 1.25, 1.5, 2.0, 4.0, 6.0, 10.0, 19.9, 20.0, std::numeric_limits<double>::infinity() })
            for (const auto knee : { 0.0, 3.0, 6.0, 12.0, 24.0 })
                for (auto x = -90.0; x <= 130.0; x += 0.0137)
                {
                    const auto c = curveOf (-30.0, ratio, knee);
                    const auto s = curve::Shape::of (c);
                    worst = std::max (worst, std::abs (curve::reduction (s, x) - ref::reduction (x, -30.0, c.inverseRatio, knee)));
                    worstSlope = std::max (worstSlope, std::abs (curve::evaluate (s, x).slope - ref::reductionSlope (x, -30.0, c.inverseRatio, knee)));
                }
        std::printf ("Vari-mu curve against the transcription: worst |d| difference %.1e dB, slope %.1e\n", worst, worstSlope);
        REQUIRE (worst < 1.0e-8);
        REQUIRE (worstSlope < 1.0e-6);
    }

    SECTION ("Ratio 1 is the identity; Ratio 20 and ∞ are eq. 4 at 20:1 with no rise")
    {
        for (auto x = -90.0; x <= 30.0; x += 0.5)
        {
            REQUIRE (myzel::dsp::isExactly (curve::reduction (curve::Shape::of (curveOf (-30.0, 1.0, 6.0)), x), 0.0));
            for (const auto ratio : { 20.0, std::numeric_limits<double>::infinity() })
            {
                const auto c = curveOf (-30.0, ratio, 6.0);
                REQUIRE (myzel::dsp::isExactly (curve::reduction (curve::Shape::of (c), x), dynamics::curveReduction ({ -30.0, 0.05, 6.0, 40.0 }, x)));
            }
        }
    }

    SECTION ("The output slope never falls under 1/20, so the feedback form exists")
    {
        for (const auto ratio : ratios)
            for (const auto knee : knees)
                for (auto x = -60.0; x <= 40.0; x += 0.1)
                    REQUIRE (1.0 - curve::evaluate (curve::Shape::of (curveOf (-30.0, ratio, knee)), x).slope >= 1.0 / 20.0 - 1.0e-12);
    }

    SECTION ("Below both T − W/2 and T − 13 dB the curve is under 10⁻⁶ dB at every Ratio (NOTES.md, Identity)")
    {
        for (auto ratio = 1.0; ratio <= 20.0; ratio *= 1.01)
            for (const auto knee : knees)
            {
                const auto below = -30.0 + std::min (-0.5 * knee, -13.0);
                REQUIRE (curve::reduction (curve::Shape::of (curveOf (-30.0, ratio, knee)), below) < 1.0e-6);
            }
    }
}

TEST_CASE ("Vari-mu curve: the NOTES' fits, repeated", "[varimu][curve]")
{
    // NOTES.md, Gain computer: at Ratio 4, Knee 6 the local ratio against the reduction.
    const std::pair<double, double> localRatios[] { { 6.0, 4.1 }, { 10.0, 4.8 }, { 12.0, 6.3 }, { 14.0, 9.6 }, { 16.0, 13.8 }, { 20.0, 18.9 }, { 24.0, 19.9 } };
    for (const auto& [depth, notes] : localRatios)
    {
        const auto s = curve::Shape::of (curveOf (0.0, 4.0, 6.0));
        const auto local = curve::localRatio (s, curve::levelForReduction (s, depth));
        std::printf ("Vari-mu Ratio 4, Knee 6: local ratio %.2f at %g dB of reduction (NOTES %.1f)\n", local, depth, notes);
        // The NOTES print one decimal; 13.85 at 16 dB sits on the rounding edge.
        REQUIRE_THAT (local, WithinAbs (notes, 0.06));
    }

    // The first reference's factory curve, digitised (NOTES.md, What the references show), against
    // Ratio 6, Knee 7.4, Threshold +3.75 dBm: rms 0.114, max 0.235 dB in M0's fit.
    const std::pair<double, double> factory[] { { 3.2, 0.4 }, { 5.3, 1.6 }, { 7.4, 3.1 }, { 9.0, 4.4 }, { 12.2, 6.9 },
                                                 { 15.9, 10.0 }, { 19.1, 12.9 }, { 22.8, 16.4 }, { 24.4, 18.0 } };
    auto sq = 0.0, worst = 0.0;
    for (const auto& [input, digitised] : factory)
    {
        const auto e = curve::reduction (curve::Shape::of (curveOf (3.75, 6.0, 7.4)), input) - digitised;
        sq += e * e;
        worst = std::max (worst, std::abs (e));
    }
    std::printf ("Vari-mu against the digitised factory curve (Ratio 6, Knee 7.4, T +3.75): rms %.3f dB, max %.3f dB (M0: 0.114 / 0.235)\n",
                 std::sqrt (sq / 9.0), worst);
    REQUIRE (worst < 0.25);
}

TEST_CASE ("Vari-mu curve: the per-sample inverse solves G(x) = y", "[varimu][curve][solver]")
{
    // Random levels on random curves, each solve from the last one on another curve or far away:
    // the model's own G(x) = y within 2·10⁻⁶ dB (20 × acceptDb at a hard knee's corner, less
    // elsewhere), and the demand against the transcription's regula falsi on the exact formula.
    std::mt19937 rng (7u);
    std::uniform_real_distribution<double> unit (0.0, 1.0);
    auto worst = 0.0, worstDemand = 0.0;

    for (int trial = 0; trial < 20000; ++trial)
    {
        const auto ratio = trial % 10 == 0 ? std::numeric_limits<double>::infinity() : std::pow (20.0, unit (rng));
        const auto c = curveOf (-60.0 * unit (rng), ratio, trial % 7 == 0 ? 0.0 : 24.0 * unit (rng));
        const auto s = curve::Shape::of (c);
        curve::Inverse inverse;
        for (int k = 0; k < 4; ++k)
        {
            const auto y = c.thresholdDb - 30.0 + 90.0 * unit (rng);
            const auto demand = inverse.demand (s, y);
            const auto x = y + demand;
            worst = std::max (worst, std::abs (x - curve::reduction (s, x) - y));
            REQUIRE (demand >= 0.0);

            ref::Settings rs;
            rs.thresholdDb = c.thresholdDb;
            rs.inverseRatio = c.inverseRatio;
            rs.kneeDb = c.kneeDb;
            worstDemand = std::max (worstDemand, std::abs (demand - ref::demand (y, rs)));
        }
    }
    std::printf ("Vari-mu inverse, random levels: worst |G(x) − y| %.1e dB; demand within %.1e dB of the transcription's\n", worst, worstDemand);
    REQUIRE (worst < 2.0e-6);
    REQUIRE (worstDemand < 1.0e-5);

    // A channel's estimate as the loop moves it: a 0.5 Hz swell of ±25 dB around 10 dB over
    // Threshold with a little noise, and the held level's jumps. The evaluations a solve takes.
    const auto s = curve::Shape::of (curveOf (-30.0, 4.0, 6.0));
    curve::Inverse inverse;
    long evaluations = 0, solves = 0;
    int most = 0;
    for (int n = 0; n < 480000; ++n)
    {
        const auto y = -30.0 + 5.0 + 25.0 * std::sin (2.0 * std::numbers::pi * 0.5 * n / rate) + 0.3 * (unit (rng) - 0.5) + (n % 240 < 120 ? 0.0 : 3.0);
        const auto x = y + inverse.demand (s, y);
        if (x > y)
        {
            evaluations += inverse.evaluationsTaken();
            ++solves;
            most = std::max (most, inverse.evaluationsTaken());
        }
        REQUIRE (std::abs (x - curve::reduction (s, x) - y) < 1.0e-8);
    }
    std::printf ("Vari-mu inverse, a moving estimate: %.2f curve evaluations a solve on average, %d at most\n", (double) evaluations / (double) solves, most);
    REQUIRE (most <= 8);
}

namespace
{
    struct StaticResult
    {
        double worst = 0.0, worstLevel = 0.0;
    };

    StaticResult staticError (VariMuModel& m, const ModelParams& p, double hz)
    {
        const auto t = p.curve.thresholdDb;
        const auto s = staircase (m, p, rate, t - 20.0, t + 40.0, hz);
        StaticResult r;
        for (size_t i = 0; i < s.levels.size(); ++i)
        {
            const auto e = std::abs (s.settled[i] - ref::staticReduction (s.levels[i], t, p.curve.inverseRatio, p.curve.kneeDb, p.curve.rangeDb));
            if (e > r.worst)
                r = { e, s.levels[i] - t };
        }
        return r;
    }
}

TEST_CASE ("Vari-mu static curve: the carrier and 1 kHz sines, the grid of the gate table", "[varimu][static]")
{
    // NOTES.md, Gate table: T − 20 … T + 40 dB in 1 dB steps, R 1.5, 2, 4, 6, 10, 20, ∞,
    // W 0, 6, 12, 24, Range 40 and 10, Attack 10 ms, Release 1 s, Auto off: ±0.25 dB of r_s.
    auto worstCarrier = 0.0, worstSine = 0.0;
    auto m = prepared (rate);

    for (const auto ratio : ratios)
        for (const auto knee : knees)
            for (const auto range : { 40.0, 10.0 })
            {
                const auto p = params (-30.0, ref::inverseRatioOf (ratio), knee, range, 10.0, 1000.0);
                m.reset();
                const auto c = staticError (m, p, 0.0);
                m.reset();
                const auto s = staticError (m, p, 1000.0);
                worstCarrier = std::max (worstCarrier, c.worst);
                worstSine = std::max (worstSine, s.worst);
                INFO ("ratio " << ratio << ", knee " << knee << ", range " << range << ": carrier " << c.worst << " dB at T + " << c.worstLevel
                               << ", 1 kHz " << s.worst << " dB at T + " << s.worstLevel);
                REQUIRE (c.worst <= ref::staticToleranceDb);
                REQUIRE (s.worst <= ref::staticToleranceDb);
            }

    std::printf ("Vari-mu static curve (R 1.5 … ∞, W 0/6/12/24, Range 40/10): worst |error| carrier %.1e dB, 1 kHz %.1e dB\n", worstCarrier, worstSine);
}

TEST_CASE ("Vari-mu static curve: 100 Hz gated, 50 Hz reported, other Thresholds", "[varimu][static]")
{
    auto worst100 = 0.0, worst50 = 0.0;
    auto m = prepared (rate);

    for (const auto ratio : ratios)
        for (const auto knee : { 0.0, 6.0, 24.0 })
        {
            const auto p = params (-30.0, ref::inverseRatioOf (ratio), knee, 40.0, 10.0, 1000.0);
            m.reset();
            const auto e100 = staticError (m, p, 100.0);
            m.reset();
            const auto e50 = staticError (m, p, 50.0);
            worst100 = std::max (worst100, e100.worst);
            worst50 = std::max (worst50, e50.worst);
            INFO ("ratio " << ratio << ", knee " << knee << ": 100 Hz " << e100.worst << " dB at T + " << e100.worstLevel);
            REQUIRE (e100.worst <= ref::staticToleranceDb);
        }

    auto worstT = 0.0;
    for (const auto t : { -60.0, -10.0 })
    {
        const auto p = params (t, 0.25, 6.0, 40.0, 10.0, 1000.0);
        m.reset();
        worstT = std::max (worstT, staticError (m, p, 0.0).worst);
        m.reset();
        worstT = std::max (worstT, staticError (m, p, 1000.0).worst);
    }
    REQUIRE (worstT <= ref::staticToleranceDb);

    std::printf ("Vari-mu static curve: 100 Hz worst %.1e dB (gated), 50 Hz worst %.4f dB (reported); T −60 and −10 worst %.1e dB\n", worst100,
                 worst50, worstT);
}

TEST_CASE ("Vari-mu static curve: the ratio rises with level (Ratio 4, Knee 6), and holds 20:1 at Ratio 20", "[varimu][static][rising]")
{
    // NOTES.md, Gate table: secants from the settled 1 dB steps on the carrier, read at 8, 16 and
    // 20 dB of reduction: ≤ 5, ≥ 12, ≥ 17. At Ratio 20, 20 ± 1 everywhere above the knee.
    auto m = prepared (rate);
    const auto p = params (-30.0, 0.25, 6.0, 40.0, 10.0, 1000.0);
    const auto s = staircase (m, p, rate, -50.0, 10.0);

    std::vector<double> secant (s.levels.size(), 0.0);
    for (size_t i = 1; i < s.levels.size(); ++i)
        secant[i] = 1.0 / (1.0 - (s.settled[i] - s.settled[i - 1]));

    for (const auto& limit : ref::risingRatioLimits)
    {
        auto measured = std::numeric_limits<double>::quiet_NaN();
        for (size_t i = 2; i < s.levels.size(); ++i)
            if (s.settled[i - 1] <= limit.reductionDb && s.settled[i] >= limit.reductionDb)
                measured = secant[i - 1] + (limit.reductionDb - s.settled[i - 1]) / (s.settled[i] - s.settled[i - 1]) * (secant[i] - secant[i - 1]);

        const auto expected = ref::secantRatioAtReduction (limit.reductionDb, 0.25, 6.0);
        std::printf ("Vari-mu rising ratio at %g dB of reduction: secant %.2f (transcription %.2f; tangent %.2f), gate %s\n", limit.reductionDb, measured,
                     expected, ref::localRatioAtReduction (limit.reductionDb, 0.25, 6.0),
                     limit.atLeast > 0.0 ? ("≥ " + std::to_string ((int) limit.atLeast)).c_str() : ("≤ " + std::to_string ((int) limit.atMost)).c_str());
        REQUIRE (measured <= limit.atMost);
        REQUIRE (measured >= limit.atLeast);
        REQUIRE_THAT (measured, WithinAbs (expected, 0.05 * expected));
    }

    m.reset();
    const auto cap = params (-30.0, 0.05, 6.0, 40.0, 10.0, 1000.0);
    const auto c = staircase (m, cap, rate, -27.0 + 1.0, 10.0);
    auto worst = 0.0;
    for (size_t i = 1; i < c.levels.size(); ++i)
    {
        const auto r = 1.0 / (1.0 - (c.settled[i] - c.settled[i - 1]));
        worst = std::max (worst, std::abs (r - 20.0));
    }
    std::printf ("Vari-mu Ratio 20 above the knee: secants within ±%.3f of 20\n", worst);
    REQUIRE (worst <= ref::capRatioTolerance);
}

TEST_CASE ("Vari-mu: Range sits on the loop's state, Auto Gain inverts the curve", "[varimu][range]")
{
    // The core's Range row at the cap: 30 dB over, Range 10 → 10 dB ±0.05; the attack with Range
    // engaged within ±10 % of the unclamped one.
    auto m = prepared (rate);
    const auto p = params (-30.0, 0.0, 0.0, 10.0, 5.0, 150.0);
    const auto r = reductionFor (m, p, carrier (amplitudeFor (0.0), samplesFor (1.0, rate)));
    REQUIRE_THAT (r.back(), WithinAbs (10.0, 0.05));

    // Range on the state never slows the attack: until the state reaches Range the clamped trace
    // is the unclamped one, sample for sample; then it holds Range. (The 63 % time itself is
    // shorter clamped, since on the rising curve a shallower step attacks faster.)
    for (const auto ratio : { 4.0, 20.0 })
    {
        const auto clamped = params (-30.0, 1.0 / ratio, 0.0, 40.0, 5.0, 150.0);
        auto free = clamped;
        free.curve.rangeDb = 200.0;
        auto key = silence (16);
        append (key, carrier (amplitudeFor (-30.0 + 60.0), samplesFor (1.0, rate)));
        m.reset();
        const auto a = reductionFor (m, clamped, key);
        m.reset();
        const auto b = reductionFor (m, free, key);

        size_t reached = 0;
        while (reached < a.size() && a[reached] < 40.0)
            ++reached;
        auto worst = 0.0;
        for (size_t i = 0; i < reached; ++i)
            worst = std::max (worst, std::abs (a[i] - b[i]));
        std::printf ("Vari-mu Range 40 against none, Ratio %g, 60 dB over: identical for %.2f ms until Range (worst %.1e dB), then %.2f dB (free: %.2f)\n",
                     ratio, 1000.0 * (double) reached / rate, worst, a.back(), b.back());
        REQUIRE (reached > 16);
        REQUIRE (myzel::dsp::isExactly (worst, 0.0));
        REQUIRE_THAT (a.back(), WithinAbs (40.0, 1.0e-9));
        REQUIRE (b.back() > 45.0);
    }

    for (const auto ratio : ratios)
        for (const auto knee : knees)
            for (const auto t : { -40.0, -18.0, -6.0 })
            {
                auto q = params (t, ref::inverseRatioOf (ratio), knee, 40.0, 10.0, 150.0);
                REQUIRE_THAT (autoGainDb (m, q), WithinAbs (ref::autoGainDb (t, q.curve.inverseRatio, knee, 40.0), 0.01));
            }
}
