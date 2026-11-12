#pragma once

#include "support/MachineGates.h"

#include <catch2/catch_test_macros.hpp>

/**
    The quality gates every machine passes (PLAN.md), as Catch2 test cases. A machine's test file
    instantiates them with its options:

        MULBERRY_MACHINE_GATES (Machine::reel, "Reel", "[reel]", reelGates())

    Relaxed gates are relaxed here, with the reason in the machine's NOTES.md.
*/
namespace mulberry::gates
{

struct GateOptions
{
    double timeToleranceSamples = -1.0;     // >= 0: first echo within this many samples (Clean)
    double timeTolerancePercent = 0.1;      // otherwise the mean echo time within this
    double repeatLossToleranceDb = 0.2;
    double driveSpreadDb = 0.4;             // +-0.2 dB
    double flutterTarget = -1.0;            // < 0: no target (report only)
    double flutterTolerance = 0.2;          // relative
    double glideCrestDb = -60.0;
    double maxPeak = 2.0;                   // +6 dBFS
    double runawayMinDb = -40.0;
    double runawaySteadyDb = 1.5;
    double pullBackDb = -30.0;
    float paramA = 0.5f, paramB = 0.5f;
};

} // namespace mulberry::gates

#define MULBERRY_MACHINE_GATES(machineValue, name, tag, optionsExpr)                                         \
    TEST_CASE (name ": echo time follows Time at every rate", tag)                                           \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })                                         \
            for (const auto ms : { 80.0f, 375.0f, 1000.0f })                                                 \
            {                                                                                                \
                const auto measured = mulberry::gates::echoTimeMs (machineValue, fs, ms, o.paramA, o.paramB); \
                INFO (fs << " Hz, " << ms << " ms: measured " << measured << " ms");                          \
                if (o.timeToleranceSamples >= 0.0)                                                           \
                    CHECK (std::abs (measured - ms) * fs * 0.001 <= o.timeToleranceSamples);                  \
                else                                                                                         \
                    CHECK (100.0 * std::abs (measured - ms) / ms <= o.timeTolerancePercent);                  \
            }                                                                                                \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": per-repeat loss matches the predicted loop loss", tag)                                \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        for (const auto& r : mulberry::gates::repeatLosses (machineValue, 48000.0, 300.0f, 0.7f, 8, o.paramA, o.paramB)) \
        {                                                                                                    \
            INFO (r.frequency << " Hz: measured " << r.measuredDb << " dB per repeat, predicted " << r.predictedDb); \
            CHECK (std::abs (r.measuredDb - r.predictedDb) <= o.repeatLossToleranceDb);                       \
        }                                                                                                    \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": Drive changes colour, not the echo level", tag)                                       \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        double lo = 1.0e9, hi = -1.0e9;                                                                      \
        for (float d = -12.0f; d <= 24.0f; d += 6.0f)                                                        \
        {                                                                                                    \
            const auto level = mulberry::gates::echoLevelDb (machineValue, 48000.0, d, o.paramA, o.paramB);  \
            lo = std::min (lo, level);                                                                       \
            hi = std::max (hi, level);                                                                       \
        }                                                                                                    \
        INFO ("echo level spread over Drive: " << hi - lo << " dB");                                         \
        CHECK (hi - lo <= o.driveSpreadDb);                                                                  \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": Wobble 0 is steady, nominal Wobble hits its flutter target", tag)                     \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        const auto still = mulberry::gates::flutterPercent (machineValue, 48000.0, 0.0f, o.paramA, o.paramB, 8.0); \
        INFO ("flutter at Wobble 0: " << still << " %");                                                     \
        CHECK (still < 1.0e-4);                                                                              \
        if (o.flutterTarget > 0.0)                                                                           \
        {                                                                                                    \
            const auto nominal = mulberry::gates::flutterPercent (machineValue, 48000.0, 0.5f, o.paramA, o.paramB); \
            INFO ("flutter at nominal Wobble: " << nominal << " % (target " << o.flutterTarget << " %)");    \
            CHECK (std::abs (nominal - o.flutterTarget) <= o.flutterTolerance * o.flutterTarget);            \
        }                                                                                                    \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": a Time jump changes time the machine's way, without a click", tag)                    \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        const auto r = mulberry::gates::glide (machineValue, 48000.0, o.paramA, o.paramB);                  \
        INFO ("3rd-difference crest " << r.jumpDb << " dB through the jump, " << r.steadyDb << " dB steady"); \
        CHECK (r.excessDb() <= 0.0);                                                                         \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": 120 % at Drive max with a 0 dBFS square stays bounded", tag)                          \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        for (int m = 0; m < mulberry::numModes; ++m)                                                         \
        {                                                                                                    \
            const auto r = mulberry::gates::squareStress (machineValue, 48000.0, 20.0, o.paramA, o.paramB, static_cast<mulberry::Mode> (m)); \
            INFO ("mode " << m << ": peak " << mulberry::analysis::toDb (r.peak) << " dBFS");               \
            CHECK (r.finite);                                                                                \
            CHECK (r.peak <= o.maxPeak);                                                                     \
        }                                                                                                    \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": above 100 % it runs away to a steady level, and comes back", tag)                     \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        const auto r = mulberry::gates::runaway (machineValue, 48000.0, o.paramA, o.paramB);                \
        INFO ("level " << r.levelDb << " dBFS, peak steadiness " << r.steadinessDb << " dB, RMS change "      \
                       << r.rmsChangeDb << " dB, after pulling back "                                         \
                       << r.afterPullBackDb << " dB");                                                        \
        CHECK (r.finite);                                                                                    \
        CHECK (r.levelDb > o.runawayMinDb);                                                                  \
        CHECK (std::abs (r.steadinessDb) < o.runawaySteadyDb);                                               \
        CHECK (r.afterPullBackDb < o.pullBackDb);                                                            \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": up to 100 % every trail decays, in every mode", tag)                                  \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        uint32_t seed = 12345u;                                                                              \
        auto pick = [&seed] (float lo, float hi)                                                             \
        {                                                                                                    \
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;                                       \
            return lo + (hi - lo) * (float) (seed % 1000u) / 999.0f;                                         \
        };                                                                                                   \
        for (int corner = 0; corner < 12; ++corner)                                                          \
        {                                                                                                    \
            auto s = mulberry::gates::machineAt (machineValue, pick (20.0f, 400.0f), 1.0f);                  \
            s.plan.mode = static_cast<mulberry::Mode> (corner % mulberry::numModes);                         \
            s.paramA = corner % 3 == 0 ? o.paramA : pick (0.0f, 1.0f);                                      \
            s.paramB = corner % 3 == 0 ? o.paramB : pick (0.0f, 1.0f);                                      \
            s.driveDb = pick (-12.0f, 24.0f);                                                                \
            s.diffuse = pick (0.0f, 1.0f);                                                                   \
            s.wobble = pick (0.0f, 1.0f);                                                                    \
            s.plan.crossFeed = pick (0.0f, 1.0f);                                                            \
            s.plan.groove = pick (0.0f, 0.5f);                                                               \
            s.plan.steps = 1 + (int) pick (0.0f, 15.0f);                                                     \
            s.plan.stepLevels.fill (1.0f);                                                                   \
            s.timeMs[1] = pick (20.0f, 400.0f);                                                              \
            mulberry::dsp::EchoChain chain;                                                                  \
            chain.prepare (48000.0, 256);                                                                    \
            const auto e = mulberry::gates::trailEnergies (chain, s, 48000.0, 8);                            \
            /* The loop is contractive; the diffuser (an allpass) may move energy later in time, */            \
            /* so a window can rise a little while the trail smears: never above the first by    */            \
            /* 0.5 dB, and down over the trail.                                                   */            \
            double highest = 0.0;                                                                            \
            for (size_t i = 1; i < e.size(); ++i)                                                            \
                highest = std::max (highest, mulberry::analysis::toDb (e[i] / e[0]));                        \
            INFO ("corner " << corner << ": mode " << (int) s.plan.mode << ", A " << s.paramA << ", B " << s.paramB \
                  << ", drive " << s.driveDb << ", diffuse " << s.diffuse << ": highest window "            \
                  << highest << " dB over the first, last " << mulberry::analysis::toDb (e.back() / e[0]) << " dB"); \
            CHECK (highest <= 0.5);                                                                          \
            CHECK ((e.back() < e[0] || e.back() <= 0.0));                                                   \
        }                                                                                                    \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": Hold keeps the loop for a minute without loss or pitch drift", tag)                   \
    {                                                                                                        \
        const auto r = mulberry::gates::holdDrift (machineValue, 48000.0, 60.0);                            \
        INFO ("worst octave-band drift " << r.worstBandDriftDb << " dB, seam crest " << r.seamCrestDb        \
              << " dB (the machine's own " << r.steadyCrestDb << " dB)");                                     \
        CHECK (r.worstBandDriftDb <= 0.5);                                                                   \
        CHECK (r.seamExcessDb() <= 0.0);                                                                     \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": silence in gives exact silence out at Noise 0", tag)                                  \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        mulberry::dsp::EchoChain chain;                                                                      \
        chain.prepare (48000.0, 256);                                                                        \
        auto s = mulberry::gates::machineAt (machineValue, 150.0f, 0.8f);                                    \
        s.paramA = o.paramA;                                                                                 \
        s.paramB = o.paramB;                                                                                 \
        s.wobble = 0.5f;                                                                                     \
        s.diffuse = 0.4f;                                                                                    \
        const auto n = (size_t) (48000 * 40);                                                                \
        const auto out = mulberry::analysis::render (chain, s, mulberry::analysis::impulse (n, 48000.0, 0.5f), n); \
        CHECK (mulberry::analysis::peak (out.left, n - 48000) == 0.0);                                      \
        CHECK (mulberry::analysis::peak (out.right, n - 48000) == 0.0);                                     \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": per-repeat loss is the same from 44.1 to 192 kHz", tag)                               \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        std::vector<mulberry::gates::RepeatLoss> reference;                                                  \
        for (const auto fs : { 48000.0, 44100.0, 96000.0, 192000.0 })                                         \
        {                                                                                                    \
            const auto losses = mulberry::gates::repeatLosses (machineValue, fs, 300.0f, 0.7f, 5, o.paramA, o.paramB); \
            if (reference.empty())                                                                           \
                reference = losses;                                                                          \
            for (size_t i = 0; i < losses.size(); ++i)                                                       \
            {                                                                                                \
                INFO (fs << " Hz, " << losses[i].frequency << " Hz: " << losses[i].measuredDb << " vs "      \
                         << reference[i].measuredDb << " dB at 48 kHz");                                     \
                CHECK (std::abs (losses[i].measuredDb - reference[i].measuredDb) <= 0.3);                    \
            }                                                                                                \
        }                                                                                                    \
    }                                                                                                        \
                                                                                                             \
    TEST_CASE (name ": CPU within budget", tag "[.perf]")                                                    \
    {                                                                                                        \
        const auto o = optionsExpr;                                                                          \
        const auto single = mulberry::gates::cpuPercent (machineValue, mulberry::Mode::single, 10.0, o.paramA, o.paramB); \
        const auto pattern = mulberry::gates::cpuPercent (machineValue, mulberry::Mode::pattern, 10.0, o.paramA, o.paramB); \
        WARN (name << ": " << single << " % (Single), +" << pattern - single << " % (Pattern, 16 steps)");    \
        CHECK (single <= mulberry::info (machineValue).cpuBudgetPercent);                                    \
    }
