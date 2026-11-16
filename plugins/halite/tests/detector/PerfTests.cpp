// The detector's CPU cost (PLAN.md, Performance budget: Split or Wide ≤ 0.5 % of a core at
// 48 kHz stereo with the detector included, Harsh ≤ +0.3 %; what the detector takes is left to
// the reduction). Reports only, in process CPU time (std::clock), so a loaded machine inflates it
// less than wall time, with the load average beside it; pin it to one core for stable numbers:
// taskset -c 2 HaliteTests_detector "[.perf]"

#include "Signals.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace halite;
using namespace halite::testing;

namespace
{
    struct NullObserver final : dsp::DetectorObserver {};

    void printLoadAverage()
    {
       #if defined (__unix__) || defined (__APPLE__)
        double load[3] {};
        if (getloadavg (load, 3) == 3)
            std::printf ("Load average %.2f, %.2f, %.2f (1, 5, 15 min)\n", load[0], load[1], load[2]);
       #endif
    }

    /** Percent of one core to run the detector in real time: the fastest of several passes. */
    double percentOfCore (double rate, const dsp::DetectorSettings& s, const Samples& left, const Samples& right)
    {
        const juce::ScopedNoDenormals noDenormals;
        constexpr int block = 512;
        const auto n = (int) left.size();
        std::array<const float*, 2> in {};
        NullObserver observer;

        auto best = 1.0e9;
        for (int pass = 0; pass < 5; ++pass)
        {
            dsp::Detector detector;
            detector.prepare (rate);

            const auto t0 = std::clock();
            for (int pos = 0; pos + block <= n; pos += block)
            {
                in = { left.data() + pos, right.data() + pos };
                detector.process (in.data(), 2, nullptr, 0, block, s, &observer);
            }
            const auto seconds = (double) (std::clock() - t0) / CLOCKS_PER_SEC;
            best = std::min (best, seconds);
        }
        return 100.0 * best / (n / rate);
    }
}

TEST_CASE ("Perf: detector CPU at 48 kHz stereo", "[.perf]")
{
    for (const auto rate : { 48000.0, 96000.0 })
    {
        // A voice with esses on both channels, slightly different, so Linked switches paths.
        const auto n = samplesFor (20.0, rate);
        auto left = vowel (180.0, n, rate, 0.1, 1);
        auto right = vowel (182.0, n, rate, 0.08, 2);
        const auto ess = bandNoise (4500.0, 12000.0, n, rate, 3, 0.05);
        for (int i = 0; i < n; ++i)
        {
            const auto inEss = (i % samplesFor (0.5, rate)) < samplesFor (0.12, rate);
            left[(size_t) i] += inEss ? ess[(size_t) i] : 0.0f;
            right[(size_t) i] += inEss ? 0.8f * ess[(size_t) i] : 0.0f;
        }

        std::printf ("\nDetector CPU, %% of one core, %.0f kHz stereo, 512-sample blocks (fastest of 5 passes over 20 s):\n", rate / 1000.0);
        printLoadAverage();
        std::printf ("  %-12s %-9s %-10s %s\n", "cue", "stereo", "Harsh", "CPU");

        for (const auto cue : { dsp::NoiseCueKind::periodicity, dsp::NoiseCueKind::flatness, dsp::NoiseCueKind::blend })
            for (const auto stereo : { StereoMode::linked, StereoMode::unlinked })
                for (const auto harsh : { false, true })
                {
                    auto s = voiceSettings (cue);
                    s.thresholdDb = -12.0f;
                    s.stereo = stereo;
                    s.harshOn = harsh;
                    const auto cpu = percentOfCore (rate, s, left, right);
                    std::printf ("  %-12s %-9s %-10s %.3f %%\n",
                                 cue == dsp::NoiseCueKind::periodicity ? "periodicity" : (cue == dsp::NoiseCueKind::flatness ? "flatness" : "blend"),
                                 stereo == StereoMode::linked ? "Linked" : "Unlinked", harsh ? "on" : "off", cpu);
                }

        auto mid = voiceSettings();
        mid.stereo = StereoMode::mid;
        std::printf ("  %-12s %-9s %-10s %.3f %%\n", "periodicity", "Mid", "off", percentOfCore (rate, mid, left, right));
        printLoadAverage();
    }
}
