/*
    Bias (CircuitModel::setBias): every model moves its operating point, stays bounded and
    keeps its nominal sound bit-identical at 0.
*/

#include "dsp/models/ModelFactory.h"
#include "support/SignalAnalysis.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <random>

using namespace umber;

namespace
{
    constexpr double osRate = 192000.0;

    const char* name (int m) { return params::modelNames()[m].toRawUTF8(); }

    test::HarmonicResult measure (int m, double bias, double amplitude, double freq = 1000.0)
    {
        auto model = dsp::createModel (static_cast<Model> (m));
        model->prepare (osRate);
        model->setBias (bias);
        return test::measureHarmonics (*model, osRate, freq, amplitude);
    }

    double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }
}

TEST_CASE ("Bias report", "[.bias]")
{
    std::printf ("\nmodel     bias   level   gain dB   H2 dBc   H3 dBc   THD %%\n");

    for (int m = 0; m < numModels; ++m)
        for (double amp : { 0.25, 1.0, 4.0 })
            for (double bias : { -1.0, -0.5, 0.0, 0.5, 1.0 })
            {
                const auto r = measure (m, bias, amp);
                std::printf ("%-8s %5.1f %7.2f %9.2f %8.1f %8.1f %7.2f\n", name (m), bias, amp, r.gainDb,
                             db (r.harmonics[0] / r.fundamental), db (r.harmonics[1] / r.fundamental), 100.0 * r.thd);
            }
}

TEST_CASE ("Bias 0 is the nominal circuit, bit for bit", "[bias]")
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> dist (-2.0f, 2.0f);
    std::vector<float> input (20000);
    for (auto& v : input)
        v = dist (rng);

    for (int m = 0; m < numModels; ++m)
    {
        INFO (name (m));
        auto plain = dsp::createModel (static_cast<Model> (m));
        auto biased = dsp::createModel (static_cast<Model> (m));
        plain->prepare (osRate);
        biased->prepare (osRate);

        // Visit a bias and come back: the model must return exactly to nominal.
        biased->setBias (0.7);
        biased->setBias (0.0);
        biased->reset();

        auto a = input, b = input;
        plain->process (a.data(), (int) a.size());
        biased->process (b.data(), (int) b.size());

        bool identical = true;
        for (size_t i = 0; i < a.size(); ++i)
            identical = identical && a[i] == b[i];
        CHECK (identical);
    }
}

TEST_CASE ("Bias changes the harmonic balance but not the level much", "[bias]")
{
    for (int m = 0; m < numModels; ++m)
    {
        const auto nominal = measure (m, 0.0, 1.0);

        for (double bias : { -1.0, 1.0 })
        {
            const auto r = measure (m, bias, 1.0);
            const auto h2Shift = std::abs (db (r.harmonics[0] / r.fundamental) - db (nominal.harmonics[0] / nominal.fundamental));
            INFO (name (m) << " bias " << bias << ": gain " << r.gainDb << " dB (nominal " << nominal.gainDb
                           << "), H2 moved " << h2Shift << " dB");
            CHECK (std::abs (r.gainDb - nominal.gainDb) < 3.0);   // Auto gain stays roughly right
            CHECK (h2Shift > 3.0);                                // and the knob is clearly audible
        }
    }
}

TEST_CASE ("Biased circuits stay finite and bounded under abuse", "[bias]")
{
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> dist (-1000.0f, 1000.0f);
    std::vector<float> input (40000);
    for (auto& v : input)
        v = dist (rng);

    for (int m = 0; m < numModels; ++m)
        for (double bias : { -1.0, 1.0 })
        {
            INFO (name (m) << " bias " << bias);
            auto model = dsp::createModel (static_cast<Model> (m));
            model->prepare (osRate);
            model->setBias (bias);
            const auto r = test::measureBounds (*model, input);
            CHECK (r.allFinite);
            CHECK (r.peak < 8.0);
        }
}

TEST_CASE ("Moving the bias while playing does not click", "[bias]")
{
    // Sweep the bias across its range in 32-sample steps (as the chain does) under a steady
    // tone. The output's second difference must stay close to the worst steady-state one over
    // the operating points visited (a hot bias clips harder, so its waveform is sharper anyway).
    const int n = (int) (0.6 * osRate);
    const auto tone = test::sine (220.0, 0.5, osRate, n);

    auto maxD2 = [n] (const std::vector<float>& x)
    {
        double d = 0.0;
        for (int i = n / 4; i < n; ++i)
            d = std::max (d, (double) std::abs (x[(size_t) i] - 2.0f * x[(size_t) i - 1] + x[(size_t) i - 2]));
        return d;
    };

    for (int m = 0; m < numModels; ++m)
    {
        INFO (name (m));
        double reference = 0.0;

        for (double bias : { -1.0, -0.5, 0.0, 0.5, 1.0 })
        {
            auto steady = dsp::createModel (static_cast<Model> (m));
            steady->prepare (osRate);
            steady->setBias (bias);
            steady->reset();
            auto a = tone;
            steady->process (a.data(), n);
            reference = std::max (reference, maxD2 (a));
        }

        auto moving = dsp::createModel (static_cast<Model> (m));
        moving->prepare (osRate);
        auto b = tone;

        for (int start = 0; start < n; start += 32)
        {
            const auto t = (double) start / n;
            moving->setBias (std::sin (2.0 * 3.14159265358979 * 2.0 * t));   // two full sweeps
            moving->process (b.data() + start, std::min (32, n - start));
        }

        INFO ("worst steady " << reference << ", moving " << maxD2 (b));
        CHECK (maxD2 (b) < 1.5 * reference);

        // Silence stays (nearly) silent: the operating point moves without a thump.
        auto silent = dsp::createModel (static_cast<Model> (m));
        silent->prepare (osRate);
        std::vector<float> z ((size_t) n, 0.0f);

        for (int start = 0; start < n; start += 32)
        {
            silent->setBias (std::sin (2.0 * 3.14159265358979 * 2.0 * (double) start / n));
            silent->process (z.data() + start, std::min (32, n - start));
        }

        double peak = 0.0;
        for (auto v : z)
            peak = std::max (peak, (double) std::abs (v));

        INFO ("silent input while the bias sweeps: peak " << peak);
        CHECK (peak < 0.003);   // -50 dBFS, and only at sub-audio rates
    }
}


