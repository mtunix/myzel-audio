#pragma once

// Conformance suite every CircuitModel must pass. Use from a model's test file:
//
//     #include "support/ModelConformance.h"
//     UMBER_MODEL_CONFORMANCE_TESTS (ValveModel, "[valve]", 0.01)
//
// The last argument is the maximum THD (ratio) allowed at -24 dBFS: 0.01 for most models,
// up to 0.05 for Fuzz.
//
// The thresholds here are deliberately loose "contract" bounds (see CircuitModel.h);
// per-model character targets belong in the model's own tests.

#include "support/SignalAnalysis.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

namespace umber::test
{

inline constexpr double conformanceRates[] = { 88200.0, 192000.0, 384000.0, 768000.0 };

template <typename ModelType>
void checkSmallSignalGain()
{
    for (auto rate : conformanceRates)
    {
        ModelType model;
        const auto r = measureHarmonics (model, rate, 1000.0, 0.001);
        INFO ("rate " << rate << " gain " << r.gainDb << " dB");
        CHECK (std::abs (r.gainDb) < 0.5);
        CHECK_FALSE (r.inverted);
    }
}

template <typename ModelType>
void checkSaturationOnset (double cleanThdLimit)
{
    ModelType model;
    constexpr double rate = 192000.0;

    const auto quiet  = measureHarmonics (model, rate, 1000.0, 0.063);
    const auto warm   = measureHarmonics (model, rate, 1000.0, 0.25);
    const auto driven = measureHarmonics (model, rate, 1000.0, 1.0);
    const auto heavy  = measureHarmonics (model, rate, 1000.0, 8.0);

    INFO ("THD % at -24/-12/0/+18 dBFS: " << quiet.thd * 100 << " / " << warm.thd * 100
          << " / " << driven.thd * 100 << " / " << heavy.thd * 100);

    CHECK (quiet.thd < cleanThdLimit);
    CHECK (driven.thd > 0.005);
    CHECK (driven.thd < 0.6);
    CHECK (heavy.thd > 0.03);
    CHECK (quiet.thd < warm.thd);
    CHECK (warm.thd < driven.thd);
}

template <typename ModelType>
void checkBoundedUnderAbuse()
{
    for (auto rate : conformanceRates)
    {
        INFO ("rate " << rate);
        ModelType model;
        model.prepare (rate);
        model.reset();

        const int n = (int) (0.2 * rate);

        // +60 dBFS square wave at 100 Hz and 5 kHz
        for (double freq : { 100.0, 5000.0 })
        {
            std::vector<float> square ((size_t) n);
            const int half = std::max (1, (int) (rate / freq / 2));
            for (int i = 0; i < n; ++i)
                square[(size_t) i] = ((i / half) % 2 == 0) ? 1000.0f : -1000.0f;

            const auto r = measureBounds (model, square);
            CHECK (r.allFinite);
            CHECK (r.peak < 8.0);
        }

        // +60 dBFS white noise
        std::mt19937 rng (1234);
        std::uniform_real_distribution<float> dist (-1000.0f, 1000.0f);
        std::vector<float> noise ((size_t) n);
        for (auto& v : noise)
            v = dist (rng);

        const auto r = measureBounds (model, noise);
        CHECK (r.allFinite);
        CHECK (r.peak < 8.0);

        // ...and it must recover to silence afterwards: no self-oscillation and no latch-up.
        // Real circuits may legitimately settle slowly (coupling caps discharging, residual
        // tape remanence), so slow DC drift is allowed; audible activity and stuck bias are not.
        const int silenceLength = (int) (1.0 * rate);
        std::vector<float> silence ((size_t) silenceLength, 0.0f);
        model.process (silence.data(), silenceLength);

        const int tail = silenceLength / 4;
        double mean = 0.0, maxStep = 0.0;
        for (int i = silenceLength - tail; i < silenceLength; ++i)
        {
            mean += silence[(size_t) i];
            maxStep = std::max (maxStep, (double) std::abs (silence[(size_t) i] - silence[(size_t) i - 1]));
        }
        mean /= tail;

        INFO ("tail mean " << mean << ", max sample step " << maxStep);
        CHECK (std::isfinite (mean));
        CHECK (std::abs (mean) < 0.05);  // no latched bias (the chain DC-blocks small offsets)
        CHECK (maxStep < 1.0e-5);        // no oscillation, buzz or chatter
    }
}

template <typename ModelType>
void checkDeterministic()
{
    ModelType model;
    constexpr double rate = 192000.0;
    model.prepare (rate);

    auto run = [&]
    {
        model.reset();
        auto s = sine (440.0, 2.0, rate, 20000);
        model.process (s.data(), 7);                    // odd block sizes on purpose
        model.process (s.data() + 7, 1000);
        model.process (s.data() + 1007, 20000 - 1007);
        return s;
    };

    const auto a = run();
    const auto b = run();
    CHECK (a == b);
}

template <typename ModelType>
void reportPerformance (const char* name)
{
    ModelType model;
    for (auto rate : { 192000.0, 384000.0 })
        for (auto amp : { 0.1, 4.0 })
            WARN (name << " @ " << rate / 1000 << " kHz, amp " << amp << ": "
                       << nanosecondsPerSample (model, rate, amp) << " ns/sample");
}

} // namespace umber::test

#define UMBER_MODEL_CONFORMANCE_TESTS(ModelType, tag, cleanThd)                                          \
    TEST_CASE (#ModelType ": unity small-signal gain, non-inverted, at all rates", tag "[conformance]") \
    { umber::test::checkSmallSignalGain<ModelType>(); }                                          \
    TEST_CASE (#ModelType ": saturation onset follows the level contract", tag "[conformance]")   \
    { umber::test::checkSaturationOnset<ModelType> (cleanThd); }                                     \
    TEST_CASE (#ModelType ": bounded and recovers under +60 dBFS abuse", tag "[conformance]")     \
    { umber::test::checkBoundedUnderAbuse<ModelType>(); }                                        \
    TEST_CASE (#ModelType ": deterministic after reset", tag "[conformance]")                     \
    { umber::test::checkDeterministic<ModelType>(); }                                            \
    TEST_CASE (#ModelType ": performance report", tag "[.perf]")                                  \
    { umber::test::reportPerformance<ModelType> (#ModelType); }
