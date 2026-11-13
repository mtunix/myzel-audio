#pragma once

#include "dsp/common/SeedRng.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace inkcap::dsp
{

/**
    SeedRng::smooth for a position that moves a little each sample: the four lattice values
    around it are kept until the position leaves their cell, so a per-sample evaluation costs the
    spline, not four hashes. The same numbers as SeedRng::smooth, to the bit, from any position
    and in any order. Real-time safe.
*/
class ValueNoise
{
public:
    ValueNoise() = default;
    explicit ValueNoise (const SeedRng& source) noexcept : rng (source) {}

    void setSource (const SeedRng& source) noexcept
    {
        rng = source;
        cell = std::numeric_limits<int64_t>::min();
    }

    double at (double position) noexcept
    {
        const auto floored = std::floor (position);
        const auto k = (int64_t) floored;
        if (k != cell)
            fill (k);

        const auto f = position - floored;
        const auto f2 = f * f;
        const auto f3 = f2 * f;
        const auto w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
        const auto w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
        const auto w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
        const auto w3 = f3 / 6.0;
        return w0 * lattice[0] + w1 * lattice[1] + w2 * lattice[2] + w3 * lattice[3];
    }

private:
    void fill (int64_t k) noexcept
    {
        // One cell on (the usual case): three of the four values are already here.
        if (k == cell + 1)
        {
            lattice = { lattice[1], lattice[2], lattice[3], rng.bipolar (k + 2) };
        }
        else
        {
            for (int i = 0; i < 4; ++i)
                lattice[(size_t) i] = rng.bipolar (k - 1 + i);
        }
        cell = k;
    }

    SeedRng rng { 0, Stream::warbleDrift };
    int64_t cell = std::numeric_limits<int64_t>::min();
    std::array<double, 4> lattice {};
};

} // namespace inkcap::dsp
