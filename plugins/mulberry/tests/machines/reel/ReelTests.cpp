#include "support/GateCases.h"

#include "dsp/machines/reel/ReelMachine.h"

using namespace mulberry;
using namespace mulberry::analysis;

namespace
{
    gates::GateOptions reelGates()
    {
        gates::GateOptions o;
        o.flutterTarget = 0.05;
        return o;
    }

    constexpr double fs = 48000.0;

    double passDb (float timeMs, double hz, float speed = 0.5f, float bias = 0.5f)
    {
        dsp::MachineSettings ms;
        ms.timeMs = { timeMs, timeMs };
        ms.paramA = speed;
        ms.paramB = bias;
        dsp::ReelMachine m;
        dsp::SlotMemory memory;
        memory.allocate (fs);
        m.prepare (fs, memory);
        return toDb (m.predictedPassGain (hz, ms));
    }
}

MULBERRY_MACHINE_GATES (Machine::reel, "Reel", "[reel]", reelGates())

TEST_CASE ("Reel: the head slides, so the tone is the same at every Time", "[reel]")
{
    for (const auto hz : { 125.0, 1000.0, 8000.0, 15000.0 })
        CHECK (std::abs (passDb (60.0f, hz) - passDb (3000.0f, hz)) < 1.0e-9);
}

TEST_CASE ("Reel: faster tape is brighter, the head bump moves up with it", "[reel]")
{
    const auto slow = 0.0f, fast = 1.0f;
    CHECK (passDb (300.0f, 12000.0, fast) > passDb (300.0f, 12000.0, slow) + 1.0);
    // The bump region: 30 ips keeps more of 50 Hz than 7.5 ips loses below its 35 Hz bump? The
    // contour follows v / wavelength: at 7.5 ips 20 Hz is under the bump, at 30 ips far under.
    CHECK (passDb (300.0f, 20.0, slow) > passDb (300.0f, 20.0, fast));
}

TEST_CASE ("Reel: under-bias is brighter and dirtier, over-bias cleaner and duller", "[reel]")
{
    const auto under = 0.0f, nominal = 0.5f, over = 1.0f;
    // Brightness is the 10 kHz pass relative to 1 kHz: an under-biased tape is quieter overall
    // (its core expansion is divided out), but keeps more top.
    const auto tilt = [] (float bias) { return passDb (300.0f, 10000.0, 0.5f, bias) - passDb (300.0f, 1000.0, 0.5f, bias); };
    INFO ("10 kHz re 1 kHz: under " << tilt (under) << " dB, nominal " << tilt (nominal) << " dB, over " << tilt (over) << " dB");
    CHECK (tilt (under) > tilt (nominal));
    CHECK (tilt (over) < tilt (nominal) - 0.5);

    const auto thd = [] (float bias) { return gates::thdPercent (Machine::reel, -6.0, 0.0f, 0.5f, bias); };
    const auto u = thd (under), n = thd (nominal), o = thd (over);
    INFO ("THD at -6 dBFS: under " << u << " %, nominal " << n << " %, over " << o << " %");
    CHECK (u > n);
    CHECK (o < n);
}
