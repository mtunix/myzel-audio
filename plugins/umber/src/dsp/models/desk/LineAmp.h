#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace umber::dsp::desk
{

/**
    Line-amplifier gain stage: a discrete op-amp in non-inverting configuration with modest loop
    gain and a class-AB output stage that runs into its rails softly.

        x ──(+)── A ──[ output stage S ]──┬── y
             │-                           │
             └────────── beta ────────────┘

        y = Vr * S(u),   u = (A / Vr) (x - beta y),   loop gain L = A beta,
        closed-loop gain G = A / (1 + L)

    S is the normalised output-stage swing: unity slope at the origin and a soft knee into the
    rails, S = u / sqrt(1 + (u/ceil)^2), ceil = 1 on the positive side and un < 1 on the negative
    side (quasi-complementary output stage: the pull-down half saturates a little earlier). The
    asymmetry is tiny at low level (H2 well below H3) and grows as the stage is pushed into the
    rails; with the feedback the clip gets firmer than the open-loop curve.

    The loop is memoryless (its closed-loop bandwidth is far above audio), so its exact solution
    s(w) of u + L S(u) = w (w = A x / Vr; strictly monotone, slope between 1 and 1 + L) is
    tabulated once per (L, un) in a shared table and read back with cubic Hermite interpolation
    (values + exact slopes at the nodes, error < 1e-8 of full swing). Beyond it (> ~+20 dB over
    the clip point) a second Hermite table in z = range / |w| covers the far field out to
    infinity (s approaches the rail smoothly in 1/w; error < 1e-9), so the audio path never
    iterates. The bracketed Newton solver below builds both tables.

    Slew limiting: the input pair's tail current charging the compensation capacitor caps
    |dy/dt| at the slew rate. Modelled with infinite small-signal bandwidth: the output follows the
    loop exactly until the per-sample step approaches SR*T, then rounds smoothly (quadratic knee)
    into a hard rate limit. Rate independent below the limit.
*/
struct LineAmpParams
{
    double gain;      // closed-loop small-signal gain G
    double loopGain;  // L = A beta
    double rail;      // Vr: positive swing limit (V)
    double negSwing;  // un: negative swing as a fraction of Vr
    double slewRate;  // V/s
};

/** Closed-loop transfer s(w) of the line amp: exact solver plus its shared lookup table. */
class LineAmpCurve
{
public:
    static constexpr double range = 16.0;
    static constexpr double stepsPerUnit = 16.0;
    static constexpr int size = (int) (2.0 * range * stepsPerUnit) + 2;

    struct Point { double s, ds; };

    /** Normalised output stage S = v / sqrt(1 + (v/ceil)^2), ceil = 1 (positive) or un. */
    static Point outputStage (double v, double un) noexcept
    {
        const double r = v >= 0.0 ? v : v / un;
        const double q = 1.0 / std::sqrt (1.0 + r * r);
        return { v * q, q * q * q };
    }

    /** Solves u + L S(u) = w; returns s = S(u) and ds/dw. Safeguarded Newton (bisection when a
        step leaves the bracket or stalls), bounded to 100 iterations. Beyond |w| > 2 the
        asymptotic start is on the side from which Newton converges monotonically (g is concave
        for u > 0 and convex for u < 0), so it takes 2-4 iterations there. Table building only. */
    static Point solve (double w, double L, double un) noexcept
    {
        double lo = w - L;
        double hi = w + L * un;
        double v  = std::abs (w) > 2.0 ? (w > 0.0 ? lo : hi) : w / (1.0 + L);
        double gPrev = 1.0e300;
        Point st {};

        for (int it = 0; it < 100; ++it)
        {
            st = outputStage (v, un);
            const double g = v + L * st.s - w;

            if (g > 0.0) hi = v;
            else         lo = v;

            double next = v - g / (1.0 + L * st.ds);
            if (! (next >= lo && next <= hi) || std::abs (g) > 0.5 * gPrev)
                next = 0.5 * (lo + hi);

            gPrev = std::abs (g);

            if (std::abs (next - v) <= 1.0e-15 * (1.0 + std::abs (v)))
            {
                v = next;
                st = outputStage (v, un);
                break;
            }

            v = next;
        }

        return { st.s, st.ds / (1.0 + L * st.ds) };
    }

    /** Shared table for (L, un); call off the audio thread. */
    static std::shared_ptr<const LineAmpCurve> get (double L, double un);

    /** s(w): table inside +-range; beyond it a second table in z = range / |w| (the far field,
        where s approaches the rails smoothly in 1/w), so no runtime iteration is ever needed. */
    double evaluate (double w) const noexcept
    {
        const double pos = (w + range) * stepsPerUnit;

        if (! (pos >= 0.0 && pos < (double) (size - 1)))
            return evaluateFar (w);

        const int i = (int) pos;
        const double t = pos - (double) i;
        const auto& n0 = nodes[(size_t) i];
        const auto& n1 = nodes[(size_t) i + 1];

        const double t2 = t * t, t3 = t2 * t;
        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h11 = t3 - t2;
        constexpr double h = 1.0 / stepsPerUnit;
        return n1.s + h00 * (n0.s - n1.s) + h * (h10 * n0.ds + h11 * n1.ds);
    }

    LineAmpCurve (double L, double un) : loopGain (L), negSwing (un), nodes ((size_t) size)
    {
        for (int i = 0; i < size; ++i)
            nodes[(size_t) i] = solve (-range + (double) i / stepsPerUnit, L, un);

        // far field: nodes in z = range / |w| on [0, 1], values and slopes ds/dz.
        // At z = 0 (|w| -> infinity) s sits on the rail and ds/dz -> 0 (s - rail ~ z^2).
        for (int side = 0; side < 2; ++side)
        {
            const double sign = side == 0 ? 1.0 : -1.0;
            auto& table = side == 0 ? farPos : farNeg;
            table[0] = { side == 0 ? 1.0 : -un, 0.0 };

            for (int j = 1; j <= farSteps; ++j)
            {
                const double z = (double) j / farSteps;
                const double w = sign * range / z;
                const auto p = solve (w, L, un);
                table[(size_t) j] = { p.s, -p.ds * w / z }; // dw/dz = -w / z
            }
        }
    }

    const double loopGain, negSwing;

private:
    static constexpr int farSteps = 256;

    double evaluateFar (double w) const noexcept
    {
        if (! std::isfinite (w))
            return w > 0.0 ? farPos[0].s : (w < 0.0 ? farNeg[0].s : 0.0);

        const auto& table = w > 0.0 ? farPos : farNeg;
        const double pos = std::min (range / std::abs (w), 1.0) * farSteps; // |w| >= range here
        const int i = std::min ((int) pos, farSteps - 1);
        const double t = pos - (double) i;
        const auto& n0 = table[(size_t) i];
        const auto& n1 = table[(size_t) i + 1];

        const double t2 = t * t, t3 = t2 * t;
        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h11 = t3 - t2;
        constexpr double h = 1.0 / farSteps;
        return n1.s + h00 * (n0.s - n1.s) + h * (h10 * n0.ds + h11 * n1.ds);
    }

    std::vector<Point> nodes;
    std::array<Point, (size_t) farSteps + 1> farPos {}, farNeg {};
};

class LineAmp
{
public:
    /** Call off the audio thread (may build the shared curve table). */
    void setParams (const LineAmpParams& p)
    {
        params = p;
        curve = LineAmpCurve::get (p.loopGain, p.negSwing);
    }

    void prepare (double sampleRate) noexcept
    {
        const auto& p = params;
        openLoop  = p.gain * (1.0 + p.loopGain) / p.rail;
        slewStep  = p.slewRate / sampleRate;
        reset();
    }

    void reset() noexcept { y = 0.0; }

    /**
        Input offset trim (volts at the + input; 0 = nominal, exactly). It moves the stage's
        operating point towards one rail, so it clips asymmetrically. The output coupling
        capacitor holds the resulting DC, so only the swing around the new operating point is
        passed on (quasi-static: no thump when the trim moves).
    */
    void setInputOffset (double volts) noexcept
    {
        offset = volts;
        offsetOutput = volts != 0.0 ? staticTransfer (volts) : 0.0;
    }

    double process (double x) noexcept
    {
        const double target = offset != 0.0 ? staticTransfer (x + offset) - offsetOutput : staticTransfer (x);

        // smooth rate limit on the per-sample step
        const double step = (target - y) / slewStep;
        y += slewStep * rateKnee (step);
        return y;
    }

    void flushDenormals() noexcept
    {
        if (std::abs (y) < 1.0e-30)
            y = 0.0;
    }

    /** Static closed-loop transfer (no slew), volts in -> volts out. */
    double staticTransfer (double x) const noexcept { return params.rail * curve->evaluate (openLoop * x); }

private:
    static double rateKnee (double step) noexcept
    {
        constexpr double knee = 0.25;
        const double m = std::abs (step);

        if (m <= 1.0 - knee)
            return step;

        const double over = std::min (m, 1.0 + knee) - (1.0 - knee);
        return std::copysign (1.0 - knee + over - over * over / (4.0 * knee), step);
    }

    LineAmpParams params {};
    std::shared_ptr<const LineAmpCurve> curve;
    double openLoop = 1.0, slewStep = 1.0;
    double y = 0.0;
    double offset = 0.0, offsetOutput = 0.0;
};

} // namespace umber::dsp::desk
