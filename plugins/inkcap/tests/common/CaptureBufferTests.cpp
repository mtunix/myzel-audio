/*
    CaptureBuffer: delay 0 is the input to the bit, integer delays are exact, fractional reads are
    accurate, and a head that slides from the input to far behind it never clicks on the way
    through the interpolators.
*/

#include "dsp/common/CaptureBuffer.h"

#include "common/dsp/FloatCompare.h"
#include "support/Analysis.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace inkcap;
using namespace inkcap::dsp;
using myzel::dsp::isExactly;

TEST_CASE ("CaptureBuffer: delay 0 is the input, whole samples are exact, the past before a restart is silence", "[capture]")
{
    CaptureBuffer capture;
    capture.prepare (2, 1000);
    const auto x = analysis::noise (3000, 3);

    bool exact = true;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float frame[] = { x[i], -x[i] };
        capture.write (frame);
        exact = exact && isExactly (capture.read (0, 0.0), x[i]) && isExactly (capture.read (1, 0.0), -x[i]);
        for (const int d : { 1, 2, 5, 6, 17, 999 })
        {
            const auto expected = i >= (size_t) d ? x[i - (size_t) d] : 0.0f;
            exact = exact && isExactly (capture.at (0, d), expected) && (d > 4 || isExactly (capture.read (0, (double) d), expected));
        }
    }
    CHECK (exact);

    capture.restart();
    const float one[] = { 1.0f, 1.0f };
    capture.write (one);
    CHECK (isExactly (capture.read (0, 0.0), 1.0f));
    CHECK (isExactly (capture.at (0, 1), 0.0f));
    CHECK (isExactly (capture.read (0, 20.5), 0.0f));
    CHECK (capture.written() == 1);
}

TEST_CASE ("CaptureBuffer: fractional reads of a 1 kHz sine, through every interpolator", "[capture]")
{
    constexpr double fs = 48000.0;
    CaptureBuffer capture;
    capture.prepare (1, 4000);
    const auto x = analysis::sine (1000.0, fs, 0.5, 0.5);

    for (const auto delay : { 0.37, 2.5, 5.5, 9.25, 1234.75 })
    {
        capture.restart();
        double error = 0.0, energy = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            capture.write (&x[i]);
            if (i < 3000)
                continue;
            const auto expected = 0.5 * std::sin (2.0 * std::numbers::pi * 1000.0 * ((double) i - delay) / fs);
            const auto e = (double) capture.read (0, delay) - expected;
            error += e * e;
            energy += expected * expected;
        }
        const auto db = 10.0 * std::log10 (error / energy);
        INFO ("delay " << delay << ": error " << db << " dB");
        // Linear below 1 sample, Hermite to 5, the windowed sinc from 6: its error at 1 kHz is
        // the table's uniform 0.0013 dB below unity (Interpolators.h), -76 dB.
        CHECK (db < (delay < 1.0 ? -45.0 : (delay < 5.0 ? -90.0 : -70.0)));
    }
}

TEST_CASE ("CaptureBuffer: a head that falls behind from the input is click-free through the interpolators", "[capture]")
{
    constexpr double fs = 48000.0;
    CaptureBuffer capture;
    capture.prepare (1, 4000);
    const auto x = analysis::sine (330.0, fs, 1.0, 0.5);

    // The delay grows from 0 to 12 samples over 0.5 s (a 0.05 % slowdown), as a warble coming on.
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        capture.write (&x[i]);
        const auto delay = std::min (12.0, 12.0 * (double) i / (0.5 * fs));
        y[i] = capture.read (0, delay);
    }

    const auto crest = analysis::clickCrestDb (y, 100);
    const auto steady = analysis::clickCrestDb (x, 100);
    INFO ("crest " << crest << " dB, the sine's own " << steady << " dB");
    CHECK (crest < steady + 3.0);
}
