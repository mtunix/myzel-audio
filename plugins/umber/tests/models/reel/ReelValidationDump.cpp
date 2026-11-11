// Writes the data the independent Python reference (validation/reel_reference.py) checks:
//
//     REEL_DUMP_DIR=/some/dir build-reel/tests/UmberTests_reel "[.dump]"
//     python3 tests/models/reel/validation/reel_reference.py /some/dir
//
//   ja_<rate>_<freq>_<amp>.csv  sampled field h and the C++ JA magnetisation m
//   triode.csv                  record-amp table vs input volts
//   eq_<rate>.csv               low-level (-50 dBFS) gain of the whole model vs frequency

#include "dsp/models/reel/ReelHysteresis.h"
#include "dsp/models/reel/ReelModel.h"
#include "dsp/models/reel/ReelRecordAmp.h"
#include "support/SignalAnalysis.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string>

using namespace umber;

TEST_CASE ("Reel: dump validation data", "[reel][.dump]")
{
    const char* dir = std::getenv ("REEL_DUMP_DIR");
    REQUIRE (dir != nullptr);
    const std::string base (dir);

    // JA core driven by sampled sines (4 periods, from the demagnetised state)
    for (double rate : { 88200.0, 192000.0, 768000.0 })
        for (double freq : { 1000.0, 10000.0 })
            for (double amp : { 0.3, 1.0, 3.0, 10.0 })
            {
                char name[256];
                std::snprintf (name, sizeof (name), "%s/ja_%.0f_%.0f_%g.csv", base.c_str(), rate, freq, amp);
                auto* f = std::fopen (name, "w");
                REQUIRE (f != nullptr);

                dsp::reel::TapeHysteresis tape;
                const int n = (int) std::round (4.0 * rate / freq);
                for (int i = 0; i <= n; ++i)
                {
                    const auto h = amp * std::sin (2.0 * std::numbers::pi * freq * i / rate);
                    std::fprintf (f, "%.17g,%.17g\n", h, tape.process (h));
                }
                std::fclose (f);
            }

    // record amp table
    {
        auto* f = std::fopen ((base + "/triode.csv").c_str(), "w");
        REQUIRE (f != nullptr);
        const auto& curve = dsp::reel::RecordAmpCurve::instance();
        for (double v = -40.0; v <= 40.0; v += 0.0731)
            std::fprintf (f, "%.17g,%.17g\n", v, curve.process (v));
        std::fclose (f);
    }

    // low-level frequency response of the full model
    for (double rate : { 88200.0, 192000.0, 768000.0 })
    {
        char name[256];
        std::snprintf (name, sizeof (name), "%s/eq_%.0f.csv", base.c_str(), rate);
        auto* f = std::fopen (name, "w");
        REQUIRE (f != nullptr);
        for (double freq = 10.0; freq < 22000.0; freq *= 1.1)
        {
            dsp::ReelModel model;
            const auto r = test::measureHarmonics (model, rate, freq, 0.00316, 1);
            std::fprintf (f, "%.17g,%.17g\n", freq, r.gainDb);
        }
        std::fclose (f);
    }
}
