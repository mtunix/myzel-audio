// Component-level tests for the Reel model: the Jiles–Atherton tape core and the record amp.

#include "dsp/models/reel/ReelHysteresis.h"
#include "dsp/models/reel/ReelRecordAmp.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

using umber::dsp::reel::RecordAmpCurve;
using umber::dsp::reel::TapeHysteresis;
using umber::dsp::reel::TriodeStage;

namespace
{
    struct Loop
    {
        std::vector<double> h, m;
    };

    /** Drives the core with a sine for `periods` periods (`perPeriod` samples each). */
    Loop drive (TapeHysteresis& tape, double amp, int perPeriod, int periods)
    {
        Loop loop;
        for (int i = 0; i <= perPeriod * periods; ++i)
        {
            const auto h = amp * std::sin (2.0 * std::numbers::pi * i / perPeriod);
            loop.h.push_back (h);
            loop.m.push_back (tape.process (h));
        }
        return loop;
    }
} // namespace

TEST_CASE ("Reel tape: Langevin function matches its closed form and series", "[reel][component]")
{
    for (double q : { -30.0, -3.0, -0.5, -0.1001, -0.0999, -1.0e-3, 1.0e-6, 0.05, 0.0999, 0.1001, 0.7, 4.0, 40.0 })
    {
        double L, dL;
        TapeHysteresis::langevin (q, L, dL);
        const auto refL  = 1.0 / std::tanh (q) - 1.0 / q;
        const auto refDL = 1.0 / (q * q) - 1.0 / (std::sinh (q) * std::sinh (q));
        INFO ("q " << q);
        CHECK (std::abs (L - refL) < 1.0e-9);
        if (std::abs (q) > 1.0e-3)   // the reference itself cancels catastrophically below this
            CHECK (std::abs (dL - refDL) < 1.0e-7);
        CHECK (dL > 0.0);
        CHECK (dL <= 1.0 / 3.0 + 1.0e-15);
    }

    double L0, dL0;
    TapeHysteresis::langevin (0.0, L0, dL0);
    CHECK (std::fpclassify (L0) == FP_ZERO);
    CHECK (std::abs (dL0 - 1.0 / 3.0) < 1.0e-15);
}

TEST_CASE ("Reel tape: hysteresis loop sanity", "[reel][component]")
{
    for (double amp : { 0.3, 1.0, 3.0, 10.0 })
    {
        INFO ("amplitude " << amp);
        TapeHysteresis tape;
        constexpr int per = 400;
        const auto loop = drive (tape, amp, per, 12);   // JA minor loops accommodate over a few cycles

        // 1. magnetisation moves with the field (dM/dH >= 0 on every step) and stays in [-1, 1]
        bool followsField = true, inRange = true;
        for (size_t i = 1; i < loop.h.size(); ++i)
        {
            followsField = followsField && (loop.m[i] - loop.m[i - 1]) * (loop.h[i] - loop.h[i - 1]) >= 0.0;
            inRange = inRange && std::abs (loop.m[i]) <= 1.0;
        }
        CHECK (followsField);
        CHECK (inRange);

        // 2. steady state: the last two periods trace the same closed loop
        const size_t a = (size_t) (10 * per), b = (size_t) (11 * per);
        double drift = 0.0;
        for (size_t i = 0; i < (size_t) per; ++i)
            drift = std::max (drift, std::abs (loop.m[a + i] - loop.m[b + i]));
        CHECK (drift < 1.0e-6);

        // 3. a real loop: on the falling branch m(h = 0) is above the rising branch's (remanence)
        const auto descendingZero = loop.m[b + per / 2];       // h crosses 0 going down
        const auto ascendingZero  = loop.m[b + per];           // h crosses 0 going up
        CHECK (descendingZero > 0.0);
        CHECK (ascendingZero < 0.0);
        CHECK (descendingZero - ascendingZero > 1.0e-4);

        // 4. odd symmetry: m(t + T/2) == -m(t)
        double asym = 0.0;
        for (size_t i = 0; i < (size_t) per / 2; ++i)
            asym = std::max (asym, std::abs (loop.m[b + i] + loop.m[b + i + per / 2]));
        CHECK (asym < 1.0e-6);
    }

    // saturation: approaches (but never exceeds) Ms
    TapeHysteresis tape;
    const auto big = drive (tape, 200.0, 4000, 2);
    double peak = 0.0;
    for (auto m : big.m) peak = std::max (peak, m);
    CHECK (peak > 0.98);
    CHECK (peak <= 1.0);
}

TEST_CASE ("Reel tape: small-signal susceptibility and rate independence", "[reel][component]")
{
    // tiny excursion from the demagnetised state follows the initial (reversible) susceptibility
    TapeHysteresis tape;
    const auto chi = tape.initialSusceptibility();
    tape.process (1.0e-4);
    CHECK (std::abs (tape.magnetisation() / 1.0e-4 - chi) < 1.0e-4 * chi);

    // JA is rate-independent: the same loop sampled 8x more finely ends at the same place
    for (double amp : { 0.5, 2.0, 8.0 })
    {
        TapeHysteresis coarse, fine;
        const auto c = drive (coarse, amp, 64, 3);
        const auto f = drive (fine, amp, 512, 3);
        double err = 0.0;
        for (size_t i = 0; i < c.m.size(); ++i)
            err = std::max (err, std::abs (c.m[i] - f.m[i * 8]));
        INFO ("amplitude " << amp << " max diff " << err);
        CHECK (err < 2.0e-3);
    }
}

TEST_CASE ("Reel tape: bounded work and finite output for absurd field steps", "[reel][component]")
{
    TapeHysteresis tape;
    std::mt19937 rng (7);
    std::uniform_real_distribution<double> dist (-1.0e6, 1.0e6);
    bool ok = true;
    for (int i = 0; i < 20000; ++i)
    {
        const auto m = tape.process (dist (rng));
        ok = ok && std::isfinite (m) && std::abs (m) <= 1.0;
    }
    CHECK (ok);

    tape.process (std::numeric_limits<double>::quiet_NaN());
    CHECK (std::isfinite (tape.magnetisation()));
}

TEST_CASE ("Reel record amp: table matches the direct circuit solve", "[reel][component]")
{
    const auto& curve = RecordAmpCurve::instance();
    CHECK (curve.allConverged());

    const auto q = TriodeStage::quiescent();
    REQUIRE (q.converged);
    INFO ("Q point: Vp " << q.plate << " V, Vk " << q.cathode << " V");
    CHECK (q.plate > 120.0);
    CHECK (q.plate < 220.0);
    CHECK (q.cathode > 0.5);
    CHECK (q.cathode < 2.5);

    // gain of an unbypassed-cathode 12AX7 stage into 100k || 470k: roughly mu Rl / (Rl + rp + (mu+1) Rk)
    CHECK (curve.smallSignalGain() < -20.0);
    CHECK (curve.smallSignalGain() > -40.0);

    std::mt19937 rng (99);
    std::uniform_real_distribution<double> dist (-RecordAmpCurve::inputRange, RecordAmpCurve::inputRange);
    double worst = 0.0;
    for (int i = 0; i < 300; ++i)
    {
        const auto vin = dist (rng);
        TriodeStage::Solution guess = q;
        // walk the direct solve to vin in small steps so NR converges from a good start
        for (int s = 1; s <= 64; ++s)
            guess = TriodeStage::solve (vin * s / 64.0, guess);
        REQUIRE (guess.converged);
        worst = std::max (worst, std::abs ((guess.plate - curve.quiescentPlateVolts()) - curve.process (vin)));
    }
    INFO ("worst table error " << worst << " V");
    CHECK (worst < 1.0e-6);

    // inverting, monotone, physically bounded swing
    double previous = curve.process (-RecordAmpCurve::inputRange);
    bool monotone = true;
    for (double v = -RecordAmpCurve::inputRange; v <= RecordAmpCurve::inputRange; v += 0.01)
    {
        const auto y = curve.process (v);
        monotone = monotone && y <= previous + 1.0e-9;
        previous = y;
    }
    CHECK (monotone);
    CHECK (std::fpclassify (curve.process (0.0)) == FP_ZERO);
    CHECK (curve.process (-1.0e6) < TriodeStage::supply);
    CHECK (curve.process (1.0e6) > -curve.quiescentPlateVolts());
}
