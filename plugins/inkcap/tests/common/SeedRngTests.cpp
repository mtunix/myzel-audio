/*
    The counter-based random numbers: golden values that pin the frozen hash, then the
    properties the modules rely on (uniformity, independence of streams and seeds, value noise
    that is smooth and bounded, every value addressable by its index alone).
*/

#include "dsp/common/SeedRng.h"

#include "common/dsp/FloatCompare.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <vector>

using namespace inkcap::dsp;
using myzel::dsp::isExactly;

namespace
{
    // From tests/common/validation/seed_reference.py, an independent implementation of NOTES.md.
    struct BitsVector
    {
        uint32_t seed;
        uint32_t stream;
        int64_t index;
        uint64_t expected;
    };

    constexpr std::array<BitsVector, 9> bitsVectors { {
        { 0x00000000u, 0x0101u, 0ll, 0xCC0F9E19A6092ABFull },
        { 0x00000000u, 0x0101u, 1ll, 0xD54F33DE5F1152B4ull },
        { 0x3F2A91C7u, 0x0101u, 0ll, 0xB07E6BA59647F500ull },
        { 0x3F2A91C7u, 0x0101u, 17ll, 0x68072597DA2B1DB5ull },
        { 0x3F2A91C7u, 0x0102u, 17ll, 0x0EBC8C0B0215F1DBull },
        { 0x3F2A91C7u, 0x0501u, 172800000ll, 0x296A3E854665470Full },
        { 0xFFFFFFFFu, 0x0601u, -1ll, 0x64DD2208ACB242CAull },
        { 0x43930A3Du, 0x0103u, -96ll, 0x29D0A55A23B91B74ull },
        { 0x00000001u, 0x0401u, 1099511627781ll, 0x613E43F2F964B2E6ull },
    } };

    // The hash is constexpr: a drift fails the build before it fails a test.
    static_assert (rand (0x3F2A91C7u, Stream::glitchSlots, 17) == 0x68072597DA2B1DB5ull);
    static_assert (rand (0xFFFFFFFFu, Stream::roomModulation, -1) == 0x64DD2208ACB242CAull);

    /** Pearson correlation of two sequences. */
    double correlation (const std::vector<double>& a, const std::vector<double>& b)
    {
        double ma = 0, mb = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            ma += a[i];
            mb += b[i];
        }
        ma /= (double) a.size();
        mb /= (double) b.size();

        double sab = 0, saa = 0, sbb = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            sab += (a[i] - ma) * (b[i] - mb);
            saa += (a[i] - ma) * (a[i] - ma);
            sbb += (b[i] - mb) * (b[i] - mb);
        }
        return sab / std::sqrt (saa * sbb);
    }

    /** Chi-square of n uniform draws over k buckets. */
    double chiSquare (const SeedRng& rng, int n, int k, int64_t start = 0)
    {
        std::vector<int> counts ((size_t) k, 0);
        for (int i = 0; i < n; ++i)
            ++counts[(size_t) rng.below (start + i, k)];

        const auto expected = (double) n / (double) k;
        double chi = 0.0;
        for (const auto c : counts)
            chi += ((double) c - expected) * ((double) c - expected) / expected;
        return chi;
    }
}

TEST_CASE ("SeedRng: the hash is frozen (golden values)", "[seeds][golden]")
{
    for (const auto& v : bitsVectors)
    {
        INFO ("seed " << v.seed << " stream " << v.stream << " index " << v.index);
        CHECK (SeedRng (v.seed, static_cast<Stream> (v.stream)).bits (v.index) == v.expected);
    }

    const SeedRng shape (0x3F2A91C7u, Stream::glitchShape);
    CHECK (shape.lane (0).bits (5) == 0xBC30D483ABA76050ull);
    CHECK (shape.lane (1).bits (5) == 0xC7480A57B433E77Aull);
    CHECK (shape.lane (2).bits (5) == 0x8997E29789568546ull);

    // The conversions are exact (dyadic): the reference's doubles match to the bit.
    const SeedRng slots (0x3F2A91C7u, Stream::glitchSlots);
    constexpr std::array<std::array<double, 2>, 4> uniforms { {
        { 0.689429023672602, 0.3788580473452039 },
        { 0.04658122865259062, -0.9068375426948188 },
        { 0.10033882608180122, -0.7993223478363976 },
        { 0.2546208288679087, -0.4907583422641826 },
    } };
    for (int i = 0; i < 4; ++i)
    {
        CHECK (isExactly (slots.uniform (i), uniforms[(size_t) i][0]));
        CHECK (isExactly (slots.bipolar (i), uniforms[(size_t) i][1]));
    }

    const SeedRng drift (0x43930A3Du, Stream::warbleDrift);
    constexpr std::array<std::array<double, 2>, 7> smooth { {
        { 0.0, 0.5095891595179746 },
        { 0.25, 0.5568547961213767 },
        { 0.5, 0.6050133532452684 },
        { 1.0, 0.6154301475976722 },
        { 7.75, 0.020900916201252943 },
        { -3.5, 0.2218714345894654 },
        { 1234567.125, -0.3516300070154134 },
    } };
    for (const auto& [position, expected] : smooth)
    {
        INFO ("position " << position);
        CHECK (std::abs (drift.smooth (position) - expected) < 1.0e-12);
    }
}

TEST_CASE ("SeedRng: a value depends on its index only, never on what was drawn before", "[seeds]")
{
    const SeedRng a (0x1234ABCDu, Stream::glitchSlots);
    std::vector<uint64_t> forward;
    for (int64_t i = -50; i < 50; ++i)
        forward.push_back (a.bits (i));

    // Backwards, with other draws in between, from a copy: the same numbers.
    const SeedRng b = a;
    const SeedRng other (0x1234ABCDu, Stream::hissNoise);
    for (int64_t i = 49; i >= -50; --i)
    {
        (void) other.bits (i * 7);
        CHECK (b.bits (i) == forward[(size_t) (i + 50)]);
    }
}

TEST_CASE ("SeedRng: uniform, below and chance are uniform and in range", "[seeds]")
{
    constexpr int n = 200000;

    for (const auto stream : { Stream::glitchSlots, Stream::hissNoise, Stream::bitsJitter })
    {
        const SeedRng rng (0xC0FFEE11u, stream);
        double sum = 0.0, lo = 1.0, hi = 0.0;
        std::array<int, 64> ones {};

        for (int i = 0; i < n; ++i)
        {
            const auto u = rng.uniform (i);
            sum += u;
            lo = std::min (lo, u);
            hi = std::max (hi, u);

            const auto bits = rng.bits (i);
            for (int b = 0; b < 64; ++b)
                ones[(size_t) b] += (int) ((bits >> b) & 1u);
        }

        CHECK (lo >= 0.0);
        CHECK (hi < 1.0);
        CHECK (std::abs (sum / n - 0.5) < 0.005);           // 0.5 +- 7 sigma
        for (int b = 0; b < 64; ++b)
        {
            INFO ("bit " << b);
            CHECK (std::abs (ones[(size_t) b] - n / 2) < 2000);  // +- 9 sigma
        }

        // 63 degrees of freedom: mean 63, sd 11.2; the bound is about 5 sigma.
        CHECK (chiSquare (rng, n, 64) < 120.0);
        CHECK (chiSquare (rng, n, 7, -n / 2) < 40.0);
    }

    const SeedRng rng (7u, Stream::glitchType);
    for (int i = 0; i < 1000; ++i)
    {
        CHECK_FALSE (rng.chance (i, 0.0));
        CHECK (rng.chance (i, 1.0));
        const auto k = rng.below (i, 5);
        CHECK (k >= 0);
        CHECK (k < 5);
        CHECK (rng.below (i, 1) == 0);
        CHECK (rng.below (i, 0) == 0);
        CHECK (rng.bipolar (i) >= -1.0);
        CHECK (rng.bipolar (i) < 1.0);
    }

    int fired = 0;
    for (int i = 0; i < n; ++i)
        fired += rng.chance (i, 0.25) ? 1 : 0;
    CHECK (std::abs (fired - n / 4) < 1500);
}

TEST_CASE ("SeedRng: streams, lanes, neighbouring indices and neighbouring seeds are independent", "[seeds]")
{
    constexpr int n = 50000;
    const SeedRng a (0x3F2A91C7u, Stream::glitchSlots);
    const SeedRng b (0x3F2A91C7u, Stream::glitchType);
    const SeedRng c (0x3F2A91C6u, Stream::glitchSlots);     // one bit away in the seed
    const auto lane = a.lane (0);

    std::vector<double> va, vb, vc, vl, shifted;
    for (int i = 0; i < n; ++i)
    {
        va.push_back (a.uniform (i));
        vb.push_back (b.uniform (i));
        vc.push_back (c.uniform (i));
        vl.push_back (lane.uniform (i));
        shifted.push_back (a.uniform (i + 1));
    }

    // |r| of independent sequences: sd 1 / sqrt (n) = 0.0045.
    CHECK (std::abs (correlation (va, vb)) < 0.025);
    CHECK (std::abs (correlation (va, vc)) < 0.025);
    CHECK (std::abs (correlation (va, vl)) < 0.025);
    CHECK (std::abs (correlation (va, shifted)) < 0.025);

    // Avalanche: flipping one seed bit flips half the output bits on average.
    double flipped = 0.0;
    int count = 0;
    for (uint32_t bit = 0; bit < 32; ++bit)
        for (int64_t i = 0; i < 64; ++i)
        {
            const SeedRng x (0x3F2A91C7u, Stream::hissCrackle);
            const SeedRng y (0x3F2A91C7u ^ (1u << bit), Stream::hissCrackle);
            flipped += std::popcount (x.bits (i) ^ y.bits (i));
            ++count;
        }
    CHECK (std::abs (flipped / count - 32.0) < 0.5);

    // Different streams never share a key, even for seed 0.
    CHECK (SeedRng (0u, Stream::glitchSlots).key() != SeedRng (0u, Stream::glitchType).key());
    CHECK (SeedRng (0u, Stream::glitchSlots).key() != 0u);
}

TEST_CASE ("SeedRng: value noise is bounded, C2-continuous and lively", "[seeds]")
{
    const SeedRng rng (0x43930A3Du, Stream::warbleDrift);

    // At the lattice points the B-spline is (v[k-1] + 4 v[k] + v[k+1]) / 6.
    for (int64_t k = -20; k < 20; ++k)
    {
        const auto expected = (rng.bipolar (k - 1) + 4.0 * rng.bipolar (k) + rng.bipolar (k + 1)) / 6.0;
        CHECK (std::abs (rng.smooth ((double) k) - expected) < 1.0e-14);
    }

    // Value, slope and curvature agree from both sides of each lattice point.
    constexpr double h = 1.0e-4;
    double worstValue = 0, worstSlope = 0, worstCurve = 0;
    for (int k = -10; k < 10; ++k)
    {
        const auto x = (double) k;
        const auto left = rng.smooth (x - h), mid = rng.smooth (x), right = rng.smooth (x + h);
        const auto left2 = rng.smooth (x - 2 * h), right2 = rng.smooth (x + 2 * h);
        worstValue = std::max (worstValue, std::abs (right - left));
        const auto slopeLeft = (mid - left) / h, slopeRight = (right - mid) / h;
        worstSlope = std::max (worstSlope, std::abs (slopeRight - slopeLeft));
        const auto curveLeft = (mid - 2 * left + left2) / (h * h), curveRight = (right2 - 2 * right + mid) / (h * h);
        worstCurve = std::max (worstCurve, std::abs (curveRight - curveLeft));
    }
    CHECK (worstValue < 1.0e-3);    // continuous (a step would be ~0.1 .. 1)
    CHECK (worstSlope < 1.0e-3);    // C1
    CHECK (worstCurve < 0.05);      // C2: a jump in the second derivative would be order 1

    double sumSquares = 0.0, peak = 0.0;
    constexpr int n = 200000;
    for (int i = 0; i < n; ++i)
    {
        const auto v = rng.smooth (i * 0.0137 - 1000.0);
        sumSquares += v * v;
        peak = std::max (peak, std::abs (v));
        REQUIRE (std::abs (v) <= 1.0);
    }
    const auto rms = std::sqrt (sumSquares / n);
    CHECK (rms > 0.37);
    CHECK (rms < 0.43);
    CHECK (peak > 0.8);

    for (int i = -100; i < 100; ++i)
    {
        const auto position = i * 0.37;
        CHECK (isExactly (rng.held (position), rng.bipolar ((int64_t) std::floor (position))));
    }
}
