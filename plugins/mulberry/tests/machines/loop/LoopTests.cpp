#include "support/GateCases.h"

#include "dsp/machines/loop/LoopMachine.h"

using namespace mulberry;
using namespace mulberry::analysis;

namespace
{
    // Gates on one head and new tape: wear's dropouts and the erase ghost are deliberately not
    // predictable per repeat. Flutter is checked at the default wear below.
    gates::GateOptions loopGates()
    {
        gates::GateOptions o;
        o.paramA = 0.0f;
        o.paramB = 0.0f;
        return o;
    }

    constexpr double fs = 48000.0;

    dsp::ChainSettings loop (float ms, float fb = 0.0f, float heads = 0.0f, float wear = 0.0f)
    {
        auto s = gates::machineAt (Machine::loop, ms, fb);
        s.paramA = heads;
        s.paramB = wear;
        return s;
    }
}

MULBERRY_MACHINE_GATES (Machine::loop, "Loop", "[loop]", loopGates())

TEST_CASE ("Loop: flutter at the default heads and wear is 0.25 %", "[loop]")
{
    const auto f = gates::flutterPercent (Machine::loop, fs, 0.5f, 0.0f, 0.5f, 24.0);
    INFO ("AES6 weighted flutter " << f << " %");
    CHECK (std::abs (f - 0.25) <= 0.05);
}

TEST_CASE ("Loop: three heads at Time, 2 Time and 3 Time", "[loop]")
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    const auto n = (size_t) (1.2 * fs);
    const auto settle = (size_t) (0.5 * fs);
    const auto out = render (chain, loop (100.0f, 0.0f, 1.0f), impulse (n, fs, 0.2f, settle), n);
    for (int h = 1; h <= 3; ++h)
    {
        const auto at = settle + (size_t) (h * 0.1 * fs);
        const auto t = groupDelay (out.left, at - 200, at + 200, 1000.0, fs);
        INFO ("head " << h << ": " << (t - (double) settle) / fs * 1000.0 << " ms");
        CHECK (std::abs ((t - (double) settle) / fs * 1000.0 - 100.0 * h) < 0.1 * h);
    }
}

TEST_CASE ("Loop: a Time change bends the echoes already on the tape", "[loop]")
{
    // A steady tone; Time doubles. The motor slows, so the echo playing during the change drops
    // in pitch (by up to the speed ratio) before the new, slower recording arrives.
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    const auto a = loop (300.0f), b = loop (600.0f);
    const auto in = sine (1000.0, fs, 3.0, 0.1);
    const auto change = (size_t) (1.5 * fs);
    const auto out = render (chain, [&] (size_t pos) { return pos >= change ? b : a; }, in, in.size());

    const auto pitchAt = [&] (size_t from)
    {
        double best = 0.0, bestHz = 0.0;
        for (double hz = 400.0; hz < 1100.0; hz += 1.0)
        {
            const auto amp = toneAmplitude (out.left, from, (size_t) (0.05 * fs), hz, fs);
            if (amp > best) { best = amp; bestHz = hz; }
        }
        return bestHz;
    };
    const auto before = pitchAt (change - (size_t) (0.1 * fs));
    const auto during = pitchAt (change + (size_t) (0.3 * fs));
    INFO ("pitch before " << before << " Hz, 0.3 s into the change " << during << " Hz");
    CHECK (std::abs (before - 1000.0) < 3.0);
    CHECK (during < 900.0);
}

TEST_CASE ("Loop: slower tape, darker repeats; losses hold beyond the physical range", "[loop]")
{
    dsp::MachineSettings ms;
    dsp::LoopMachine m;
    dsp::SlotMemory memory;
    memory.allocate (fs);
    m.prepare (fs, memory);
    const auto at = [&] (float timeMs, double hz)
    {
        ms.timeMs = { timeMs, timeMs };
        ms.paramB = 0.0f;
        return toDb (m.predictedPassGain (hz, ms));
    };
    CHECK (at (800.0f, 8000.0) < at (200.0f, 8000.0) - 6.0);
    CHECK (std::abs (at (1000.0f, 8000.0) - at (3000.0f, 8000.0)) < 1.0e-9);
    CHECK (std::abs (at (50.0f, 8000.0) - at (10.0f, 8000.0)) < 1.0e-9);
}

TEST_CASE ("Loop: worn tape drops out at the same spots every pass of the loop", "[loop]")
{
    // A steady tone into a worn loop at 100 ms: the head-1 echo's envelope dips where the bad
    // spots pass; the loop takes loopLength / speed to come round, so the pattern repeats.
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = loop (100.0f, 0.0f, 0.0f, 1.0f);
    s.wobble = 0.0f;
    const auto period = dsp::LoopMachine::loopLength / (dsp::LoopMachine::headSpacing / 0.1);   // seconds
    const auto n = (size_t) ((2.0 * period + 1.0) * fs);
    const auto in = sine (500.0, fs, (double) n / fs, 0.1);
    const auto out = render (chain, s, in, n);

    const auto block = (size_t) (0.01 * fs);
    std::vector<double> env;
    for (size_t i = (size_t) (0.5 * fs); i + block < n; i += block)
        env.push_back (rms (out.left, i, i + block));
    const auto lag = (size_t) std::lround (period / 0.01);
    double ab = 0.0, aa = 0.0, bb = 0.0, minimum = 1.0, maximum = 0.0;
    for (size_t i = 0; i + lag < env.size(); ++i)
    {
        const auto x = env[i] - 0.07, y = env[i + lag] - 0.07;
        ab += x * y; aa += x * x; bb += y * y;
        minimum = std::min (minimum, env[i]);
        maximum = std::max (maximum, env[i]);
    }
    INFO ("envelope " << toDb (minimum) << " .. " << toDb (maximum) << " dB, correlation one loop apart " << ab / std::sqrt (aa * bb));
    CHECK (toDb (maximum / minimum) > 3.0);
    CHECK (ab / std::sqrt (aa * bb) > 0.9);
}
