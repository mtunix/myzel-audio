#pragma once

#include "dsp/dynamics/Levels.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// The static curve, Giannoulis et al. 2012 eq. 4, its closed-form inverse for the feedback
// models, and the 20 ms fade that blends the old and the new curve's ballistics, so a knob jump
// never reaches the gain as a step (NOTES.md, Gain computer, The feedback form, Clicks).

namespace basalt::dynamics
{

/** What the gain computer reads, all in dB except the slope term. */
struct Curve
{
    double thresholdDb = -18.0;
    double inverseRatio = 0.25;     // 1/R: 1 at Ratio 1, exactly 0 at ∞ (Parameters.h, inverseRatio)
    double kneeDb = 6.0;            // W, centred on the threshold; 0 is the hard knee
    double rangeDb = 40.0;          // the most reduction (or lift) a model may apply
};

/**
    eq. 4's reduction x − G(x) at the level x, before Range. Computed as the reduction itself, not
    as x minus the curve, so it is exactly 0 below the knee, at Ratio 1 and at the knee's foot.
*/
inline double curveReduction (const Curve& c, double x) noexcept
{
    const auto over = x - c.thresholdDb;

    if (2.0 * over < -c.kneeDb)
        return 0.0;

    const auto slope = 1.0 - c.inverseRatio;

    if (c.kneeDb > 0.0 && 2.0 * over <= c.kneeDb)
    {
        const auto v = over + 0.5 * c.kneeDb;
        return slope * v * v / (2.0 * c.kneeDb);
    }

    return slope * over;
}

/** The static reduction a feed-forward model settles on: min(x − G(x), Range). */
inline double staticReduction (const Curve& c, double x) noexcept
{
    return std::min (curveReduction (c, x), c.rangeDb);
}

/** G(x), the output level for the transfer-curve display, Range included. */
inline double curveOutput (const Curve& c, double x) noexcept
{
    return x - staticReduction (c, x);
}

/** dG/dx before Range: 1 below the knee, 1/R above it. */
inline double curveSlope (const Curve& c, double x) noexcept
{
    const auto over = x - c.thresholdDb;

    if (2.0 * over < -c.kneeDb)
        return 1.0;

    if (c.kneeDb > 0.0 && 2.0 * over <= c.kneeDb)
        return 1.0 + (c.inverseRatio - 1.0) * (over + 0.5 * c.kneeDb) / c.kneeDb;

    return c.inverseRatio;
}

/** The feedback inverse needs a finite ratio; models cap theirs far below this (NOTES.md, The ratio cap). */
inline constexpr double minimumFeedbackInverseRatio = 1.0e-3;

/**
    The feedback computer r_fb(y) = G⁻¹(y) − y: the reduction for which the output reads y. With
    it the closed loop settles on eq. 4 exactly, knee included. Inside the knee the root is taken
    in its rationalised form, 2v / (1 + √(1 + 4a·v)), which has no cancellation and holds at
    Ratio 1 (a = 0), where the demand is exactly 0.
*/
inline double feedbackDemand (const Curve& c, double y) noexcept
{
    const auto inverseRatio = std::max (c.inverseRatio, minimumFeedbackInverseRatio);
    const auto slope = 1.0 - inverseRatio;
    const auto halfKnee = 0.5 * c.kneeDb;
    const auto over = y - c.thresholdDb;
    const auto v = over + halfKnee;

    if (v <= 0.0)
        return 0.0;

    if (c.kneeDb > 0.0 && over <= halfKnee * inverseRatio)
    {
        const auto a = -slope / (2.0 * c.kneeDb);
        const auto vx = 2.0 * v / (1.0 + std::sqrt (std::max (1.0 + 4.0 * a * v, 0.0)));
        return vx - v;
    }

    return over * slope / inverseRatio;
}

/** G⁻¹(y), the input level whose output is y. */
inline double curveInverse (const Curve& c, double y) noexcept
{
    return y + feedbackDemand (c, y);
}

/**
    A change of Threshold, Ratio (as 1/R), Knee, Range or Link crossfades the reduction: while it
    lasts a model runs the ballistics of both settings and blends the two reductions with a
    raised-cosine weight over 20 ms, so the reduction's slope never steps, whatever the curves' own
    corners (the hard knee, Range's clamp) or a rising target meeting a hold that still releases
    would do on a ramp of the parameters (NOTES.md, Clicks). A target that moves during a fade
    waits for it to end, the latest one winning. Link is λ, 0 where it does not apply.
*/
class CurveFade
{
public:
    static constexpr double fadeMs = 20.0;

    void prepare (double sampleRate) noexcept
    {
        steps = std::max (1, (int) std::lround (fadeMs * 0.001 * sampleRate));
        position = steps;
    }

    /** Lands on c at once. */
    void jumpTo (const Curve& c, double link = 0.0) noexcept
    {
        from = to = { c, link };
        position = steps;
        pending = false;
    }

    void setTarget (const Curve& c, double link = 0.0) noexcept
    {
        const Shape s { c, link };

        if (position < steps || pending)
        {
            pending = ! same (s, to);
            waiting = s;
            return;
        }

        if (same (s, to))
            return;

        from = to;
        to = s;
        position = 0;
    }

    /**
        One sample on: the weight of target() for this sample, 1 when settled. Below 1 the
        reduction is source()'s plus this weight of the difference to target()'s.
    */
    double next() noexcept
    {
        if (position >= steps)
        {
            if (! pending)
                return 1.0;

            from = to;
            to = waiting;
            pending = false;
            position = 0;
        }

        return ++position >= steps ? 1.0 : 0.5 - 0.5 * std::cos (std::numbers::pi * position / steps);
    }

    /** Whether the last next() blended two settings. */
    bool fading() const noexcept { return position < steps; }

    /** Neither fading nor waiting: next() returns 1 until the target moves. */
    bool settled() const noexcept { return position >= steps && ! pending; }

    const Curve& source() const noexcept { return from.curve; }
    const Curve& target() const noexcept { return to.curve; }
    double sourceLink() const noexcept { return from.link; }
    double targetLink() const noexcept { return to.link; }

    /** Where it ends up, a waiting target included. */
    const Curve& finalCurve() const noexcept { return pending ? waiting.curve : to.curve; }

    static bool same (const Curve& a, const Curve& b) noexcept
    {
        using myzel::dsp::isExactly;
        return isExactly (a.thresholdDb, b.thresholdDb) && isExactly (a.inverseRatio, b.inverseRatio)
            && isExactly (a.kneeDb, b.kneeDb) && isExactly (a.rangeDb, b.rangeDb);
    }

private:
    struct Shape
    {
        Curve curve;
        double link = 0.0;
    };

    static bool same (const Shape& a, const Shape& b) noexcept { return same (a.curve, b.curve) && myzel::dsp::isExactly (a.link, b.link); }

    Shape from, to, waiting;
    int steps = 960, position = 960;
    bool pending = false;
};

} // namespace basalt::dynamics
