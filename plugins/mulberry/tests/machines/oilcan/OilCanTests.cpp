#include "support/GateCases.h"

#include "dsp/machines/oilcan/OilCanMachine.h"

using namespace mulberry;
using namespace mulberry::analysis;

namespace
{
    // One pickup: a brush cluster is a comb by design (not a per-repeat level).
    gates::GateOptions oilCanGates()
    {
        gates::GateOptions o;
        o.paramA = 0.0f;
        o.paramB = 0.5f;
        o.flutterTarget = 1.0;
        return o;
    }

    double passDb (float ms, double hz, float oil)
    {
        dsp::OilCanMachine m;
        dsp::SlotMemory memory;
        memory.allocate (48000.0);
        m.prepare (48000.0, memory);
        dsp::MachineSettings s;
        s.timeMs = { ms, ms };
        s.paramA = 0.0f;
        s.paramB = oil;
        return toDb (m.predictedPassGain (hz, s));
    }
}

MULBERRY_MACHINE_GATES (Machine::oilCan, "Oil Can", "[oilcan]", oilCanGates())

TEST_CASE ("Oil Can: longer delays are quieter and softer; old oil more so", "[oilcan]")
{
    CHECK (passDb (900.0f, 1000.0, 0.5f) < passDb (200.0f, 1000.0, 0.5f) - 0.5);
    CHECK (passDb (900.0f, 4000.0, 0.5f) < passDb (200.0f, 4000.0, 0.5f) - 3.0);
    CHECK (passDb (300.0f, 1000.0, 1.0f) < passDb (300.0f, 1000.0, 0.0f) - 0.3);
    CHECK (passDb (300.0f, 6000.0, 1.0f) < passDb (300.0f, 6000.0, 0.0f) - 3.0);
}

TEST_CASE ("Oil Can: brushes smear one echo into a cluster", "[oilcan]")
{
    // An impulse: one pickup gives one echo; the full cluster spreads it over 15 % of Time.
    const auto spread = [] (float brushes)
    {
        dsp::EchoChain chain;
        chain.prepare (48000.0, 256);
        auto s = gates::machineAt (Machine::oilCan, 400.0f);
        s.paramA = brushes;
        const auto n = (size_t) (0.6 * 48000.0);
        const auto out = render (chain, s, impulse (n, 48000.0, 0.3f, 4800), n);
        double total = 0.0, early = 0.0;
        for (size_t i = 4800 + 15000; i < n; ++i)
        {
            total += (double) out.left[i] * out.left[i];
            if (i < 4800 + 18500)
                early += (double) out.left[i] * out.left[i];
        }
        return early / std::max (total, 1.0e-30);
    };
    CHECK (spread (0.0f) < 0.05);
    CHECK (spread (1.0f) > 0.3);
}
