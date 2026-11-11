// Golden-file regression tests: every algorithm's impulse response at fixed settings is stored in
// tests/golden/<name>.f32 (interleaved stereo float32). A refactor that changes the sound fails
// here. After an intended sound change, re-record:
//
//     CERULEAN_UPDATE_GOLDEN=1 build/plugins/cerulean/tests/CeruleanTests "[golden]"

#include "analysis/ReverbAnalysis.h"
#include "dsp/AlgorithmInfo.h"
#include "dsp/chain/EngineFactory.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>

using namespace cerulean;

#if ! CERULEAN_ONLY_HALL
namespace
{
    /** A characteristic, non-default setting per algorithm: exercises Mod, Size, A and B. */
    dsp::EngineParams goldenParams (Algorithm a)
    {
        dsp::EngineParams p;
        p.decaySeconds = 2.5f;
        p.mod = 0.5f;
        p.sizeScale = 1.2f;
        p.tone = -0.2f;
        p.lowEnd = 1.2f;
        p.paramA = 0.6f;
        p.paramB = 0.7f;
        p.echoTimeMs = 240.0f;
        if (a == Algorithm::room)
            p.decaySeconds = 0.9f;
        return p;
    }

    std::string goldenPath (int a)
    {
        return std::string (CERULEAN_TEST_DATA_DIR) + "/golden/" + algorithmInfos[(size_t) a].key + ".f32";
    }

    analysis::Stereo render (int a)
    {
        const auto algorithm = static_cast<Algorithm> (a);
        auto engine = dsp::createEngine (algorithm, 1);
        engine->prepare (48000.0, dsp::Engine::maxSubBlock);
        engine->reset();

        // A short burst rather than a lone impulse, so swells and blooms produce output too.
        analysis::Stereo in;
        in.sampleRate = 48000.0;
        in.left = analysis::pinkNoise (2400, 4242);
        in.right = analysis::pinkNoise (2400, 4343);
        return analysis::renderEngine (*engine, goldenParams (algorithm), in, (size_t) (48000 * 1.5));
    }
}
#endif

TEST_CASE ("Golden: every algorithm matches its recorded response", "[golden]")
{
#if CERULEAN_ONLY_HALL
    SKIP ("built with CERULEAN_ONLY_HALL");
#else
    const auto update = std::getenv ("CERULEAN_UPDATE_GOLDEN") != nullptr;

    for (int a = 0; a < numAlgorithms; ++a)
    {
        INFO (algorithmInfos[(size_t) a].key);
        const auto out = render (a);
        REQUIRE (analysis::allFinite (out));

        std::vector<float> interleaved (out.size() * 2);
        for (size_t i = 0; i < out.size(); ++i)
        {
            interleaved[2 * i] = out.left[i];
            interleaved[2 * i + 1] = out.right[i];
        }

        if (update)
        {
            std::ofstream file (goldenPath (a), std::ios::binary);
            file.write (reinterpret_cast<const char*> (interleaved.data()), (std::streamsize) (interleaved.size() * sizeof (float)));
            continue;
        }

        std::ifstream file (goldenPath (a), std::ios::binary);
        if (! file)
        {
            WARN ("no golden file for " << algorithmInfos[(size_t) a].key << ": record with CERULEAN_UPDATE_GOLDEN=1");
            continue;
        }

        std::vector<float> golden (interleaved.size());
        file.read (reinterpret_cast<char*> (golden.data()), (std::streamsize) (golden.size() * sizeof (float)));
        REQUIRE ((size_t) file.gcount() == golden.size() * sizeof (float));

        // Error energy, not the worst sample: the modulated feedback loops amplify rounding
        // differences between compilers (FMA on Apple Silicon, MSVC's libm) to about -85 dB.
        // Any real change to an algorithm lands far above -60 dB.
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
#endif
}
