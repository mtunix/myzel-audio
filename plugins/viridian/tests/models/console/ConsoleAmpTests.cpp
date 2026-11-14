// The Console's NE5534 stages: common/dsp/OpAmpClip (the loop at the chip's loop gain, its clip
// at 8x with antiderivative antialiasing) against the exact loop, a fine simulation and itself.

#include "dsp/models/console/ConsoleModel.h"

#include "common/dsp/OpAmpClip.h"
#include "common/dsp/Oversampler2x.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

using namespace viridian::dsp;
using myzel::dsp::OpAmpClip;
using myzel::dsp::OpAmpLoop;

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    OpAmpClip prepared (const myzel::dsp::LineAmpParams& p, double rate)
    {
        OpAmpClip a;
        a.setParams (p);
        a.prepare (rate);
        return a;
    }

    /** A signal that dwells below the rails, passes the knee slowly and quickly, and clips hard. */
    double testSignal (int n, double rate)
    {
        const double t = n / rate;
        const double envelope = 0.5 + 0.5 * std::sin (twoPi * 0.9 * t);
        return 16.0 * (1.6 * envelope * envelope * envelope * std::sin (twoPi * 1234.5 * t)
                       + (n % 40000 < 3000 ? 4.0 * std::sin (twoPi * 7000.0 * t) : 0.0) + 0.02 * std::sin (twoPi * 20.0 * t));
    }
} // namespace

TEST_CASE ("Console amps: the loop's tables follow the exact loop", "[console][amp]")
{
    for (const auto& p : { ConsoleModel::inputAmpParams(), ConsoleModel::summingAmpParams() })
    {
        const auto loop = OpAmpLoop::get (p.loopGain);
        double worstTotal = 0.0, worstSmooth = 0.0;
        for (int i = 0; i <= 400000; ++i)
        {
            const double q = 3.0 * i / 400000.0;
            const auto e = OpAmpLoop::solve (q, p.loopGain, q > 1.0);
            const double d = loop->smoothPart (q) + loop->kneePart (q).d;
            worstTotal = std::max (worstTotal, std::abs (d - e.d));
            if (q <= OpAmpLoop::inner)
                worstSmooth = std::max (worstSmooth, std::abs (loop->smoothPart (q) - e.d));
        }
        INFO ("loop gain " << p.loopGain << ": d within " << worstTotal << " of the swing, below the window " << worstSmooth);
        CHECK (worstTotal < 1.0e-8);
        CHECK (worstSmooth < 1.0e-12);

        // The closed loop itself, by bisection in s on q = s + c (s / sqrt (1 - s^2) - s) in long double.
        for (const double q : { 0.1, 0.485, 0.9, 1.0, 1.01, 1.5, 4.0 })
        {
            const auto e = OpAmpLoop::solve (q, p.loopGain, q > 1.0);
            const long double c = 1.0L / (1.0L + (long double) p.loopGain);
            long double lo = 0.0L, hi = 1.0L;
            for (int it = 0; it < 200; ++it)
            {
                const long double s = 0.5L * (lo + hi);
                const long double oneMinus = (1.0L - s) * (1.0L + s);
                (s + c * (s / std::sqrt (oneMinus) - s) < (long double) q ? lo : hi) = s;
            }
            const long double s = 0.5L * (lo + hi);
            INFO ("q " << q << ": d " << e.d << ", bisection " << (double) (s - std::min (1.0L, (long double) q)));
            CHECK (std::abs ((long double) e.d - (s - std::min (1.0L, (long double) q))) < 1.0e-14L);
            CHECK (std::abs (e.s - std::min (q, 1.0) - e.d) < 1.0e-12);
        }
    }
}

TEST_CASE ("Console amps: the knee's antiderivatives are its own", "[console][amp]")
{
    // The kernel is exact for the interpolated d_k only if D1' = d_k and D2' = D1 to rounding.
    for (const auto& p : { ConsoleModel::inputAmpParams(), ConsoleModel::summingAmpParams() })
    {
        const auto loop = OpAmpLoop::get (p.loopGain);
        double worst1 = 0.0, worst2 = 0.0;
        const double h = 1.0e-6;
        for (int i = 1; i < 6000; ++i)
        {
            const double q = 0.45 + 2.5 * i / 6000.0;
            const auto mid = loop->kneePart (q);
            const auto far = [&] (double x) { return loop->kneePart (x).far ? loop->farIntegrals (x) : loop->kneePart (x); };
            const auto flo = far (q - h), fhi = far (q + h);
            worst1 = std::max (worst1, std::abs ((fhi.d1 - flo.d1) / (2.0 * h) - mid.d));
            worst2 = std::max (worst2, std::abs ((fhi.d2 - flo.d2) / (2.0 * h) - far (q).d1));
        }
        INFO ("loop gain " << p.loopGain << ": D1' - d " << worst1 << ", D2' - D1 " << worst2);
        CHECK (worst1 < 1.0e-6);
        CHECK (worst2 < 1.0e-8);
    }
}

TEST_CASE ("Console amps: near a rail the knee is sampled only where it is nearly linear", "[console][amp]")
{
    // Within sampledSpan of its distance from the rail a segment takes the triangle on d_k's values
    // at its knots instead of on d_k itself; the two must agree there (lines through the knots).
    using Lane = myzel::dsp::opamp_detail::Lane;
    for (const auto& p : { ConsoleModel::inputAmpParams(), ConsoleModel::summingAmpParams() })
    {
        const auto loop = OpAmpLoop::get (p.loopGain);
        Lane lane;
        lane.loop = loop.get();
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        double worst = 0.0;
        for (int n = 0; n < 400000; ++n)
        {
            const bool below = n % 2 == 0;
            const double edge = below ? Lane::sampledBelow + (1.0 - Lane::sampledBelow) * u (rng) : 1.0 + (Lane::sampledBeyond - 1.0) * u (rng);
            const double span = Lane::sampledSpan * std::abs (1.0 - edge) * u (rng);
            double x0 = edge, x1 = below ? edge - span : edge + span;
            if (u (rng) < 0.5)
                std::swap (x0, x1);
            const auto k0 = lane.kneeAt (x0), k1 = lane.kneeAt (x1);
            double a = 0.0, b = 0.0;
            lane.kneePieces (0.0, 1.0, x0, k0, x1, k1, a, b);
            worst = std::max ({ worst, std::abs (a - (2.0 * k0.d + k1.d) / 6.0), std::abs (b - (k0.d + 2.0 * k1.d) / 6.0) });
        }
        INFO ("loop gain " << p.loopGain << ": sampled against integrated, within " << worst << " of the swing");
        CHECK (worst < 2.0e-5);
    }
}

TEST_CASE ("Console amps: below the window the stage is the delayed loop, exactly", "[console][amp]")
{
    // The window's start over the up FIRs' and the prefilter's largest overshoot (0.315 of the swing),
    // and at 384 kHz the slew's knee (0.25).
    for (const auto rate : { 88200.0, 192000.0, 384000.0 })
    {
        auto a = prepared (ConsoleModel::summingAmpParams(), rate);
        std::vector<double> in (20000);
        for (size_t n = 0; n < in.size(); ++n)
            in[n] = 16.0 * 0.19 * std::sin (twoPi * 997.0 * (double) n / rate);
        double worst = 0.0;
        for (size_t n = 0; n < in.size(); ++n)
        {
            const double y = a.process (in[n]);
            CHECK_FALSE (a.isComputingResidual());
            if (n >= (size_t) OpAmpClip::latencySamples)
                worst = std::max (worst, std::abs (y - a.staticTransfer (in[n - (size_t) OpAmpClip::latencySamples])));
        }
        INFO (rate << " Hz: " << worst << " V");
        CHECK (worst < 1.0e-12);
    }
}

TEST_CASE ("Console amps: the residual computed only when needed is the residual computed always, to the bit", "[console][amp]")
{
    for (const auto rate : { 88200.0, 96000.0, 384000.0 })
        for (auto p : { ConsoleModel::inputAmpParams(), ConsoleModel::summingAmpParams() })
            for (const double negSwing : { 1.0, 0.92 })
            {
                p.negSwing = negSwing;
                auto adaptive = prepared (p, rate);
                auto always = prepared (p, rate);
                always.setAlwaysActive (true);
                int differ = 0, active = 0;
                const int n = (int) (1.5 * rate);
                for (int i = 0; i < n; ++i)
                {
                    const double x = testSignal (i, rate);
                    differ += std::bit_cast<std::uint64_t> (adaptive.process (x)) != std::bit_cast<std::uint64_t> (always.process (x)) ? 1 : 0;
                    active += adaptive.isComputingResidual() ? 1 : 0;
                }
                INFO (rate << " Hz, loop gain " << p.loopGain << ", negative swing " << negSwing << ": " << active << " of " << n << " samples computed it");
                CHECK (differ == 0);
                CHECK (active > n / 20);
                CHECK (active < n);
            }
}

TEST_CASE ("Console amps: a slow clipped tone follows the static loop, delayed", "[console][amp]")
{
    // Kernel, compensation and the linear-phase path together: at 20 Hz the clip's corners are
    // smoothed by far less than a sample, so the output is the static loop of the input 22 samples ago.
    for (const auto& p : { ConsoleModel::inputAmpParams(), ConsoleModel::summingAmpParams() })
        for (const double amplitude : { 0.95, 1.02, 3.0 })
        {
            const double rate = 88200.0;
            auto a = prepared (p, rate);
            double worst = 0.0;
            const int n = 2 * (int) rate;
            std::vector<double> in ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                in[(size_t) i] = 16.0 * amplitude * std::sin (twoPi * 20.0 * i / rate);
                const double y = a.process (in[(size_t) i]);
                if (i > n / 2)
                    worst = std::max (worst, std::abs (y - a.staticTransfer (in[(size_t) (i - OpAmpClip::latencySamples)])));
            }
            INFO ("loop gain " << p.loopGain << ", " << amplitude << " of the swing: " << worst << " V");
            CHECK (worst < 2.0e-3 * amplitude);
        }
}

TEST_CASE ("Console amps: the rails and the knee", "[console][amp]")
{
    // The static loop: unity gain inside, the rail at +-16 V, the knee about (1 / (sqrt 2 (1 + L)))^(2/3)
    // below the rail at its edge (NOTES.md, "Amplifier").
    for (const auto& p : { ConsoleModel::inputAmpParams(), ConsoleModel::summingAmpParams() })
    {
        const auto a = prepared (p, 96000.0);
        const double knee = -OpAmpLoop::solve (1.0, p.loopGain).d;
        INFO ("loop gain " << p.loopGain << ": knee " << knee);
        CHECK (std::abs (knee / std::pow (1.0 / (std::sqrt (2.0) * (1.0 + p.loopGain)), 2.0 / 3.0) - 1.0) < 0.15);
        CHECK (std::abs (a.staticTransfer (1.0e-6) - 1.0e-6) < 1.0e-15);
        CHECK (std::abs (a.staticTransfer (16.0) - 16.0 * (1.0 - knee)) < 16.0 * 1.0e-8);
        CHECK (std::abs (a.staticTransfer (-16.0) + 16.0 * (1.0 - knee)) < 16.0 * 1.0e-8);
        CHECK (a.staticTransfer (1.0e6) <= 16.0);
        CHECK (a.staticTransfer (1.0e6) > 16.0 - 1.0e-9);
        CHECK (a.staticTransfer (-1.0e6) >= -16.0);
    }
}

TEST_CASE ("Console amps: the slew rate holds", "[console][amp]")
{
    // A square (band-limited by a 2x upsampler, as the chain feeds the stage) into a stage slowed
    // to 0.5 V/us, so the slew rather than the band sets the edge: 10 ... 90 % of the 32 V swing
    // in 51 us, and no faster than the band allows.
    auto p = ConsoleModel::inputAmpParams();
    p.slewRate = 0.5e6;
    const double rate = 384000.0;
    auto a = prepared (p, rate);
    myzel::dsp::Oversampler2x up;
    std::vector<double> y (20000);
    for (size_t i = 0; i < y.size(); i += 2)
    {
        double y0, y1;
        up.upsample ((i / 2000) % 2 == 0 ? 40.0 : -40.0, y0, y1);
        y[i] = a.process (y0);
        y[i + 1] = a.process (y1);
    }
    double fastest = 0.0;
    for (size_t i = 100; i < y.size(); ++i)
        fastest = std::max (fastest, std::abs (y[i] - y[i - 1]) * rate);
    size_t from = 2000, at10 = 0, at90 = 0;
    for (size_t i = from; i < y.size() && (at10 == 0 || at90 == 0); ++i)
    {
        if (at10 == 0 && y[i] < 16.0 - 0.1 * 32.0)
            at10 = i;
        if (at90 == 0 && y[i] < 16.0 - 0.9 * 32.0)
            at90 = i;
    }
    const double riseUs = 1.0e6 * (double) (at90 - at10) / rate;
    INFO ("fastest " << fastest * 1.0e-6 << " V/us, 10 ... 90 % in " << riseUs << " us");
    CHECK (fastest < p.slewRate * 1.06);     // the band-limited corner of a slewing edge overshoots by 5 %
    CHECK (std::abs (riseUs / (0.8 * 32.0 / 0.5) - 1.0) < 0.03);
}

TEST_CASE ("Console amps: the slewed clip on a cubic segment is solved exactly", "[console][amp]")
{
    // Random segments of the input's spline, inside and across the rails, slower and faster than
    // the limit, against a fine time-step simulation of the clamp followed by a hard rate limiter.
    // The fast path's closed-form shares equal the exact path's where nothing clips or slews.
    using namespace myzel::dsp::opamp_detail;
    for (const double rate : { 88200.0, 192000.0 })
    {
        const auto loop = OpAmpLoop::get (550.0);
        Lane l;
        l.loop = loop.get();
        l.lo = -0.92;
        l.slew = 6.0e6 / (4.0 * rate * 16.0);
        std::mt19937 rng (3);
        std::uniform_real_distribution<double> u (-1.0, 1.0);
        constexpr int steps = 20000;
        double worstExact = 0.0, worstFast = 0.0;
        int nonlinear = 0, linear = 0;
        for (int k = 0; k < 4000; ++k)
        {
            const double scale = k % 2 == 1 ? 0.9 : 1.6, spread = k % 3 == 0 ? 3.0 : 0.3, base = scale * u (rng);
            double c[4];
            for (auto& v : c)
                v = base + spread * l.slew * u (rng);
            const double a[4] { node (c[0], c[1], c[2]), 0.5 * (c[2] - c[0]), 0.5 * (c[0] - 2.0 * c[1] + c[2]), (3.0 * (c[1] - c[2]) + c[3] - c[0]) / 6.0 };

            double held = std::clamp (Lane::eval (a, 0.0), l.lo, 1.0), m[4] {};
            const double dt = 1.0 / steps;
            for (int i = 0; i < steps; ++i)
            {
                const double t = (i + 0.5) * dt, target = std::clamp (Lane::eval (a, t), l.lo, 1.0);
                held += std::clamp (target - held, -l.slew * dt, l.slew * dt);
                double power = dt * held;
                for (auto& moment : m)
                {
                    moment += power;
                    power *= t;
                }
            }

            l.tracking = true;
            l.held = std::clamp (Lane::eval (a, 0.0), l.lo, 1.0);
            l.numCrossings = 0;
            Moments exact;
            l.exactSegment (a, exact);
            for (size_t j = 0; j < 4; ++j)
                worstExact = std::max (worstExact, std::abs (exact.m[j] - m[j]));
            if (exact.nonlinear)
                ++nonlinear;
            else
            {
                ++linear;
                const auto s1 = Shares::of (exact), s2 = Shares::cubic (c);
                worstFast = std::max ({ worstFast, std::abs (s1.newest - s2.newest), std::abs (s1.third - s2.third), std::abs (s1.second - s2.second),
                                        std::abs (s1.oldest - s2.oldest) });
            }
        }
        // The simulation's own step error is of the order of the limit per step.
        INFO (rate << " Hz: exact against the simulation " << worstExact << " (its step " << l.slew / steps << "), fast against exact "
                   << worstFast << "; " << nonlinear << " segments clipped or slewed, " << linear << " not");
        CHECK (worstExact < l.slew / steps);
        CHECK (worstFast < 1.0e-13);
        CHECK (nonlinear > 1000);
        CHECK (linear > 500);
    }
}

TEST_CASE ("Console amps: two channels in one stage are two stages, to the bit", "[console][amp]")
{
    // Left loud in the first half, right in the second, a burst on both near the end: the residual
    // runs for both lanes while either needs it, which must not change a bit of either.
    for (const double loopGain : { 2200.0, 550.0 })
        for (const double rate : { 88200.0, 192000.0 })
            for (const bool always : { false, true })
            {
                const myzel::dsp::LineAmpParams p { 1.0, loopGain, 16.0, 1.0, 6.0e6 };
                myzel::dsp::OpAmpClipLanes<2> stereo;
                stereo.setParams (p);
                stereo.prepare (rate);
                stereo.setAlwaysActive (always);
                auto left = prepared (p, rate), right = prepared (p, rate);
                int differ = 0;
                const int n = (int) (2.0 * rate);
                for (int i = 0; i < n; ++i)
                {
                    const double t = i / rate;
                    const double el = (i < n / 2 ? 1.6 : 0.1) + (i > 9 * n / 10 ? 2.0 : 0.0), er = (i >= n / 2 ? 1.4 : 0.05) + (i > 9 * n / 10 ? 3.0 : 0.0);
                    const double x[2] { 16.0 * el * std::sin (twoPi * 7000.0 * t), 16.0 * er * std::sin (twoPi * 3100.0 * t + 0.3) };
                    double y[2];
                    stereo.processFrame (x, y);
                    differ += std::bit_cast<std::uint64_t> (y[0]) != std::bit_cast<std::uint64_t> (left.process (x[0])) ? 1 : 0;
                    differ += std::bit_cast<std::uint64_t> (y[1]) != std::bit_cast<std::uint64_t> (right.process (x[1])) ? 1 : 0;
                }
                INFO ("loop gain " << loopGain << ", " << rate << " Hz, " << (always ? "always" : "adaptive") << ": " << differ << " samples differ");
                CHECK (differ == 0);
            }
}
