// Golden renders: every Room type at fixed settings, input, seed and song position, stored in
// tests/modules/room/golden/<type>.f32 (interleaved stereo float32, 0.5 s at 48 kHz). A refactor that
// changes the sound fails here. After an intended sound change, re-record (and say so in the commit):
//
//     INKCAP_UPDATE_GOLDEN=1 build/plugins/inkcap/tests/InkcapTests_room "[golden]"

#include "dsp/modules/room/RoomModule.h"

#include "support/ModuleHarness.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <string>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::testing;

namespace
{
    constexpr std::array<const char*, 4> typeKeys { "closet", "room", "spring", "hall" };

    RoomSettings goldenSettings (int type)
    {
        RoomSettings s;
        s.on = true;
        s.amount = 1.0f;
        s.type = (RoomType) type;
        s.size = 0.6f;
        s.duck = 0.3f;
        return s;
    }

    analysis::Stereo goldenInput (size_t n)
    {
        // 50 ms of programme, then the space's tail.
        auto in = analysis::programme (48000.0, (double) n / 48000.0, 42);
        for (auto* channel : { &in.left, &in.right })
            std::fill (channel->begin() + 2400, channel->end(), 0.0f);
        return in;
    }

    constexpr Macros goldenMacros { 0.9f, 0.5f, 0x4393'0A3Du };
}

TEST_CASE ("Room: every type matches its golden render", "[room][golden]")
{
    const auto update = std::getenv ("INKCAP_UPDATE_GOLDEN") != nullptr;
    constexpr double fs = 48000.0;
    const auto n = (size_t) (0.5 * fs);

    for (int type = 0; type < (int) typeKeys.size(); ++type)
    {
        INFO (typeKeys[(size_t) type]);
        RoomModule module;
        module.prepare ({ fs, 256, 2 });
        const auto out = renderModule (module, goldenSettings (type), goldenMacros, goldenInput (n), 256, fs, { true, 16.0, 120.0 });

        std::vector<float> interleaved (2 * n);
        for (size_t i = 0; i < n; ++i)
        {
            interleaved[2 * i] = out.left[i];
            interleaved[2 * i + 1] = out.right[i];
        }

        const auto path = std::string (INKCAP_TEST_DATA_DIR) + "/modules/room/golden/" + typeKeys[(size_t) type] + ".f32";
        if (update)
        {
            std::ofstream file (path, std::ios::binary);
            file.write (reinterpret_cast<const char*> (interleaved.data()), (std::streamsize) (interleaved.size() * sizeof (float)));
            continue;
        }

        std::ifstream file (path, std::ios::binary);
        if (! file)
        {
            WARN ("no golden file for " << typeKeys[(size_t) type] << ": record with INKCAP_UPDATE_GOLDEN=1");
            continue;
        }

        std::vector<float> golden (interleaved.size());
        file.read (reinterpret_cast<char*> (golden.data()), (std::streamsize) (golden.size() * sizeof (float)));
        REQUIRE ((size_t) file.gcount() == golden.size() * sizeof (float));

        // Error energy, not the worst sample: libms round differently. Any real change lands far above -60 dB.
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
