#include "dsp/models/desk/DeskModel.h"
#include "support/ModelConformance.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>
#include <numbers>

using umber::dsp::DeskModel;

UMBER_MODEL_CONFORMANCE_TESTS (DeskModel, "[desk]", 0.01)

namespace
{

using namespace umber;

constexpr double characterRate = 96000.0;

/** Like test::measureHarmonics, but with a longer settle: a sine switched on at phase 0 leaves a
    DC flux offset in the cores (as with transformer inrush) that takes a few hundred ms to
    decay through the magnetising inductances and the output coupling capacitor. */
test::HarmonicResult measureSettled (dsp::CircuitModel& model, double rate, double freq, double amplitude)
{
    model.prepare (rate);
    model.reset();

    const int settle = (int) (1.5 * rate);
    auto pre = test::sine (freq, amplitude, rate, settle);
    model.process (pre.data(), settle);

    // continue the same sine seamlessly: measureHarmonics would prepare/reset, so do it inline
    const int periods = std::max (20, (int) std::round (0.1 * freq));
    const int window  = (int) std::round (periods * rate / freq);
    auto x = test::sine (freq, amplitude, rate, settle + window);
    std::vector<float> y (x.begin() + settle, x.end());
    const std::vector<float> in (y);
    model.process (y.data(), window);

    double mean = 0.0;
    for (auto v : y) mean += v;
    mean /= window;
    for (auto& v : y) v = (float) (v - mean);

    test::HarmonicResult r;
    r.fundamental = test::toneAmplitude (y.data(), window, freq, rate);
    r.gainDb = 20.0 * std::log10 (std::max (r.fundamental, 1.0e-12) / amplitude);
    r.inverted = test::correlation (in.data(), y.data(), window) < 0.0;

    double sumSq = 0.0;
    for (int h = 2; h <= 10 && h * freq < 0.45 * rate; ++h)
    {
        const auto a = test::toneAmplitude (y.data(), window, h * freq, rate);
        r.harmonics.push_back (a);
        sumSq += a * a;
    }

    r.thd = std::sqrt (sumSq) / std::max (r.fundamental, 1.0e-12);
    return r;
}

/** Runs a sine through one transformer stage, returns {B at H rising through 0, H at B rising
    through 0, peak |B|} measured over the last period. */
struct LoopFeatures { double remanence, coercivity, peakB; };

LoopFeatures measureLoop (const dsp::desk::TransformerParams& params, double freq, double volts, double rate)
{
    dsp::desk::TransformerStage stage;
    stage.setParams (params);
    stage.prepare (rate);

    const int period = (int) std::round (rate / freq);
    const int total  = period * (int) std::ceil (0.6 * freq);
    LoopFeatures f {};
    double hPrev = 0.0, bPrev = 0.0;

    for (int i = 0; i < total; ++i)
    {
        stage.process (volts * std::sin (2.0 * std::numbers::pi * freq * i / rate));
        const double h = stage.fieldH(), b = stage.fluxDensity();

        if (i >= total - period)
        {
            if (hPrev < 0.0 && h >= 0.0) f.remanence  = b;
            if (bPrev < 0.0 && b >= 0.0) f.coercivity = h;
            f.peakB = std::max (f.peakB, std::abs (b));
        }

        hPrev = h;
        bPrev = b;
    }

    return f;
}

} // namespace

TEST_CASE ("DeskModel: low frequencies saturate the iron first", "[desk]")
{
    DeskModel model;

    const auto hf0  = test::measureHarmonics (model, characterRate, 1000.0, 1.0);
    const auto lf0  = measureSettled (model, characterRate, 50.0, 1.0);
    const auto hf6  = test::measureHarmonics (model, characterRate, 1000.0, 0.5);
    const auto lf6  = measureSettled (model, characterRate, 30.0, 0.5);
    const auto hf12 = test::measureHarmonics (model, characterRate, 1000.0, 0.25);
    const auto lf12 = measureSettled (model, characterRate, 20.0, 0.25);

    INFO ("THD % 0 dBFS 1k/50: " << hf0.thd * 100 << " / " << lf0.thd * 100
          << ", -6 dBFS 1k/30: " << hf6.thd * 100 << " / " << lf6.thd * 100
          << ", -12 dBFS 1k/20: " << hf12.thd * 100 << " / " << lf12.thd * 100);

    CHECK (lf0.thd > 2.0 * hf0.thd);
    CHECK (lf6.thd > 5.0 * hf6.thd);
    CHECK (lf12.thd > 5.0 * hf12.thd);
}

TEST_CASE ("DeskModel: iron distortion is odd-order, the amp adds even order only when pushed", "[desk]")
{
    DeskModel model;

    const auto iron = measureSettled (model, characterRate, 40.0, 0.5);
    const auto amp  = test::measureHarmonics (model, characterRate, 1000.0, 1.0);

    INFO ("even/odd 40 Hz -6 dBFS: " << iron.evenOddRatio() << ", 1 kHz 0 dBFS: " << amp.evenOddRatio());
    CHECK (iron.evenOddRatio() < 0.2);
    CHECK (amp.evenOddRatio() > 0.02); // asymmetric rails: a touch of H2
    CHECK (amp.evenOddRatio() < 0.5);  // ... but odd-dominated
}

TEST_CASE ("DeskModel: low-level frequency response is flat 40 Hz - 20 kHz at all rates", "[desk]")
{
    for (auto rate : test::conformanceRates)
    {
        DeskModel model;

        for (double f : { 40.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 20000.0 })
        {
            const auto r = test::measureHarmonics (model, rate, f, 0.001);
            INFO ("rate " << rate << " f " << f << " gain " << r.gainDb << " dB");
            CHECK (std::abs (r.gainDb) < 0.5);
            CHECK_FALSE (r.inverted);
        }

        // gentle transformer LF roll-off, no more
        const auto r20 = test::measureHarmonics (model, rate, 20.0, 0.001);
        INFO ("rate " << rate << " 20 Hz gain " << r20.gainDb << " dB");
        CHECK (r20.gainDb > -1.0);
        CHECK (r20.gainDb < 0.0);
    }
}

TEST_CASE ("DeskModel: transformer cores show a sane hysteresis loop", "[desk]")
{
    const auto circuit = umber::dsp::DeskCircuit::standard();
    constexpr double mu0 = 4.0e-7 * std::numbers::pi;

    for (const auto* params : { &circuit.input, &circuit.output })
    {
        // moderate drive: well below saturation, clear loop
        const auto mid = measureLoop (*params, 20.0, 0.25 * params->saturationFlux * 2.0 * std::numbers::pi * 20.0, characterRate);
        // heavy drive: deep saturation
        const auto sat = measureLoop (*params, 20.0, 3.0 * params->saturationFlux * 2.0 * std::numbers::pi * 20.0, characterRate);

        const double bSat = mu0 * params->core.Ms;
        INFO ("mid: Br " << mid.remanence << " Hc " << mid.coercivity << " Bpk " << mid.peakB
              << "  sat: Br " << sat.remanence << " Hc " << sat.coercivity << " Bpk " << sat.peakB << "  Bsat " << bSat);

        // rising branch crosses B = 0 at positive H (loop is traversed counter-clockwise,
        // lagging) and sits at negative B when H crosses 0 (remanence of the previous half-cycle)
        CHECK (mid.coercivity > 0.0);
        CHECK (mid.remanence < 0.0);
        CHECK (sat.coercivity > mid.coercivity);
        CHECK (std::abs (sat.remanence) > std::abs (mid.remanence));

        // coercivity of the order of the pinning constant k, flux bounded by saturation
        CHECK (sat.coercivity < 3.0 * params->core.k);
        CHECK (sat.peakB > 0.8 * bSat);
        CHECK (sat.peakB < 1.3 * bSat);
    }
}

TEST_CASE ("DeskModel: recovers cleanly after slamming the iron", "[desk]")
{
    constexpr double rate = 192000.0;
    DeskModel model;
    const auto fresh = test::measureHarmonics (model, rate, 1000.0, 0.01);

    model.prepare (rate);
    model.reset();

    // +40 dBFS 30 Hz burst drives both cores far into saturation and leaves remanence behind
    auto burst = test::sine (30.0, 100.0, rate, (int) (0.3 * rate));
    model.process (burst.data(), (int) burst.size());

    std::vector<float> silence ((size_t) (0.5 * rate), 0.0f);
    model.process (silence.data(), (int) silence.size());

    const int n = (int) (0.3 * rate);
    auto tone = test::sine (1000.0, 0.01, rate, n);
    model.process (tone.data(), n);

    const int w = (int) (0.1 * rate);
    const double amp = test::toneAmplitude (tone.data() + n - w, w, 1000.0, rate);
    const double gainDb = 20.0 * std::log10 (amp / 0.01);

    INFO ("fresh " << fresh.gainDb << " dB, after burst " << gainDb << " dB");
    CHECK (std::abs (gainDb - fresh.gainDb) < 0.2);

    for (auto v : tone)
        CHECK (std::isfinite (v));
}

TEST_CASE ("DeskModel: saturation character is consistent across oversampling rates", "[desk]")
{
    for (double freq : { 50.0, 1000.0, 5000.0 })
    {
        DeskModel model;
        const auto lo = test::measureHarmonics (model, 88200.0, freq, 1.0);
        const auto hi = test::measureHarmonics (model, 768000.0, freq, 1.0);

        INFO (freq << " Hz THD % at 88.2k / 768k: " << lo.thd * 100 << " / " << hi.thd * 100);
        CHECK (std::abs (lo.thd - hi.thd) < 0.1 * hi.thd + 0.002);
        CHECK (std::abs (lo.gainDb - hi.gainDb) < 0.3);
    }
}

TEST_CASE ("DeskModel: long silence leaves no denormals", "[desk]")
{
    constexpr double rate = 88200.0;
    DeskModel model;
    model.prepare (rate);
    model.reset();

    auto tone = test::sine (40.0, 1.0, rate, (int) (0.2 * rate));
    model.process (tone.data(), (int) tone.size());

    std::vector<float> silence (4096, 0.0f);
    for (int block = 0; block < (int) (60.0 * rate / 4096); ++block)
    {
        model.process (silence.data(), 4096);
        std::fill (silence.begin(), silence.end(), 0.0f);
    }

    model.process (silence.data(), 4096);

    for (auto v : silence)
        CHECK (std::fpclassify (v) != FP_SUBNORMAL);

    for (const auto* t : { &model.inputTransformer(), &model.outputTransformer() })
        CHECK (std::fpclassify (t->fieldH()) != FP_SUBNORMAL);
}
