// Golden renders: every Glitch type at fixed settings, input, seed and song position
// (tests/modules/glitch/golden/<type>.f32). Re-record only for an intended sound change:
//
//     INKCAP_UPDATE_GOLDEN=1 build/plugins/inkcap/tests/InkcapTests_glitch "[golden]"

#include "dsp/modules/glitch/GlitchModule.h"

#include "support/Golden.h"
#include "support/ModuleHarness.h"

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::testing;

TEST_CASE ("Glitch: every type matches its golden render", "[glitch][golden]")
{
    constexpr double fs = 48000.0;
    constexpr std::array<const char*, 5> keys { "skip", "chew", "stall", "bend", "any" };
    const auto in = analysis::programme (fs, 1.0, 61);

    for (int type = 0; type < (int) keys.size(); ++type)
    {
        INFO (keys[(size_t) type]);
        GlitchModule module;
        module.prepare ({ fs, 256, 2 });
        const GlitchSettings s { true, 0.8f, (GlitchType) type, 5, 0.6f, 0.58f };     // 1/16D with some swing
        const auto out = renderModule (module, s, Macros { 0.85f, 0.35f, 0x8719'8E02u }, in, 256, fs, { true, 8.0, 112.0 });
        checkGolden (std::string ("modules/glitch/golden/") + keys[(size_t) type] + ".f32", out);
    }
}
