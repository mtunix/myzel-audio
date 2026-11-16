#include "dsp/common/RealFft.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

TEST_CASE ("RealFft: matches the direct DFT", "[detector][fft]")
{
    for (const auto order : { 2, 3, 6, 11, 13 })
    {
        const auto n = 1 << order;
        std::mt19937 rng ((unsigned) order);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> x ((size_t) n);
        for (auto& v : x)
            v = dist (rng);

        halite::dsp::RealFft fft;
        fft.prepare (order);
        REQUIRE (fft.size() == n);
        std::vector<float> re ((size_t) n / 2 + 1), im ((size_t) n / 2 + 1);
        fft.forward (x.data(), re.data(), im.data());

        // Bins checked: all of them for small sizes, a spread for the large ones.
        auto worst = 0.0;
        const auto step = std::max (1, n / 256);
        for (int k = 0; k <= n / 2; k += (k == n / 2 - 1 ? 1 : step))
        {
            auto dr = 0.0, di = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const auto phase = 2.0 * std::numbers::pi * (double) ((long long) k * i % n) / n;
                dr += x[(size_t) i] * std::cos (phase);
                di -= x[(size_t) i] * std::sin (phase);
            }
            worst = std::max (worst, std::hypot (re[(size_t) k] - dr, im[(size_t) k] - di));
        }

        // Float rounding grows with √n · log n; the bins themselves are about √n.
        INFO ("order " << order << ", largest error " << worst);
        CHECK (worst < 1.0e-5 * std::sqrt ((double) n) * order);
    }
}
