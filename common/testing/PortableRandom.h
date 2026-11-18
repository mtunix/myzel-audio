#pragma once

#include <cmath>
#include <cstdint>
#include <random>

namespace myzel::testing
{

/**
    Seeded test noise that is the same on every platform. std::mt19937 is specified bit for bit,
    but the std distributions are not: libstdc++, libc++ and MSVC turn the same draws into
    different numbers. This follows libstdc++'s algorithms (generate_canonical, the Marsaglia
    polar method), so signals measured on Linux stay the ones every CI runner sees.
*/
class PortableRandom
{
public:
    explicit PortableRandom (unsigned seed) : engine (seed) {}

    /** Uniform in [0, 1): std::uniform_real_distribution<double> in libstdc++. */
    double uniform() noexcept
    {
        const auto low = (double) engine();
        const auto sum = low + (double) engine() * 4294967296.0;
        const auto u = sum / 18446744073709551616.0;
        return u >= 1.0 ? std::nextafter (1.0, 0.0) : u;
    }

    double uniform (double low, double high) noexcept { return uniform() * (high - low) + low; }

    /** Uniform in [low, high) as std::uniform_real_distribution<float> in libstdc++. */
    float uniformFloat (float low, float high) noexcept
    {
        auto u = (float) engine() / 4294967296.0f;
        if (u >= 1.0f)
            u = std::nextafter (1.0f, 0.0f);
        return u * (high - low) + low;
    }

    /** Standard normal: std::normal_distribution<double> in libstdc++. */
    double gaussian() noexcept
    {
        if (hasSpare)
        {
            hasSpare = false;
            return spare;
        }
        double x, y, r2;
        do
        {
            x = 2.0 * uniform() - 1.0;
            y = 2.0 * uniform() - 1.0;
            r2 = x * x + y * y;
        } while (r2 > 1.0 || r2 == 0.0);
        const auto scale = std::sqrt (-2.0 * std::log (r2) / r2);
        spare = x * scale;
        hasSpare = true;
        return y * scale;
    }

    /** std::normal_distribution<double> (mean, sigma) in libstdc++. */
    double gaussian (double mean, double sigma) noexcept { return gaussian() * sigma + mean; }

private:
    std::mt19937 engine;
    double spare = 0.0;
    bool hasSpare = false;
};

} // namespace myzel::testing
