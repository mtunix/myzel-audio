#pragma once

#include "dsp/dynamics/GainComputer.h"

#include <algorithm>
#include <array>
#include <cmath>

// Vari-mu's static curve (NOTES.md here, Gain computer; Settled 40): eq. 4 plus a rise towards
// 20:1 centred U_r above Threshold, capped at 20, and the per-sample inverse its feedback loop asks
// for. Shared by the model, the transfer curve and Auto Gain.

namespace basalt::models::varimu
{

/** The ratio cap: neither reference passes 20:1, and ∞ reads as 20 with no rise. */
inline constexpr double ratioCap = 20.0;
inline constexpr double capInverseRatio = 1.0 / ratioCap;

/** U_r and w_r: the rise's centre above Threshold and its logistic width, in dB. */
inline constexpr double riseCentreDb = 16.0;
inline constexpr double riseWidthDb = 2.0;

/** φ(R) fades the rise in between Ratio 1 and 1 + this, so Ratio 1 stays the identity. */
inline constexpr double riseFadeRatios = 0.5;

/**
    Below this many widths under its centre the rise is taken as exactly 0: it is 1.8·10⁻¹³ dB
    there, and inputs far below Threshold skip it.
*/
inline constexpr double riseCutoffWidths = -30.0;

/** The curve the loop solves: eq. 4 at the capped ratio, plus the rise's scale A = φ(R)·(1/R − 1/20). */
struct Shape
{
    dynamics::Curve base;     // inverseRatio already capped at 1/20
    double rise = 0.0;        // A; 0 at Ratio 1 and from Ratio 20 on

    static Shape of (const dynamics::Curve& c) noexcept
    {
        Shape s { c, 0.0 };
        s.base.inverseRatio = std::clamp (c.inverseRatio, capInverseRatio, 1.0);
        const auto ratio = 1.0 / s.base.inverseRatio;
        s.rise = std::min (1.0, (ratio - 1.0) / riseFadeRatios) * (s.base.inverseRatio - capInverseRatio);
        return s;
    }
};

/** softplus(u) = ln(1 + e^u) and its slope, the logistic σ(u). */
struct Softplus
{
    double value = 0.0, slope = 0.0;
};

/**
    softplus from a table: cubic Hermite on its exact values and slopes every 1/16, within
    5·10⁻⁹ of ln(1 + e^u) (10⁻⁸ dB of rise); 0 below the cutoff, u above its mirror. The slope is
    the interpolant's own, so a Newton step sees the function it solves. Built at load time.
*/
Softplus softplus (double u) noexcept;

/** d(x), its slope d'(x) and its curvature d''(x) at one level, before Range. */
struct Point
{
    double reduction = 0.0, slope = 0.0, curvature = 0.0;
};

inline Point evaluate (const Shape& s, double x) noexcept
{
    Point p { dynamics::curveReduction (s.base, x), 1.0 - dynamics::curveSlope (s.base, x), 0.0 };

    if (s.base.kneeDb > 0.0 && 2.0 * std::abs (x - s.base.thresholdDb) <= s.base.kneeDb)
        p.curvature = (1.0 - s.base.inverseRatio) / s.base.kneeDb;

    const auto u = (x - s.base.thresholdDb - riseCentreDb) / riseWidthDb;
    if (s.rise > 0.0 && u > riseCutoffWidths)
    {
        const auto sp = softplus (u);
        p.reduction += s.rise * riseWidthDb * sp.value;
        p.slope += s.rise * sp.slope;
        p.curvature += s.rise * sp.slope * (1.0 - sp.slope) / riseWidthDb;
    }

    return p;
}

/** d(x) before Range. */
inline double reduction (const Shape& s, double x) noexcept { return evaluate (s, x).reduction; }

/** r_s(x) = min(d(x), Range): the static curve. */
inline double staticReduction (const dynamics::Curve& c, double x) noexcept
{
    return std::min (reduction (Shape::of (c), x), c.rangeDb);
}

/** The local ratio 1/G'(x), G(x) = x − d(x). */
inline double localRatio (const Shape& s, double x) noexcept
{
    return 1.0 / (1.0 - evaluate (s, x).slope);
}

/** Where d(x) is exactly 0, so x itself is G⁻¹(x) without a solve. */
inline double identityBelow (const Shape& s) noexcept
{
    const auto foot = s.base.thresholdDb - 0.5 * s.base.kneeDb;
    return s.rise > 0.0 ? std::min (foot, s.base.thresholdDb + riseCentreDb + riseCutoffWidths * riseWidthDb) : foot;
}

/**
    G⁻¹ for the loop's demand c(y) = G⁻¹(y) − y, per sample and detector channel.

    d is convex, so G is concave and increasing (slope ≥ 1/20) and its inverse convex: Newton
    converges from either side, a step from above the root landing under it. Each solve starts
    from the last one, extrapolated along the inverse's slope and, within a dB, its curvature.
    Once |G(x) − y| is under acceptDb a last Newton step is taken without evaluating its result:
    its error is the step's square times the curvature, under 10⁻⁸ dB, or 20 × acceptDb where the
    step crosses a hard knee's corner. Without a rise the closed form of eq. 4 applies.
*/
class Inverse
{
public:
    static constexpr double acceptDb = 1.0e-7;
    static constexpr int maxEvaluations = 16;

    void reset() noexcept { last = {}; }

    double demand (const Shape& s, double y) noexcept
    {
        if (s.rise <= 0.0)
            return dynamics::feedbackDemand (s.base, y);

        if (y <= identityBelow (s))
            return 0.0;

        const auto dy = y - last.y;
        auto x = std::max (y, last.x + dy * (last.slope + (std::abs (dy) < 1.0 ? 0.5 * last.curvature * dy : 0.0)));
        auto p = evaluate (s, x);
        auto f = x - p.reduction - y;
        auto evaluations = 1;

        while (std::abs (f) > acceptDb && evaluations < maxEvaluations)
        {
            x = std::max (y, x - f / (1.0 - p.slope));
            p = evaluate (s, x);
            f = x - p.reduction - y;
            ++evaluations;
        }

        const auto g = 1.0 - p.slope;
        x -= f / g;
        last = { y, x, 1.0 / g, p.curvature / (g * g * g) };
        lastEvaluations = evaluations;
        return x - y;
    }

    /** Curve evaluations the last solve took; for the tests. */
    int evaluationsTaken() const noexcept { return lastEvaluations; }

private:
    struct Solution
    {
        double y = dynamics::silentDb, x = dynamics::silentDb, slope = 1.0, curvature = 0.0;   // x(y), x', x''
    };

    Solution last;
    int lastEvaluations = 0;
};

/**
    The level where d(x) = depth, by bisection; for the attack's reference point and the tests.
    Bounded, no allocation. A curve that never gets there (Ratio 1) returns the highest level tried.
*/
inline double levelForReduction (const Shape& s, double depthDb) noexcept
{
    auto lo = s.base.thresholdDb - 0.5 * s.base.kneeDb, hi = s.base.thresholdDb + std::max (depthDb, 1.0);

    for (int i = 0; i < 60 && reduction (s, hi) < depthDb; ++i)
    {
        lo = hi;
        hi = s.base.thresholdDb + 2.0 * (hi - s.base.thresholdDb);
    }

    for (int i = 0; i < 100 && hi - lo > 1.0e-12; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        (reduction (s, mid) < depthDb ? lo : hi) = mid;
    }

    return 0.5 * (lo + hi);
}

} // namespace basalt::models::varimu
