// Hidden measurement reports for the Reel model (not run by default):
//
//     build-reel/tests/UmberTests_reel "[.report]"
//
// Prints the THD table at the contract levels, harmonic balance, low-level frequency
// response and the level-dependent HF compression. Numbers are copied into NOTES.md.

#include "dsp/models/reel/ReelModel.h"
#include "support/SignalAnalysis.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using umber::dsp::ReelModel;
using namespace umber::test;

TEST_CASE ("Reel: THD / harmonic report", "[reel][.report]")
{
    for (double rate : { 88200.0, 192000.0, 768000.0 })
    {
        std::printf ("\n-- %.1f kHz --\n level(dBFS)  gain(dB)  THD(%%)   H2      H3      H4      H5      H7  (dB re fundamental)\n", rate / 1000);
        for (double db : { -60.0, -36.0, -24.0, -18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0, 24.0, 38.0 })
        {
            ReelModel model;
            const auto amp = std::pow (10.0, db / 20.0);
            const auto r = measureHarmonics (model, rate, 1000.0, amp);
            auto h = [&] (int n) { return 20.0 * std::log10 (std::max (r.harmonics[(size_t) n - 2], 1.0e-12) / r.fundamental); };
            std::printf (" %+6.0f      %+6.2f   %7.3f  %6.1f  %6.1f  %6.1f  %6.1f  %6.1f\n", db, r.gainDb, r.thd * 100,
                         h (2), h (3), h (4), h (5), h (7));
        }
    }
}

TEST_CASE ("Reel: frequency response report", "[reel][.report]")
{
    for (double rate : { 88200.0, 192000.0, 768000.0 })
    {
        std::printf ("\n-- %.1f kHz: gain (dB) vs frequency at several levels --\n  freq ", rate / 1000);
        const double levels[] = { -40.0, -12.0, 0.0, 12.0, 24.0 };
        for (auto l : levels) std::printf ("  %+5.0fdBFS", l);
        std::printf ("\n");

        for (double f : { 20.0, 30.0, 50.0, 75.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 15000.0, 20000.0 })
        {
            std::printf (" %6.0f", f);
            for (auto l : levels)
            {
                ReelModel model;
                const auto r = measureHarmonics (model, rate, f, std::pow (10.0, l / 20.0), 1);
                std::printf ("  %+9.2f", r.gainDb);
            }
            std::printf ("\n");
        }
    }
}

TEST_CASE ("Reel: worst-case cost report", "[reel][.report]")
{
    for (double rate : { 88200.0, 192000.0, 768000.0 })
    {
        ReelModel model;
        std::printf ("%.1f kHz: ns/sample  -20 dBFS %.1f   0 dBFS %.1f   +24 dBFS %.1f   +60 dBFS %.1f\n", rate / 1000,
                     nanosecondsPerSample (model, rate, 0.1), nanosecondsPerSample (model, rate, 1.0),
                     nanosecondsPerSample (model, rate, 16.0), nanosecondsPerSample (model, rate, 1000.0));
    }

    // +60 dBFS white noise: every sample is a huge field step (sub-stepping at its cap)
    ReelModel model;
    model.prepare (192000.0);
    std::vector<float> noise (1 << 18);
    unsigned seed = 1;
    for (auto& v : noise)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (float) ((seed >> 8) * (2.0 / 16777216.0) - 1.0) * 1000.0f;
    }
    const auto start = std::chrono::steady_clock::now();
    model.process (noise.data(), (int) noise.size());
    const auto end = std::chrono::steady_clock::now();
    std::printf ("192 kHz, +60 dBFS white noise: %.1f ns/sample\n",
                 std::chrono::duration<double, std::nano> (end - start).count() / (double) noise.size());
}
