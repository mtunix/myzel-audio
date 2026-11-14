#pragma once

#include "dsp/common/NetworkTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace viridian::dsp
{

/**
    Exact composition of separately generated networks at one node, at control rate: a source
    network with its Thevenin impedance driving a load network. tools/netlist solves each side on
    its own (small, fast) and this joins them numerically, where solving the joined network
    symbolically would take hours and give a header of many megabytes (console/NOTES.md,
    "Implementation"). Polynomials are ascending in sigma = s / omegaRef, as the generated ones.
*/
namespace composition
{
    inline constexpr int maxDegree = 48;

    struct Poly
    {
        int degree = 0;
        std::array<double, maxDegree + 1> c {};
    };

    inline Poly constant (double v) noexcept
    {
        Poly p;
        p.c[0] = v;
        return p;
    }

    inline Poly multiply (const Poly& a, const Poly& b) noexcept
    {
        Poly p;
        p.degree = std::min (a.degree + b.degree, maxDegree);
        for (int i = 0; i <= a.degree; ++i)
            for (int j = 0; j <= b.degree && i + j <= maxDegree; ++j)
                p.c[(size_t) (i + j)] += a.c[(size_t) i] * b.c[(size_t) j];
        return p;
    }

    inline Poly add (const Poly& a, const Poly& b, double scaleB = 1.0) noexcept
    {
        Poly p;
        p.degree = std::max (a.degree, b.degree);
        for (int k = 0; k <= p.degree; ++k)
            p.c[(size_t) k] = (k <= a.degree ? a.c[(size_t) k] : 0.0) + scaleB * (k <= b.degree ? b.c[(size_t) k] : 0.0);
        return p;
    }

    inline std::complex<double> evaluate (const Poly& p, std::complex<double> x) noexcept
    {
        std::complex<double> v {};
        for (int k = p.degree; k >= 0; --k)
            v = v * x + p.c[(size_t) k];
        return v;
    }

    /** The product of a generated network's numerators (times its gain) and of its denominators. */
    template <typename Polynomials>
    void expand (const Polynomials& in, Poly& num, Poly& den) noexcept
    {
        num = constant (in.gain);
        den = constant (1.0);
        for (int b = 0; b < in.numBlocks; ++b)
        {
            const auto& block = in.blocks[(size_t) b];
            Poly n, d;
            n.degree = block.numDegree;
            d.degree = block.denDegree;
            for (int k = 0; k <= n.degree; ++k)
                n.c[(size_t) k] = block.num[(size_t) k];
            for (int k = 0; k <= d.degree; ++k)
                d.c[(size_t) k] = block.den[(size_t) k];
            num = multiply (num, n);
            den = multiply (den, d);
        }
    }

    /** a / b where b divides a exactly (up to rounding): long division from the top. */
    inline Poly quotient (Poly a, const Poly& b) noexcept
    {
        Poly q;
        q.degree = std::max (a.degree - b.degree, 0);
        for (int k = a.degree - b.degree; k >= 0; --k)
        {
            const auto t = a.c[(size_t) (k + b.degree)] / b.c[(size_t) b.degree];
            q.c[(size_t) k] = t;
            for (int j = 0; j <= b.degree; ++j)
                a.c[(size_t) (k + j)] -= t * b.c[(size_t) j];
        }
        return q;
    }

    /**
        a - b with the leading terms that cancel exactly (both sides equal up to rounding) dropped,
        so the difference has its true degree.
    */
    inline Poly difference (const Poly& a, const Poly& b) noexcept
    {
        auto p = add (a, b, -1.0);
        while (p.degree > 0)
        {
            const auto k = (size_t) p.degree;
            const auto scale = (p.degree <= a.degree ? std::abs (a.c[k]) : 0.0) + (p.degree <= b.degree ? std::abs (b.c[k]) : 0.0);
            if (std::abs (p.c[k]) > 1.0e-12 * scale)
                break;
            p.c[k] = 0.0;
            --p.degree;
        }
        return p;
    }
} // namespace composition

} // namespace viridian::dsp
