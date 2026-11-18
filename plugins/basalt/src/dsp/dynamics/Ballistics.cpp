#include "dsp/dynamics/Ballistics.h"

#include <array>
#include <cmath>

namespace basalt::dynamics
{

namespace
{
    // What is left of a unit step at t (in units of τ_A) after the cascade of h·τ_A then τ_A:
    // (h·e^(−t/h) − e^(−t)) / (h − 1), with its limit at h = 1.
    double remaining (double h, double t) noexcept
    {
        if (std::abs (h - 1.0) < 1.0e-7)
            return (1.0 + t) * std::exp (-t);

        return (h * std::exp (-t / h) - std::exp (-t)) / (h - 1.0);
    }

    constexpr int tablePoints = 256;
    constexpr double tableDecades = 4.0;
    constexpr double pointsPerDecade = (tablePoints - 1) / tableDecades;

    // h(ρ)/(ρ − 1) rather than h itself: it runs from 1 at ρ = 1 through a dip (0.86 near
    // ρ = 1.6) back to 1, so linear interpolation on 256 points stays within 0.04 % of h (64 points:
    // 0.54 %, near ρ = 1.25).
    struct Table
    {
        std::array<double, tablePoints> scaled {};

        Table() noexcept
        {
            scaled[0] = 1.0;
            for (int i = 1; i < tablePoints; ++i)
            {
                const auto rho = std::pow (10.0, i / pointsPerDecade);
                scaled[(size_t) i] = solveReleaseRatio (rho) / (rho - 1.0);
            }
        }
    };

    const Table table;
} // namespace

double solveReleaseRatio (double rho) noexcept
{
    constexpr auto target = 0.36787944117144233;  // 1/e

    // remaining() rises with h: from e^(−ρ) < 1/e at h → 0 to above 1/e at h = ρ.
    auto lo = 0.0, hi = rho;
    for (int i = 0; i < 200 && hi - lo > 1.0e-15 * rho; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        (remaining (mid, rho) < target ? lo : hi) = mid;
    }

    return 0.5 * (lo + hi);
}

double releaseRatio (double rho) noexcept
{
    // Written so a NaN reads the floor too: it must never reach the table's index.
    if (! (rho >= minimumReleaseRatio))
        rho = minimumReleaseRatio;

    if (rho >= 1.0e4)
        return rho - 1.0;

    const auto u = std::log10 (rho) * pointsPerDecade;
    const auto i = std::min ((int) u, tablePoints - 2);
    const auto f = u - i;
    const auto a = table.scaled[(size_t) i], b = table.scaled[(size_t) i + 1];
    return (rho - 1.0) * (a + f * (b - a));
}

} // namespace basalt::dynamics
