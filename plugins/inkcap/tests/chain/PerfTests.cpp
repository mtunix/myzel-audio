/*
    The performance budget (PLAN.md): at 48 kHz, stereo, one core. Hidden ([.perf]): it reports,
    and checks only the idle budget and a loose ceiling, since CI machines vary.
*/

#include "support/ChainGates.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::gates;

TEST_CASE ("Chain: CPU at 48 kHz stereo, idle and with every module at full Damage", "[.perf]")
{
    ChainSettings idle;
    idle.glitch.on = idle.warble.on = idle.crush.on = idle.bits.on = idle.hiss.on = idle.room.on = false;
    const auto idleLoad = cpuPercent (idle, 48000.0);

    const auto fullLoad = cpuPercent (heaviest(), 48000.0);

    WARN ("idle " << idleLoad << " %, every module at full Damage at its heaviest " << fullLoad << " % (budget 0.2 % and 4 %)");
    CHECK (idleLoad <= 0.2);
    CHECK (fullLoad <= 4.0);
}

TEST_CASE ("Chain: CPU per module at full Damage, its heaviest type", "[.perf]")
{
    // Each module alone at its costliest setting (heaviest()), then all of them.
    double sum = 0.0;
    for (int m = 0; m < numModules; ++m)
    {
        const auto load = cpuPercent (heaviest (m), 48000.0);
        sum += load;
        WARN (moduleInfo ((Module) m).name << ": " << load << " %");
    }
    WARN ("all six: " << cpuPercent (heaviest(), 48000.0) << " % (the modules alone add up to " << sum << " %)");
}
