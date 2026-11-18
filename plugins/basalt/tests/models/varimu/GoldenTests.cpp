// Golden renders at model level (NOTES.md, Gate table, Goldens): the reduction Vari-mu applies to
// two deterministic stereo keys, a mix bus at Ratio 4, Attack 50 ms, Auto on, and a drum bus at
// Attack 0.4 ms, Release 0.3 s, each file the left channel's reduction (Linked, so both) every
// fourth sample over 2 s, float32. The processor-level goldens on the corpus come with the chain.
// A change of the sound fails here; after an intended one only, re-record (and say so in the
// commit):
//
//     BASALT_UPDATE_GOLDEN=1 BasaltTests_varimu "[golden]"

#include "VariMuHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using namespace basalt::testing::varimu;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int decimation = 4;
    constexpr double fromS = 1.0, lengthS = 2.0, renderS = 3.0;

    /** Pink noise under a slow swell, with a louder accent every 0.6 s, the channels unrelated: a mix bus. */
    std::pair<Signal, Signal> mixBus()
    {
        const auto n = samplesFor (renderS, rate);
        auto l = pinkNoise (n, 21u), r = pinkNoise (n, 22u);
        for (int i = 0; i < n; ++i)
        {
            const auto t = i / rate;
            const auto swell = 0.25 * (1.0 + 0.6 * std::sin (2.0 * std::numbers::pi * 0.4 * t));
            const auto accent = 1.0 + 1.5 * std::exp (-std::fmod (t, 0.6) / 0.04);
            l[(size_t) i] *= swell * accent;
            r[(size_t) i] *= swell * accent * 0.8;
        }
        return { l, r };
    }

    /** Kicks (a falling 50 Hz sine) on the beat at 120 BPM, snares (noise bursts) on the off-beat, a hat every eighth. */
    std::pair<Signal, Signal> drumBus()
    {
        const auto n = samplesFor (renderS, rate);
        Signal l ((size_t) n, 0.0), r ((size_t) n, 0.0);
        std::mt19937 rng (31u);
        std::normal_distribution<double> white (0.0, 1.0);
        for (int i = 0; i < n; ++i)
        {
            const auto t = i / rate;
            const auto beat = std::fmod (t, 0.5), half = std::fmod (t + 0.25, 0.5), eighth = std::fmod (t, 0.25);
            const auto kick = 0.9 * std::exp (-beat / 0.12) * std::sin (2.0 * std::numbers::pi * (50.0 * beat + 60.0 * 0.03 * (1.0 - std::exp (-beat / 0.03))));
            const auto snare = 0.5 * std::exp (-half / 0.07) * white (rng);
            const auto hat = 0.15 * std::exp (-eighth / 0.015) * white (rng);
            l[(size_t) i] = kick + snare + hat;
            r[(size_t) i] = kick + 0.8 * snare + 1.2 * hat;
        }
        return { l, r };
    }

    struct Golden
    {
        const char* name;
        ModelParams params;
        std::pair<Signal, Signal> (*key)();
    };

    std::vector<Golden> goldens()
    {
        auto mix = params (-24.0, 0.25, 6.0, 40.0, 50.0, 300.0, true);
        auto drums = params (-20.0, 0.25, 6.0, 40.0, 0.4, 300.0, false);
        mix.linked = drums.linked = true;
        return { { "varimu-mix-bus", mix, &mixBus }, { "varimu-drum-bus", drums, &drumBus } };
    }

    std::string pathFor (const char* name)
    {
        return (std::filesystem::path (__FILE__).parent_path() / "golden" / (std::string (name) + ".f32")).string();
    }

    std::vector<float> render (const Golden& g)
    {
        const auto [l, r] = g.key();
        const Signal lookahead (l.size(), 0.0);
        auto m = prepared (rate);
        const auto out = runModel (m, g.params, l, &r, 256, &lookahead);
        std::vector<float> kept;
        for (auto i = (size_t) samplesFor (fromS, rate); i < (size_t) samplesFor (fromS + lengthS, rate); i += decimation)
            kept.push_back ((float) out[0][i]);
        return kept;
    }
}

TEST_CASE ("Vari-mu golden: the mix bus and drum bus reductions match their recorded renders", "[varimu][golden]")
{
    const auto update = std::getenv ("BASALT_UPDATE_GOLDEN") != nullptr;

    for (const auto& g : goldens())
    {
        INFO (g.name);
        const auto r = render (g);
        const auto path = pathFor (g.name);

        if (update)
        {
            std::filesystem::create_directories (std::filesystem::path (path).parent_path());
            std::ofstream file (path, std::ios::binary);
            file.write (reinterpret_cast<const char*> (r.data()), (std::streamsize) (r.size() * sizeof (float)));
            continue;
        }

        // A missing file fails: a checkout without the golden files must not pass with nothing compared.
        std::ifstream file (path, std::ios::binary);
        if (! file)
        {
            FAIL_CHECK ("no golden file for " << g.name << " (" << path << ")");
            continue;
        }

        std::vector<float> golden (r.size());
        file.read (reinterpret_cast<char*> (golden.data()), (std::streamsize) (golden.size() * sizeof (float)));
        REQUIRE ((size_t) file.gcount() == golden.size() * sizeof (float));

        auto worst = 0.0, peak = 0.0;
        for (size_t i = 0; i < golden.size(); ++i)
        {
            worst = std::max (worst, std::abs ((double) r[i] - golden[i]));
            peak = std::max (peak, (double) golden[i]);
        }
        std::printf ("Vari-mu golden %s: up to %.2f dB of reduction, the render within %.1e dB of it\n", g.name, peak, worst);
        REQUIRE (peak > 3.0);
        CHECK (worst < 1.0e-3);
    }
}
