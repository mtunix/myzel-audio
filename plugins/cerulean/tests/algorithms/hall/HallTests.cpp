#include "dsp/algorithms/hall/HallEngine.h"
#include "support/EngineGates.h"

namespace
{
    cerulean::gates::Options hallOptions()
    {
        cerulean::gates::Options o;
        o.echoDensityTargetS = 0.12;
        return o;
    }
}

CERULEAN_ENGINE_GATES ("[hall]", cerulean::dsp::HallEngine, hallOptions())

TEST_CASE ("Hall: Early adds a reflection cluster before the late field", "[hall]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::HallEngine> (48000.0);

    auto p = dsp::EngineParams {};
    p.paramA = 0.0f;
    const auto dry = analysis::impulseResponse (*e, p, 48000.0, 0.5);
    p.paramA = 1.0f;
    const auto withEarly = analysis::impulseResponse (*e, p, 48000.0, 0.5);

    // First 40 ms: the early cluster dominates.
    const auto a = analysis::rms (dry.left, 0, 1920), b = analysis::rms (withEarly.left, 0, 1920);
    INFO ("first 40 ms: no early " << analysis::toDb (a) << " dB, full early " << analysis::toDb (b) << " dB");
    CHECK (analysis::toDb (b) > analysis::toDb (a) + 6.0);
}

TEST_CASE ("Hall: Build slows the onset of the late field", "[hall]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::HallEngine> (48000.0);

    // Time until 10 % of the first second's energy has arrived.
    auto arrival = [] (const analysis::Stereo& ir)
    {
        double total = 0.0;
        for (size_t i = 0; i < 48000; ++i)
            total += (double) ir.left[i] * ir.left[i];
        double acc = 0.0;
        for (size_t i = 0; i < 48000; ++i)
        {
            acc += (double) ir.left[i] * ir.left[i];
            if (acc >= 0.1 * total)
                return (double) i / 48000.0;
        }
        return 1.0;
    };

    auto p = dsp::EngineParams {};
    p.paramA = 0.0f;
    p.paramB = 0.0f;
    const auto fast = arrival (analysis::impulseResponse (*e, p, 48000.0, 1.0));
    p.paramB = 1.0f;
    const auto slow = arrival (analysis::impulseResponse (*e, p, 48000.0, 1.0));

    INFO ("10 % energy arrival: Build 0 " << fast * 1000.0 << " ms, Build 1 " << slow * 1000.0 << " ms");
    CHECK (slow > fast + 0.015);
}
