#pragma once

#include "common/dsp/JilesAtherton.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace myzel::dsp
{

/**
    The Langevin function L(q) = coth q - 1/q and its derivative, tabulated for q in [0, 32]
    (step 1/64, cubic Hermite on L, L' and L''): within 1e-10 of the closed forms, with no exp or
    division per call. Beyond 32, coth q is 1 to double precision.
*/
class LangevinTable
{
public:
    static constexpr double range = 32.0;
    static constexpr int perUnit = 64;
    static constexpr int size = (int) range * perUnit + 2;

    static const LangevinTable& instance()
    {
        static const LangevinTable table;
        return table;
    }

    void evaluate (double q, double& L, double& dL) const noexcept
    {
        const auto aq = std::abs (q);
        if (aq >= range)
        {
            const auto inv = 1.0 / aq;
            L = std::copysign (1.0 - inv, q);
            dL = inv * inv;
            return;
        }

        const auto x = aq * perUnit;
        const auto i = (int) x;
        const auto t = x - i;
        const auto& c = cubics[(size_t) i];
        L = std::copysign (((c.L[3] * t + c.L[2]) * t + c.L[1]) * t + c.L[0], q);
        dL = ((c.dL[3] * t + c.dL[2]) * t + c.dL[1]) * t + c.dL[0];
    }

private:
    /** A cubic in t = (q - q_i) * perUnit for L and for dL on each interval (Hermite, Horner form). */
    struct Cubic
    {
        std::array<double, 4> L, dL;
    };

    static std::array<double, 4> hermite (double y0, double d0, double y1, double d1) noexcept
    {
        // d0, d1 are derivatives in q; the interval is 1 / perUnit long in q.
        const auto s0 = d0 / perUnit, s1 = d1 / perUnit;
        return { y0, s0, 3.0 * (y1 - y0) - 2.0 * s0 - s1, 2.0 * (y0 - y1) + s0 + s1 };
    }

    LangevinTable()
    {
        struct Point { double L, dL, d2L; };
        std::vector<Point> points ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            const auto q = (double) i / perUnit;
            auto& p = points[(size_t) i];
            myzel::dsp::TapeHysteresis::langevin (q, p.L, p.dL);
            if (q < 0.1)
            {
                const auto q2 = q * q;
                p.d2L = q * (-2.0 / 15.0 + q2 * (8.0 / 189.0 - q2 * (12.0 / 675.0)));
            }
            else
            {
                const auto s = 1.0 / std::sinh (q);
                p.d2L = -2.0 / (q * q * q) + 2.0 * s * s / std::tanh (q);
            }
        }

        cubics.resize ((size_t) size - 1);
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const auto& a = points[i];
            const auto& b = points[i + 1];
            cubics[i] = { hermite (a.L, a.dL, b.L, b.dL), hermite (a.dL, a.d2L, b.dL, b.d2L) };
        }
    }

    std::vector<Cubic> cubics;
};

/**
    The Jiles-Atherton core of common/dsp/JilesAtherton.h (same equations, parameters, field
    integration and fading memory), solved for the 8x record path: the tabulated Langevin
    function and midpoint (RK2) sub-steps of at most 0.2 in field (RK2 is stable to 0.6 for the
    stiffest relaxation, field scale 0.3). A larger step left solver error that folded back as
    aliasing (Mulberry's machines/loop/NOTES.md); RK4 at 8x cost half again as much for no gain.
*/
class TapeCore
{
public:
    using Parameters = myzel::dsp::TapeHysteresis::Parameters;

    static constexpr double maxStep = 0.2;
    static constexpr int maxSubSteps = 16;
    static constexpr double memorySpan = maxStep * maxSubSteps;

    TapeCore() noexcept = default;
    explicit TapeCore (Parameters p) noexcept : params (p) {}

    void reset() noexcept
    {
        m = 0.0;
        hPrev = 0.0;
    }

    void setParameters (Parameters p) noexcept { params = p; }
    const Parameters& getParameters() const noexcept { return params; }

    double initialSusceptibility() const noexcept
    {
        const auto r = params.c / 3.0;
        return r / (1.0 - params.alpha * r);
    }

    double process (double h) noexcept
    {
        const auto dh = h - hPrev;
        if (! (dh < 0.0 || dh > 0.0))
            return m;

        const auto dir = dh > 0.0 ? 1.0 : -1.0;
        auto hh = hPrev;
        auto mm = m;

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
            mm = std::clamp (mm + k2, -1.0, 1.0);
            hh += s;
        }

        hPrev = h;
        m = mm;
        return m;
    }

private:
    double branchEstimate (double h, double dir) const noexcept
    {
        double L, dL;
        table->evaluate (h, L, dL);
        table->evaluate (h + params.alpha * L, L, dL);
        return std::clamp (L - dir * params.k * dL, -1.0, 1.0);
    }

    /**
        dm/dh as in TapeHysteresis::slope. 1 / (1 - alpha c L') by its series: alpha c L' is at most
        alpha / 3 (under 0.007 for every core here), so three terms are exact to 2e-9.
    */
    double slope (double h, double mag, double dir) const noexcept
    {
        const auto& p = params;
        double L, dL;
        table->evaluate (h + p.alpha * mag, L, dL);

        const auto diff = L - mag;
        const auto cdL = p.c * dL;
        const auto x = p.alpha * cdL;
        const auto feedback = 1.0 + x * (1.0 + x * (1.0 + x));
        if (diff * dir > 0.0)
        {
            const auto pinning = (1.0 - p.c) * p.k;
            const auto den = std::max (pinning - p.alpha * std::abs (diff), 0.25 * pinning);
            return ((1.0 - p.c) * std::abs (diff) / den + cdL) * feedback;
        }
        return cdL * feedback;
    }

    const LangevinTable* table = &LangevinTable::instance();
    Parameters params;
    double m = 0.0, hPrev = 0.0;
};

} // namespace myzel::dsp
