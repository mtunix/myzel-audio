// Unit tests of the shared reverb building blocks (src/dsp/common). The properties tested here are
// the ones the algorithms' stability rests on.

#include "dsp/Mappings.h"
#include "dsp/common/Fdn.h"
#include "dsp/common/ReverbBlocks.h"

#include <catch2/catch_test_macros.hpp>

#include <complex>

using namespace cerulean::dsp;

namespace
{
    /** |H(e^jw)| of a filter measured by running a sine through it (steady state). */
    template <typename Process>
    double gainAt (double hz, double fs, Process&& process)
    {
        const auto n = (int) (fs * 0.5);
        double inSq = 0.0, outSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto x = (float) std::sin (2.0 * 3.14159265358979 * hz * i / fs);
            const auto y = process (x);
            if (i > n / 2)
            {
                inSq += (double) x * x;
                outSq += (double) y * y;
            }
        }
        return std::sqrt (outSq / inSq);
    }
}

TEST_CASE ("Blocks: Lagrange reads never amplify (|H| <= 1 at every fraction)", "[blocks]")
{
    for (const auto frac : { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f })
        for (const auto hz : { 100.0, 1000.0, 5000.0, 12000.0, 20000.0 })
        {
            DelayLine line;
            line.setMaxDelay (64);
            const auto g = gainAt (hz, 48000.0, [&] (float x)
            {
                line.write (x);
                return line.readCubic (10.0f + frac);
            });
            INFO ("fraction " << frac << ", " << hz << " Hz: " << g);
            CHECK (g <= 1.0 + 1.0e-4);
        }
}

TEST_CASE ("Blocks: integer Lagrange reads are exact", "[blocks]")
{
    DelayLine line;
    line.setMaxDelay (32);
    for (int i = 0; i < 100; ++i)
        line.write ((float) i);
    CHECK (std::abs (line.readCubic (5.0f) - 95.0f) < 1.0e-4f);
    CHECK (std::abs (line.read (1) - 99.0f) < 1.0e-6f);
}

TEST_CASE ("Blocks: decay filter never exceeds its largest band gain", "[blocks]")
{
    for (const auto& [gl, gm, gh] : { std::tuple { 0.9f, 0.7f, 0.3f }, std::tuple { 0.4f, 0.8f, 0.95f },
                                     std::tuple { 0.99f, 0.5f, 0.99f } })
    {
        const auto limit = std::max ({ gl, gm, gh });
        for (const auto hz : { 30.0, 250.0, 1000.0, 4000.0, 15000.0 })
        {
            DecayFilter f;
            f.setCrossovers (250.0f, 4000.0f, 48000.0);
            f.setGains (gl, gm, gh);
            const auto g = gainAt (hz, 48000.0, [&] (float x) { return f.process (x); });
            INFO (hz << " Hz: " << g << " (limit " << limit << ")");
            CHECK (g <= limit + 1.0e-3);
        }
    }
}

TEST_CASE ("Blocks: Hadamard and Householder mixing preserve energy", "[blocks]")
{
    Rng rng (5);
    for (int trial = 0; trial < 20; ++trial)
    {
        std::array<float, 16> a {}, b {};
        float before = 0.0f;
        for (size_t i = 0; i < 16; ++i)
        {
            a[i] = b[i] = rng.bipolar();
            before += a[i] * a[i];
        }
        hadamardInPlace<16> (a.data());
        householderInPlace<16> (b.data());

        float ea = 0.0f, eb = 0.0f;
        for (size_t i = 0; i < 16; ++i)
        {
            ea += a[i] * a[i];
            eb += b[i] * b[i];
        }
        CHECK (std::abs (ea - before) < 1.0e-4f * before);
        CHECK (std::abs (eb - before) < 1.0e-4f * before);
    }
}

TEST_CASE ("Blocks: Hadamard sign rows are orthogonal", "[blocks]")
{
    for (int r1 = 0; r1 < 8; ++r1)
        for (int r2 = 0; r2 < 8; ++r2)
        {
            float dot = 0.0f;
            for (int c = 0; c < 16; ++c)
                dot += hadamardSign (r1, c) * hadamardSign (r2, c);
            CHECK (std::abs (dot - (r1 == r2 ? 16.0f : 0.0f)) < 1.0e-6f);
        }
}

TEST_CASE ("Blocks: band gains hit the RT60 and freeze makes them exactly 1", "[blocks]")
{
    const auto g = computeBandGains (4800.0f, 2.0f, 1.0f, 0.5f, 0.0f, 48000.0f);
    // 0.1 s per pass at RT60 2 s: -3 dB per pass.
    CHECK (std::abs (20.0f * std::log10 (g.mid) + 3.0f) < 1.0e-3f);
    CHECK (g.high < g.mid);

    const auto frozen = computeBandGains (4800.0f, 2.0f, 0.5f, 0.2f, 1.0f, 48000.0f);
    CHECK (frozen.low >= 1.0f);
    CHECK (frozen.mid >= 1.0f);
    CHECK (frozen.high >= 1.0f);
}

TEST_CASE ("Blocks: random generators are deterministic per seed", "[blocks]")
{
    Rng a (42), b (42), c (43);
    bool differs = false;
    for (int i = 0; i < 100; ++i)
    {
        const auto x = a.next();
        CHECK (x == b.next());
        differs |= x != c.next();
    }
    CHECK (differs);

    SmoothRandom r;
    r.setSeed (9);
    r.setRate (3.0f, 48000.0);
    std::vector<float> first;
    for (int i = 0; i < 1000; ++i)
        first.push_back (r.next());
    r.reset();
    for (int i = 0; i < 1000; ++i)
        CHECK (std::abs (r.next() - first[(size_t) i]) <= 0.0f);
}

TEST_CASE ("Blocks: glide is slew-limited and arrives", "[blocks]")
{
    GlideValue g;
    g.prepare (48000.0, 50.0f, 0.1f);
    g.snap (100.0f);
    g.setTarget (5000.0f);
    float previous = g.get();
    for (int i = 0; i < 96000; ++i)   // 4900 samples at <= 0.1 per sample: ~1 s, then the approach
    {
        const auto v = g.next();
        CHECK (v - previous <= 0.1f + 1.0e-4f);
        previous = v;
    }
    CHECK (std::abs (g.get() - 5000.0f) < 1.0f);
}

TEST_CASE ("Blocks: a frozen FDN is lossless and an unfrozen one decays", "[blocks]")
{
    constexpr std::array<float, 8> ms { 29.0f, 31.7f, 37.3f, 41.1f, 43.9f, 47.3f, 53.1f, 59.3f };

    for (const auto freeze : { 0.0f, 1.0f })
    {
        Fdn<8> fdn;
        fdn.prepare (48000.0, { .delayMs = ms, .maxScale = 2.0f, .maxModMs = 1.0f, .nominalModHz = 0.5f, .seed = 3 });

        Fdn<8>::Controls c;
        c.rt60 = 1.0f;
        c.freeze = freeze;
        c.modDepthMs = 1.0f;

        std::array<float, 8> in {}, out {};
        auto energyOver = [&] (int samples, bool impulse)
        {
            double e = 0.0;
            for (int pos = 0; pos < samples; pos += 32)
            {
                fdn.update (32, c);
                for (int k = 0; k < 32; ++k)
                {
                    in.fill (0.0f);
                    if (impulse && pos == 0 && k == 0)
                        in.fill (1.0f);
                    fdn.process (in.data(), out.data());
                    for (const auto y : out)
                        e += (double) y * y;
                }
            }
            return e;
        };

        energyOver (48000, true);
        const auto a = energyOver (48000, false);
        energyOver (48000 * 3, false);
        const auto b = energyOver (48000, false);

        INFO ("freeze " << freeze << ": energy second 2 " << a << ", second 6 " << b);
        if (freeze >= 1.0f)
            CHECK (std::abs (10.0 * std::log10 (b / a)) < 0.1);
        else
            CHECK (10.0 * std::log10 (b / a) < -200.0);
    }
}

TEST_CASE ("Blocks: mappings", "[blocks]")
{
    CHECK (std::abs (maps::sizeToScale (0.5f) - 1.0f) < 1.0e-6f);
    CHECK (maps::springCount (0.0f) == 1);
    CHECK (maps::springCount (1.0f) == 3);
    CHECK (maps::haloIntervalIndex (1.0f) == maps::numHaloIntervals - 1);
    CHECK (std::abs (maps::riseTimeMs (0.0f) - 50.0f) < 1.0e-3f);
    CHECK (std::abs (maps::riseTimeMs (1.0f) - 4000.0f) < 0.1f);
    CHECK (std::abs (maps::echoTimeMs (1.0f) - 1500.0f) < 0.1f);

    const auto flat = maps::toneToHighDecay (0.0f);
    const auto dark = maps::toneToHighDecay (-1.0f);
    const auto bright = maps::toneToHighDecay (1.0f);
    CHECK (dark.ratio < flat.ratio);
    CHECK (flat.ratio < bright.ratio);
    CHECK (bright.ratio <= 1.0f);
    CHECK (dark.crossoverHz < flat.crossoverHz);
}
