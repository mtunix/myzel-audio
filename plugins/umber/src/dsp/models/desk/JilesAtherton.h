#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace umber::dsp::desk
{

/**
    Jiles–Atherton ferromagnetic core material (SI units: A/m, dimensionless).

    Ms     saturation magnetisation (A/m) — B_sat ~ mu0 * Ms
    a      anhysteretic shape field (A/m) — larger = softer knee, lower permeability
    k      pinning (A/m) — loop half-width, ~ coercivity
    c      reversible fraction (0..1) — larger = thinner loop, more "elastic" core
    alpha  inter-domain coupling (mean field)

    The parameter sets keep 2 * alpha * Ms < (1 - c) * k, so the irreversible denominator never
    crosses zero (it is clamped anyway).
*/
struct JaMaterial
{
    double Ms, a, k, c, alpha;

    /** Small-signal (reversible, demagnetised) susceptibility: c * dMan/dH at the origin. */
    constexpr double initialSusceptibility() const noexcept
    {
        const double dMan = Ms / (3.0 * a);
        return c * dMan / (1.0 - alpha * c * dMan);
    }
};

/** Langevin function L(x) = coth(x) - 1/x with its first two derivatives (reference version). */
struct LangevinPoint
{
    double value, slope, curvature;

    static LangevinPoint exact (long double x) noexcept
    {
        const long double ax = std::abs (x);
        LangevinPoint p {};

        if (ax < 0.25L)
        {
            // Bernoulli series, error < 1e-15 on this range
            const long double x2 = x * x;
            p.value     = (double) (x * (1.0L / 3 - x2 * (1.0L / 45 - x2 * (2.0L / 945 - x2 * (1.0L / 4725 - x2 * (2.0L / 93555))))));
            p.slope     = (double) (1.0L / 3 - x2 * (1.0L / 15 - x2 * (2.0L / 189 - x2 * (1.0L / 675 - x2 * (2.0L / 10395)))));
            p.curvature = (double) (x * (-2.0L / 15 + x2 * (8.0L / 189 - x2 * (6.0L / 675 - x2 * (16.0L / 10395)))));
            return p;
        }

        const long double coth = 1.0L / std::tanh (ax);
        const long double csch2 = coth * coth - 1.0L;
        p.value     = (double) std::copysign (coth - 1.0L / ax, x);
        p.slope     = (double) (1.0L / (ax * ax) - csch2);
        p.curvature = (double) std::copysign (2.0L * coth * csch2 - 2.0L / (ax * ax * ax), x);
        return p;
    }
};

/**
    Shared lookup table for the Langevin function and its slope: cubic Hermite interpolation on a
    uniform grid (step 1/32, values + exact derivatives at the nodes, so both L and L' are C1 and
    accurate to ~1e-10). Beyond the table L = 1 - 1/x and L' = 1/x^2 to double precision.
    Built once (static), ~12 KB, read-only afterwards.
*/
class LangevinTable
{
public:
    static constexpr double range = 20.0;
    static constexpr double stepsPerUnit = 32.0;
    static constexpr int size = (int) (range * stepsPerUnit) + 2;

    struct Result { double value, slope; };

    static const LangevinTable& get()
    {
        static const LangevinTable table;
        return table;
    }

    Result evaluate (double x) const noexcept
    {
        const double ax = std::abs (x);

        if (! (ax < range))
        {
            const double inv = 1.0 / std::max (ax, range);
            return { std::copysign (1.0 - inv, x), inv * inv };
        }

        const double pos = ax * stepsPerUnit;
        const int i = (int) pos;
        const double t = pos - (double) i;
        const auto& n0 = nodes[(size_t) i];
        const auto& n1 = nodes[(size_t) i + 1];

        // cubic Hermite basis on [0, 1], derivatives scaled by the step
        const double t2 = t * t, t3 = t2 * t;
        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h01 = 1.0 - h00;
        const double h11 = t3 - t2;
        constexpr double h = 1.0 / stepsPerUnit;

        const double value = h00 * n0.value + h01 * n1.value + h * (h10 * n0.slope + h11 * n1.slope);
        const double slope = h00 * n0.slope + h01 * n1.slope + h * (h10 * n0.curvature + h11 * n1.curvature);
        return { std::copysign (value, x), slope };
    }

private:
    LangevinTable()
    {
        for (int i = 0; i < size; ++i)
            nodes[(size_t) i] = LangevinPoint::exact ((long double) i / (long double) stepsPerUnit);
    }

    std::array<LangevinPoint, (size_t) size> nodes {};
};

/**
    Differential susceptibility dM/dH of the Jiles–Atherton model for a field moving in
    direction delta (+1 / -1), in the form used by Chowdhury (DAFx 2019):

        dM/dH = [ (1-c) dM (Man - M) / ((1-c) delta k - alpha (Man - M)) + c dMan/dHe ]
                / [ 1 - alpha c dMan/dHe ]

    with dM = 1 when (Man - M) has the sign of delta, else 0 (removes the non-physical negative
    susceptibility just after a field reversal). The result is never negative.

    Constants are folded once in setMaterial(); evaluation costs one table lookup and no
    division (the ratio is returned unevaluated).
*/
class JaCore
{
public:
    void setMaterial (const JaMaterial& m) noexcept
    {
        material = m;
        invA     = 1.0 / m.a;
        msInvA   = m.Ms / m.a;
        oneMinusC = 1.0 - m.c;
        pin      = oneMinusC * m.k;
        pinFloor = 0.05 * pin;
        langevin = &LangevinTable::get();
    }

    const JaMaterial& getMaterial() const noexcept { return material; }

    /** dM/dH as an unevaluated ratio num / den (den > 0, num >= 0), so callers can fold the
        division into their own. */
    struct Ratio
    {
        double num, den;
        double value() const noexcept { return num / den; }
    };

    Ratio susceptibility (double H, double M, double delta) const noexcept
    {
        const auto lang   = langevin->evaluate ((H + material.alpha * M) * invA);
        const double diff = material.Ms * lang.value - M;
        const double rev  = material.c * msInvA * std::max (lang.slope, 0.0);
        const double den2 = std::max (1.0 - material.alpha * rev, 0.05);

        if (delta * diff > 0.0)
        {
            // (irr + rev) / den2 with irr = (1-c)|diff| / den1
            const double ad   = std::abs (diff);
            const double den1 = std::max (pin - material.alpha * ad, pinFloor);
            return { oneMinusC * ad + rev * den1, den1 * den2 };
        }

        return { rev, den2 };
    }

private:
    JaMaterial material { 1.0, 1.0, 1.0, 0.5, 0.0 };
    double invA = 1.0, msInvA = 1.0, oneMinusC = 0.5, pin = 0.5, pinFloor = 0.025;
    const LangevinTable* langevin = nullptr;
};

} // namespace umber::dsp::desk
