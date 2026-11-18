#include "dsp/models/varimu/VariMuCurve.h"

#include <array>

namespace basalt::models::varimu
{

namespace
{
    constexpr double tableFrom = riseCutoffWidths, tableTo = -riseCutoffWidths, tableStep = 1.0 / 16.0;
    constexpr int tableSize = (int) ((tableTo - tableFrom) / tableStep) + 1;

    // Initialised at load, before any audio thread runs; nothing reads it during static initialisation.
    const std::array<Softplus, tableSize> table = []
    {
        std::array<Softplus, tableSize> t {};
        for (int i = 0; i < tableSize; ++i)
        {
            const auto u = tableFrom + i * tableStep;
            const auto e = std::exp (-std::abs (u));
            t[(size_t) i] = { std::max (u, 0.0) + std::log1p (e), (u > 0.0 ? 1.0 : e) / (1.0 + e) };
        }
        return t;
    }();
}

Softplus softplus (double u) noexcept
{
    if (! (u > tableFrom))
        return {};
    if (u >= tableTo)
        return { u, 1.0 };

    const auto position = (u - tableFrom) / tableStep;
    const auto i = std::min ((int) position, tableSize - 2);
    const auto t = position - i, t2 = t * t, t3 = t2 * t;
    const auto& a = table[(size_t) i];
    const auto& b = table[(size_t) i + 1];

    return { (2.0 * t3 - 3.0 * t2 + 1.0) * a.value + (t3 - 2.0 * t2 + t) * tableStep * a.slope + (3.0 * t2 - 2.0 * t3) * b.value
                 + (t3 - t2) * tableStep * b.slope,
             6.0 * (t2 - t) * (a.value - b.value) / tableStep + (3.0 * t2 - 4.0 * t + 1.0) * a.slope + (3.0 * t2 - 2.0 * t) * b.slope };
}

} // namespace basalt::models::varimu
