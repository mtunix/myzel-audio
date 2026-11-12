// Golden-file regression tests: every machine in every mode, a pink-noise burst through the full
// chain at fixed settings, stored in tests/golden/<machine>-<mode>.f32 (interleaved stereo
// float32, 0.5 s at 48 kHz). A refactor that changes the sound fails here. After an intended
// sound change, re-record (and say so in the commit):
//
//     MULBERRY_UPDATE_GOLDEN=1 build/plugins/mulberry/tests/MulberryTests "[golden]"

#include "support/EchoAnalysis.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>

using namespace mulberry;

namespace
{
    constexpr std::array<const char*, numModes> modeKeys { "single", "dual", "pingpong", "pattern" };

    dsp::ChainSettings goldenSettings (Machine m, Mode mode)
    {
        auto s = analysis::wetOnly (m);
        s.timeMs = { 120.0f, 170.0f };
        s.feedback = 0.6f;
        s.driveDb = 6.0f;
        s.wobble = 0.5f;
        s.diffuse = 0.2f;
        s.lowCutHz = 80.0f;
        s.highCutHz = 9000.0f;
        s.paramA = 0.3f;
        s.paramB = 0.7f;
        s.plan.mode = mode;
        s.plan.width = 0.8f;
        s.plan.steps = 5;
        s.plan.stepLevels = { 1.0f, 0.4f, 0.8f, 0.2f, 0.9f };
        s.plan.groove = 0.2f;
        s.plan.crossFeed = 0.3f;
        return s;
    }

    std::string goldenPath (Machine m, Mode mode)
    {
        return std::string (MULBERRY_TEST_DATA_DIR) + "/golden/" + info (m).key + "-" + modeKeys[(size_t) mode] + ".f32";
    }
}

TEST_CASE ("Golden: every machine in every mode matches its recorded response", "[golden]")
{
    const auto update = std::getenv ("MULBERRY_UPDATE_GOLDEN") != nullptr;
    const auto fs = 48000.0;
    const auto n = (size_t) (0.5 * fs);

    for (const auto& machine : machineInfos)
        for (int mode = 0; mode < numModes; ++mode)
        {
            INFO (machine.key << " " << modeKeys[(size_t) mode]);
            dsp::EchoChain chain;
            chain.prepare (fs, 256);

            auto in = analysis::Stereo::silence (n, fs);
            const auto l = analysis::pinkNoise (2400, 4242), r = analysis::pinkNoise (2400, 4343);
            for (size_t i = 0; i < l.size(); ++i)
            {
                in.left[i] = 0.5f * l[i];
                in.right[i] = 0.5f * r[i];
            }
            const auto out = analysis::render (chain, goldenSettings (machine.machine, static_cast<Mode> (mode)), in, n);
            REQUIRE (analysis::allFinite (out));

            std::vector<float> interleaved (2 * n);
            for (size_t i = 0; i < n; ++i)
            {
                interleaved[2 * i] = out.left[i];
                interleaved[2 * i + 1] = out.right[i];
            }

            const auto path = goldenPath (machine.machine, static_cast<Mode> (mode));
            if (update)
            {
                std::ofstream file (path, std::ios::binary);
                file.write (reinterpret_cast<const char*> (interleaved.data()), (std::streamsize) (interleaved.size() * sizeof (float)));
                continue;
            }

            std::ifstream file (path, std::ios::binary);
            if (! file)
            {
                WARN ("no golden file for " << machine.key << " " << modeKeys[(size_t) mode] << ": record with MULBERRY_UPDATE_GOLDEN=1");
                continue;
            }

            std::vector<float> golden (interleaved.size());
            file.read (reinterpret_cast<char*> (golden.data()), (std::streamsize) (golden.size() * sizeof (float)));
            REQUIRE ((size_t) file.gcount() == golden.size() * sizeof (float));

            // Error energy, not the worst sample: feedback loops amplify rounding differences
            // between compilers and libms. Any real change lands far above -60 dB.
            double errorSq = 0.0, signalSq = 0.0;
            for (size_t i = 0; i < golden.size(); ++i)
            {
                const double e = (double) interleaved[i] - golden[i];
                errorSq += e * e;
                signalSq += (double) golden[i] * golden[i];
            }
            const auto errorDb = 10.0 * std::log10 ((errorSq + 1.0e-30) / (signalSq + 1.0e-30));
            INFO ("difference " << errorDb << " dB relative to the reference");
            CHECK (errorDb < -60.0);
        }
}
