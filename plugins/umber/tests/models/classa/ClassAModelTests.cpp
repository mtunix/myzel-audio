#include "dsp/models/classa/ClassAModel.h"
#include "support/ModelConformance.h"

#include <catch2/catch_test_macros.hpp>

#include <cfloat>

using umber::dsp::ClassAModel;
using namespace umber::test;

UMBER_MODEL_CONFORMANCE_TESTS (ClassAModel, "[classa]", 0.01)

namespace
{
    constexpr double rate = 192000.0;

    double db (double ratio) { return 20.0 * std::log10 (ratio); }

    double h (const HarmonicResult& r, size_t n) { return r.harmonics[n - 2] / r.fundamental; }

    /** Fundamental amplitude of a tone burst in a 10 ms window starting at t seconds. */
    double burstLevel (const std::vector<float>& y, double t, double freq)
    {
        return toneAmplitude (y.data() + (int) (t * rate), (int) (0.01 * rate), freq, rate);
    }
}

TEST_CASE ("ClassAModel: DC operating point is a sane class-A bias", "[classa]")
{
    umber::dsp::classa::ClassACircuit circuit;
    const umber::dsp::classa::Components c;
    circuit.prepare (rate, c);
    REQUIRE (circuit.dcConverged());

    using C = umber::dsp::classa::ClassACircuit;
    const auto& v = circuit.dcNodeVoltages();
    const auto ic1 = (c.vcc - v[C::nodeC1]) / c.rc;
    const auto ie2 = v[C::nodeE3] / c.re3;
    INFO ("Ic1 " << ic1 * 1e3 << " mA, Ie2 " << ie2 * 1e3 << " mA, Vc1 " << v[C::nodeC1]);

    CHECK (ic1 > 1.5e-3);
    CHECK (ic1 < 3.5e-3);
    CHECK (ie2 > 5.0e-3);
    CHECK (ie2 < 10.0e-3);
    CHECK (v[C::nodeC1] - v[C::nodeE1] > 6.0); // Q1 well out of saturation
    CHECK (v[C::nodeC1] < c.vcc - 8.0);        // and well out of cut-off
    CHECK (std::abs (v[C::nodeT]) < 1.0e-6);   // the transformer sees no DC
}

TEST_CASE ("ClassAModel: H2 leads at moderate drive, H3 catches up as it clips", "[classa]")
{
    ClassAModel model;

    const auto warm = measureHarmonics (model, rate, 1000.0, 0.25);   // -12 dBFS
    const auto driven = measureHarmonics (model, rate, 1000.0, 1.0);  //   0 dBFS
    const auto pushed = measureHarmonics (model, rate, 1000.0, 2.0);  //  +6 dBFS

    INFO ("-12 dBFS H2 " << h (warm, 2) * 100 << "% H3 " << h (warm, 3) * 100 << "%");
    INFO ("  0 dBFS H2 " << h (driven, 2) * 100 << "% H3 " << h (driven, 3) * 100 << "%");
    INFO (" +6 dBFS H2 " << h (pushed, 2) * 100 << "% H3 " << h (pushed, 3) * 100 << "%");

    // single-ended: the second harmonic dominates the onset
    CHECK (h (warm, 2) > 0.003);
    CHECK (h (warm, 2) > 10.0 * h (warm, 3));
    CHECK (warm.evenOddRatio() > 5.0);

    // driven into asymmetric clipping: strong 2nd plus a healthy 3rd
    CHECK (h (driven, 2) > 0.03);
    CHECK (h (driven, 3) > 0.4 * h (driven, 2));
    CHECK (h (pushed, 2) > h (pushed, 3));
    CHECK (h (pushed, 3) > 0.25 * h (pushed, 2));
}

TEST_CASE ("ClassAModel: clipping is asymmetric", "[classa]")
{
    ClassAModel model;
    model.prepare (rate);

    auto y = sine (1000.0, 2.0, rate, (int) (0.3 * rate));
    model.process (y.data(), (int) y.size());

    const auto last = y.end() - (int) (0.02 * rate);
    double mean = 0.0;
    for (auto it = last; it != y.end(); ++it)
        mean += *it;
    mean /= (double) (y.end() - last);

    double hi = 0.0, lo = 0.0;
    for (auto it = last; it != y.end(); ++it)
    {
        hi = std::max (hi, *it - mean);
        lo = std::min (lo, *it - mean);
    }

    INFO ("positive peak " << hi << ", negative peak " << lo);
    CHECK (std::abs (db (hi / -lo)) > 1.0);
}

TEST_CASE ("ClassAModel: bias shift compresses sustained bursts, quiet passages untouched", "[classa]")
{
    ClassAModel model;
    model.prepare (rate);

    auto run = [&] (double amplitude)
    {
        model.reset();
        auto y = sine (1000.0, amplitude, rate, (int) (0.3 * rate));
        model.process (y.data(), (int) y.size());
        return db (burstLevel (y, 0.25, 1000.0) / burstLevel (y, 0.005, 1000.0));
    };

    const auto quiet = run (0.1);
    const auto loud  = run (4.0);
    INFO ("level change over the burst: quiet " << quiet << " dB, loud " << loud << " dB");

    CHECK (std::abs (quiet) < 0.1);
    CHECK (loud < -1.5);  // the coupling/emitter caps charge and pull the bias
    CHECK (loud > -10.0); // ...as soft compression, not blocking
}

TEST_CASE ("ClassAModel: small-signal frequency response", "[classa]")
{
    ClassAModel model;
    model.prepare (rate);
    const auto& circuit = model.getCircuit();
    const auto ref = std::abs (circuit.smallSignalResponse (1000.0));
    auto rel = [&] (double f) { return db (std::abs (circuit.smallSignalResponse (f)) / ref); };

    for (double f : { 20.0, 50.0, 100.0, 500.0, 2000.0, 5000.0, 10000.0 })
    {
        INFO (f << " Hz: " << rel (f) << " dB");
        CHECK (std::abs (rel (f)) < 0.5);
    }

    CHECK (rel (20000.0) > -1.0);  // gentle Miller roll-off above the audio band
    CHECK (rel (40000.0) < -1.0);
    CHECK (rel (4.0) < -10.0);     // coupling caps + transformer: sub-sonic cut, no LF peak
    double lfPeak = -100.0;
    for (double f = 2.0; f < 100.0; f *= 1.1)
        lfPeak = std::max (lfPeak, rel (f));
    INFO ("highest point below 100 Hz: " << lfPeak << " dB");
    CHECK (lfPeak < 0.3);

    // end to end, through the nonlinear solver, at another rate
    for (double f : { 40.0, 10000.0 })
    {
        const auto r = measureHarmonics (model, 88200.0, f, 0.003);
        INFO (f << " Hz at 88.2 kHz: " << r.gainDb << " dB");
        CHECK (std::abs (r.gainDb) < 0.6);
        CHECK_FALSE (r.inverted);
    }
}

TEST_CASE ("ClassAModel: output transformer thickens the lows only when driven", "[classa]")
{
    ClassAModel model;

    const auto lowQuiet = measureHarmonics (model, rate, 30.0, 0.25);
    const auto midQuiet = measureHarmonics (model, rate, 1000.0, 0.25);
    const auto lowLoud  = measureHarmonics (model, rate, 30.0, 1.0);
    const auto midLoud  = measureHarmonics (model, rate, 1000.0, 1.0);

    INFO ("THD -12 dBFS: 30 Hz " << lowQuiet.thd * 100 << "%, 1 kHz " << midQuiet.thd * 100 << "%");
    INFO ("THD   0 dBFS: 30 Hz " << lowLoud.thd * 100 << "%, 1 kHz " << midLoud.thd * 100 << "%");

    CHECK (std::abs (db (lowQuiet.thd / midQuiet.thd)) < 1.5); // core in its linear region
    CHECK (lowLoud.thd > 1.4 * midLoud.thd);                   // core saturation at LF
    CHECK (lowLoud.thd < 0.4);
}

TEST_CASE ("ClassAModel: character is consistent across oversampled rates", "[classa]")
{
    ClassAModel model;
    const auto ref = measureHarmonics (model, 192000.0, 1000.0, 1.0);

    for (double r : { 88200.0, 384000.0, 768000.0 })
    {
        const auto m = measureHarmonics (model, r, 1000.0, 1.0);
        INFO ("rate " << r << ": THD " << m.thd * 100 << "% vs " << ref.thd * 100 << "% at 192 kHz");
        CHECK (std::abs (db (m.thd / ref.thd)) < 1.0);
        CHECK (std::abs (m.gainDb - ref.gainDb) < 0.2);
    }
}

TEST_CASE ("ClassAModel: Newton solver stays bounded and converges quickly", "[classa]")
{
    ClassAModel model;
    model.prepare (rate);

    for (double amplitude : { 0.25, 1.0, 8.0, 80.0 })
    {
        model.reset();
        auto x = sine (997.0, amplitude, rate, (int) (0.1 * rate));
        long total = 0;
        int worst = 0;
        bool finite = true;
        for (auto& s : x)
        {
            model.process (&s, 1);
            const auto it = model.getCircuit().lastIterations();
            total += it;
            worst = std::max (worst, it);
            finite = finite && std::isfinite (s);
        }

        const auto mean = (double) total / (double) x.size();
        INFO ("amplitude " << amplitude << ": mean " << mean << ", worst " << worst << " iterations");
        CHECK (finite);
        CHECK (worst <= umber::dsp::classa::ClassACircuit::Solver::maxIterations);
        CHECK (mean < (amplitude < 2.0 ? 3.2 : 4.0));
    }
}

TEST_CASE ("ClassAModel: long silence after signal never produces denormals", "[classa]")
{
    ClassAModel model;
    model.prepare (88200.0);

    auto burst = sine (50.0, 2.0, 88200.0, 8820);
    model.process (burst.data(), (int) burst.size());

    std::vector<float> silence ((size_t) (20 * 88200), 0.0f);
    model.process (silence.data(), (int) silence.size());

    int denormals = 0;
    for (auto v : silence)
        if (std::abs (v) > 0.0f && std::abs (v) < FLT_MIN)
            ++denormals;

    CHECK (denormals == 0);
    CHECK (std::abs (silence.back()) < 1.0e-6);
}

TEST_CASE ("ClassAModel: switching events do not fold back into the audio band", "[classa]")
{
    // A 7 kHz tone at +9 dBFS (Drive 5 at 44.1 kHz in Standard quality) makes the follower cut
    // off and turn back on within a fraction of a microsecond: its output really contains
    // harmonics far above the 176.4 kHz the model runs at. Measure what folds back below 20 kHz.
    constexpr double osRate = 176400.0, f0 = 7000.0, amplitude = 2.81;
    ClassAModel model;
    model.prepare (osRate);

    const int settle = (int) (0.3 * osRate), window = (int) (0.1 * osRate);
    auto y = sine (f0, amplitude, osRate, settle + window);
    model.process (y.data(), (int) y.size());
    const float* w = y.data() + settle;

    const auto fundamental = toneAmplitude (w, window, f0, osRate);
    double worst = -200.0, worstFreq = 0.0;
    for (int n = 4; n <= 60; ++n)
    {
        auto f = std::fmod (n * f0, osRate);
        f = std::min (f, osRate - f);
        if (n * f0 < 0.5 * osRate || f > 20000.0)
            continue;   // a real harmonic, or folded outside the audio band
        const auto level = db (toneAmplitude (w, window, f, osRate) / fundamental);
        if (level > worst)
        {
            worst = level;
            worstFreq = f;
        }
    }

    INFO ("worst fold-back " << worst << " dBc at " << worstFreq << " Hz");
    CHECK (worst < -70.0);   // sampled at the instants alone: -53 dBc
}
