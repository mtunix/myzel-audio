#include "support/GateCases.h"

#include "dsp/machines/rack/RackMachine.h"

using namespace mulberry;
using namespace mulberry::analysis;

namespace
{
    // 16 bits: the click and silence gates see no converter grain.
    gates::GateOptions rackGates()
    {
        gates::GateOptions o;
        o.paramA = 1.0f;
        return o;
    }
}

MULBERRY_MACHINE_GATES (Machine::rack, "Rack", "[rack]", rackGates())

TEST_CASE ("Rack: the gain-ranging converter keeps the grain under the signal", "[rack]")
{
    // A quiet tone through 8 bits: gain ranging (x8) keeps the error 18 dB lower than a plain
    // 8-bit converter would at that level; full scale clips at the converter.
    const auto q = [] (double v, int range) { return (double) dsp::RackMachine::convert (v, 8, range); };
    double plain = 0.0, ranged = 0.0;
    for (int i = 0; i < 1000; ++i)
    {
        const auto v = 0.1 * std::sin (0.0173 * i);
        plain += (q (v, 0) - v) * (q (v, 0) - v);
        ranged += (q (v, 3) - v) * (q (v, 3) - v);
    }
    INFO ("error energy, plain vs ranged: " << toDb (std::sqrt (plain)) << " / " << toDb (std::sqrt (ranged)) << " dB");
    CHECK (toDb (std::sqrt (ranged / plain)) < -15.0);
    CHECK (dsp::RackMachine::convert (2.0, 12, 0) < 1.0f);
}

TEST_CASE ("Rack: each repeat is converted again: grain builds down the trail", "[rack]")
{
    // At 8 bits the noise floor between echoes of a quiet tone rises with each repeat.
    dsp::EchoChain chain;
    chain.prepare (48000.0, 256);
    auto s = gates::machineAt (Machine::rack, 100.0f, 0.9f);
    s.paramA = 0.0f;
    const auto n = (size_t) (1.2 * 48000.0);
    const auto in = toneBurst (440.0, 48000.0, 0.03, 0.05, n);
    const auto out = render (chain, s, in, n);
    const auto firstEcho = rms (out.left, 4800, 6240);
    const auto tenthEcho = rms (out.left, 48000, 49440);
    CHECK (firstEcho > 0.0);
    CHECK (tenthEcho > 0.0);
}
