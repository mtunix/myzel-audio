// The dynamics core's blocks against dynamics/NOTES.md: the gain computer and its inverse, the
// release calibration h(ρ), the running maximum, the detectors, the key EQ and delay, the stereo
// matrix and link, and the exact 1.0.

#include "Harness.h"

#include "dsp/dynamics/Ballistics.h"
#include "dsp/dynamics/Detectors.h"
#include "dsp/dynamics/GainComputer.h"
#include "dsp/dynamics/Glide.h"
#include "dsp/dynamics/Stereo.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <deque>
#include <numbers>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using myzel::dsp::isExactly;

namespace
{
    constexpr double rate = 48000.0;

    // eq. 4 written out as the paper prints it, for comparison.
    double paperCurve (double x, double t, double ratio, double w)
    {
        if (2.0 * (x - t) < -w)
            return x;
        if (2.0 * std::abs (x - t) <= w)
            return x + (1.0 / ratio - 1.0) * (x - t + w / 2.0) * (x - t + w / 2.0) / (2.0 * w);
        return t + (x - t) / ratio;
    }
}

TEST_CASE ("Gain computer: eq. 4, its slope and the exact zeros", "[dynamics][curve]")
{
    for (const auto t : { -60.0, -30.0, -10.0 })
        for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, 20.0 })
            for (const auto w : { 0.0, 6.0, 24.0 })
                for (double x = t - 30.0; x <= t + 40.0; x += 0.25)
                {
                    const Curve c { t, 1.0 / ratio, w, 100.0 };
                    const auto expected = w > 0.0 ? x - paperCurve (x, t, ratio, w) : (x > t ? (x - t) * (1.0 - 1.0 / ratio) : 0.0);
                    REQUIRE_THAT (curveReduction (c, x), WithinAbs (expected, 1.0e-9));
                    REQUIRE (curveReduction (c, x) >= 0.0);

                    const auto h = 1.0e-5;
                    if (w > 0.0 || std::abs (x - t) > 2.0 * h)
                        REQUIRE_THAT (curveSlope (c, x), WithinAbs ((curveOutput (c, x + h) - curveOutput (c, x - h)) / (2.0 * h), 1.0e-5));
                }

    SECTION ("Ratio 1, Range 0 and below the knee give exactly 0")
    {
        for (double x = -100.0; x <= 20.0; x += 0.37)
        {
            REQUIRE (isExactly (curveReduction ({ -30.0, 1.0, 6.0, 40.0 }, x), 0.0));
            REQUIRE (isExactly (staticReduction ({ -30.0, 0.25, 6.0, 0.0 }, x), 0.0));
            if (x < -33.0)
                REQUIRE (isExactly (curveReduction ({ -30.0, 0.0, 6.0, 40.0 }, x), 0.0));
        }
    }

    SECTION ("∞:1 is flat above the knee, and Range caps")
    {
        const Curve c { -20.0, 0.0, 0.0, 10.0 };
        REQUIRE_THAT (curveOutput (c, -15.0), WithinAbs (-20.0, 1.0e-12));
        REQUIRE_THAT (staticReduction (c, 10.0), WithinAbs (10.0, 1.0e-12));
    }
}

TEST_CASE ("Gain computer: the closed-form inverse undoes eq. 4", "[dynamics][curve][feedback]")
{
    auto worst = 0.0;
    for (const auto ratio : { 1.0, 1.5, 2.0, 4.0, 20.0, 50.0, 1000.0 })
        for (const auto w : { 0.0, 0.01, 6.0, 24.0 })
            for (double x = -60.0; x <= 20.0; x += 0.05)
            {
                const Curve c { -30.0, 1.0 / ratio, w, 100.0 };
                const auto y = x - curveReduction (c, x);
                worst = std::max (worst, std::abs (curveInverse (c, y) - x));
                REQUIRE (feedbackDemand (c, y) >= 0.0);
            }

    std::printf ("Inverse curve: worst |G⁻¹(G(x)) − x| = %.2e dB\n", worst);
    REQUIRE (worst < 1.0e-9);

    SECTION ("Ratio 1 demands exactly nothing; the hard knee is eq. 18")
    {
        for (double y = -60.0; y <= 10.0; y += 0.3)
            REQUIRE (isExactly (feedbackDemand ({ -30.0, 1.0, 12.0, 40.0 }, y), 0.0));
        REQUIRE_THAT (feedbackDemand ({ -30.0, 0.25, 0.0, 40.0 }, -27.0), WithinAbs (9.0, 1.0e-12));
    }
}

TEST_CASE ("Gain computer: the 20 ms curve fade", "[dynamics][curve]")
{
    CurveFade fade;
    fade.prepare (rate);
    const Curve a { -18.0, 0.25, 6.0, 40.0 }, b { -38.0, 0.0, 6.0, 20.0 }, c { -10.0, 0.5, 0.0, 10.0 };
    fade.jumpTo (a);
    REQUIRE (isExactly (fade.next(), 1.0));
    REQUIRE_FALSE (fade.fading());

    // A raised cosine from a to b; c, set mid-way, waits and then fades in from b.
    fade.setTarget (b);
    const auto steps = samplesFor (0.020, rate);
    for (int i = 1; i < steps; ++i)
    {
        if (i == steps / 2)
            fade.setTarget (c);
        const auto w = fade.next();
        REQUIRE (fade.fading());
        REQUIRE (CurveFade::same (fade.source(), a));
        REQUIRE (CurveFade::same (fade.target(), b));
        REQUIRE_THAT (w, WithinAbs (0.5 - 0.5 * std::cos (std::numbers::pi * i / steps), 1.0e-12));
    }
    REQUIRE (isExactly (fade.next(), 1.0));
    REQUIRE_FALSE (fade.fading());
    REQUIRE (CurveFade::same (fade.finalCurve(), c));

    REQUIRE (fade.next() < 1.0e-5);
    REQUIRE (CurveFade::same (fade.source(), b));
    REQUIRE (CurveFade::same (fade.target(), c));
    for (int i = 1; i < steps; ++i)
        (void) fade.next();
    REQUIRE_FALSE (fade.fading());
    REQUIRE (isExactly (fade.target().inverseRatio, 0.5));

    // A target that returns to where the fade is going cancels the wait.
    fade.setTarget (a);
    (void) fade.next();
    fade.setTarget (b);
    fade.setTarget (a);
    for (int i = 0; i < 3 * steps; ++i)
        (void) fade.next();
    REQUIRE (CurveFade::same (fade.target(), a));
    REQUIRE (CurveFade::same (fade.source(), c));
}

TEST_CASE ("Glides: the smooth glide's value and slope never step, and it lands exactly", "[dynamics][curve]")
{
    SmoothGlide glide;
    glide.prepare (20.0, rate);
    glide.jumpTo (0.0);
    const auto steps = samplesFor (0.020, rate);

    // Retargets at a quarter and three quarters, one of them reversing the motion.
    std::vector<double> v { glide.current() };
    glide.setTarget (1.0);
    for (int i = 0; i < 3 * steps; ++i)
    {
        if (i == steps / 4)
            glide.setTarget (-0.5);
        if (i == steps + 3 * steps / 4)
            glide.setTarget (0.25);
        v.push_back (glide.next());
    }

    // A slope that stepped would show as a second difference of the order of 1/steps; a smooth
    // cubic's stays of the order of 1/steps².
    auto worst = 0.0;
    for (size_t i = 2; i < v.size(); ++i)
        worst = std::max (worst, std::abs (v[i] - 2.0 * v[i - 1] + v[i - 2]));
    INFO ("largest second difference x steps^2: " << worst * steps * steps);
    REQUIRE (worst * steps * steps < 20.0);
    REQUIRE (isExactly (v.back(), 0.25));
    REQUIRE_FALSE (glide.moving());
}

TEST_CASE ("Ballistics: h(ρ) matches NOTES.md's table, and the table its own solve", "[dynamics][ballistics]")
{
    // dynamics/NOTES.md, Feed-forward ballistics.
    const std::array<std::pair<double, double>, 10> notes { { { 1.01, 0.0100 }, { 1.1, 0.0952 }, { 1.25, 0.2235 }, { 1.5, 0.4287 },
                                                               { 2.0, 0.8652 }, { 3.0, 1.8294 }, { 5.0, 3.8676 }, { 10.0, 8.9397 },
                                                               { 30.0, 28.982 }, { 100.0, 98.995 } } };
    for (const auto& [rho, h] : notes)
    {
        REQUIRE_THAT (solveReleaseRatio (rho), WithinAbs (h, 0.6e-4 + 2.0e-5 * h));
        REQUIRE_THAT (releaseRatio (rho), WithinAbs (h, 0.6e-4 + 2.0e-3 * h));
    }

    auto worst = 0.0;
    for (double u = std::log10 (minimumReleaseRatio); u < 4.0; u += 0.0013)
    {
        const auto rho = std::pow (10.0, u);
        worst = std::max (worst, std::abs (releaseRatio (rho) / solveReleaseRatio (rho) - 1.0));
    }
    std::printf ("Release table: worst relative error of h against the solve, ρ 1.01 … 10⁴: %.3f %%\n", 100.0 * worst);
    REQUIRE (worst < 5.0e-4);

    REQUIRE_THAT (releaseRatio (0.5), WithinRel (releaseRatio (minimumReleaseRatio), 1.0e-12));
    REQUIRE_THAT (releaseRatio (2.0e4), WithinRel (2.0e4 - 1.0, 1.0e-12));
}

TEST_CASE ("Ballistics: the smooth decoupled cascade releases in the set time", "[dynamics][ballistics]")
{
    // A unit cascade on a reduction step: measured by NOTES.md's definition, the knob exactly
    // (the discrete check under Feed-forward ballistics: 149.99 ms for 150 ms at Attack 10 ms).
    auto worst = 0.0;
    for (const auto attackMs : { 0.1, 1.0, 10.0, 100.0, 300.0 })
        for (const auto releaseMs : { 5.0, 12.0, 50.0, 150.0, 500.0, 5000.0 })
        {
            const auto poles = SmoothDecoupled::polesFor (attackMs, releaseMs, rate);
            SmoothDecoupled d;
            d.reset (10.0);

            Trace r;
            const auto expected = std::max (releaseMs, minimumReleaseRatio * attackMs);
            for (int i = 0; i < samplesFor (4.0 * expected * 0.001, rate) + 16; ++i)
                r.push_back (d.process (0.0, poles));

            const auto t = releaseTime (r, 0, rate, 10.0) * 1000.0;
            worst = std::max (worst, std::abs (t / expected - 1.0));
            REQUIRE (withinTimeGate (t * 0.001, expected * 0.001, rate));
        }

    std::printf ("Smooth decoupled release: worst |measured/set − 1| = %.3f %%\n", 100.0 * worst);
    REQUIRE (worst < 0.01);
}

TEST_CASE ("Running maximum: the window's maximum for any length sequence", "[dynamics][lookahead]")
{
    std::mt19937 rng (7u);
    std::uniform_real_distribution<double> value (0.0, 1.0);
    std::uniform_int_distribution<int> jump (1, 400);

    RunningMax wedge;
    wedge.prepare (600);
    std::deque<double> history;

    int length = 1, target = 1;
    for (int i = 0; i < 200000; ++i)
    {
        if (i % 997 == 0)
            target = jump (rng);
        wedge.setLength (target);
        length = target < length ? target : std::min (target, length + 1);

        // A slow fade (the wedge's worst case) half the time, noise the other half.
        const auto v = (i / 5000) % 2 == 0 ? 1.0 - i * 1.0e-6 : value (rng);
        history.push_back (v);
        if (history.size() > 600)
            history.pop_front();

        const auto got = wedge.push (v);
        REQUIRE (wedge.getLength() == length);

        auto expected = 0.0;
        for (int k = 0; k < std::min (length, (int) history.size()); ++k)
            expected = std::max (expected, history[history.size() - 1 - (size_t) k]);
        REQUIRE (isExactly (got, expected));
    }
}

TEST_CASE ("Detectors: RMS on the detector scale, the crest factor and its guard", "[dynamics][detector]")
{
    SECTION ("A sine reads its peak on both detectors; the carrier reads 3.01 dB higher on RMS")
    {
        RmsDetector rms;
        rms.setTimeConstant (50.0, rate);
        for (const auto& x : sine (amplitudeFor (-20.0), 1000.0, rate, samplesFor (1.0, rate)))
            rms.process (x * x);
        REQUIRE_THAT (meanSquareDb (rms.meanSquare()), WithinAbs (-20.0, 0.01));

        rms.reset();
        for (const auto& x : carrier (amplitudeFor (-20.0), samplesFor (1.0, rate)))
            rms.process (x * x);
        REQUIRE_THAT (meanSquareDb (rms.meanSquare()), WithinAbs (-20.0 + carrierRmsOffsetDb, 1.0e-6));
        REQUIRE_THAT (peakDb (amplitudeFor (-20.0)), WithinAbs (-20.0, 1.0e-12));
        REQUIRE (isExactly (peakDb (0.0), silentDb));
        REQUIRE (isExactly (meanSquareDb (0.0), silentDb));
    }

    SECTION ("C² is 2 on a sine, 1 on the carrier, held through silence, 2 after a reset")
    {
        CrestFactor crest;
        crest.setTimeConstant (CrestFactor::defaultMs, rate);
        REQUIRE (isExactly (crest.crestSquared(), 2.0));

        for (const auto& x : sine (0.3, 1000.0, rate, samplesFor (1.5, rate)))
            crest.process (x * x);
        REQUIRE_THAT (crest.crestSquared(), WithinAbs (2.0, 0.02));

        crest.reset();
        for (const auto& x : carrier (0.3, samplesFor (6.0, rate)))
            crest.process (x * x);
        REQUIRE_THAT (crest.crestSquared(), WithinAbs (1.0, 1.0e-9));

        // Into silence both detectors fall together, then the guard holds; never denormal.
        for (int i = 0; i < samplesFor (30.0, rate); ++i)
        {
            crest.process (0.0);
            REQUIRE (std::fpclassify (crest.peak()) != FP_SUBNORMAL);
            REQUIRE (std::fpclassify (crest.meanSquare()) != FP_SUBNORMAL);
            if (i % 4800 == 0)
                REQUIRE_THAT (crest.crestSquared(), WithinAbs (1.0, 1.0e-9));
        }
        REQUIRE (isExactly (crest.meanSquare(), 0.0));
        REQUIRE_THAT (crest.crestSquared(), WithinAbs (1.0, 1.0e-9));
    }

    SECTION ("The mean square flushes instead of going denormal")
    {
        RmsDetector rms;
        rms.setTimeConstant (75.0, rate);
        rms.process (1.0);
        for (int i = 0; i < samplesFor (20.0, rate); ++i)
            REQUIRE (std::fpclassify (rms.process (0.0)) != FP_SUBNORMAL);
        REQUIRE (isExactly (rms.meanSquare(), 0.0));
    }
}

TEST_CASE ("Key EQ: Butterworth high- and low-pass, Simper's bell, flat when off", "[dynamics][keyeq]")
{
    const auto gainAt = [] (const KeyEqSettings& s, double hz) {
        KeyEq eq;
        eq.prepare (rate);
        eq.set (s);
        auto in = 0.0, out = 0.0;
        const auto x = sine (1.0, hz, rate, samplesFor (2.0, rate));
        for (size_t i = 0; i < x.size(); ++i)
        {
            auto y = x[i];
            eq.process (&y, 1);
            if (i > x.size() / 2)
            {
                in += x[i] * x[i];
                out += y * y;
            }
        }
        return 10.0 * std::log10 (out / in);
    };

    KeyEqSettings hp;
    hp.highPass = true;
    hp.highPassHz = 100.0;
    REQUIRE_THAT (gainAt (hp, 100.0), WithinAbs (-3.01, 0.02));
    REQUIRE_THAT (gainAt (hp, 50.0), WithinAbs (-12.30, 0.05));
    REQUIRE_THAT (gainAt (hp, 2000.0), WithinAbs (0.0, 0.01));

    KeyEqSettings lp;
    lp.lowPass = true;
    lp.lowPassHz = 4000.0;
    REQUIRE_THAT (gainAt (lp, 4000.0), WithinAbs (-3.01, 0.02));
    REQUIRE_THAT (gainAt (lp, 200.0), WithinAbs (0.0, 0.01));

    for (const auto db : { -18.0, 6.0, 18.0 })
    {
        KeyEqSettings bell;
        bell.bellHz = 1000.0;
        bell.bellDb = db;
        bell.bellQ = 2.0;
        REQUIRE_THAT (gainAt (bell, 1000.0), WithinAbs (db, 0.05));
        REQUIRE_THAT (gainAt (bell, 40.0), WithinAbs (0.0, 0.1));
    }

    KeyEq flat;
    flat.prepare (rate);
    flat.set ({});
    REQUIRE (flat.isFlat());
    for (const auto& x : pinkNoise (1000, 3u))
    {
        double frame[] { x, -x };
        if (! flat.isFlat())
            flat.process (frame, 2);
        REQUIRE (isExactly (frame[0], x));
        REQUIRE (isExactly (frame[1], -x));
    }
}

TEST_CASE ("Key EQ: a jump glides, so the key's level never steps", "[dynamics][keyeq][clicks]")
{
    // A Nyquist-rate carrier, which a TPT low-pass nulls at any cutoff: turning the low-pass off
    // used to hand the detector the full carrier within a sample. Each change below runs from a
    // settled state; the carrier's magnitude may move per sample by no more than a 20 ms
    // smoothstep over its whole amplitude would (1.5/960 of it), never at once.
    struct Change
    {
        const char* name;
        KeyEqSettings from, to;
    };
    KeyEqSettings lowPass, flat, highLowPass;
    lowPass.lowPass = true;
    lowPass.lowPassHz = 2000.0;
    highLowPass = lowPass;
    highLowPass.lowPassHz = 19000.0;
    const Change changes[] {
        { "low-pass 2 kHz -> Off", lowPass, flat },
        { "low-pass Off -> 2 kHz", flat, lowPass },
        { "low-pass 2 -> 19 kHz, then Off", lowPass, highLowPass },
    };

    for (const auto& change : changes)
    {
        KeyEq eq;
        eq.prepare (rate);
        eq.set (change.from);
        auto previous = -1.0, largest = 0.0, low = 1.0e9, high = 0.0;
        for (int i = 0; i < samplesFor (0.1, rate); ++i)
        {
            if (i == samplesFor (0.05, rate))
                eq.set (change.to);
            if (i == samplesFor (0.06, rate) && &change == &changes[2])
                eq.set (flat);
            double x = (i % 2 == 0) ? 0.5 : -0.5;
            if (! eq.isFlat())
                eq.process (&x, 1);
            const auto magnitude = std::abs (x);
            if (i > samplesFor (0.04, rate))
            {
                largest = std::max (largest, std::abs (magnitude - previous));
                low = std::min (low, magnitude);
                high = std::max (high, magnitude);
            }
            previous = magnitude;
        }
        INFO (change.name << ": magnitude " << low << " ... " << high << ", largest change per sample " << largest);
        REQUIRE (largest <= 1.5 * 0.5 / samplesFor (0.020, rate) * 1.1);
        REQUIRE (high - low > 1.0e-6);
    }
}

TEST_CASE ("Key delay: the look-ahead tap and its 50 ms glide", "[dynamics][lookahead]")
{
    KeyDelay delay;
    delay.prepare (1, samplesFor (0.012, rate));
    LinearGlide glide;
    glide.prepare (lookaheadGlideMs, rate);
    glide.jumpTo (0.0);

    const auto latency = samplesFor (0.010, rate);
    const auto target = (double) latency;
    glide.setTarget (target);

    // A ramp key, so the read value says where the tap is.
    const auto steps = samplesFor (lookaheadGlideMs * 0.001, rate);
    for (int i = 0; i < steps + 100; ++i)
    {
        const double frame[] { (double) i };
        delay.push (frame);
        const auto la = glide.next();
        const auto expectedLa = i + 1 >= steps ? target : target * (i + 1) / steps;
        REQUIRE_THAT (la, WithinAbs (expectedLa, 1.0e-9));

        if (i >= latency)
            REQUIRE_THAT (delay.read (0, latency - la), WithinAbs (i - (latency - la), 1.0e-9));
    }
    REQUIRE_FALSE (glide.moving());
}

TEST_CASE ("Stereo: layouts, the key matrix, the link and the gains", "[dynamics][stereo]")
{
    REQUIRE (layoutFor (StereoMode::linked, 2).detectorChannels == 2);
    REQUIRE (layoutFor (StereoMode::linked, 2).linked);
    REQUIRE_FALSE (layoutFor (StereoMode::unlinked, 2).linked);
    REQUIRE (layoutFor (StereoMode::mid, 2).detectorChannels == 1);
    REQUIRE (layoutFor (StereoMode::side, 2).detectorChannels == 1);
    REQUIRE (layoutFor (StereoMode::midSide, 2).detectorChannels == 2);
    REQUIRE (layoutFor (StereoMode::midSide, 1).detectorChannels == 1);

    double d[2] {};
    encodeKey (StereoMode::midSide, 0.75, 0.25, d);
    REQUIRE ((isExactly (d[0], 0.5) && isExactly (d[1], 0.25)));
    encodeKey (StereoMode::side, 0.75, 0.25, d);
    REQUIRE (isExactly (d[0], 0.25));

    SECTION ("(1 − λ)·r + λ·max: λ = 1 gives both the louder one to the bit, λ = 0 changes nothing")
    {
        std::mt19937 rng (11u);
        std::uniform_real_distribution<double> r (0.0, 40.0);
        for (int i = 0; i < 10000; ++i)
        {
            const auto a = r (rng), b = r (rng);
            double full[] { a, b }, none[] { a, b }, half[] { a, b };
            linkReductions (full, 1.0);
            linkReductions (none, 0.0);
            linkReductions (half, 0.5);
            REQUIRE ((isExactly (full[0], std::max (a, b)) && isExactly (full[1], std::max (a, b))));
            REQUIRE ((isExactly (none[0], a) && isExactly (none[1], b)));
            REQUIRE_THAT (half[0], WithinAbs (0.5 * a + 0.5 * std::max (a, b), 1.0e-12));
        }
    }

    SECTION ("Gains of exactly 1 leave the audio untouched in every mode; M/S gains act on M and S")
    {
        std::mt19937 rng (5u);
        std::uniform_real_distribution<float> x (-1.0f, 1.0f);
        constexpr int n = 4096;
        std::vector<float> l (n), r (n);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t) i] = x (rng);
            r[(size_t) i] = x (rng);
        }

        const std::vector<float> unity (n, 1.0f), half (n, 0.5f);
        for (const auto mode : { StereoMode::linked, StereoMode::unlinked, StereoMode::mid, StereoMode::side, StereoMode::midSide })
        {
            auto a = l, b = r;
            float* audio[] { a.data(), b.data() };
            const float* gains[] { unity.data(), unity.data() };
            applyGains (mode, audio, 2, gains, n);
            REQUIRE ((a == l && b == r));
        }

        auto a = l, b = r;
        float* audio[] { a.data(), b.data() };
        const float* gains[] { half.data(), unity.data() };
        applyGains (StereoMode::midSide, audio, 2, gains, n);
        for (int i = 0; i < n; ++i)
        {
            const auto m = 0.5f * (a[(size_t) i] + b[(size_t) i]);
            const auto s = 0.5f * (a[(size_t) i] - b[(size_t) i]);
            REQUIRE_THAT (m, WithinAbs (0.25 * (l[(size_t) i] + r[(size_t) i]), 1.0e-6));
            REQUIRE_THAT (s, WithinAbs (0.5 * (l[(size_t) i] - r[(size_t) i]), 1.0e-6));
        }
    }
}

TEST_CASE ("Idle: a reduction of exactly 0 is a gain of exactly 1, and tiny ones snap", "[dynamics][idle]")
{
    REQUIRE (isExactly (gainFor (0.0), 1.0));
    REQUIRE (isExactly (snapReduction (0.9e-6), 0.0));
    REQUIRE (isExactly (snapReduction (-0.9e-6), 0.0));
    REQUIRE (isExactly (snapReduction (2.0e-6), 2.0e-6));
    REQUIRE_THAT (gainFor (6.0), WithinRel (0.501187, 1.0e-5));
    REQUIRE_THAT (gainFor (-6.0), WithinRel (1.995262, 1.0e-5));
}
