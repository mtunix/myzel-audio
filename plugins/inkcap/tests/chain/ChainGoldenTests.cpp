// Golden renders of the whole chain (tests/chain/golden/<name>.f32): the module goldens lock each
// module, these lock what the chain adds (the Damage curves, Match, Mix). Re-record only for an
// intended sound change:
//
//     INKCAP_UPDATE_GOLDEN=1 build/plugins/inkcap/tests/InkcapTests_chain "[golden]"

#include "support/ChainGates.h"
#include "support/Golden.h"

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::gates;
using namespace inkcap::testing;

TEST_CASE ("Chain: the defaults and every module match their golden renders", "[chain][golden]")
{
    constexpr double fs = 48000.0;
    const auto in = analysis::programme (fs, 1.0, 63);
    const Transport transport { true, 12.0, 96.0 };

    checkGolden ("chain/golden/defaults.f32", render (ChainSettings {}, in, 256, fs, transport));

    auto every = everyModule (0.7f, 0.45f, 0.6f, 4);
    every.mix = 0.8f;
    every.seed = 0x8719'8E02u;
    checkGolden ("chain/golden/every-module.f32", render (every, in, 256, fs, transport));
}
