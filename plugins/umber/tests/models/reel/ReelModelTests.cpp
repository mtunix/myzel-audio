#include "dsp/models/reel/ReelModel.h"
#include "support/ModelConformance.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>

using umber::dsp::ReelModel;
using namespace umber::test;

UMBER_MODEL_CONFORMANCE_TESTS (ReelModel, "[reel]", 0.01)

namespace
{
    double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

    double gainAt (double rate, double freq, double levelDb)
    {
        ReelModel model;
        return measureHarmonics (model, rate, freq, dbToGain (levelDb), 1).gainDb;
    }

    double harmonicDb (const HarmonicResult& r, int n)
    {
        return 20.0 * std::log10 (std::max (r.harmonics[(size_t) n - 2], 1.0e-15) / r.fundamental);
    }

    /** Peak-to-RMS ratio (dB) of the mean-removed signal. */
    double crestDb (const std::vector<float>& v)
    {
        double mean = 0.0, peak = 0.0, sumSq = 0.0;
        for (auto s : v) mean += s;
        mean /= (double) v.size();
        for (auto s : v)
        {
            const auto x = s - mean;
            peak = std::max (peak, std::abs (x));
            sumSq += x * x;
        }
        return 20.0 * std::log10 (peak / std::sqrt (sumSq / (double) v.size()));
    }
} // namespace

TEST_CASE ("ReelModel: THD sits in the middle of the contract windows", "[reel][character]")
{
    ReelModel model;
    constexpr double rate = 192000.0;

    const auto quiet  = measureHarmonics (model, rate, 1000.0, 0.063).thd;
    const auto warm   = measureHarmonics (model, rate, 1000.0, 0.25).thd;
    const auto driven = measureHarmonics (model, rate, 1000.0, 1.0).thd;
    const auto heavy  = measureHarmonics (model, rate, 1000.0, 8.0).thd;
    const auto wreck  = measureHarmonics (model, rate, 1000.0, 80.0);

    INFO ("THD %: " << quiet * 100 << " / " << warm * 100 << " / " << driven * 100 << " / " << heavy * 100
                    << " / " << wreck.thd * 100);
    CHECK (quiet < 0.003);
    CHECK (warm > 0.003);
    CHECK (warm < 0.03);
    CHECK (driven > 0.03);
    CHECK (driven < 0.15);
    CHECK (heavy > 0.15);
    CHECK (heavy < 0.40);
    CHECK (wreck.thd < 0.5);   // destroyed, but a rounded square, not a brick-wall one
}

TEST_CASE ("ReelModel: mostly low-order odd harmonics, a little 2nd from the record amp", "[reel][character]")
{
    ReelModel model;
    constexpr double rate = 192000.0;

    for (double level : { 0.25, 1.0, 8.0 })
    {
        const auto r = measureHarmonics (model, rate, 1000.0, level);
        INFO ("level " << level << ": H2 " << harmonicDb (r, 2) << " H3 " << harmonicDb (r, 3)
                       << " H5 " << harmonicDb (r, 5) << " H7 " << harmonicDb (r, 7));
        CHECK (harmonicDb (r, 3) > harmonicDb (r, 2));          // odd-dominant (tape)
        CHECK (harmonicDb (r, 2) > -70.0);                      // ... with some 2nd (record amp)
        CHECK (harmonicDb (r, 3) > harmonicDb (r, 5));          // falling spectrum: soft, low order
        CHECK (harmonicDb (r, 5) > harmonicDb (r, 7) - 3.0);
    }

    const auto quiet = measureHarmonics (model, rate, 1000.0, 0.063);
    CHECK (harmonicDb (quiet, 2) > -75.0);
}

TEST_CASE ("ReelModel: low-level response is flat apart from the head bump", "[reel][character]")
{
    for (double rate : { 88200.0, 192000.0, 768000.0 })
    {
        INFO ("rate " << rate);
        const auto bump = gainAt (rate, 75.0, -40.0);
        CHECK (bump > 1.6);
        CHECK (bump < 2.5);
        CHECK (gainAt (rate, 30.0, -40.0) < bump - 1.5);       // rolls off below the bump
        CHECK (gainAt (rate, 20.0, -40.0) > -2.0);

        for (double f : { 400.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            CHECK (std::abs (gainAt (rate, f, -40.0)) < 0.5);

        const auto top = gainAt (rate, 20000.0, -40.0);        // gap / spacing loss
        CHECK (top < -0.5);
        CHECK (top > -1.5);
    }
}

TEST_CASE ("ReelModel: highs compress progressively with drive (pre-emphasis into tape)", "[reel][character]")
{
    constexpr double rate = 192000.0;
    double previous = 1.0e9;

    for (double level : { -36.0, -12.0, 0.0, 12.0 })
    {
        const auto hfRelative = gainAt (rate, 10000.0, level) - gainAt (rate, 1000.0, level);
        INFO ("level " << level << " dBFS: 10 kHz relative to 1 kHz = " << hfRelative << " dB");
        CHECK (hfRelative < previous + 0.05);
        previous = hfRelative;
    }

    CHECK (gainAt (rate, 10000.0, 0.0) - gainAt (rate, 1000.0, 0.0) < -3.0);
    CHECK (gainAt (rate, 10000.0, 12.0) - gainAt (rate, 1000.0, 12.0) < -6.0);

    // lows are not singled out: 100 Hz tracks 1 kHz at 0 dBFS (bump aside)
    const auto lowRelative = gainAt (rate, 200.0, 0.0) - gainAt (rate, 1000.0, 0.0);
    CHECK (std::abs (lowRelative - (gainAt (rate, 200.0, -40.0) - gainAt (rate, 1000.0, -40.0))) < 0.5);
}

TEST_CASE ("ReelModel: transients are softened progressively, never boosted", "[reel][character]")
{
    constexpr double rate = 192000.0;
    const int n = (int) (0.15 * rate);
    double previousCrest = 1.0e9;
    double lowCrest = 0.0;

    for (double level : { -24.0, 0.0, 12.0, 24.0 })
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto t = i / rate;
            x[(size_t) i] = (float) (dbToGain (level) * std::exp (-t / 0.03) * std::sin (2.0 * std::numbers::pi * 150.0 * t));
        }

        ReelModel model;
        model.prepare (rate);
        auto y = x;
        model.process (y.data(), n);

        const auto crest = crestDb (y);
        INFO ("level " << level << ": crest in " << crestDb (x) << " out " << crest);
        CHECK (crest < previousCrest + 0.1);
        previousCrest = crest;
        if (level < -20.0)
            lowCrest = crest;
    }

    CHECK (lowCrest - previousCrest > 4.0);
}

TEST_CASE ("ReelModel: no latch-up: a loud burst does not change the low-level gain", "[reel][character]")
{
    constexpr double rate = 192000.0;

    for (double burstDb : { 12.0, 38.0, 60.0 })
    {
        ReelModel model;
        model.prepare (rate);
        auto loud = sine (100.0, dbToGain (burstDb), rate, (int) (0.2 * rate));
        model.process (loud.data(), (int) loud.size());

        // the coupling cap discharges the clipping DC over ~0.2 s (as in the real circuit)
        const int settle = (int) (1.0 * rate), window = (int) (20 * rate / 1000);
        auto s = sine (1000.0, 0.01, rate, settle + window);
        model.process (s.data(), (int) s.size());

        std::vector<float> y (s.begin() + settle, s.end());
        double mean = 0.0;
        for (auto v : y) mean += v;
        mean /= (double) y.size();
        for (auto& v : y) v = (float) (v - mean);

        const auto fundamental = toneAmplitude (y.data(), window, 1000.0, rate);
        const auto h2 = toneAmplitude (y.data(), window, 2000.0, rate);
        INFO ("after +" << burstDb << " dBFS: gain " << 20 * std::log10 (fundamental / 0.01) << " dB, H2 "
                        << 20 * std::log10 (h2 / fundamental) << " dB");
        CHECK (std::abs (20.0 * std::log10 (fundamental / 0.01)) < 0.2);
        CHECK (h2 / fundamental < 0.002);    // remanence leaves at most a faint (-54 dB) 2nd
        CHECK (std::abs (mean) < 0.1);       // and only a small DC offset
    }
}

TEST_CASE ("ReelModel: silence in, exact silence out; no denormals; non-finite input is contained", "[reel][robustness]")
{
    constexpr double rate = 384000.0;
    ReelModel model;
    model.prepare (rate);

    std::vector<float> silence (4096, 0.0f);
    model.process (silence.data(), (int) silence.size());
    bool allZero = true;
    for (auto v : silence)
        allZero = allZero && std::fpclassify (v) == FP_ZERO;
    CHECK (allZero);

    // a burst, then a long decay: no denormal floats may reach the output
    auto burst = sine (3000.0, 0.5, rate, 2000);
    model.process (burst.data(), (int) burst.size());
    bool anyDenormal = false;
    for (int block = 0; block < 400; ++block)
    {
        std::vector<float> tail (4096, 0.0f);
        model.process (tail.data(), (int) tail.size());
        for (auto v : tail)
            anyDenormal = anyDenormal || std::fpclassify (v) == FP_SUBNORMAL;
    }
    CHECK_FALSE (anyDenormal);

    // NaN / Inf samples never poison the state
    std::vector<float> bad { 0.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity(), 0.2f };
    model.process (bad.data(), (int) bad.size());
    auto after = sine (1000.0, 0.5, rate, 20000);
    const auto r = measureBounds (model, after);
    CHECK (r.allFinite);
    CHECK (r.peak < 8.0);
}

TEST_CASE ("ReelModel: THD is consistent across oversampled rates", "[reel][character]")
{
    for (double level : { 0.25, 1.0, 8.0 })
    {
        ReelModel a, b;
        const auto lo = measureHarmonics (a, 88200.0, 1000.0, level);
        const auto hi = measureHarmonics (b, 768000.0, 1000.0, level);
        INFO ("level " << level << ": THD " << lo.thd * 100 << " % vs " << hi.thd * 100 << " %");
        CHECK (std::abs (lo.thd - hi.thd) < 0.02 * hi.thd + 1.0e-4);
        CHECK (std::abs (lo.gainDb - hi.gainDb) < 0.05);
    }
}
