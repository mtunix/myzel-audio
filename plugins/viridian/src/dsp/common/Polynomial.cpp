#include "dsp/common/Polynomial.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace viridian::dsp::poly
{

void quadraticRoots (Complex c0, Complex c1, Complex c2, ProjectiveRoot& r1, ProjectiveRoot& r2) noexcept
{
    if (isExactly (c2, {}) && isExactly (c1, {}))
    {
        r1 = { { 1.0, 0.0 }, {} };
        r2 = r1;
        return;
    }
    // q = -(c1 + sgn sqrt (disc)) / 2 with the sign that adds magnitudes: roots q / c2 and c0 / q.
    const auto d = std::sqrt (c1 * c1 - 4.0 * c2 * c0);
    const auto q = std::real (std::conj (c1) * d) >= 0.0 ? -0.5 * (c1 + d) : -0.5 * (c1 - d);
    if (isExactly (q, {}))
    {
        // c1 = 0 and c0 * c2 = 0 with c2 != 0: a double root at 0.
        r1 = { {}, { 1.0, 0.0 } };
        r2 = r1;
        return;
    }
    r1 = { q, c2 };
    r2 = { c0, q };
}

namespace
{
    struct Derivatives
    {
        Complex p, dp;
    };

    /** Rounding error of evaluating the monic polynomial at x, from its coefficients' magnitudes: below it, p (x) is noise. */
    double roundingBound (std::span<const double> magnitudes, Complex x) noexcept
    {
        double sum = 0.0;
        const auto r = std::sqrt (std::norm (x));
        for (size_t k = magnitudes.size(); k-- > 0;)
            sum = sum * r + magnitudes[k];
        return 8.0 * std::numeric_limits<double>::epsilon() * (double) magnitudes.size() * sum;
    }

    /**
        a / b by the conjugate (std::complex's division goes through the library's range-checked
        routine, which dominated the sweeps); b's magnitude stays far from under- and overflow here
        (normalised coefficients, distinct roots), and the library's division takes the rest.
    */
    Complex quotient (Complex a, Complex b) noexcept
    {
        const auto n = std::norm (b);
        if (! (n > 1.0e-280 && n < 1.0e280))
            return a / b;
        return a * std::conj (b) / n;
    }

    /**
        True when c[0 ... n] has a root beyond `radius`: |c[n-k]| / |c[n]| is the k-th elementary
        symmetric function of the roots, at most C (n, k) times the largest root's k-th power.
    */
    bool rootBeyond (std::span<const Complex> c, int n, double radius) noexcept
    {
        const auto lead = std::sqrt (std::norm (c[(size_t) n]));
        if (! (lead > 0.0))
            return true;
        double binomial = 1.0, power = 1.0;
        for (int k = 1; k <= n; ++k)
        {
            binomial *= (double) (n - k + 1) / (double) k;
            power *= radius;
            if (std::sqrt (std::norm (c[(size_t) (n - k)])) > binomial * lead * power)
                return true;
        }
        return false;
    }

    Derivatives evaluateWithDerivative (std::span<const Complex> c, Complex x) noexcept
    {
        Complex p {}, dp {};
        for (size_t k = c.size(); k-- > 0;)
        {
            dp = dp * x + p;
            p = p * x + c[k];
        }
        return { p, dp };
    }
} // namespace

void RootFinder::begin (std::span<const Complex> c, std::span<const Complex> start, bool warm, double infinityRadius) noexcept
{
    done = true;
    count = m = 0;
    int n = (int) c.size() - 1;
    if (n < 1 || n > maxDegree)
        return;

    // Squared magnitudes throughout: std::abs on a complex is hypot, which dominated small degrees.
    double largest = 0.0;
    for (const auto& x : c)
        largest = std::max (largest, std::norm (x));
    if (myzel::dsp::isExactly (largest, 0.0))
        return;
    // A block whose roots span many decades has a finite top root over a leading coefficient far
    // below the others: judged by its size, the later card's high-pass front drops one at 293 kHz.
    if (infinityRadius > 0.0)
        while (n > 0 && rootBeyond (c, n, infinityRadius))
            --n;
    else
        while (n > 0 && std::norm (c[(size_t) n]) <= 1.0e-28 * largest)
            --n;

    int zeros = 0;
    while (zeros < n && isExactly (c[(size_t) zeros], {}))
        ++zeros;

    a = {};
    m = n - zeros;
    for (int k = 0; k <= m; ++k)
    {
        a[(size_t) k] = c[(size_t) (k + zeros)] / c[(size_t) n];
        magnitudes[(size_t) k] = std::sqrt (std::norm (a[(size_t) k]));
    }
    count = n;
    z = {};
    if (m == 1)
        z[0] = -a[0];
    if (m == 2)
    {
        ProjectiveRoot r1, r2;
        quadraticRoots (a[0], a[1], a[2], r1, r2);
        z[0] = r1.value();
        z[1] = r2.value();
    }
    if (m <= 2)
        return;

    bool usable = warm && (int) start.size() >= m;
    for (int k = 0; k < m && usable; ++k)
    {
        z[(size_t) k] = start[(size_t) k];
        usable = std::isfinite (z[(size_t) k].real()) && std::isfinite (z[(size_t) k].imag());
    }
    for (int k = 0; k < m && usable; ++k)
        for (int j = 0; j < k; ++j)
            if (isExactly (z[(size_t) k], z[(size_t) j]))
                usable = false;
    // Real starts keep every iterate of a real polynomial on the real axis, so two real roots that
    // have met and turned complex would bounce on it until the cold restart: each real start sits
    // a little off the axis, by turns above and below.
    for (int k = 0; k < m && usable; ++k)
        if (isReal (z[(size_t) k]))
            z[(size_t) k] += Complex { 0.0, (k % 2 == 0 ? 1.0e-6 : -1.0e-6) * std::max (std::abs (z[(size_t) k].real()), 1.0e-300) };
    attempt = 0;
    iteration = 0;
    done = false;
    if (! usable)
        coldStart();
}

void RootFinder::coldStart() noexcept
{
    // A circle at the geometric mean root radius, off the real axis.
    const auto radius = std::pow (std::max (std::abs (a[0]), 1.0e-300), 1.0 / m);
    for (int k = 0; k < m; ++k)
        z[(size_t) k] = std::polar (radius, 2.0 * std::numbers::pi * (k + 0.25) / m + 0.4);
}

bool RootFinder::step (int sweeps) noexcept
{
    const std::span<const Complex> p { a.data(), (size_t) m + 1 };
    const std::span<const double> sizes { magnitudes.data(), (size_t) m + 1 };
    for (; sweeps > 0 && ! done; --sweeps)
    {
        bool converged = true;
        for (int k = 0; k < m; ++k)
        {
            const auto [v, dv] = evaluateWithDerivative (p, z[(size_t) k]);
            if (isExactly (v, {}))
                continue;
            const auto bound = roundingBound (sizes, z[(size_t) k]);
            if (std::norm (v) <= bound * bound)
                continue;   // as exact as the coefficients allow: the root has converged
            Complex repulsion {};
            for (int j = 0; j < m; ++j)
                if (j != k)
                    repulsion += quotient (1.0, z[(size_t) k] - z[(size_t) j]);
            const auto ratio = quotient (v, dv);
            const auto stepTaken = quotient (ratio, 1.0 - ratio * repulsion);
            if (! (std::isfinite (stepTaken.real()) && std::isfinite (stepTaken.imag())))
                continue;
            z[(size_t) k] -= stepTaken;
            if (std::norm (stepTaken) > 1.0e-28 * std::max (std::norm (z[(size_t) k]), 1.0e-300))
                converged = false;
        }
        ++iteration;
        if (converged)
            polish();
        else if (iteration == 200)
        {
            if (attempt == 0)
            {
                attempt = 1;
                iteration = 0;
                coldStart();
            }
            else
                polish();
        }
    }
    return done;
}

void RootFinder::polish() noexcept
{
    const std::span<const Complex> p { a.data(), (size_t) m + 1 };
    for (int k = 0; k < m; ++k)
        for (int round = 0; round < 2; ++round)
        {
            const auto [v, dv] = evaluateWithDerivative (p, z[(size_t) k]);
            if (! isExactly (dv, {}))
            {
                const auto next = z[(size_t) k] - quotient (v, dv);
                if (std::isfinite (next.real()) && std::isfinite (next.imag()))
                    z[(size_t) k] = next;
            }
        }
    done = true;
}

int RootFinder::result (std::span<Complex> out) const noexcept
{
    if ((int) out.size() < count)
        return 0;
    for (int k = 0; k < count; ++k)
        out[(size_t) k] = k < m ? z[(size_t) k] : Complex {};
    return count;
}

int roots (std::span<const Complex> c, std::span<Complex> out, bool warm, double infinityRadius) noexcept
{
    if ((int) out.size() < (int) c.size() - 1)
        return 0;
    RootFinder finder;
    finder.begin (c, out, warm, infinityRadius);
    while (! finder.step (1 << 20))
    {
    }
    return finder.result (out);
}

void symmetrise (std::span<Complex> r) noexcept
{
    for (auto& x : r)
        if (x.imag() * x.imag() <= 1.0e-18 * std::norm (x))
            x = { x.real(), 0.0 };

    std::array<bool, 64> taken {};
    for (size_t k = 0; k < r.size() && k < taken.size(); ++k)
    {
        if (taken[k] || r[k].imag() <= 0.0)
            continue;
        size_t best = r.size();
        double distance = 0.0;
        for (size_t j = 0; j < r.size() && j < taken.size(); ++j)
        {
            if (taken[j] || j == k || r[j].imag() >= 0.0)
                continue;
            const auto d = std::norm (r[j] - std::conj (r[k]));
            if (best == r.size() || d < distance)
            {
                best = j;
                distance = d;
            }
        }
        if (best < r.size())
        {
            const auto mean = 0.5 * (r[k] + std::conj (r[best]));
            r[k] = mean;
            r[best] = std::conj (mean);
            taken[k] = taken[best] = true;
        }
    }
}

} // namespace viridian::dsp::poly
