#pragma once

#include <algorithm>
#include <cmath>

namespace umber::dsp::reel
{

/**
    Jiles–Atherton magnetic hysteresis of the tape coating, in normalised units
    (m = M / Ms, h = H / a, so the anhysteretic curve is the Langevin function of
    q = h + alpha * m).

    The model ODE (Jiles & Atherton 1986; real-time form as in J. Chowdhury, "Real-time
    physical modelling for analog tape machines", DAFx 2019):

        dm/dh = [ (1-c) dM (L(q) - m) / ((1-c) d k - alpha (L(q) - m))  +  c L'(q) ]
                / [ 1 - c alpha L'(q) ]

    with d = sign(dh/dt) and dM = 1 when (L(q) - m) has the sign of d, else 0 (dM removes
    the classic unphysical negative-susceptibility branch).

    Solver: JA is rate-independent, so instead of integrating in time we integrate dm/dh
    directly along the input field, which is piecewise linear between samples (d is then
    constant over each step). Each sample is one classic RK4 step in h; steps larger than
    `maxStep` are split into equal sub-steps. Because the step is in field space, accuracy is
    set by how far the field moves per sample, not by the sample rate, and the solver behaves
    identically at every rate. `maxStep` keeps every sub-step inside RK4's stability region for
    the stiffest (irreversible) relaxation, h-scale ((1-c) k - 2 alpha) / (1-c) = 0.3.

    Bounded work: field jumps longer than `memorySpan` (only reachable at absurd drive) use the
    JA's fading memory and integrate just the final `memorySpan` of the path (see process()),
    so a sample never costs more than `maxSubSteps` RK4 steps.

    Guards: the irreversible denominator is kept >= 25 % of (1-c) k (it cannot cross zero
    for the parameters used, the floor only matters if parameters are changed), coth / 1/q
    cancellation near q = 0 uses the Taylor series, and m is clamped to [-1, 1].
*/
class TapeHysteresis
{
public:
    struct Parameters
    {
        double alpha = 0.01;    // inter-domain coupling (normalised alpha Ms / a)
        double k     = 0.5;     // pinning / coercivity (normalised k / a): loop half-width
        double c     = 0.9;     // reversible fraction (bias-linearised; see NOTES.md)
    };

    static constexpr double maxStep     = 0.5;     // RK4 sub-step in h (stable: see NOTES.md)
    static constexpr int    maxSubSteps = 6;
    static constexpr double memorySpan  = maxStep * maxSubSteps;

    TapeHysteresis() noexcept = default;
    explicit TapeHysteresis (Parameters p) noexcept : params (p) {}

    void reset() noexcept
    {
        m = 0.0;
        hPrev = 0.0;
    }

    /** Advances to field h (normalised) and returns the normalised magnetisation. */
    double process (double h) noexcept
    {
        const auto dh = h - hPrev;
        if (! (dh < 0.0 || dh > 0.0))   // no movement (also rejects NaN): state is held
            return m;

        const auto dir = dh > 0.0 ? 1.0 : -1.0;
        auto hh = hPrev;
        auto mm = m;

        // Fading memory: along a monotone sweep the irreversible term pulls m onto the major
        // branch with a field "time constant" of about k, so after `memorySpan` (several k) the
        // start state is forgotten to ~1e-3 of its offset. For longer jumps, only the last
        // `memorySpan` of the path is integrated, starting from the major-branch estimate,
        // kept consistent with dm/dh >= 0 (m can only move with the field).
        if (std::abs (dh) > memorySpan)
        {
            hh = h - dir * memorySpan;
            mm = dir > 0.0 ? std::max (mm, branchEstimate (hh, dir))
                           : std::min (mm, branchEstimate (hh, dir));
        }

        const auto span = h - hh;
        const auto n = std::min (maxSubSteps, 1 + (int) (std::abs (span) * (1.0 / maxStep)));
        const auto s = span / n;

        for (int i = 0; i < n; ++i)
        {
            const auto k1 = s * slope (hh, mm, dir);
            const auto k2 = s * slope (hh + 0.5 * s, mm + 0.5 * k1, dir);
            const auto k3 = s * slope (hh + 0.5 * s, mm + 0.5 * k2, dir);
            const auto k4 = s * slope (hh + s, mm + k3, dir);
            mm = std::clamp (mm + (k1 + 2.0 * (k2 + k3) + k4) * (1.0 / 6.0), -1.0, 1.0);
            hh += s;
        }

        hPrev = h;
        m = mm;
        return m;
    }

    double magnetisation() const noexcept { return m; }

    /** dm/dh at the demagnetised origin (reversible part only): the small-signal gain. */
    double initialSusceptibility() const noexcept
    {
        const auto r = params.c / 3.0;
        return r / (1.0 - params.alpha * r);
    }

    /** Langevin function L(q) = coth q - 1/q and its derivative. */
    static void langevin (double q, double& L, double& dL) noexcept
    {
        const auto aq = std::abs (q);

        if (aq < 0.1)
        {
            const auto q2 = q * q;
            L  = q * (1.0 / 3.0 - q2 * (1.0 / 45.0 - q2 * (2.0 / 945.0 - q2 * (1.0 / 4725.0))));
            dL = 1.0 / 3.0 - q2 * (1.0 / 15.0 - q2 * (2.0 / 189.0 - q2 * (2.0 / 675.0)));
            return;
        }

        const auto e = std::exp (-2.0 * aq);        // in (0, 0.82] here
        const auto r = 1.0 / (1.0 - e);
        const auto inv = 1.0 / aq;
        const auto coth = (1.0 + e) * r;
        L  = std::copysign (coth - inv, q);
        dL = inv * inv - 4.0 * e * r * r;          // 1/q^2 - csch^2 q
    }

    /**
        Major-loop branch estimate at field h for a sweep in direction dir: quasi-steady on a
        branch dm/dh ~ L', which balances the irreversible term when L - m ~ dir k L'.
    */
    double branchEstimate (double h, double dir) const noexcept
    {
        double L, dL;
        langevin (h, L, dL);
        langevin (h + params.alpha * L, L, dL);
        return std::clamp (L - dir * params.k * dL, -1.0, 1.0);
    }

    /** dm/dh for field direction dir (+1 / -1). */
    double slope (double h, double mag, double dir) const noexcept
    {
        const auto& p = params;
        double L, dL;
        langevin (h + p.alpha * mag, L, dL);

        const auto diff = L - mag;
        double irreversible = 0.0;

        if (diff * dir > 0.0)
        {
            const auto pinning = (1.0 - p.c) * p.k;
            const auto den = std::max (pinning - p.alpha * std::abs (diff), 0.25 * pinning);
            irreversible = (1.0 - p.c) * std::abs (diff) / den;
        }

        const auto cdL = p.c * dL;
        return (irreversible + cdL) / (1.0 - p.alpha * cdL);
    }

private:
    Parameters params;
    double m = 0.0, hPrev = 0.0;
};

} // namespace umber::dsp::reel
