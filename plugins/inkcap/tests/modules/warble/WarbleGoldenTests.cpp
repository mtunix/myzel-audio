// Golden renders: every Warble type, free and synced, at fixed settings, input, seed and song
// position (tests/modules/warble/golden/<type>[-sync].f32). Re-record only for an intended sound change:
//
//     INKCAP_UPDATE_GOLDEN=1 build/plugins/inkcap/tests/InkcapTests_warble "[golden]"

#include "dsp/modules/warble/WarbleModule.h"

#include "support/Golden.h"
#include "support/ModuleHarness.h"

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::testing;

TEST_CASE ("Warble: every type matches its golden render", "[warble][golden]")
{
    constexpr double fs = 48000.0;
    constexpr std::array<const char*, 3> keys { "wow", "flutter", "drift" };
    const auto in = analysis::programme (fs, 1.0, 62);

    for (int type = 0; type < (int) keys.size(); ++type)
        for (const auto sync : { false, true })
        {
            INFO (keys[(size_t) type] << (sync ? " synced" : " free"));
            WarbleModule module;
            module.prepare ({ fs, 256, 2 });
            const WarbleSettings s { true, 0.8f, (WarbleType) type, 0.6f, sync, 0.5f };
            const auto out = renderModule (module, s, Macros { 0.8f, 0.4f, 0x8719'8E02u }, in, 256, fs, { true, 8.0, 112.0 });
            checkGolden (std::string ("modules/warble/golden/") + keys[(size_t) type] + (sync ? "-sync" : "") + ".f32", out);
        }
}
