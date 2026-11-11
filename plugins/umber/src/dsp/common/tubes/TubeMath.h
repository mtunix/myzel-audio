#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numbers>
#include <vector>

namespace umber::dsp::tubes
{

/**
    Scalar math kernels for the tube equations, evaluated inside the per-sample Newton solver.

    The Newton iteration is one long dependency chain per sample, so what matters is the latency
    from a port voltage to its current and conductance. The tube currents are therefore built
    from shared lookup tables (cubic Hermite, a few cycles of arithmetic each) instead of
    exp / log / atan calls, which are 30-80 cycles each and were the bulk of that latency.
    All functions are pure, overflow-safe for any finite input and never return NaN or Inf.
*/

/** e^x, overflow-safe for any finite x. */
inline double fastExp (double x) noexcept
{
    return std::exp (std::clamp (x, -700.0, 700.0));
}

/** Natural log for x > 0 (non-positive input is clamped to a tiny positive number). */
inline double fastLog (double x) noexcept
{
    return std::log (std::max (x, 1.0e-300));
}

/**
    Linear up to |x| = knee, then a tanh shoulder approaching +-ceiling. C2-continuous at the
    knee (tanh has zero curvature at 0), so it adds nothing below the knee and no hard corner
    above it. Models the finite voltage swing of whatever drives the circuit.
*/
inline double softLimit (double x, double knee, double ceiling) noexcept
{
    const double ax = std::abs (x);
    if (ax <= knee)
        return x;

    const double range = ceiling - knee;
    return std::copysign (knee + range * std::tanh ((ax - knee) / range), x);
}

/**
    Cubic Hermite interpolation of a smooth function on a uniform grid, built from its exact
    values and slopes at the nodes (so the interpolant is C1 and its slope C0: smooth Jacobians
    for Newton-Raphson). Tuned for latency: the interval is found by the round-to-nearest
    "shifter" trick (no float <-> int conversions), each interval stores its polynomial around
    the interval centre (|f| <= 1/2) and value and slope are evaluated in Estrin form.

    Valid for lo < x < hi (callers handle the outside with asymptotes); one padding interval
    on each side absorbs rounding at the ends.
*/
class CubicTable
{
public:
    struct Point
    {
        double value, slope;
    };

    CubicTable() = default;

    /** exact (x) -> Point. Builds (lo, hi) in steps of 'step' (a power of two). */
    template <typename Exact>
    CubicTable (double lo, double hi, double step, Exact&& exact)
        : invStep (1.0 / step), offset (0.5 - lo / step)
    {
        const int numIntervals = (int) std::lround ((hi - lo) / step);
        coeffs.resize ((size_t) numIntervals + 2);

        for (int j = 0; j < numIntervals + 2; ++j)
        {
            // entry j covers [lo + (j - 1) step, lo + j step]
            const double x0 = lo + (j - 1) * step, x1 = x0 + step;
            const Point p0 = exact (x0), p1 = exact (x1);
            const double m0 = step * p0.slope, m1 = step * p1.slope;

            // Hermite cubic in u = (x - x0) / step in [0, 1] ...
            const double a0 = p0.value, a1 = m0;
            const double a2 = 3.0 * (p1.value - p0.value) - 2.0 * m0 - m1;
            const double a3 = 2.0 * (p0.value - p1.value) + m0 + m1;

            // ... re-expanded around the centre, f = u - 1/2
            auto& c = coeffs[(size_t) j];
            c.v = { a0 + 0.5 * a1 + 0.25 * a2 + 0.125 * a3,
                    a1 + a2 + 0.75 * a3,
                    a2 + 1.5 * a3,
                    a3 };
            c.s = { c.v[1] * invStep, 2.0 * c.v[2] * invStep, 3.0 * c.v[3] * invStep, 0.0 };
        }
    }

    Point operator() (double x) const noexcept
    {
        constexpr double shifter = 0x1.8p52; // adding it rounds to an integer held in the low bits

        const double s  = x * invStep + offset;
        const double ts = s + shifter;
        const auto   j  = (size_t) (std::bit_cast<std::uint64_t> (ts) & 0x7ffffffull);
        const double f  = s - (ts - shifter);
        const auto&  c  = coeffs[std::min (j, coeffs.size() - 1)];

        const double f2 = f * f;
        return { (c.v[0] + c.v[1] * f) + f2 * (c.v[2] + c.v[3] * f),
                 (c.s[0] + c.s[1] * f) + f2 * c.s[2] };
    }

private:
    struct alignas (64) Interval
    {
        std::array<double, 4> v, s;
    };

    double invStep = 1.0, offset = 0.0;
    std::vector<Interval> coeffs;
};

namespace detail
{
    /** Tables keyed by a parameter, built once (off the audio thread) and shared. */
    template <typename Table>
    const Table& sharedTable (double parameter)
    {
        static std::mutex lock;
        static std::vector<std::unique_ptr<const Table>> cache;

        const std::scoped_lock guard (lock);
        for (const auto& t : cache)
            if (std::bit_cast<std::uint64_t> (t->parameter) == std::bit_cast<std::uint64_t> (parameter))
                return *t;

        cache.push_back (std::make_unique<const Table> (parameter));
        return *cache.back();
    }
} // namespace detail

/**
    P(a) = softplus (a)^n and dP/da for a fixed exponent n, softplus (a) = ln (1 + e^a).

    Every Koren-style current is  K * (V * softplus (a))^n = K * V^n * P(a)  (with
    PowerTable for V^n), and the grid diode is softplus itself (n = 1).

    Table on -24 < a < 40 (h = 1/16): relative error < 3e-8 where the tube conducts (a > 0,
    about -150 dB) and < 2e-7 deep in cutoff, where the current is itself < 1e-4 of the
    operating current. Outside: P = e^(n a) (1 - n e^a / 2) below (error < 1e-20) and
    P = a^n above (softplus (a) = a to 1e-17). The range covers a cold grid diode (a ~ -18)
    without an exp.

    Shared per exponent (~64 KB), built on first use by a tube's constructor.
*/
class SoftplusPowerTable
{
public:
    using Result = CubicTable::Point;

    static const SoftplusPowerTable& forExponent (double n) { return detail::sharedTable<SoftplusPowerTable> (n); }

    explicit SoftplusPowerTable (double n)
        : parameter (n),
          unitExponent (std::bit_cast<std::uint64_t> (n) == std::bit_cast<std::uint64_t> (1.0)),
          table (lo, hi, 1.0 / 16.0, [n] (double a) { return exact (a, n); })
    {
    }

    Result operator() (double a) const noexcept
    {
        if (a <= lo)
        {
            // softplus (a) = e^a (1 - e^a / 2 + ...): P = e^(na) (1 - n e^a / 2),
            // dP/da = n e^(na) (1 - (n + 1) e^a / 2), to O(e^2a) < 1e-20
            const double ea  = std::exp (std::max (a, -700.0));
            const double ena = unitExponent ? ea : std::exp (std::max (parameter * a, -700.0));
            return { ena * (1.0 - 0.5 * parameter * ea), parameter * ena * (1.0 - 0.5 * (parameter + 1.0) * ea) };
        }

        if (a >= hi)
        {
            const double v = std::exp (parameter * std::log (std::min (a, 1.0e300)));
            return { v, parameter * v / a };
        }

        return table (a);
    }

    /** Exact reference (used to build the table and by tests). */
    static Result exact (double a, double n) noexcept
    {
        const double sp = a > 0.0 ? a + std::log1p (std::exp (-a)) : std::log1p (std::exp (a));
        const double sigmoid = 1.0 / (1.0 + std::exp (-a));
        const double v = std::pow (sp, n);
        return { v, n * v / sp * sigmoid };
    }

    const double parameter; // the exponent n

private:
    static constexpr double lo = -24.0, hi = 40.0;
    const bool unitExponent;
    CubicTable table;
};

/**
    x^n for x > 0 and a fixed exponent n: x = 2^e m (m in [1, 2)), x^n = 2^(e n) m^n with
    2^(e n) from a table over e and m^n from a 64-interval Hermite table indexed straight from
    the mantissa bits. Relative error < 1e-10. x is clamped to [2^-100, 2^100].
*/
class PowerTable
{
public:
    static const PowerTable& forExponent (double n) { return detail::sharedTable<PowerTable> (n); }

    explicit PowerTable (double n) : parameter (n)
    {
        for (int e = -emax; e <= emax; ++e)
            scale[(size_t) (e + emax)] = std::exp2 ((double) e * n);

        constexpr double h = 1.0 / intervals;
        for (int k = 0; k < intervals; ++k)
        {
            const double m0 = 1.0 + k * h, m1 = m0 + h;
            const double y0 = std::pow (m0, n), y1 = std::pow (m1, n);
            const double d0 = h * n * y0 / m0, d1 = h * n * y1 / m1;
            coeffs[(size_t) k] = { y0, d0, 3.0 * (y1 - y0) - 2.0 * d0 - d1, 2.0 * (y0 - y1) + d0 + d1 };
        }
    }

    double operator() (double x) const noexcept
    {
        x = std::clamp (x, 0x1p-100, 0x1p100);

        const auto bits = std::bit_cast<std::uint64_t> (x);
        const auto e = (size_t) ((int) (bits >> 52) - 1023 + emax);
        const auto k = (size_t) ((bits >> (52 - indexBits)) & (intervals - 1));

        // position inside the interval, exact: the remaining mantissa bits as 1.xxx - 1
        constexpr std::uint64_t restMask = (1ull << (52 - indexBits)) - 1;
        const double f = std::bit_cast<double> (((bits & restMask) << indexBits) | 0x3ff0000000000000ull) - 1.0;

        const auto& c = coeffs[k];
        const double f2 = f * f;
        return scale[e] * ((c[0] + c[1] * f) + f2 * (c[2] + c[3] * f));
    }

    const double parameter; // the exponent n

private:
    static constexpr int indexBits = 6, intervals = 1 << indexBits, emax = 100;
    std::array<double, 2 * emax + 1> scale {};
    std::array<std::array<double, 4>, intervals> coeffs {};
};

/**
    atan (q) with  |q| <= 1: a Hermite table (h = 1/64, error < 1e-9), |q| > 1: pi/2 - atan (1/q).
    About half the latency of a libm atan.
*/
class AtanTable
{
public:
    static const AtanTable& instance()
    {
        static const AtanTable table;
        return table;
    }

    double operator() (double q) const noexcept
    {
        const double aq = std::abs (q);
        if (aq <= 1.0)
            return table (q).value;

        const double inner = table (1.0 / std::min (aq, 1.0e300)).value;
        return std::copysign (0.5 * std::numbers::pi - inner, q);
    }

private:
    AtanTable()
        : table (-1.0, 1.0, 1.0 / 64.0, [] (double q) { return CubicTable::Point { std::atan (q), 1.0 / (1.0 + q * q) }; })
    {
    }

    CubicTable table;
};

} // namespace umber::dsp::tubes
