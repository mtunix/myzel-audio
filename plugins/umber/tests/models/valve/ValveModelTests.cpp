#include "dsp/models/valve/ValveModel.h"
#include "support/ModelConformance.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using umber::dsp::ValveModel;

UMBER_MODEL_CONFORMANCE_TESTS (ValveModel, "[valve]", 0.01)

namespace
{
    constexpr double rate = 192000.0;

    double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }

    /** RMS of y[from, to). */
    double rms (const std::vector<float>& y, int from, int to)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i)
            sum += (double) y[(size_t) i] * y[(size_t) i];
        return std::sqrt (sum / std::max (1, to - from));
    }
} // namespace

TEST_CASE ("ValveModel: 12AX7 operating point is the textbook one", "[valve]")
{
    ValveModel model;
    model.prepare (rate);

    // 100k plate load, 1.5k cathode, ~250 V: Vgk ~ -1.2 V, plate ~ 170 V
    INFO ("Vgk " << model.quiescentGridCathodeVolts() << " V, Vpk " << model.quiescentPlateCathodeVolts() << " V");
    CHECK (model.quiescentGridCathodeVolts() < -1.0);
    CHECK (model.quiescentGridCathodeVolts() > -1.5);
    CHECK (model.quiescentPlateCathodeVolts() > 150.0);
    CHECK (model.quiescentPlateCathodeVolts() < 190.0);
}

TEST_CASE ("ValveModel: THD lands in the contract windows", "[valve]")
{
    ValveModel model;
    const auto quiet  = umber::test::measureHarmonics (model, rate, 1000.0, 0.063);
    const auto warm   = umber::test::measureHarmonics (model, rate, 1000.0, 0.25);
    const auto driven = umber::test::measureHarmonics (model, rate, 1000.0, 1.0);
    const auto heavy  = umber::test::measureHarmonics (model, rate, 1000.0, 8.0);

    INFO ("THD %: " << quiet.thd * 100 << " / " << warm.thd * 100 << " / " << driven.thd * 100 << " / " << heavy.thd * 100);
    CHECK (quiet.thd < 0.003);
    CHECK (warm.thd > 0.003);
    CHECK (warm.thd < 0.03);
    CHECK (driven.thd > 0.03);
    CHECK (driven.thd < 0.15);
    CHECK (heavy.thd > 0.15);
    CHECK (heavy.thd < 0.45);
}

TEST_CASE ("ValveModel: second harmonic dominates at moderate drive", "[valve]")
{
    ValveModel model;

    for (double amplitude : { 0.063, 0.25, 1.0 })
    {
        const auto r = umber::test::measureHarmonics (model, rate, 1000.0, amplitude);
        const double h2 = r.harmonics[0], h3 = r.harmonics[1];
        INFO ("A " << amplitude << ": H2 " << db (h2 / r.fundamental) << " dB, H3 " << db (h3 / r.fundamental) << " dB");
        CHECK (db (h2 / h3) > 15.0);
        CHECK (r.evenOddRatio() > 5.0);
    }
}

TEST_CASE ("ValveModel: heavy hits shift the grid bias, which then recovers", "[valve]")
{
    ValveModel model;
    model.prepare (rate);
    model.reset();

    const double quiescent = model.quiescentGridCathodeVolts();

    // 50 ms of a hard 1 kHz hit: grid conduction charges the input coupling cap
    auto burst = umber::test::sine (1000.0, 8.0, rate, (int) (0.05 * rate));
    model.process (burst.data(), (int) burst.size());

    std::vector<float> silence ((size_t) (0.002 * rate), 0.0f);
    model.process (silence.data(), (int) silence.size());
    const double shifted = model.gridCathodeVolts();

    INFO ("Vgk quiescent " << quiescent << " V, 2 ms after the hit " << shifted << " V");
    CHECK (shifted < quiescent - 1.0); // blocked: the stage sits colder after the hit

    silence.assign ((size_t) (0.5 * rate), 0.0f);
    model.process (silence.data(), (int) silence.size());
    INFO ("Vgk after 500 ms " << model.gridCathodeVolts() << " V");
    CHECK (std::abs (model.gridCathodeVolts() - quiescent) < 0.02);
}

TEST_CASE ("ValveModel: a hard hit squashes over the first milliseconds (blocking)", "[valve]")
{
    ValveModel model;
    model.prepare (rate);
    model.reset();

    auto y = umber::test::sine (1000.0, 8.0, rate, (int) (0.03 * rate));
    model.process (y.data(), (int) y.size());

    const int ms = (int) (0.001 * rate);
    const double attack  = rms (y, 0, 2 * ms);
    const double settled = rms (y, 18 * ms, 22 * ms);

    INFO ("first 2 ms " << db (attack) << " dB, 18-22 ms " << db (settled) << " dB");
    CHECK (db (attack / settled) > 0.25); // the attack comes through, then the bias walks colder
    CHECK (db (attack / settled) < 3.0);  // ...a squash, not a gate
}

TEST_CASE ("ValveModel: frequency response is flat in the audio band with a gentle top", "[valve]")
{
    ValveModel model;

    auto gainAt = [&] (double hz) { return umber::test::measureHarmonics (model, rate, hz, 0.001, 1).gainDb; };

    for (double hz : { 40.0, 100.0, 300.0, 3000.0, 6000.0 })
    {
        INFO (hz << " Hz");
        CHECK (std::abs (gainAt (hz)) < 0.5);
    }

    const double top = gainAt (20000.0); // Miller capacitance against the grid stopper
    INFO ("20 kHz: " << top << " dB");
    CHECK (top < -0.5);
    CHECK (top > -3.0);
}

TEST_CASE ("ValveModel: Newton-Raphson converges well inside its budget", "[valve]")
{
    for (double sr : { 88200.0, 768000.0 })
    {
        ValveModel model;
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
            CHECK (worst <= (amplitude <= 8.0 ? 4 : 8));
        }
    }
}

TEST_CASE ("ValveModel: no denormals after long silence", "[valve]")
{
    ValveModel model;
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

TEST_CASE ("ValveModel: harmonic signature is independent of the oversampled rate", "[valve]")
{
    ValveModel a, b;
    const auto low  = umber::test::measureHarmonics (a, 88200.0, 1000.0, 1.0);
    const auto high = umber::test::measureHarmonics (b, 768000.0, 1000.0, 1.0);

    INFO ("THD " << low.thd * 100 << " % vs " << high.thd * 100 << " %");
    CHECK (std::abs (db (low.thd / high.thd)) < 0.5);
    CHECK (std::abs (db (low.harmonics[0] / high.harmonics[0])) < 0.5);
}
