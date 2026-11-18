// VCA's static rows (models/vca/NOTES.md, Gates): the curve with T_eff = T − 6 dB/R on 1 kHz sines
// and the carrier for both detectors, the moving knee found by extrapolation, RMS on band-limited
// pink noise, and the exact zeros (below T_eff − W/2, Ratio 1 at every Threshold, Range 0).

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
using myzel::dsp::isExactly;

TEST_CASE ("VCA: the static curve with the moving threshold, both detectors", "[vca][static]")
{
    // The core's grid (dynamics/NOTES.md, Gate table) at Attack 10 ms, Release 1 s, Auto off:
    // ascending 1 dB steps from T − 20 to T + 30, each settled for 100 ms, levels on each
    // detector's own reading.
    const auto settle = samplesFor (0.100, rate), measure = samplesFor (0.020, rate);

    for (const auto d : { Detector::peak, Detector::rms })
    {
        auto worstSine = 0.0, worstCarrier = 0.0;
        char worstSineAt[128] {}, worstCarrierAt[128] {};

        for (const auto useSine : { true, false })
            for (const auto t : { -60.0, -30.0, -10.0 })
                for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 20.0, 0.0 })
                    for (const auto knee : { 0.0, 6.0, 24.0 })
                        for (const auto range : { 40.0, 10.0 })
                        {
                            const auto inverseRatio = ratio > 0.0 ? 1.0 / ratio : 0.0;
                            const auto p = params (t, inverseRatio, knee, range, 10.0, 1000.0);

                            Signal key;
                            std::vector<double> levels;
                            for (auto level = t - 20.0; level <= t + 30.0 + 1.0e-9; level += 1.0)
                            {
                                const auto n = settle + measure;
                                append (key, useSine ? sine (amplitudeFor (level), 1000.0, rate, n, 2.0 * std::numbers::pi * 1000.0 * (double) key.size() / rate)
                                                     : carrier (carrierAmplitude (d, level), n));
                                levels.push_back (level);
                            }

                            auto m = prepared (d);
                            const auto r = runModel (m, p, key)[0];

                            for (size_t s = 0; s < levels.size(); ++s)
                            {
                                const auto end = (s + 1) * (size_t) (settle + measure);
                                const auto got = mean (r, end - (size_t) measure, end);
                                const auto expected = ref::staticReductionDb (levels[s], t, inverseRatio, knee, range);
                                const auto error = std::abs (got - expected);
                                auto& worst = useSine ? worstSine : worstCarrier;
                                if (error > worst)
                                {
                                    worst = error;
                                    std::snprintf (useSine ? worstSineAt : worstCarrierAt, sizeof (worstSineAt), "T %g, R %g, knee %g, range %g, level %g",
                                                   t, ratio, knee, range, levels[s]);
                                }

                                INFO (nameOf (d) << (useSine ? ", sine" : ", carrier") << ", T " << t << ", ratio " << ratio << ", knee " << knee
                                                 << ", range " << range << ", level " << levels[s] << ": " << got << " for " << expected);
                                REQUIRE (error <= 0.25);
                            }
                        }

        std::printf ("VCA %s static curve, worst |error|: 1 kHz sine %.4f dB (%s), carrier %.4f dB (%s)\n", nameOf (d), worstSine,
                     worstSineAt, worstCarrier, worstCarrierAt);
    }
}

TEST_CASE ("VCA: the model's static curve is the specified one", "[vca][static]")
{
    // What the display, Auto Gain and Learn read (staticReduction) against the reference.
    for (const auto d : { Detector::peak, Detector::rms })
    {
        auto m = prepared (d);
        for (const auto t : { -60.0, -18.0, 0.0 })
            for (const auto ratio : { 1.0, 1.5, 2.0, 4.0, 10.0, 20.0, 0.0 })
                for (const auto knee : { 0.0, 6.0, 24.0 })
                    for (const auto range : { 0.0, 10.0, 40.0 })
                        for (auto level = t - 30.0; level <= t + 30.0; level += 0.25)
                        {
                            const auto inverseRatio = ratio > 0.0 ? 1.0 / ratio : 0.0;
                            const auto got = m.staticReduction (params (t, inverseRatio, knee, range, 10.0, 150.0), level);
                            REQUIRE_THAT (got, WithinAbs (ref::staticReductionDb (level, t, inverseRatio, knee, range), 1.0e-9));
                        }
    }
}

TEST_CASE ("VCA: the knee moves with the ratio, T - 6 dB/R", "[vca][static][coupling]")
{
    // NOTES.md, Gates, Coupling: hard knee, the carrier on a 0.01 dB level sweep from T − 5 to
    // T + 6 dB (60 ms a step, Attack 10 ms, Release 1 s); the knee is where the line through the
    // reductions between 0.5 and 2 dB meets 0, expected at T − 3, −1.5, −0.6, −0.3, 0 dB ±0.05.
    constexpr double t = -30.0;
    const auto stepLength = samplesFor (0.060, rate), measure = samplesFor (0.010, rate);

    for (const auto d : { Detector::peak, Detector::rms })
        for (const auto ratio : { 2.0, 4.0, 10.0, 20.0, 0.0 })
        {
            const auto inverseRatio = ratio > 0.0 ? 1.0 / ratio : 0.0;
            const auto p = params (t, inverseRatio, 0.0, 40.0, 10.0, 1000.0);

            Signal key;
            std::vector<double> levels;
            for (int s = 0; s <= 1100; ++s)
            {
                levels.push_back (t - 5.0 + 0.01 * s);
                append (key, carrier (carrierAmplitude (d, levels.back()), stepLength));
            }

            auto m = prepared (d);
            const auto r = runModel (m, p, key, nullptr, 512)[0];

            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            int count = 0;
            for (size_t s = 0; s < levels.size(); ++s)
            {
                const auto end = (s + 1) * (size_t) stepLength;
                const auto y = mean (r, end - (size_t) measure, end);
                if (y < 0.5 || y > 2.0)
                    continue;
                sx += levels[s];
                sy += y;
                sxx += levels[s] * levels[s];
                sxy += levels[s] * y;
                ++count;
            }

            const auto slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
            const auto knee = (sx - sy / slope) / count;
            const auto expected = ref::couplingKneeDb (t, inverseRatio);
            std::printf ("VCA %s coupling, Ratio %g: knee at T %+.3f dB (expected %+.2f), slope %.4f (expected %.4f)\n", nameOf (d), ratio,
                         knee - t, expected - t, slope, 1.0 - inverseRatio);
            INFO (nameOf (d) << ", ratio " << ratio << ": knee " << knee << " for " << expected);
            REQUIRE_THAT (knee, WithinAbs (expected, 0.05));
            REQUIRE_THAT (slope, WithinAbs (1.0 - inverseRatio, 0.01));
        }
}

namespace
{
    /** RBJ biquad, direct form I in double. */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        static Biquad pass (bool highPass, double hz, double q)
        {
            const auto w = 2.0 * std::numbers::pi * hz / rate, alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            const auto a0 = 1.0 + alpha;
            Biquad f;
            f.b0 = (highPass ? (1.0 + c) : (1.0 - c)) / 2.0 / a0;
            f.b1 = (highPass ? -(1.0 + c) : (1.0 - c)) / a0;
            f.b2 = f.b0;
            f.a1 = -2.0 * c / a0;
            f.a2 = (1.0 - alpha) / a0;
            return f;
        }

        double process (double x)
        {
            const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };

    /** Pink noise band-limited to 20 Hz … 20 kHz: fourth-order Butterworth high-pass and low-pass. */
    Signal bandLimitedPink (int n, std::uint32_t seed)
    {
        auto s = pinkNoise (n, seed);
        Biquad stages[] { Biquad::pass (true, 20.0, 0.54119610), Biquad::pass (true, 20.0, 1.3065630),
                          Biquad::pass (false, 20000.0, 0.54119610), Biquad::pass (false, 20000.0, 1.3065630) };
        for (auto& v : s)
            for (auto& f : stages)
                v = f.process (v);
        return s;
    }
}

TEST_CASE ("VCA: RMS on band-limited pink noise reads the curve at its level", "[vca][static][noise]")
{
    // NOTES.md, Gates, Static curve: pink noise band-limited to 20 Hz … 20 kHz, the mean
    // reduction in dB over 9 s after 1 s of settling within ±0.25 dB of the curve at the noise's
    // own RMS reading (M0: 6.004 dB for 6.000 at the mark, 6.017 at Attack 100 ms). Release at
    // 5.07 × Attack, below the detector's floor at Ratio 4, so the model is one time constant.
    constexpr double t = -30.0, inverseRatio = 0.25, depth = 6.0;
    const auto n = samplesFor (10.0, rate), skip = samplesFor (1.0, rate);
    auto key = bandLimitedPink (n, 11u);

    auto power = 0.0;
    for (int i = skip; i < n; ++i)
        power += key[(size_t) i] * key[(size_t) i];
    power /= (n - skip);
    const auto level = ref::levelForReductionDb (depth, t, inverseRatio);
    scale (key, std::sqrt (0.5 * std::pow (10.0, level / 10.0) / power));

    for (const auto attackMs : { ref::markAttackMs, 100.0 })
        for (const auto releaseMs : { 5.0667 * attackMs, 1000.0 })
        {
            auto m = prepared (Detector::rms);
            const auto r = runModel (m, params (t, inverseRatio, 0.0, 40.0, attackMs, releaseMs), key)[0];
            const auto got = mean (r, (size_t) skip, r.size());
            const auto gated = releaseMs < 1000.0;
            std::printf ("VCA RMS pink noise, Attack %g ms, Release %g ms: mean reduction %.3f dB for a %.3f dB curve%s\n", attackMs, releaseMs, got,
                         depth, gated ? "" : " (reported)");
            if (gated)
                REQUIRE_THAT (got, WithinAbs (depth, 0.25));
        }
}

TEST_CASE ("VCA: Peak at 50 and 100 Hz, reported", "[vca][static]")
{
    // NOTES.md, Gates: Peak's in-cycle droop at 50 Hz, −0.14 dB at Attack 10 ms, Release 100 ms
    // and −0.28 dB at 10/50 ms in M0's simulation. Reported, not gated; RMS beside it. Read two
    // ways over the last second: the mean reduction in dB, and the gain on the fundamental.
    constexpr double t = -30.0, inverseRatio = 0.25;
    for (const auto d : { Detector::peak, Detector::rms })
        for (const auto hz : { 50.0, 100.0 })
            for (const auto releaseMs : { 50.0, 100.0, 1000.0 })
            {
                const auto level = t + 10.0;
                const auto amplitude = amplitudeFor (level);
                const auto key = sine (amplitude, hz, rate, samplesFor (3.0, rate));
                auto m = prepared (d);
                const auto r = runModel (m, params (t, inverseRatio, 0.0, 40.0, 10.0, releaseMs), key)[0];
                const auto from = (size_t) samplesFor (2.0, rate);
                const auto got = mean (r, from, r.size());

                double s = 0.0, c = 0.0;
                for (auto i = from; i < r.size(); ++i)
                {
                    const auto y = key[i] * std::pow (10.0, -r[i] / 20.0);
                    s += y * std::sin (2.0 * std::numbers::pi * hz * (double) i / rate);
                    c += y * std::cos (2.0 * std::numbers::pi * hz * (double) i / rate);
                }
                const auto n = (double) (r.size() - from);
                const auto onFundamental = -20.0 * std::log10 (2.0 * std::sqrt (s * s + c * c) / n / amplitude);
                const auto expected = ref::staticReductionDb (level, t, inverseRatio, 0.0, 40.0);
                std::printf ("VCA %s, %g Hz, Attack 10 ms, Release %g ms: mean reduction %+.3f dB, on the fundamental %+.3f dB against the curve\n",
                             nameOf (d), hz, releaseMs, got - expected, onFundamental - expected);
            }
}

TEST_CASE ("VCA: the reduction is exactly 0 below the moving knee, at Ratio 1 and at Range 0", "[vca][static][idle]")
{
    const auto allZero = [] (const Trace& r) {
        return std::all_of (r.begin(), r.end(), [] (double v) { return isExactly (v, 0.0) && isExactly (gainFor (v), 1.0); });
    };

    for (const auto d : { Detector::peak, Detector::rms })
        for (const auto autoRelease : { false, true })
        {
            // Just under T_eff − W/2 on the detector's reading (not T − W/2): exactly 0.
            for (const auto ratio : { 1.5, 2.0, 4.0, 20.0, 0.0 })
                for (const auto knee : { 0.0, 6.0, 24.0 })
                {
                    constexpr double t = -30.0;
                    const auto inverseRatio = ratio > 0.0 ? 1.0 / ratio : 0.0;
                    const auto foot = ref::effectiveThresholdDb (t, inverseRatio) - 0.5 * knee;
                    auto m = prepared (d);
                    INFO (nameOf (d) << ", ratio " << ratio << ", knee " << knee);
                    REQUIRE (allZero (runModel (m, params (t, inverseRatio, knee, 40.0, 10.0, 150.0, autoRelease),
                                                carrier (carrierAmplitude (d, foot - 0.01), samplesFor (0.3, rate)))[0]));
                }

            // Ratio 1 at every Threshold, with a key far over it: the 6 dB shift must not leak.
            for (auto t = -60.0; t <= 0.0; t += 1.0)
            {
                auto m = prepared (d);
                REQUIRE (allZero (runModel (m, params (t, 1.0, 6.0, 40.0, 0.02, 5.0, autoRelease), carrier (amplitudeFor (0.0), samplesFor (0.05, rate)))[0]));
                m.reset();
                REQUIRE (allZero (runModel (m, params (t, 1.0, 0.0, 40.0, 10.0, 150.0, autoRelease), sine (1.0, 1000.0, rate, samplesFor (0.05, rate)))[0]));
            }

            auto m = prepared (d);
            REQUIRE (allZero (runModel (m, params (-60.0, 0.0, 6.0, 0.0, 0.02, 5.0, autoRelease), carrier (1.0, samplesFor (0.2, rate)))[0]));
        }
}

TEST_CASE ("VCA: Range caps the static reduction before the ballistics", "[vca][static][range]")
{
    // The core's Range row: Ratio ∞, 30 dB over, Range 10: settled at 10 dB ±0.05. Peak's attack
    // with Range engaged keeps the knob (the clamp sits before the ballistics); RMS's attack is
    // the detector's own rise, which crosses 10 dB long before it would reach 30, so it is reported.
    constexpr double t = -30.0;
    for (const auto d : { Detector::peak, Detector::rms })
    {
        const auto p = params (t, 0.0, 0.0, 10.0, 5.0, 150.0);
        auto key = silence (16);
        append (key, carrier (carrierAmplitude (d, t + 30.0), samplesFor (0.5, rate)));

        auto m = prepared (d);
        const auto r = runModel (m, p, key)[0];
        const auto attack = attackTime (r, 16, rate, r.back());
        std::printf ("VCA %s, Range 10 dB at 30 dB over: settled %.4f dB, attack %.3f ms\n", nameOf (d), r.back(), 1000.0 * attack);
        INFO (nameOf (d));
        REQUIRE_THAT (r.back(), WithinAbs (10.0, 0.05));
        if (d == Detector::peak)
            REQUIRE (withinTimeGate (attack, 0.005, rate));
    }
}

TEST_CASE ("VCA: Auto Gain reads the curve with the moving threshold", "[vca][static]")
{
    // M = r(−18 dB) with T_eff (NOTES.md here, 4. Make-up): at Threshold −18, Ratio 4, hard knee,
    // T_eff is −19.5 and a sine peaking at −18 dBFS is reduced by 1.5 × 0.75 dB, on both detectors.
    for (const auto d : { Detector::peak, Detector::rms })
    {
        const auto m = prepared (d);
        REQUIRE_THAT (autoGainDb (m, params (-18.0, 0.25, 0.0, 40.0, 10.0, 150.0)), WithinAbs (1.125, 1.0e-12));
        REQUIRE (isExactly (autoGainDb (m, params (-18.0, 1.0, 6.0, 40.0, 10.0, 150.0)), 0.0));
    }
}
