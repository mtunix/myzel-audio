#include "dsp/models/fuzz/FuzzModel.h"
#include "support/ModelConformance.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using umber::dsp::FuzzModel;

UMBER_MODEL_CONFORMANCE_TESTS (FuzzModel, "[fuzz]", 0.05)

namespace
{
    constexpr double rate = 192000.0;

    // port indices of FuzzModel::portVolts
    constexpr int driverGrid = 0, outputGrid = 2, outputPlate = 4;

    double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }
} // namespace

TEST_CASE ("FuzzModel: operating points are those of a cathode-biased SE EL84 amp", "[fuzz]")
{
    FuzzModel model;
    model.prepare (rate);

    INFO ("12AX7 Vgk " << model.quiescentPortVolts (driverGrid) << " V, EL84 Vg1k "
          << model.quiescentPortVolts (outputGrid) << " V, Vpk " << model.quiescentPortVolts (outputPlate) << " V");
    CHECK (model.quiescentPortVolts (driverGrid) < -1.0);
    CHECK (model.quiescentPortVolts (driverGrid) > -1.6);
    CHECK (model.quiescentPortVolts (outputGrid) < -7.0);  // a little colder than the datasheet -7.3 V
    CHECK (model.quiescentPortVolts (outputGrid) > -10.0);
    CHECK (model.quiescentPortVolts (outputPlate) > 240.0);
    CHECK (model.quiescentPortVolts (outputPlate) < 290.0);
}

TEST_CASE ("FuzzModel: the netlist's compile-time sparsity holds at every rate", "[fuzz]")
{
    // The per-sample products skip entries the structural analysis proves zero; a non-zero
    // entry outside that pattern at any rate would be silently dropped.
    for (double sr : { 88200.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0, 768000.0, 800000.0 })
    {
        FuzzModel model;
        model.prepare (sr);
        INFO ("rate " << sr);
        CHECK (model.solverStructureHolds());
    }
}

TEST_CASE ("FuzzModel: THD lands in its windows (dirtier than the contract)", "[fuzz]")
{
    FuzzModel model;
    const auto quiet  = umber::test::measureHarmonics (model, rate, 1000.0, 0.063);
    const auto warm   = umber::test::measureHarmonics (model, rate, 1000.0, 0.25);
    const auto driven = umber::test::measureHarmonics (model, rate, 1000.0, 1.0);
    const auto heavy  = umber::test::measureHarmonics (model, rate, 1000.0, 8.0);

    INFO ("THD %: " << quiet.thd * 100 << " / " << warm.thd * 100 << " / " << driven.thd * 100 << " / " << heavy.thd * 100);
    CHECK (quiet.thd < 0.005);
    CHECK (warm.thd > 0.003);
    CHECK (warm.thd < 0.03);
    CHECK (driven.thd > 0.05);
    CHECK (driven.thd < 0.25);
    CHECK (heavy.thd > 0.30);
    CHECK (heavy.thd < 0.55);
}

TEST_CASE ("FuzzModel: driven hard it is odd-harmonic rich, with an uneven edge", "[fuzz]")
{
    FuzzModel model;
    const auto r = umber::test::measureHarmonics (model, rate, 1000.0, 1.0);
    const double h2 = r.harmonics[0], h3 = r.harmonics[1], h5 = r.harmonics[3];

    INFO ("H2 " << db (h2 / r.fundamental) << " dB, H3 " << db (h3 / r.fundamental) << " dB, H5 "
          << db (h5 / r.fundamental) << " dB, even/odd " << r.evenOddRatio());
    CHECK (db (h3 / h2) > 6.0);        // odd-dominant...
    CHECK (r.evenOddRatio() < 0.5);
    CHECK (db (h2 / r.fundamental) > -40.0); // ...but not a symmetric clipper: the 2nd is there
    CHECK (db (h5 / r.fundamental) > -50.0); // and the spectrum extends upwards
}

TEST_CASE ("FuzzModel: slammed, the interstage bias starves and gates what follows", "[fuzz]")
{
    FuzzModel model;
    model.prepare (rate);
    model.reset();

    const int ms = (int) (0.001 * rate);
    auto tone = [&] (const std::vector<float>& y, int fromMs, int toMs)
    {
        return umber::test::toneAmplitude (y.data() + fromMs * ms, (toMs - fromMs) * ms, 1000.0, rate);
    };

    // reference: a moderate tone on its own
    auto reference = umber::test::sine (1000.0, 0.25, rate, 40 * ms);
    model.process (reference.data(), (int) reference.size());
    const double steady = tone (reference, 20, 40);

    // a slam, then the same moderate tone
    model.reset();
    auto slam = umber::test::sine (1000.0, 30.0, rate, 100 * ms);
    double meanBias = 0.0;
    for (int i = 0; i < (int) slam.size(); ++i)
    {
        model.process (&slam[(size_t) i], 1);
        if (i >= 80 * ms)
            meanBias += model.portVolts (outputGrid) / (20 * ms);
    }

    auto after = umber::test::sine (1000.0, 0.25, rate, 300 * ms);
    model.process (after.data(), (int) after.size());

    const double gated     = tone (after, 5, 15);
    const double recovered = tone (after, 250, 300);

    INFO ("EL84 grid bias: quiescent " << model.quiescentPortVolts (outputGrid) << " V, mean during the slam " << meanBias << " V");
    INFO ("1 kHz tone: alone " << db (steady) << " dB, 5-15 ms after the slam " << db (gated) << " dB, 250 ms later " << db (recovered) << " dB");
    CHECK (meanBias < model.quiescentPortVolts (outputGrid) - 15.0); // grid conduction charged Cc
    CHECK (db (steady / gated) > 10.0);                              // the stage is starved: gated
    CHECK (std::abs (db (steady / recovered)) < 1.0);                // and it comes back
}

TEST_CASE ("FuzzModel: band-limited by the output transformer, flat in between", "[fuzz]")
{
    FuzzModel model;
    auto gainAt = [&] (double hz) { return umber::test::measureHarmonics (model, rate, hz, 0.001, 1).gainDb; };

    for (double hz : { 150.0, 300.0, 3000.0, 6000.0 })
    {
        INFO (hz << " Hz");
        CHECK (std::abs (gainAt (hz)) < 1.0);
    }

    const double low = gainAt (40.0), top = gainAt (20000.0);
    INFO ("40 Hz: " << low << " dB, 20 kHz: " << top << " dB");
    CHECK (low < -1.5); // tight low end
    CHECK (low > -8.0);
    CHECK (top < -0.5);
    CHECK (top > -4.0);
}

TEST_CASE ("FuzzModel: Newton-Raphson converges well inside its budget", "[fuzz]")
{
    for (double sr : { 88200.0, 768000.0 })
    {
        FuzzModel model;
        model.prepare (sr);

        for (double amplitude : { 0.01, 1.0, 8.0, 80.0, 630.0 })
        {
            model.reset();
            auto x = umber::test::sine (997.0, amplitude, sr, (int) (0.05 * sr));
            int worst = 0;
            for (auto& v : x)
            {
                model.process (&v, 1);
                worst = std::max (worst, model.lastIterations());
            }

            INFO ("rate " << sr << ", A " << amplitude << ": worst " << worst << " evaluations");
            CHECK (worst <= (amplitude <= 8.0 ? 6 : 12));
        }
    }
}

TEST_CASE ("FuzzModel: no denormals after long silence", "[fuzz]")
{
    FuzzModel model;
    model.prepare (88200.0);
    model.reset();

    auto x = umber::test::sine (440.0, 2.0, 88200.0, 8820);
    model.process (x.data(), (int) x.size());

    std::vector<float> silence (88200, 0.0f);
    bool anySubnormal = false;
    for (int second = 0; second < 30; ++second)
    {
        std::fill (silence.begin(), silence.end(), 0.0f);
        model.process (silence.data(), (int) silence.size());
        for (auto v : silence)
            anySubnormal = anySubnormal || std::fpclassify (v) == FP_SUBNORMAL;
    }

    CHECK_FALSE (anySubnormal);
}

TEST_CASE ("FuzzModel: harmonic signature is independent of the oversampled rate", "[fuzz]")
{
    FuzzModel a, b;
    const auto low  = umber::test::measureHarmonics (a, 88200.0, 1000.0, 1.0);
    const auto high = umber::test::measureHarmonics (b, 768000.0, 1000.0, 1.0);

    INFO ("THD " << low.thd * 100 << " % vs " << high.thd * 100 << " %");
    CHECK (std::abs (db (low.thd / high.thd)) < 0.5);
    CHECK (std::abs (db (low.harmonics[1] / high.harmonics[1])) < 0.5);
}
