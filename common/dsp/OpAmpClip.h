#pragma once

#include "common/dsp/FloatCompare.h"
#include "common/dsp/LineAmp.h"
#include "common/dsp/Oversampler2x.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace myzel::dsp
{

/**
    LineAmp's loop at an IC op-amp's loop gain, in units of one side's output swing (the loop is
    the same curve in units of each side's swing: negSwing only scales the negative side). For
    the linear output q = G x / swing >= 0 the output is the rail's hard clip plus a correction,

        s (q) = min (q, 1) + d (q),

    d = -(u - S (u)) / (1 + L) inside the rail (the loop's residue: below 1e-4 of the swing up to
    half of it at L = 2200), about -(1 / (sqrt 2 (1 + L)))^(2/3) on it (the knee), and beyond it
    the approach to the rail, to leading order (1 / (1 + L))^2 / (2 (q - 1 + 1 / (1 + L))^2),
    which is what the tables continue with beyond `range`. LineAmpCurve is uniform
    in the loop's input (1 + L) q and cannot resolve a knee at L in the thousands; these tables
    are uniform in q.

    d is split by a smooth window at [inner, outer] of the swing: d_in = d w is smooth and small
    and runs at the caller's rate; d_k = d (1 - w) is zero below `inner` and holds the knee. Both
    have exact antiderivatives for antialiasing (each cell a cubic Hermite, its integrals the
    polynomial's own, so the kernel of the interpolated function is exact).
*/
class OpAmpLoop
{
public:
    static constexpr double inner = 0.65, outer = 0.9;  // the window, in units of the swing
    static constexpr double range = 2.0;                // the tables' end; the asymptote beyond
    static constexpr int stepsPerUnit = 1024;                    // d_k's table
    static constexpr int cells = (int) (range * stepsPerUnit);
    static constexpr int kink = stepsPerUnit;
    static constexpr int smoothStepsPerUnit = 2048;             // d_in's, finer: within 1e-12 of the swing up to the window
    static constexpr int innerCells = (int) (outer * smoothStepsPerUnit) + 1;

    /** The exact loop at q >= 0: the output, d and dd/dq (on the rail, from below unless asked). */
    struct Exact
    {
        double s, d, slope;
    };

    /**
        Solves c u + (1 - c) S (u) = q, c = 1 / (1 + L), S (u) = u / sqrt (1 + u^2), by Newton from
        u = q: the left side is increasing and concave for u >= 0 and starts at or below q, so every
        step lands at or below the root and the iteration rises to it. Table building and tests.
    */
    static Exact solve (double q, double loopGain, bool fromAbove = false) noexcept
    {
        const double c = 1.0 / (1.0 + loopGain);
        double u = q;
        for (int it = 0; it < 400; ++it)
        {
            const double r = std::sqrt (1.0 + u * u);
            const double g = c * u + (1.0 - c) * u / r - q;
            const double next = u - g / (c + (1.0 - c) / (r * r * r));
            if (! (next > u))
                break;
            const bool done = next - u <= 1.0e-16 * next;
            u = next;
            if (done)
                break;
        }

        const double r = std::sqrt (1.0 + u * u);
        const double sPrime = 1.0 / (r * r * r);
        const double ds = sPrime / (c + (1.0 - c) * sPrime);
        if (q < 1.0 || (q <= 1.0 && ! fromAbove))
            return { u / r, -c * u * u * u / (r * (r + 1.0)), c * (sPrime - 1.0) / (c + (1.0 - c) * sPrime) };   // u - S (u), s' - 1 without cancellation
        return { u / r, -1.0 / (r * (r + u)), ds };                             // 1 - S (u), likewise
    }

    /** A part of d and its first and second antiderivatives from 0 (beyond `range`: farIntegrals()). */
    struct Knee
    {
        double d = 0.0, d1 = 0.0, d2 = 0.0;
        bool far = false;
    };

    /** Shared tables for a loop gain; call off the audio thread. */
    static std::shared_ptr<const OpAmpLoop> get (double loopGain)
    {
        static std::mutex lock;
        static std::vector<std::shared_ptr<const OpAmpLoop>> cache;

        const std::scoped_lock guard (lock);
        for (const auto& k : cache)
            if (isExactly (k->loopGain, loopGain))
                return k;

        cache.push_back (std::make_shared<const OpAmpLoop> (loopGain));
        return cache.back();
    }

    explicit OpAmpLoop (double L)
        : loopGain (L), c (1.0 / (1.0 + L)), smooth ((size_t) innerCells + 1), smoothCells ((size_t) innerCells + 1), knee ((size_t) cells + 1)
    {
        constexpr double hs = 1.0 / smoothStepsPerUnit, h = 1.0 / stepsPerUnit;
        for (int i = 0; i <= innerCells; ++i)
        {
            const double q = (double) i * hs;
            const auto e = solve (q, L);
            const auto [w, dw] = window (q);
            smooth[(size_t) i] = { e.d * w, e.slope * w + e.d * dw };
        }

        // Each cell a cubic from its ends' values and slopes, its integrals exact.
        const auto integrate = [] (std::vector<Cell>& table, int count, double step, const auto& end)
        {
            double d1 = 0.0, d2 = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const auto [v0, m0] = end (i, false);
                const auto [v1, m1] = end (i + 1, true);
                auto& k = table[(size_t) i];
                k.a = { v0, m0, 3.0 * (v1 - v0) - 2.0 * m0 - m1, 2.0 * (v0 - v1) + m0 + m1 };
                k.d1 = d1;
                k.d2 = d2;
                d2 += step * d1 + step * step * (k.a[0] / 2.0 + k.a[1] / 6.0 + k.a[2] / 12.0 + k.a[3] / 20.0);
                d1 += step * (k.a[0] + k.a[1] / 2.0 + k.a[2] / 3.0 + k.a[3] / 4.0);
            }
            table[(size_t) count].d1 = d1;
            table[(size_t) count].d2 = d2;
        };

        integrate (smoothCells, innerCells, hs, [&] (int node, bool) { return std::pair { smooth[(size_t) node].v, smooth[(size_t) node].m * hs }; });
        smoothCells[(size_t) innerCells].a = { 0.0, 0.0, 0.0, 0.0 };

        integrate (knee, cells, h,
                   [&] (int node, bool end)
                   {
                       // On the rail d has a kink: each cell takes its slope from its own side.
                       const double q = (double) node * h;
                       const auto e = solve (q, L, end ? node > kink : node >= kink);
                       const auto [w, dw] = window (q);
                       return std::pair { e.d * (1.0 - w), (e.slope * (1.0 - w) - e.d * dw) * h };
                   });
        knee[(size_t) cells].a = { -0.5 * c * c / ((range - 1.0 + c) * (range - 1.0 + c)), 0.0, 0.0, 0.0 };
        for (auto& k : knee)
        {
            k.a1h = k.a[1] / 2.0;
            k.a2t = k.a[2] / 3.0;
            k.a0h = k.a[0] / 2.0;
            k.a1s = k.a[1] / 6.0;
            k.a2w = k.a[2] / 12.0;
        }
    }

    /** d_in (q), q >= 0. */
    double smoothPart (double q) const noexcept
    {
        const double pos = q * smoothStepsPerUnit;
        if (! (pos < (double) innerCells))
            return 0.0;
        const int i = (int) pos;
        const double t = pos - (double) i;
        const auto& n0 = smooth[(size_t) i];
        const auto& n1 = smooth[(size_t) i + 1];
        const double t2 = t * t, t3 = t2 * t;
        constexpr double h = 1.0 / smoothStepsPerUnit;
        return n1.v + (2.0 * t3 - 3.0 * t2 + 1.0) * (n0.v - n1.v) + h * ((t3 - 2.0 * t2 + t) * n0.m + (t3 - t2) * n1.m);
    }

    /** d_in's first and second antiderivatives from 0, q >= 0 (beyond the window d_in is 0 and they continue flat and linearly). */
    void smoothIntegrals (double q, double& d1, double& d2) const noexcept
    {
        constexpr double h = 1.0 / smoothStepsPerUnit;
        const double pos = q * smoothStepsPerUnit;
        if (! (pos < (double) innerCells))
        {
            const auto& e = smoothCells[(size_t) innerCells];
            d1 = e.d1;
            d2 = e.d2 + e.d1 * (q - (double) innerCells * h);
            return;
        }
        const int i = (int) pos;
        const double t = pos - (double) i;
        const auto& k = smoothCells[(size_t) i];
        const auto& a = k.a;
        d1 = k.d1 + h * t * (a[0] + t * (a[1] * 0.5 + t * (a[2] * (1.0 / 3.0) + t * a[3] * 0.25)));
        d2 = k.d2 + h * t * k.d1 + h * h * t * t * (a[0] * 0.5 + t * (a[1] * (1.0 / 6.0) + t * (a[2] * (1.0 / 12.0) + t * a[3] * 0.05)));
    }

    /** d_k (q) and its antiderivatives, q >= 0. */
    Knee kneePart (double q) const noexcept
    {
        constexpr double h = 1.0 / stepsPerUnit;
        if (q <= inner)
            return {};
        const double pos = q * stepsPerUnit;
        if (pos < (double) cells)
        {
            const int i = (int) pos;
            const double t = pos - (double) i;
            const auto& k = knee[(size_t) i];
            const auto& a = k.a;
            return { a[0] + t * (a[1] + t * (a[2] + t * a[3])),
                     k.d1 + h * t * (a[0] + t * (k.a1h + t * (k.a2t + t * a[3] / 4.0))),
                     k.d2 + h * t * k.d1 + h * h * t * t * (k.a0h + t * (k.a1s + t * (k.a2w + t * a[3] / 20.0))) };
        }

        // Beyond the tables: the leading asymptote -c^2 / (2 o^2), o = q - 1 + c.
        const double o = q - 1.0 + c;
        return { std::isfinite (o) ? -0.5 * c * c / (o * o) : 0.0, 0.0, 0.0, true };
    }

    /** The asymptote's exact antiderivatives beyond `range`, continuing the tables'. */
    Knee farIntegrals (double q) const noexcept
    {
        const auto& end = knee[(size_t) cells];
        const double o = q - 1.0 + c, oEnd = range - 1.0 + c;
        const double half = 0.5 * c * c;
        return { -half / (o * o), end.d1 + half * (1.0 / o - 1.0 / oEnd),
                 end.d2 + end.d1 * (q - range) + half * (std::log (o / oEnd) - (q - range) / oEnd), true };
    }

    const double loopGain;

private:
    /** w and dw/dq: 1 below `inner`, 0 above `outer`, a quintic smoothstep between. */
    static std::pair<double, double> window (double q) noexcept
    {
        if (q <= inner)
            return { 1.0, 0.0 };
        if (q >= outer)
            return { 0.0, 0.0 };
        const double t = (q - inner) / (outer - inner), k = 1.0 / (outer - inner);
        return { 1.0 - t * t * t * (10.0 + t * (-15.0 + 6.0 * t)), -30.0 * t * t * (1.0 - t) * (1.0 - t) * k };
    }

    struct Node
    {
        double v, m;
    };

    struct Cell
    {
        std::array<double, 4> a {};     // the part = a0 + a1 t + a2 t^2 + a3 t^3 over the cell, t in [0, 1]
        double d1 = 0.0, d2 = 0.0;      // the antiderivatives at the cell's start
        double a1h = 0.0, a2t = 0.0, a0h = 0.0, a1s = 0.0, a2w = 0.0;  // a1/2, a2/3, a0/2, a1/6, a2/12: the antiderivatives' divisions, done once per table
    };

    double c;
    std::vector<Node> smooth;
    std::vector<Cell> smoothCells, knee;
};

/**
    LinearPhaseHalfBand's filter with its symmetric taps folded (half the multiplies, the same
    response; its rounding differs, so it is not interchangeable with it bit for bit), on N
    channels in one loop: each lane is the same arithmetic as N = 1, to the bit.
*/
template <int K, int N = 1>
class FoldedHalfBand
{
public:
    static constexpr int taps = LinearPhaseHalfBand<K>::taps;

    explicit FoldedHalfBand (double stopDb)
    {
        const LinearPhaseHalfBand<K> design { stopDb };
        for (int j = 0; j < half; ++j)
            coefficients[(size_t) j] = design.coefficient (j);
    }

    void reset() noexcept
    {
        upHistory = {};
        downOdd = {};
        downEven = {};
        upIndex = downIndex = 0;
    }

    /** One frame in, two out (in time order). */
    void upsample (const double* x, double* y0, double* y1) noexcept
    {
        upIndex = upIndex == 0 ? taps - 1 : upIndex - 1;
        for (int k = 0; k < N; ++k)
        {
            upHistory[(size_t) upIndex][(size_t) k] = x[k];
            upHistory[(size_t) (upIndex + taps)][(size_t) k] = x[k];
        }
        const auto* h = upHistory.data() + upIndex;    // h[j] = x[m - j]
        double acc[(size_t) N] {};
        for (int j = 0; j < half; ++j)
            for (int k = 0; k < N; ++k)
                acc[k] += coefficients[(size_t) j] * (h[j][(size_t) k] + h[taps - 1 - j][(size_t) k]);
        for (int k = 0; k < N; ++k)
        {
            y0[k] = 2.0 * acc[k];
            y1[k] = h[K][(size_t) k];
        }
    }

    /** Two frames in (in time order), one out. */
    void downsample (const double* even, const double* odd, double* y) noexcept
    {
        downIndex = downIndex == 0 ? taps - 1 : downIndex - 1;
        for (int k = 0; k < N; ++k)
        {
            downOdd[(size_t) downIndex][(size_t) k] = odd[k];
            downOdd[(size_t) (downIndex + taps)][(size_t) k] = odd[k];
            downEven[(size_t) downIndex][(size_t) k] = even[k];
            downEven[(size_t) (downIndex + taps)][(size_t) k] = even[k];
        }
        const auto* o = downOdd.data() + downIndex;
        double acc[(size_t) N] {};
        for (int j = 0; j < half; ++j)
            for (int k = 0; k < N; ++k)
                acc[k] += coefficients[(size_t) j] * (o[j][(size_t) k] + o[taps - 1 - j][(size_t) k]);
        for (int k = 0; k < N; ++k)
            y[k] = acc[k] + 0.5 * downEven[(size_t) (downIndex + K)][(size_t) k];
    }

private:
    static constexpr int half = taps / 2;

    std::array<double, (size_t) half> coefficients {};
    std::array<std::array<double, (size_t) N>, (size_t) (2 * taps)> upHistory {}, downOdd {}, downEven {};
    int upIndex = 0, downIndex = 0;
};

namespace opamp_detail
{
    /** Below this step (in units of the swing) a kernel is its function at the piece's centre of mass. */
    inline constexpr double fallbackStep = 3.0e-5;

    /**
        The inverse of (1 4 1) / 6 as its series to the third power in y = 1 - cos w, taps z^-3 ... z^0
        (symmetric): 1 / (1 - y / 3) ~ 1 + y / 3 + y^2 / 9 + y^3 / 27. Both the interpolating
        spline's prefilter (its values at the samples are (1 4 1) / 6 of its coefficients) and the
        knee's droop (a triangle on a line through samples is (1 4 1) / 6 of them).
    */
    inline constexpr std::array<double, 4> oneFourOneInverse { -1.0 / 216.0, 1.0 / 18.0, -25.0 / 72.0, 43.0 / 27.0 };

    /**
        The droop of the cubic B-spline kernel on the cubic spline (its values at the samples,
        (1 120 1191 2416 1191 120 1) / 5040), times the prefilter's series error, inverted to the
        third power in y: within 5e-6 dB to 20 kHz at 44.1 kHz.
    */
    inline constexpr std::array<double, 4> shapedInverse { -47.0 / 7560.0, 19.0 / 280.0, -107.0 / 280.0, 6203.0 / 3780.0 };

    /** A symmetric 7-tap FIR at the high rate; a run of zeros in gives exact zeros, uncomputed. */
    struct Compensation
    {
        static constexpr int length = 7;
        std::array<double, (size_t) (2 * length)> history {};
        int index = 0, quiet = length;

        void clear() noexcept
        {
            history = {};
            index = 0;
            quiet = length;
        }

        double process (double v, const std::array<double, 4>& t) noexcept
        {
            if (isExactly (v, 0.0))
            {
                if (quiet >= length)
                    return 0.0;
                ++quiet;
            }
            else
                quiet = 0;
            index = index == 0 ? length - 1 : index - 1;
            history[(size_t) index] = v;
            history[(size_t) (index + length)] = v;
            const double* h = history.data() + index;
            double acc = t[3] * h[3];
            for (int j = 0; j < 3; ++j)
                acc += t[(size_t) j] * (h[j] + h[length - 1 - j]);
            return acc;
        }
    };

    /** The integrals of the slewed clip over one segment (t from 0 to 1): of t^k y, k = 0 ... 3. */
    struct Moments
    {
        std::array<double, 4> m {};
        bool nonlinear = false;     // clipped or slewed somewhere in it
    };

    /**
        A segment's share of the cubic B-spline kernel at the four outputs it reaches, newest first:
        its integrals under (1 - t)^3, 4 - 6 t^2 + 3 t^3, 1 + 3 t + 3 t^2 - 3 t^3 and t^3 (times 6).
    */
    struct Shares
    {
        double newest = 0.0, third = 0.0, second = 0.0, oldest = 0.0;

        static Shares of (const Moments& x) noexcept
        {
            const auto& m = x.m;
            return { m[0] - 3.0 * m[1] + 3.0 * m[2] - m[3], 4.0 * m[0] - 6.0 * m[2] + 3.0 * m[3], m[0] + 3.0 * m[1] + 3.0 * m[2] - 3.0 * m[3], m[3] };
        }

        /** The same for the spline's own segment over coefficients c[0 ... 3], in closed form. */
        static Shares cubic (const double* c) noexcept
        {
            const double cm = c[0], c0 = c[1], c1 = c[2], c2 = c[3];
            return { cm * (1.0 / 42.0) + c0 * (43.0 / 280.0) + c1 * (1.0 / 14.0) + c2 * (1.0 / 840.0),
                     cm * (43.0 / 280.0) + c0 * (99.0 / 70.0) + c1 * (311.0 / 280.0) + c2 * (1.0 / 14.0),
                     cm * (1.0 / 14.0) + c0 * (311.0 / 280.0) + c1 * (99.0 / 70.0) + c2 * (43.0 / 280.0),
                     cm * (1.0 / 840.0) + c0 * (1.0 / 14.0) + c1 * (43.0 / 280.0) + c2 * (1.0 / 42.0) };
        }
    };

    /** The uniform cubic B-spline at a knot from its three coefficients. */
    inline double node (double a, double b, double c) noexcept { return (a + 4.0 * b + c) * (1.0 / 6.0); }

    /**
        One channel's residual at the high rate (4x the caller's): the input as a uniform cubic
        B-spline, the rails' clip and the slew solved exactly on each segment and integrated under
        a cubic B-spline kernel, the knee under a triangle; both kernels' droop undone.
    */
    struct Lane
    {
        const OpAmpLoop* loop = nullptr;
        double lo = -1.0, slew = 1.0;               // the negative rail and the limit per high-rate sample, in units of the swing
        double invLo = -1.0;
        std::array<double, 10> cb {};               // the spline's coefficients, oldest first: 6 kept, 4 new per caller sample
        double held = 0.0;                          // the limiter's output where it left the input
        bool tracking = true;                       // the output on the clipped input
        double pendingB = 0.0, pendingC1 = 0.0, pendingC2 = 0.0, pendingD1 = 0.0, pendingD2 = 0.0, pendingD3 = 0.0;
        int sinceNonlinear = 4;                     // segments since the last clipped or slewed one
        double kneeLast = 0.0, previousB = 0.0;
        int numCrossings = 0;
        std::array<double, 8> crossT {}, crossV {}; // the latest segment's rail crossings
        Compensation shapedFir, kneeFir;
        bool nodeCached = false, nodeIntegrated = false;
        OpAmpLoop::Knee nodeKnee {};                // d_k at the latest segment's end knot (its antiderivatives when integrated)

        // d_k sampled at the knots where it is nearly linear over the segment: clear of the knee
        // (below sampledBelow or beyond sampledBeyond of the swing), or slow against its distance from
        // the rail (the segment's span within sampledSpan of it).
        static constexpr double sampledBelow = 0.95, sampledBeyond = 1.05, sampledSpan = 0.25;

        void clear() noexcept
        {
            cb = {};
            held = 0.0;
            tracking = true;
            pendingB = pendingC1 = pendingC2 = pendingD1 = pendingD2 = pendingD3 = 0.0;
            sinceNonlinear = 4;
            kneeLast = previousB = 0.0;
            nodeCached = nodeIntegrated = false;
            shapedFir.clear();
            kneeFir.clear();
        }

        bool atRest() const noexcept
        {
            return tracking && sinceNonlinear >= 4 && isExactly (kneeLast, 0.0) && isExactly (previousB, 0.0)
                   && shapedFir.quiet >= Compensation::length && kneeFir.quiet >= Compensation::length;
        }

        /**
            True when every state is at rest and the four new segments stay inside the window and
            slower than the limit (a B-spline segment stays inside its coefficients' range, its slope
            inside their differences'): then highStep() would give exact zeros.
        */
        bool quietSample() const noexcept
        {
            if (! atRest())
                return false;
            const double top = OpAmpLoop::inner, bottom = lo * OpAmpLoop::inner;
            for (int i = 3; i < 10; ++i)
                if (cb[(size_t) i] > top || cb[(size_t) i] < bottom || (i > 3 && std::abs (cb[(size_t) i] - cb[(size_t) i - 1]) > slew))
                    return false;
            return true;
        }

        void shift() noexcept
        {
            for (int i = 0; i < 6; ++i)
                cb[(size_t) i] = cb[(size_t) i + 4];
        }

        OpAmpLoop::Knee kneeAt (double q) const noexcept
        {
            if (q >= 0.0)
                return loop->kneePart (q);
            const auto k = loop->kneePart (q / lo);
            return { lo * k.d, lo * lo * k.d1, lo * lo * lo * k.d2, k.far };
        }

        /** kneeAt() with the antiderivatives beyond the tables too. */
        OpAmpLoop::Knee farKneeAt (double q) const noexcept
        {
            if (q >= 0.0)
                return loop->farIntegrals (q);
            const auto k = loop->farIntegrals (q / lo);
            return { lo * k.d, lo * lo * k.d1, lo * lo * lo * k.d2, true };
        }

        /**
            One high-rate step for the newest coefficient cb[e], the segment over cb[e - 3 ... e]:
            what the slewed clip and the knee add to the main path, compensated. Both kernels are
            centred alike, a whole number of caller samples back.
        */
        double highStep (int e) noexcept
        {
            const double* c = cb.data() + e - 3;
            const double cm = c[0], c0 = c[1], c1 = c[2], c2 = c[3];
            const double top = std::max (std::max (cm, c0), std::max (c1, c2)), bottom = std::min (std::min (cm, c0), std::min (c1, c2));
            const bool wasRecent = sinceNonlinear < 4;
            bool nonlinear = false, fast = false;
            numCrossings = 0;
            Shares now;
            const bool slow = std::abs (c0 - cm) <= slew && std::abs (c1 - c0) <= slew && std::abs (c2 - c1) <= slew;
            if (tracking && top <= 1.0 && bottom >= lo && slow)
                fast = true;
            else if (tracking && (bottom >= 1.0 || top <= lo))
            {
                // Wholly beyond a rail and on it: the rail itself.
                const double rail = bottom >= 1.0 ? 1.0 : lo;
                now = { 0.25 * rail, 2.75 * rail, 2.75 * rail, 0.25 * rail };
                nonlinear = true;
            }
            else
            {
                const double a[4] { node (cm, c0, c1), 0.5 * (c1 - cm), 0.5 * (cm - 2.0 * c0 + c1), (3.0 * (c0 - c1) + c2 - cm) * (1.0 / 6.0) };
                Moments m;
                exactSegment (a, m);
                now = Shares::of (m);
                nonlinear = m.nonlinear;
            }
            sinceNonlinear = nonlinear ? 0 : std::min (sinceNonlinear + 1, 4);

            // The kernel's output less the spline's own (the main path holds that): zero four
            // segments after the last nonlinear one.
            double shaped = 0.0;
            if (sinceNonlinear < 4)
            {
                if (fast)
                    now = Shares::cubic (c);
                if (! wasRecent)
                {
                    // The three segments before were linear and their shares were not kept: rebuild them.
                    const auto s1 = Shares::cubic (c - 1), s2 = Shares::cubic (c - 2), s3 = Shares::cubic (c - 3);
                    pendingB = s1.third;
                    pendingC1 = s1.second;
                    pendingC2 = s2.second;
                    pendingD1 = s1.oldest;
                    pendingD2 = s2.oldest;
                    pendingD3 = s3.oldest;
                }
                const double* w = cb.data() + e - 6;
                const double linear = (w[0] + w[6]) * (1.0 / 5040.0) + (w[1] + w[5]) * (1.0 / 42.0) + (w[2] + w[4]) * (397.0 / 1680.0) + w[3] * (151.0 / 315.0);
                shaped = (now.newest + pendingB + pendingC2 + pendingD3) * (1.0 / 6.0) - linear;
                pendingD3 = pendingD2;
                pendingD2 = pendingD1;
                pendingD1 = now.oldest;
                pendingC2 = pendingC1;
                pendingC1 = now.second;
                pendingB = now.third;
            }

            // The knee's triangle on a piecewise-linear path through the segment's knots.
            double segA = 0.0, segB = 0.0;
            const double kneeTop = OpAmpLoop::inner, kneeBottom = lo * OpAmpLoop::inner;
            const bool nearRail = top > kneeTop || bottom < kneeBottom;
            bool integrated = false;
            if (nearRail)
            {
                const double x0 = node (cm, c0, c1), x1 = node (c0, c1, c2);
                const bool positive = bottom > 0.0, oneSide = positive || top < 0.0;
                const double uHi = std::max (top, bottom * invLo), uLo = positive ? bottom : top * invLo;
                const double span = positive ? top - bottom : (bottom - top) * invLo;
                const bool sampled = numCrossings == 0 && oneSide
                                     && (uHi < sampledBelow || uLo > sampledBeyond || (uHi < 1.0 && span <= sampledSpan * (1.0 - uHi))
                                         || (uLo > 1.0 && span <= sampledSpan * (uLo - 1.0)));
                // The previous segment's end knot is this one's start, the same to the bit.
                if (sampled)
                {
                    // The triangle on the line through d_k's values at the knots.
                    const double f0 = nodeCached ? nodeKnee.d : kneeAt (x0).d;
                    const double f1 = kneeAt (x1).d;
                    nodeKnee.d = f1;
                    segA = (2.0 * f0 + f1) * (1.0 / 6.0);
                    segB = (f0 + 2.0 * f1) * (1.0 / 6.0);
                }
                else
                {
                    integrated = true;
                    const auto k0 = nodeCached && nodeIntegrated ? nodeKnee : kneeAt (x0);
                    const auto k1 = kneeAt (x1);
                    nodeKnee = k1;
                    if (numCrossings == 0 && uHi < 1.0)
                        kneePieces (0.0, 1.0, x0, k0, x1, k1, segA, segB);
                    else
                    {
                        // Near or across a rail: lines through the crossings (so d_k's kink falls where
                        // the clip's does) and the pieces' midpoints.
                        const double a[4] { x0, 0.5 * (c1 - cm), 0.5 * (cm - 2.0 * c0 + c1), (3.0 * (c0 - c1) + c2 - cm) * (1.0 / 6.0) };
                        double ts[24], xs[24];
                        int np = 0;
                        ts[np] = 0.0, xs[np++] = x0;
                        for (int i = 0; i < numCrossings; ++i)
                        {
                            const double tm = 0.5 * (ts[np - 1] + crossT[(size_t) i]);
                            ts[np] = tm, xs[np++] = eval (a, tm);
                            ts[np] = crossT[(size_t) i], xs[np++] = crossV[(size_t) i];
                        }
                        const double tm = 0.5 * (ts[np - 1] + 1.0);
                        ts[np] = tm, xs[np++] = eval (a, tm);
                        ts[np] = 1.0, xs[np++] = x1;
                        auto kp = k0;
                        for (int i = 0; i + 1 < np; ++i)
                        {
                            const auto kq = i + 2 == np ? k1 : kneeAt (xs[i + 1]);
                            kneePieces (ts[i], ts[i + 1], xs[i], kp, xs[i + 1], kq, segA, segB);
                            kp = kq;
                        }
                    }
                }
            }
            nodeCached = nearRail;
            nodeIntegrated = integrated;
            const double knee = kneeLast;
            kneeLast = previousB + segA;
            previousB = segB;
            return shapedFir.process (shaped, shapedInverse) + kneeFir.process (knee, oneFourOneInverse);
        }

        static double eval (const double (&a)[4], double t) noexcept { return a[0] + t * (a[1] + t * (a[2] + t * a[3])); }
        static double slope (const double (&a)[4], double t) noexcept { return a[1] + t * (2.0 * a[2] + t * 3.0 * a[3]); }

        /** Roots in (0, 1) of 3 a3 t^2 + 2 a2 t + (a1 - v): where the segment's slope is v. */
        static int slopeRoots (const double (&a)[4], double v, double* out) noexcept
        {
            const double A = 3.0 * a[3], B = 2.0 * a[2], C = a[1] - v;
            int n = 0;
            auto keep = [&] (double t)
            {
                if (t > 0.0 && t < 1.0)
                    out[n++] = t;
            };
            if (isExactly (A, 0.0) || std::abs (A) * 1.0e12 < std::abs (B))
            {
                if (! isExactly (B, 0.0))
                {
                    // Nearly linear: the root of B t + C, refined on the quadratic (the other is far outside).
                    double t = -C / B;
                    if (! isExactly (A, 0.0))
                        for (int i = 0; i < 3; ++i)
                            t -= (A * t * t + B * t + C) / (2.0 * A * t + B);
                    keep (t);
                }
                return n;
            }
            const double disc = B * B - 4.0 * A * C;
            if (disc < 0.0)
                return 0;
            const double sq = std::sqrt (disc);
            const double qq = -0.5 * (B + (B >= 0.0 ? sq : -sq));
            keep (qq / A);
            if (! isExactly (qq, 0.0))
                keep (C / qq);
            return n;
        }

        /** The root in (p, q) of a function monotone there with g (p) g (q) < 0: Newton, bisection as safeguard. */
        template <typename G, typename D>
        static double rootIn (double p, double q, double gp, G&& g, D&& dg) noexcept
        {
            double a = p, b = q;
            double t = 0.5 * (p + q);
            for (int it = 0; it < 60; ++it)
            {
                const double v = g (t);
                if (isExactly (v, 0.0))
                    return t;
                if ((v > 0.0) == (gp > 0.0))
                    a = t;
                else
                    b = t;
                const double d = dg (t);
                double next = isExactly (d, 0.0) ? 0.5 * (a + b) : t - v / d;
                if (! (next > a && next < b))
                    next = 0.5 * (a + b);
                if (std::abs (next - t) <= 1.0e-15)
                    return next;
                t = next;
            }
            return t;
        }

        /** A polynomial piece of the output, b0 + b1 t + b2 t^2 + b3 t^3 over [p, q], into the moments. */
        static void piece (double p, double q, double b0, double b1, double b2, double b3, Moments& m) noexcept
        {
            double pp[8], qp[8];
            pp[0] = p;
            qp[0] = q;
            for (int i = 1; i < 8; ++i)
                pp[i] = pp[i - 1] * p, qp[i] = qp[i - 1] * q;
            const double b[4] { b0, b1, b2, b3 };
            for (int k = 0; k < 4; ++k)
            {
                double s = 0.0;
                for (int j = 0; j < 4; ++j)
                    if (! isExactly (b[j], 0.0))
                        s += b[j] * (qp[j + k] - pp[j + k]) / (double) (j + k + 1);
                m.m[(size_t) k] += s;
            }
        }

        /** piece() for b0 + b1 t (a rail, or the limiter's ramp), the same arithmetic. */
        static void linearPiece (double p, double q, double b0, double b1, Moments& m) noexcept
        {
            const double p2 = p * p, q2 = q * q, p3 = p2 * p, q3 = q2 * q, p4 = p3 * p, q4 = q3 * q;
            const double d[5] { q - p, q2 - p2, q3 - p3, q4 - p4, q4 * q - p4 * p };
            const bool has0 = ! isExactly (b0, 0.0), has1 = ! isExactly (b1, 0.0);
            for (int k = 0; k < 4; ++k)
            {
                double s = 0.0;
                if (has0)
                    s += b0 * d[k] / (double) (k + 1);
                if (has1)
                    s += b1 * d[k + 1] / (double) (k + 2);
                m.m[(size_t) k] += s;
            }
        }

        /**
            The slewed clip on the segment x (t) = a0 + a1 t + a2 t^2 + a3 t^3, t in [0, 1], exactly:
            split where its slope is 0 or +-slew and where it crosses a rail (on each monotone stretch),
            so that on each piece the clipped input is one rail or the cubic, its slope on one side of
            the limit; then the limiter piece by piece.
        */
        void exactSegment (const double (&a)[4], Moments& m) noexcept
        {
            constexpr double hi = 1.0;
            double bp[24];
            int n = 0;
            bp[n++] = 0.0;
            double crit[2];
            const int nc = slopeRoots (a, 0.0, crit);
            if (nc == 2 && crit[0] > crit[1])
                std::swap (crit[0], crit[1]);
            n += slopeRoots (a, slew, bp + n);
            n += slopeRoots (a, -slew, bp + n);
            for (int i = 0; i < nc; ++i)
                bp[n++] = crit[i];

            double edges[4] { 0.0, 0.0, 0.0, 1.0 };
            int ne = 0;
            edges[ne++] = 0.0;
            for (int i = 0; i < nc; ++i)
                edges[ne++] = crit[i];
            edges[ne++] = 1.0;
            const auto dx = [&] (double t) { return slope (a, t); };
            for (int i = 0; i + 1 < ne; ++i)
            {
                const double u = edges[i], v = edges[i + 1];
                if (! (v > u))
                    continue;
                for (const double rail : { hi, lo })
                {
                    const double gu = eval (a, u) - rail, gv = eval (a, v) - rail;
                    if ((gu < 0.0 && gv > 0.0) || (gu > 0.0 && gv < 0.0))
                    {
                        bp[n++] = rootIn (u, v, gu, [&] (double t) { return eval (a, t) - rail; }, dx);
                        crossT[(size_t) numCrossings] = bp[n - 1];
                        crossV[(size_t) numCrossings++] = rail;
                    }
                }
            }
            bp[n++] = 1.0;
            for (int i = 1; i < n; ++i)
                for (int j = i; j > 0 && bp[j] < bp[j - 1]; --j)
                    std::swap (bp[j], bp[j - 1]);
            for (int i = 1; i < numCrossings; ++i)
                for (int j = i; j > 0 && crossT[(size_t) j] < crossT[(size_t) j - 1]; --j)
                    std::swap (crossT[(size_t) j], crossT[(size_t) j - 1]), std::swap (crossV[(size_t) j], crossV[(size_t) j - 1]);

            for (int i = 0; i + 1 < n; ++i)
            {
                const double p = bp[i], q = bp[i + 1];
                if (q > p)
                    limiter (a, p, q, m);
            }
        }

        /**
            The hard rate limiter over [p, q], where the clipped input is one rail or the cubic, its
            slope on one side of the limit: on the input while it is there and slower than the limit,
            otherwise moving at the limit towards it until they meet.
        */
        void limiter (const double (&a)[4], double p, double q, Moments& m) noexcept
        {
            constexpr double hi = 1.0;
            const double mid = 0.5 * (p + q);
            const double xm = eval (a, mid);
            const int region = xm > hi ? 1 : (xm < lo ? -1 : 0);
            const double rail = region > 0 ? hi : lo;
            const auto target = [&] (double t) { return region == 0 ? eval (a, t) : rail; };
            const auto targetSlope = [&] (double t) { return region == 0 ? slope (a, t) : 0.0; };
            const bool fast = region == 0 && std::abs (slope (a, mid)) > slew;

            for (;;)
            {
                if (tracking)
                {
                    if (region != 0)
                    {
                        m.nonlinear = true;
                        linearPiece (p, q, rail, 0.0, m);
                        held = rail;
                        return;
                    }
                    if (! fast)
                    {
                        piece (p, q, a[0], a[1], a[2], a[3], m);
                        held = eval (a, q);
                        return;
                    }
                    tracking = false;
                    held = eval (a, p);
                }
                m.nonlinear = true;
                const double e = target (p) - held;
                const double rate = e > 0.0 || (isExactly (e, 0.0) && targetSlope (mid) > 0.0) ? slew : -slew;
                const auto g = [&] (double t) { return target (t) - (held + rate * (t - p)); };
                const double gq = g (q);
                if (! isExactly (e, 0.0) && ((gq > 0.0) != (e > 0.0) || isExactly (gq, 0.0)))
                {
                    const double t = region != 0 ? std::clamp (p + e / rate, p, q)
                                                 : (isExactly (gq, 0.0) ? q : rootIn (p, q, e, g, [&] (double s) { return targetSlope (s) - rate; }));
                    linearPiece (p, t, held - rate * p, rate, m);
                    held = target (t);
                    tracking = true;
                    if (! (t < q))
                        return;
                    p = t;
                    continue;
                }
                linearPiece (p, q, held - rate * p, rate, m);
                held += rate * (q - p);
                return;
            }
        }

        /** Both halves' integrals over [t0, t1]: of (1 - t) d_k (x (t)) into a and of t d_k (x (t)) into b, x linear from x0 to x1. */
        void kneePieces (double t0, double t1, double x0, const OpAmpLoop::Knee& k0, double x1, const OpAmpLoop::Knee& k1, double& a, double& b) const noexcept
        {
            const double length = t1 - t0;
            if (! (length > 0.0))
                return;
            const double delta = x1 - x0;
            if (std::abs (delta) < fallbackStep)
            {
                a += kneePiece (t0, t1, x0, k0, x1, k1, 1.0, -1.0);
                b += kneePiece (t0, t1, x0, k0, x1, k1, 0.0, 1.0);
                return;
            }
            if (k0.far && k1.far && (x0 > 0.0) == (x1 > 0.0))
            {
                // kneePiece()'s asymptote case for both halves, the same arithmetic.
                const double ua = 1.0 - t0, ub = 1.0 - t1;
                a += length * (2.0 * ua * k0.d + ua * k1.d + ub * k0.d + 2.0 * ub * k1.d) * (1.0 / 6.0);
                b += length * (2.0 * t0 * k0.d + t0 * k1.d + t1 * k0.d + 2.0 * t1 * k1.d) * (1.0 / 6.0);
                return;
            }
            const auto& K0 = k0.far ? farKneeAt (x0) : k0;
            const auto& K1 = k1.far ? farKneeAt (x1) : k1;
            const double slopeInv = length / delta;
            const double first = (K1.d1 - K0.d1) * slopeInv;                                    // the integral of d_k over the piece
            const double moment = (delta * K1.d1 - (K1.d2 - K0.d2)) * slopeInv * slopeInv;      // of (t - t0) d_k
            b += t0 * first + moment;
            a += (1.0 - t0) * first - moment;
        }

        /** The integral over [t0, t1] of (w0 + w1 t) d_k (x (t)), x linear from x0 to x1, from its antiderivatives. */
        double kneePiece (double t0, double t1, double x0, const OpAmpLoop::Knee& k0, double x1, const OpAmpLoop::Knee& k1, double w0, double w1) const noexcept
        {
            const double length = t1 - t0;
            if (! (length > 0.0))
                return 0.0;
            const double delta = x1 - x0, wa = w0 + w1 * t0, wb = w0 + w1 * t1;
            if (std::abs (delta) < fallbackStep)
            {
                const double sum = wa + wb;
                if (! (sum > 0.0))
                    return 0.0;
                const double xc = x0 + delta * (wa + 2.0 * wb) / (3.0 * sum);
                return 0.5 * length * sum * kneeAt (xc).d;
            }
            // Both ends on the asymptote, on one side: d_k is below 2e-6 of the swing there and
            // smooth, so the kernel of its samples' linear interpolation is exact to 1e-11.
            if (k0.far && k1.far && (x0 > 0.0) == (x1 > 0.0))
                return length * (2.0 * wa * k0.d + wa * k1.d + wb * k0.d + 2.0 * wb * k1.d) * (1.0 / 6.0);
            const auto& K0 = k0.far ? farKneeAt (x0) : k0;
            const auto& K1 = k1.far ? farKneeAt (x1) : k1;
            const double slopeInv = length / delta;
            return (wa * (K1.d1 - K0.d1) + w1 * slopeInv * (delta * K1.d1 - (K1.d2 - K0.d2))) * slopeInv;
        }
    };
} // namespace opamp_detail

/**
    An IC op-amp gain stage into its rails on N channels in one loop: LineAmp's loop (LineAmpParams:
    closed-loop gain, loop gain, positive swing, negative swing as a fraction of it, slew rate) at a
    loop gain high enough that it clips hard, its nonlinearity antialiased. A frame at a time at the
    caller's rate R (volts in, volts out, a constant delay of latencySamples); each channel's output
    is the same to the bit as an OpAmpClipLanes<1> of its own.

    The output is a main path at R plus a residual at 4R:

    - main: the linear output, delayed, plus the loop's smooth residue d_in (OpAmpLoop). While every
      sample its kernel reaches stays below `cubicBelow` of the swing, d_in sampled (there it is
      c q^3, c = -1 / (2 (1 + L)), which puts nothing above 3 times the input's band, plus a
      remainder below 7e-7 of the swing). Otherwise c q^3 sampled plus the remainder d_in - c q^3
      under a triangle through its exact antiderivatives, centred on the delayed sample (no added
      delay), its droop undone; it holds for holdSamples after the last such sample;
    - residual, at 4R through linear-phase half-band FIRs (whose cascade is a pure delay to within
      their pass-band ripple): the input clipped at the rails and slewed, plus the knee d_k, minus
      what the main path holds, antialiased by continuous-time convolution (Bilbao, Esqueda,
      Parker and Välimäki, "Antiderivative antialiasing for memoryless nonlinearities", IEEE SPL
      24(7), 2017; Parker, Zavalishin, Le Bihan, "Reducing the aliasing of nonlinear waveshaping
      using continuous-time convolution", DAFx 2016). The 4R samples are taken as an interpolating
      uniform cubic B-spline (a 7-tap prefilter gives its coefficients), whose images lie sinc^4
      down. On each of its segments the clip and the slew limit are solved together in continuous
      time (breakpoints where the slope is 0 or the limit, and where the input crosses a rail), a
      hard rate limiter following the clipped cubic; the output, piecewise polynomial, is
      integrated exactly under a cubic B-spline kernel (aliases sinc^4 of their frequency over 4R),
      less the spline's own part. d_k under a triangle through its exact antiderivatives on lines
      through the segment's knots (and, near a rail, through its crossings); where d_k is nearly
      linear over a segment (clear of the knee, or slow against its distance from the rail) the
      triangle on the line through d_k's values at the knots. A 7-tap FIR undoes each kernel's droop.

    The input must be band-limited to about R / 4, as an oversampled caller's input is: content
    above passes the main path but not the residual's FIRs, so the two no longer cancel there.

    The residual is exactly zero while the input stays below `OpAmpLoop::inner` of the swing and
    slower than the slew rate (bounds from the FIRs' and the prefilter's impulse responses), so it
    is computed only when the input comes near: at rest the stage costs the main path. Its FIRs
    restart from the input's recent history, so the output is the same to the bit as computing it
    always; while it runs, the parts that are exactly zero (a quiet segment, a FIR whose history is
    all zero) are skipped, to the same bit. With N = 2 the residual runs for both channels while
    either needs it (each the same to the bit as running it always).
*/
template <int N>
class OpAmpClipLanes
{
public:
    static constexpr int latencySamples = 22;           // at the caller's rate

    /** Call off the audio thread (may build the shared tables). */
    void setParams (const LineAmpParams& p)
    {
        params = p;
        loop = OpAmpLoop::get (p.loopGain);
    }

    void prepare (double sampleRate) noexcept
    {
        toSwing = params.gain / params.rail;
        lo = -params.negSwing;
        slew = params.slewRate / (4.0 * sampleRate * params.rail);
        for (auto& l : lanes)
        {
            l.loop = loop.get();
            l.lo = lo;
            l.invLo = 1.0 / lo;
            l.slew = slew;
        }
        measureUpBounds();
        const double nearest = OpAmpLoop::inner * std::min (1.0, params.negSwing);
        quietBelow = std::min (nearest / upAmplitudeBound, slew / upStepBound);
        cubic = -0.5 / (1.0 + params.loopGain);
        cubicNeg = cubic / (lo * lo);
        reset();
    }

    void reset() noexcept
    {
        history = {};
        mains = {};
        write = 0;
        running = false;
        countdown = {};
        clearResidual();
    }

    /** Tests: compute the residual on every sample (the output is the same to the bit). */
    void setAlwaysActive (bool always) noexcept { alwaysActive = always; }

    bool isComputingResidual() const noexcept { return running; }

    /** One frame: N samples in volts, N out. */
    void processFrame (const double* x, double* y) noexcept
    {
        bool loud = alwaysActive, mainLoud = false;
        const int n = write;
        for (int k = 0; k < N; ++k)
        {
            const double q = x[k] * toSwing, aq = std::abs (q);
            history[(size_t) n][(size_t) k] = q;
            if (aq > quietBelow)
            {
                loud = true;
                countdown[(size_t) k] = settleSamples;
            }
            auto& m = mains[(size_t) k];
            m.loudBits = (m.loudBits << 1) | (aq >= cubicBelow ? 1u : 0u);
            mainLoud = mainLoud || (m.loudBits & windowMask) != 0 || m.hold > 0;
        }
        write = (write + 1) & mask;
        const int d = (n - latencySamples) & mask;

        if (! loud && ! running && ! mainLoud)
        {
            // + 0.0: the sum below with a residual of zero, to the bit.
            for (int k = 0; k < N; ++k)
            {
                const double qd = history[(size_t) d][(size_t) k];
                y[k] = params.rail * (qd + smoothResidue (qd) + 0.0);
            }
            return;
        }
        if (loud && ! running)
            restartResidual();
        running = running || loud;

        double main[(size_t) N];
        mainResidues (d, main);

        double residual[(size_t) N] {};
        if (running)
        {
            residualStep (residual);
            bool any = alwaysActive;
            for (int k = 0; k < N; ++k)
            {
                if (countdown[(size_t) k] > 0)
                    --countdown[(size_t) k];
                any = any || countdown[(size_t) k] > 0;
            }
            running = any;
        }
        for (int k = 0; k < N; ++k)
            y[k] = params.rail * (history[(size_t) d][(size_t) k] + main[k] + residual[k]);
    }

    void flushDenormals() noexcept
    {
        if (! running)
            return;
        for (auto& l : lanes)
            for (auto* f : { &l.shapedFir.history, &l.kneeFir.history })
                for (auto& v : *f)
                    if (std::abs (v) < 1.0e-30)
                        v = 0.0;
    }

    /** The loop's static transfer, volts in -> volts out (no slew, no kernel). */
    double staticTransfer (double x) const noexcept
    {
        const double q = x * toSwing;
        const double side = q >= 0.0 ? 1.0 : lo;
        const double r = q / side;
        return params.rail * (std::clamp (q, lo, 1.0) + side * (loop->smoothPart (r) + loop->kneePart (r).d));
    }

    /** Small-signal gain (the loop's residue is flat at the origin). */
    double smallSignalGain() const noexcept { return params.gain; }

private:
    static constexpr int historySize = 64, mask = historySize - 1;
    static constexpr int primeSamples = 32;         // the up FIRs' and the prefilter's span at the caller's rate, and the kernel's
    static constexpr int settleSamples = 64;        // a loud input's last effect through both FIRs
    static constexpr double stopBandTwo = 95.0, stopBandFour = 95.0;
    static constexpr int innerK = 3;
    static constexpr double cubicBelow = 0.6;       // the main path's d_in sampled while its kernel's samples stay below it
    static constexpr int reach = 4;                 // the compensation's 3 taps and the triangle's 1 on either side
    static constexpr int holdSamples = 32;          // entering the triangle costs a refill of its window
    static constexpr std::uint32_t windowMask = ((1u << (2 * reach + 1)) - 1u) << (latencySamples - reach);
    using Frame = std::array<double, (size_t) N>;

    double smoothResidue (double q) const noexcept { return q >= 0.0 ? loop->smoothPart (q) : lo * loop->smoothPart (q / lo); }
    double cubicPart (double q) const noexcept { return (q >= 0.0 ? cubic : cubicNeg) * q * q * q; }

    /** The main path's state of one channel: the remainder's antiderivatives at each sample and its triangles near the delayed one. */
    struct MainLane
    {
        std::array<double, historySize> d1 {}, d2 {}, inv {};
        std::array<double, 7> window {};        // the triangles on the delayed sample's neighbours, oldest first
        std::uint32_t loudBits = 0;             // bit i: sample i back reached cubicBelow
        int hold = 0;
        bool wasLoud = false;
    };

    /** The remainder's first and second antiderivatives at sample i, and 1 / (q - previous q) (0 for a step too short). */
    void integralsAt (int i, int k) noexcept
    {
        auto& m = mains[(size_t) k];
        const double q = history[(size_t) i][(size_t) k];
        double d1, d2;
        if (q >= 0.0)
            loop->smoothIntegrals (q, d1, d2);
        else
        {
            loop->smoothIntegrals (q / lo, d1, d2);
            d1 *= lo * lo;
            d2 *= lo * lo * lo;
        }
        const double kk = q >= 0.0 ? cubic : cubicNeg, q4 = (q * q) * (q * q);
        m.d1[(size_t) i] = d1 - kk * q4 * 0.25;
        m.d2[(size_t) i] = d2 - kk * q4 * q * 0.05;
        const double delta = q - history[(size_t) ((i - 1) & mask)][(size_t) k];
        m.inv[(size_t) i] = std::abs (delta) < opamp_detail::fallbackStep ? 0.0 : 1.0 / delta;
    }

    double fallbackHalf (int ib, int ia, int k) const noexcept
    {
        const double x = (history[(size_t) ia][(size_t) k] + 2.0 * history[(size_t) ib][(size_t) k]) * (1.0 / 3.0);
        return 0.5 * (smoothResidue (x) - cubicPart (x));
    }

    /** The remainder's triangle on sample j, pushed into the channel's window of the last seven. */
    void pushTriangle (int j, int k) noexcept
    {
        auto& m = mains[(size_t) k];
        const int jm = (j - 1) & mask, jp = (j + 1) & mask;
        const double up = m.inv[(size_t) jp], down = -m.inv[(size_t) j];
        const double forward = isExactly (up, 0.0) ? fallbackHalf (j, jp, k) : ((m.d2[(size_t) jp] - m.d2[(size_t) j]) * up - m.d1[(size_t) j]) * up;
        const double backward = isExactly (down, 0.0) ? fallbackHalf (j, jm, k) : ((m.d2[(size_t) jm] - m.d2[(size_t) j]) * down - m.d1[(size_t) j]) * down;
        for (int i = 0; i < 6; ++i)
            m.window[(size_t) i] = m.window[(size_t) i + 1];
        m.window[6] = forward + backward;
    }

    /** d_in of the delayed sample c, each channel sampled or under its compensated triangle. */
    void mainResidues (int c, double* out) noexcept
    {
        const auto& t = opamp_detail::oneFourOneInverse;
        for (int k = 0; k < N; ++k)
        {
            auto& m = mains[(size_t) k];
            const double qc = history[(size_t) c][(size_t) k];
            if ((m.loudBits & windowMask) != 0)
                m.hold = holdSamples;
            else if (m.hold > 0)
                --m.hold;
            if (m.hold == 0)
            {
                m.wasLoud = false;
                out[k] = smoothResidue (qc);
                continue;
            }
            if (! m.wasLoud)
            {
                for (int o = -reach; o < reach; ++o)
                    integralsAt ((c + o) & mask, k);
                for (int o = 1 - reach; o < reach - 1; ++o)
                    pushTriangle ((c + o) & mask, k);
                m.wasLoud = true;
            }
            integralsAt ((c + reach) & mask, k);
            pushTriangle ((c + reach - 1) & mask, k);
            const auto& w = m.window;
            out[k] = cubicPart (qc) + t[0] * (w[0] + w[6]) + t[1] * (w[1] + w[5]) + t[2] * (w[2] + w[4]) + t[3] * w[3];
        }
    }

    /**
        Bounds on the spline's coefficients per unit of the input, through both up FIRs and the
        prefilter, from their impulse response: the largest sum of absolute taps of a phase, and of
        differences of consecutive outputs.
    */
    void measureUpBounds() noexcept
    {
        FoldedHalfBand<8> two { stopBandTwo };
        FoldedHalfBand<innerK> four { stopBandFour };
        std::array<double, 7> pre {};
        std::array<double, 4> amplitude {}, steps {};
        double previous = 0.0;
        const auto& t = opamp_detail::oneFourOneInverse;
        for (int n = 0; n < historySize + 8; ++n)
        {
            const double impulse = n == 0 ? 1.0 : 0.0;
            double x[2], y[4];
            two.upsample (&impulse, &x[0], &x[1]);
            four.upsample (&x[0], &y[0], &y[1]);
            four.upsample (&x[1], &y[2], &y[3]);
            for (size_t p = 0; p < 4; ++p)
            {
                for (size_t i = pre.size() - 1; i > 0; --i)
                    pre[i] = pre[i - 1];
                pre[0] = y[p];
                const double c = t[0] * (pre[0] + pre[6]) + t[1] * (pre[1] + pre[5]) + t[2] * (pre[2] + pre[4]) + t[3] * pre[3];
                amplitude[p] += std::abs (c);
                steps[p] += std::abs (c - previous);
                previous = c;
            }
        }
        upAmplitudeBound = *std::max_element (amplitude.begin(), amplitude.end());
        upStepBound = *std::max_element (steps.begin(), steps.end());
    }

    void clearResidual() noexcept
    {
        up2.reset();
        up4.reset();
        down4.reset();
        down2.reset();
        rb = {};
        for (auto& l : lanes)
            l.clear();
        quietFour = fourQuiet;
        quietTwo = twoQuiet;
    }

    /** Starts the residual from the input's history: its FIRs see what they would have seen. */
    void restartResidual() noexcept
    {
        clearResidual();
        double scratch[(size_t) N];
        for (int j = primeSamples; j >= 1; --j)
            residualStepFrom (history[(size_t) ((write - 1 - j) & mask)].data(), scratch);
    }

    void residualStep (double* out) noexcept { residualStepFrom (history[(size_t) ((write - 1) & mask)].data(), out); }

    void residualStepFrom (const double* q, double* out) noexcept
    {
        double two0[(size_t) N], two1[(size_t) N];
        up2.upsample (q, two0, two1);
        up4.upsample (two0, rb[6].data(), rb[7].data());
        up4.upsample (two1, rb[8].data(), rb[9].data());
        const auto& t = opamp_detail::oneFourOneInverse;
        for (int e = 6; e < 10; ++e)
            for (int k = 0; k < N; ++k)
                lanes[(size_t) k].cb[(size_t) e] = t[0] * (rb[(size_t) e][(size_t) k] + rb[(size_t) e - 6][(size_t) k])
                                                  + t[1] * (rb[(size_t) e - 1][(size_t) k] + rb[(size_t) e - 5][(size_t) k])
                                                  + t[2] * (rb[(size_t) e - 2][(size_t) k] + rb[(size_t) e - 4][(size_t) k]) + t[3] * rb[(size_t) e - 3][(size_t) k];

        double high[4][(size_t) N] {};
        bool anyWork = false;
        for (int k = 0; k < N; ++k)
        {
            auto& l = lanes[(size_t) k];
            if (! l.quietSample())
            {
                anyWork = true;
                for (int e = 6; e < 10; ++e)
                    high[e - 6][k] = l.highStep (e);
            }
            else
                l.nodeCached = false;
            l.shift();
        }
        for (int i = 0; i < 6; ++i)
            rb[(size_t) i] = rb[(size_t) i + 4];

        for (int k = 0; k < N; ++k)
            out[k] = 0.0;
        if (! anyWork && quietFour == fourQuiet && quietTwo == twoQuiet)
            return;
        double f0[(size_t) N], f1[(size_t) N];
        downFour (high[0], high[1], f0);
        downFour (high[2], high[3], f1);
        bool zero = true;
        for (int k = 0; k < N; ++k)
            zero = zero && isExactly (f0[k], 0.0) && isExactly (f1[k], 0.0);
        // A history of exact zeros gives an exact zero: the FIR need not run.
        quietTwo = zero ? std::min (quietTwo + 1, twoQuiet) : 0;
        if (quietTwo < twoQuiet)
            down2.downsample (f0, f1, out);
    }

    void downFour (const double* even, const double* odd, double* y) noexcept
    {
        bool zero = true;
        for (int k = 0; k < N; ++k)
            zero = zero && isExactly (even[k], 0.0) && isExactly (odd[k], 0.0);
        quietFour = zero ? std::min (quietFour + 1, fourQuiet) : 0;
        if (quietFour == fourQuiet)
        {
            for (int k = 0; k < N; ++k)
                y[k] = 0.0;
            return;
        }
        down4.downsample (even, odd, y);
    }

    LineAmpParams params {};
    std::shared_ptr<const OpAmpLoop> loop;
    double toSwing = 1.0, lo = -1.0, slew = 1.0, quietBelow = 0.0;
    double upAmplitudeBound = 1.0, upStepBound = 1.0;
    double cubic = 0.0, cubicNeg = 0.0;             // d_in's leading term c q^3 on each side
    bool alwaysActive = false, running = false;

    std::array<Frame, historySize> history {};      // the input in units of the swing
    int write = 0;
    std::array<int, (size_t) N> countdown {};
    std::array<MainLane, (size_t) N> mains {};

    FoldedHalfBand<8, N> up2 { stopBandTwo }, down2 { stopBandTwo };
    FoldedHalfBand<innerK, N> up4 { stopBandFour }, down4 { stopBandFour };
    std::array<Frame, 10> rb {};                    // the input at 4R before the prefilter, oldest first
    std::array<opamp_detail::Lane, (size_t) N> lanes {};
    static constexpr int fourQuiet = FoldedHalfBand<innerK>::taps + 1, twoQuiet = FoldedHalfBand<8>::taps + 1;
    int quietFour = fourQuiet, quietTwo = twoQuiet;  // exact zeros in a row into each down FIR, up to a full history
};

/** One channel, a sample at a time. */
class OpAmpClip : public OpAmpClipLanes<1>
{
public:
    double process (double x) noexcept
    {
        double y;
        processFrame (&x, &y);
        return y;
    }
};

} // namespace myzel::dsp
