// Golden-file regression tests: every built model (Console on both cards) through the full chain at
// fixed settings, stored in tests/golden/<model>[-alt].f32 (interleaved stereo float32, 1 s at
// 48 kHz). A refactor that changes the sound fails here. After an intended sound change,
// re-record (and say so in the commit):
//
//     VIRIDIAN_UPDATE_GOLDEN=1 build/plugins/viridian/tests/ViridianTests "[golden]"

#include "support/ChainGates.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <random>

#ifndef VIRIDIAN_TEST_DATA_DIR
 #error "VIRIDIAN_TEST_DATA_DIR must point at tests/"
#endif

using namespace viridian;

namespace
{
    constexpr double sampleRate = 48000.0;

    /**
        A log sweep at -12 dBFS, low bursts peaking at 0 dBFS (with the golden's Drive they reach
        the rails), and noise at -18 dBFS RMS; the right channel is the left a sample later at
        half level, so the channels differ. Box-Muller on raw mt19937 output: the same samples
        with every standard library.
    */
    gates::Stereo stimulus()
    {
        std::vector<float> x;
        const auto twoPi = gates::twoPi;
        double phase = 0.0;
        for (int i = 0; i < (int) (0.4 * sampleRate); ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (0.4 * sampleRate));
            phase += twoPi * f / sampleRate;
            x.push_back ((float) (0.25 * std::sin (phase)));
        }
        for (int burst = 0; burst < 2; ++burst)
            for (int i = 0; i < (int) (0.15 * sampleRate); ++i)
            {
                const double t = i / sampleRate;
                x.push_back ((float) (std::exp (-t * 18.0) * std::sin (twoPi * 60.0 * t)));
            }
        std::mt19937 rng (7);
        const auto uniform = [&rng] { return ((double) rng() + 0.5) / 4294967296.0; };
        while (x.size() < (size_t) sampleRate)
        {
            const auto radius = 0.126 * std::sqrt (-2.0 * std::log (uniform()));
            const auto angle = twoPi * uniform();
            x.push_back ((float) (radius * std::cos (angle)));
            x.push_back ((float) (radius * std::sin (angle)));
        }
        x.resize ((size_t) sampleRate);

        auto s = gates::Stereo::silence (x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            s.left[i] = x[i];
            s.right[i] = i > 0 ? 0.5f * x[i - 1] : 0.0f;
        }
        return s;
    }

    /** Every band and both filters at work, Drive into the rails on the bursts, Tolerance and Noise on. */
    dsp::ChainSettings goldenSettings (Model m, bool alt)
    {
        auto s = gates::consoleWorking (alt);
        s.model = m;
        s.driveDb = 9.0f;
        s.outputDb = -3.0f;
        s.tolerance = 0.3f;
        s.noise = 0.2f;
        return s;
    }

    std::string goldenPath (Model m, bool alt)
    {
        return std::string (VIRIDIAN_TEST_DATA_DIR) + "/golden/" + info (m).key + (alt ? "-alt" : "") + ".f32";
    }
} // namespace

TEST_CASE ("Golden: every built model on each card matches its recorded output", "[golden]")
{
    const auto update = std::getenv ("VIRIDIAN_UPDATE_GOLDEN") != nullptr;

    for (const auto& model : modelInfos())
        for (const bool alt : { false, true })
        {
            if (alt && ! model.alt.active)
                continue;
            dsp::EqChain chain;
            chain.prepare (sampleRate, 480, 2, goldenSettings (model.model, alt));
            if (chain.model (model.model).isPlaceholder())
                continue;
            INFO (model.key << (alt ? " (Alt)" : ""));

            const auto out = gates::render (chain, goldenSettings (model.model, alt), stimulus(), 480);
            std::vector<float> interleaved (2 * out.size());
            for (size_t i = 0; i < out.size(); ++i)
            {
                interleaved[2 * i] = out.left[i];
                interleaved[2 * i + 1] = out.right[i];
            }
            REQUIRE (std::all_of (interleaved.begin(), interleaved.end(), [] (float v) { return std::isfinite (v); }));

            const auto path = goldenPath (model.model, alt);
            if (update)
            {
                std::ofstream file (path, std::ios::binary);
                file.write (reinterpret_cast<const char*> (interleaved.data()), (std::streamsize) (interleaved.size() * sizeof (float)));
                REQUIRE (file.good());
                continue;
            }

            std::ifstream file (path, std::ios::binary);
            if (! file)
            {
                WARN ("no golden file for " << model.key << (alt ? " (Alt)" : "") << ": record with VIRIDIAN_UPDATE_GOLDEN=1");
                continue;
            }
            std::vector<float> golden (interleaved.size());
            file.read (reinterpret_cast<char*> (golden.data()), (std::streamsize) (golden.size() * sizeof (float)));
            REQUIRE ((size_t) file.gcount() == golden.size() * sizeof (float));

            double errorSq = 0.0, signalSq = 0.0;
            for (size_t i = 0; i < golden.size(); ++i)
            {
                const double e = (double) interleaved[i] - golden[i];
                errorSq += e * e;
                signalSq += (double) golden[i] * golden[i];
            }
            const auto errorDb = 10.0 * std::log10 ((errorSq + 1.0e-30) / (signalSq + 1.0e-30));
            INFO ("difference " << errorDb << " dB relative to the reference");
            CHECK (errorDb < -100.0);
        }
}
