// MulberryMeasure: renders every machine through the real chain and prints the quality gates of
// PLAN.md ("Measurement and quality gates"). Pure DSP, no JUCE.
//
//     MulberryMeasure                 every machine, every gate
//     MulberryMeasure reel loop       only these machines
//     MulberryMeasure --quick         skip the long gates (stress, hold, rates)

#include "dsp/MachineInfo.h"
#include "support/MachineGates.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace mulberry;
using namespace mulberry::gates;

namespace
{
    void line (const char* label, double value, const char* unit, bool pass)
    {
        std::printf ("  %-44s %10.3f %-6s %s\n", label, value, unit, pass ? "ok" : "FAIL");
    }

    float flutterTarget (Machine m)
    {
        switch (m)
        {
            case Machine::reel:   return 0.05f;
            case Machine::loop:   return 0.25f;
            case Machine::oilCan: return 1.0f;
            case Machine::drum:   return 0.15f;
            case Machine::clean:
            case Machine::rack:
            case Machine::bucket: break;
        }
        return -1.0f;
    }

    /** Param A / B for the gates (as the machine's tests): Loop and Drum on one head. */
    std::pair<float, float> gateParams (Machine m)
    {
        switch (m)
        {
            case Machine::loop:   return { 0.0f, 0.0f };     // one head, new tape
            case Machine::bucket: return { 1.0f, 0.0f };     // 8192 stages, no compander
            case Machine::oilCan: return { 0.0f, 0.5f };     // one pickup
            case Machine::rack:   return { 1.0f, 0.5f };     // 16 bits: the click gate sees no grain
            case Machine::drum:   return { character::stepCentre (3, 15), 0.0f };   // head 4 alone, no swell
            case Machine::clean:
            case Machine::reel:   break;
        }
        return { 0.5f, 0.5f };
    }

    void measure (Machine m, bool quick)
    {
        const auto& info = mulberry::info (m);
        const auto [a, b] = gateParams (m);
        std::printf ("\n%s (Param A %.2f, Param B %.2f)\n", info.name, a, b);

        // Time
        double worstTime = 0.0;
        for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (const auto ms : { 80.0f, 375.0f, 1000.0f })
            {
                const auto measured = echoTimeMs (m, fs, ms, a, b);
                const auto error = m == Machine::clean ? std::abs (measured - ms) * fs * 0.001         // samples
                                                       : 100.0 * std::abs (measured - ms) / ms;         // percent
                worstTime = std::max (worstTime, error);
            }
        if (m == Machine::clean)
            line ("first echo vs Time, worst (samples)", worstTime, "smp", worstTime <= 0.1);
        else
            line ("mean echo time vs Time, worst (%)", worstTime, "%", worstTime <= 0.1);

        // Per-repeat loss
        double worstLoss = 0.0;
        for (const auto& r : repeatLosses (m, 48000.0, 300.0f, 0.7f, 8, a, b))
        {
            std::printf ("    %6.0f Hz  measured %7.3f dB/repeat, predicted %7.3f\n", r.frequency, r.measuredDb, r.predictedDb);
            worstLoss = std::max (worstLoss, std::abs (r.measuredDb - r.predictedDb));
        }
        line ("per-repeat loss vs prediction, worst", worstLoss, "dB", worstLoss <= 0.2);

        // Drive compensation
        double lo = 1.0e9, hi = -1.0e9;
        for (float d = -12.0f; d <= 24.0f; d += 6.0f)
        {
            const auto level = echoLevelDb (m, 48000.0, d, a, b);
            lo = std::min (lo, level);
            hi = std::max (hi, level);
        }
        line ("echo level over Drive -12..+24 (spread)", hi - lo, "dB", hi - lo <= 0.4);

        // Flutter
        // Flutter at the machine's defaults (Loop, Drum: one head; Bucket: 8192 stages).
        const auto fa = m == Machine::loop ? 0.0f : (m == Machine::bucket ? 1.0f : (m == Machine::drum ? a : 0.5f));
        const auto f0 = flutterPercent (m, 48000.0, 0.0f, fa, 0.5f, 8.0);
        const auto f1 = flutterPercent (m, 48000.0, 0.5f, fa, 0.5f, 24.0);
        const auto target = flutterTarget (m);
        line ("AES6 weighted flutter at Wobble 0", f0, "%", f0 < 1.0e-4);
        line ("AES6 weighted flutter at nominal Wobble", f1, "%", target < 0.0 || std::abs (f1 - target) <= 0.2 * target);

        // THD of the first echo, 1 kHz (report)
        std::printf ("    THD at 1 kHz, Drive 0:");
        for (const auto level : { -24.0, -12.0, -6.0, 0.0 })
            std::printf ("  %.0f dBFS %.3f %%", level, thdPercent (m, level, 0.0f, a, b));
        std::printf ("\n");

        // Glide
        const auto g = glide (m, 48000.0, a, b);
        line ("300 -> 450 ms jump, 3rd-difference crest", g.jumpDb, "dB", g.excessDb() <= 0.0);
        line ("  (the machine's steady crest, for reference)", g.steadyDb, "dB", true);

        // Aliasing
        const auto alias = worstAliasDbc (m, a, b);
        const auto device = m == Machine::bucket || m == Machine::rack;   // alias as devices (separate gate)
        line ("7 kHz -1 dBFS, Drive max, 44.1 kHz: worst alias", alias, "dBc", device || alias <= -80.0);
        const auto aliasNominal = worstAliasDbc (m, a, b, 44100.0, 0.0f);
        line ("7 kHz -1 dBFS, Drive 0, 44.1 kHz: worst alias", aliasNominal, "dBc", device || aliasNominal <= -80.0);

        if (! quick)
        {
            const auto stress = squareStress (m, 48000.0, 60.0, a, b);
            line ("120 %, Drive max, 0 dBFS square 60 s: peak", toDb (stress.peak), "dBFS", stress.finite && stress.peak <= 2.0);

            const auto run = runaway (m, 48000.0, a, b);
            line ("runaway from a click: level after 10 s", run.levelDb, "dBFS", run.finite && run.levelDb > -40.0);
            line ("runaway: peak 8-10 s vs 6-8 s", run.steadinessDb, "dB", std::abs (run.steadinessDb) < 1.5);
            line ("runaway: RMS 8-10 s vs 6-8 s (report)", run.rmsChangeDb, "dB", true);
            line ("runaway: 10 s after pulling back to 90 %", run.afterPullBackDb, "dB", run.afterPullBackDb < -30.0);

            const auto hold = holdDrift (m, 48000.0, 60.0);
            line ("Hold 60 s: worst octave-band drift", hold.worstBandDriftDb, "dB", hold.worstBandDriftDb <= 0.5);
            line ("Hold 60 s: seam 3rd-difference crest", hold.seamCrestDb, "dB", hold.seamExcessDb() <= 0.0);

            double spread = 0.0;
            std::vector<RepeatLoss> reference;
            for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                const auto losses = repeatLosses (m, fs, 300.0f, 0.7f, 6, a, b);
                if (reference.empty())
                    reference = losses;
                for (size_t i = 0; i < losses.size(); ++i)
                    spread = std::max (spread, std::abs (losses[i].measuredDb - reference[i].measuredDb));
            }
            line ("per-repeat loss across 44.1..192 kHz, worst", spread, "dB", spread <= 0.3);
        }

        // CPU at the machine's defaults (Param A / B 0.5).
        const auto cpu = cpuPercent (m);
        line ("CPU, 48 kHz stereo, Single", cpu, "%", cpu <= info.cpuBudgetPercent);
        const auto cpuPattern = cpuPercent (m, Mode::pattern);
        line ("CPU, Pattern with 16 steps (added)", cpuPattern - cpu, "%", cpuPattern - cpu <= 1.5);
    }
}

int main (int argc, char** argv)
{
    bool quick = false;
    std::vector<Machine> chosen;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--quick") == 0)
        {
            quick = true;
            continue;
        }
        for (const auto& m : machineInfos)
            if (std::strcmp (argv[i], m.key) == 0)
                chosen.push_back (m.machine);
    }

    if (chosen.empty())
        for (const auto& m : machineInfos)
            chosen.push_back (m.machine);

    for (const auto m : chosen)
        measure (m, quick);

    return 0;
}
