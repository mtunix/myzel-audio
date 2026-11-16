// No clicks or zipper noise (PLAN.md, Measurement and quality gates): the 3rd-difference crest on
// sines stays at or under −60 dB through Shape, Detection and Stereo changes, Bypass and fast
// automation. The main input is a pair of low sines (their own crest is about −79 dB); an external
// key holds a reduction, so Wide moves the sines' level with every change of it.

#include "ChainRun.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <complex>
#include <cstdio>
#include <string>

using namespace halite;
using namespace halite::testing;

namespace
{
    constexpr double rate = 48000.0;
    constexpr double seconds = 3.0;
    constexpr double settle = 0.3;      // the first reduction's attack is the key's, not a change

    struct Scenario
    {
        const char* name;
        Automation automate;
        bool harsh = false;
    };

    /**
        A voice with a steady ess and a harsh resonance on the key, louder on the left: on the left
        7.5 dB of reduction in Voice, 5.4 dB in Allround (at −36 dBFS); with the Ess band out of
        the way (Harsh holds during an ess), 4 dB of Harsh reduction at −30 dB.
    */
    Channels keySignal (int n)
    {
        const auto voice = vowel (150.0, n, rate, 0.1, 1);
        const auto ess = bandNoise (4500.0, 12000.0, n, rate, 3, 0.025);
        const auto harsh = harmonic ({ .f0 = 150.0, .topHz = 4000.0, .peakHz = 3200.0, .peakDb = 24.0, .rms = 0.03 }, n, rate);
        const auto left = sum (sum (voice, ess), harsh);
        return { left, sum (sum (scaled (voice, 0.9), scaled (ess, 0.6)), harsh) };
    }

    double worstCrest (Shape shape, LatencyMode latency, const Scenario& scenario)
    {
        const auto n = samplesFor (seconds, rate);
        const Channels in { sine (330.0, n, rate, 0.25), sine (330.0, n, rate, 0.2, 1.1) };
        const auto key = keySignal (n);

        ChainRun run;
        run.rate = rate;
        run.settings.shape = shape;
        run.settings.detector.latency = latency;
        run.settings.detector.key = KeySource::external;
        run.settings.detector.harshOn = scenario.harsh;
        run.settings.detector.thresholdAbsDb = -36.0f;
        run.key = &key;
        run.blocks = { 256 };
        const auto base = run.settings;
        run.automate = [&] (int pos, dsp::ChainSettings& s)
        {
            s = base;
            scenario.automate (pos, s);
        };
        const auto out = runChain (in, run);
        const auto from = samplesFor (settle, rate) + dsp::HaliteChain::latencySamples (latency, rate);
        return std::max (clickCrestDb (out[0], from, n), clickCrestDb (out[1], from, n));
    }

    /** Harsh holds while the Ess band acts: keep that out of the way, and put Harsh well over its threshold. */
    void freeHarsh (dsp::ChainSettings& s)
    {
        s.detector.thresholdDb = 24.0f;     // the top of its range
        s.detector.thresholdAbsDb = 0.0f;
        s.detector.harshThresholdDb = -30.0f;
    }

    const char* nameOf (Shape s) { return s == Shape::wide ? "Wide" : (s == Shape::spectral ? "Spectral" : "Split"); }

    /** Settings that change every `period` seconds, cycling through `count` states. */
    int step (int pos, double period, int count) { return (int) (pos / (period * rate)) % count; }

    std::vector<Scenario> scenarios()
    {
        return {
            { "Shape: the other two in turn", [] (int pos, dsp::ChainSettings& s)
              {
                  const auto others = s.shape == Shape::split ? std::array { Shape::wide, Shape::spectral }
                                                              : (s.shape == Shape::wide ? std::array { Shape::split, Shape::spectral }
                                                                                        : std::array { Shape::split, Shape::wide });
                  const auto k = step (pos, 0.37, 4);
                  s.shape = k % 2 == 0 ? s.shape : others[(size_t) (k / 2)];
              } },
            { "Detection Voice <-> Allround", [] (int pos, dsp::ChainSettings& s) { s.detector.detection = step (pos, 0.37, 2) == 0 ? Detection::voice : Detection::allround; } },
            { "Stereo through all four", [] (int pos, dsp::ChainSettings& s) { s.detector.stereo = static_cast<StereoMode> (step (pos, 0.29, numStereoModes)); } },
            { "Bypass on and off", [] (int pos, dsp::ChainSettings& s) { s.bypass = step (pos, 0.31, 2) == 1; } },
            { "Output, every block", [] (int pos, dsp::ChainSettings& s) { s.outputDb = (float) (12.0 * std::sin (pos / rate * 9.0) * std::sin (pos / rate * 2.3)); } },
            { "Threshold, every block", [] (int pos, dsp::ChainSettings& s) { s.detector.thresholdDb = (float) (-18.0 + 8.0 * std::sin (pos / rate * 11.0)); } },
            { "Range, every block", [] (int pos, dsp::ChainSettings& s) { s.detector.rangeDb = (float) (6.0 + 6.0 * std::sin (pos / rate * 7.0)); } },
            { "Detect Low and High, every block", [] (int pos, dsp::ChainSettings& s)
              {
                  s.detector.detectLowHz = (float) (4500.0 * std::exp2 (std::sin (pos / rate * 5.0)));
                  s.detector.detectHighHz = (float) (12000.0 * std::exp2 (0.5 * std::sin (pos / rate * 3.0)));
              } },
            { "Harsh Threshold, every block", [] (int pos, dsp::ChainSettings& s)
              {
                  freeHarsh (s);
                  s.detector.harshThresholdDb = (float) (-30.0 + 6.0 * std::sin (pos / rate * 9.0));
              }, true },
            { "Look-ahead, every block", [] (int pos, dsp::ChainSettings& s) { s.detector.lookaheadMs = (float) (5.0 + 5.0 * std::sin (pos / rate * 6.0)); } },
            { "Harsh Freq, Width and Range, every block", [] (int pos, dsp::ChainSettings& s)
              {
                  s.detector.harshFreqHz = (float) (3200.0 * std::exp2 (0.5 * std::sin (pos / rate * 4.0)));
                  s.detector.harshWidthOct = (float) (1.0 + 0.5 * std::sin (pos / rate * 6.0));
                  s.detector.harshRangeDb = (float) (6.0 + 6.0 * std::sin (pos / rate * 5.0));
                  freeHarsh (s);
              }, true },
            { "Harsh on and off", [] (int pos, dsp::ChainSettings& s)
              {
                  s.detector.harshOn = step (pos, 0.41, 2) == 1;
                  freeHarsh (s);
              } },
            { "Listen Output <-> Removed", [] (int pos, dsp::ChainSettings& s) { s.listen = step (pos, 0.37, 2) == 0 ? Listen::output : Listen::removed; } },
        };
    }
}

TEST_CASE ("Clicks: changes and fast automation stay under -60 dB of 3rd-difference crest, Normal latency", "[clicks]")
{
    for (const auto shape : { Shape::split, Shape::wide, Shape::spectral })
        for (const auto& scenario : scenarios())
        {
            const auto crest = worstCrest (shape, LatencyMode::normal, scenario);
            INFO (nameOf (shape) << ", " << scenario.name << ": " << crest << " dB");
            CHECK (crest <= -60.0);
        }
}

namespace
{
    // Without look-ahead the detector's attack is a step at the hop: a target that jumps every block
    // (Detect swept, Wide) lands within two hops. Accepted as Live's attack (PLAN.md, Settled 40):
    // reported, held to its measured value.
    constexpr const char* jumpingTarget = "Detect Low and High, every block";
    constexpr double jumpingTargetDb = -59.2;   // measured 2026-11-14 (GCC, x86-64)
}

TEST_CASE ("Clicks: changes and fast automation stay under -60 dB of 3rd-difference crest, Live latency", "[clicks]")
{
    // The mechanisms (Shape, Stereo, Bypass, Output, Listen, the Harsh band) and the cuts are gated
    // here as in Normal. The jumping target in Wide is held to its measured value, 0.2 dB of margin
    // for other compilers' rounding.
    for (const auto shape : { Shape::split, Shape::wide })
        for (const auto& scenario : scenarios())
        {
            const auto crest = worstCrest (shape, LatencyMode::live, scenario);
            INFO ((shape == Shape::wide ? "Wide, " : "Split, ") << scenario.name << ": " << crest << " dB");
            if (shape == Shape::wide && std::string (scenario.name) == jumpingTarget)
                CHECK (crest <= jumpingTargetDb + 0.2);
            else
                CHECK (crest <= -60.0);
        }
}

namespace
{
    /**
        In the band: the output's complex envelope at 6 kHz (bin 64 of a 512-point frame at 48 kHz),
        heterodyned to 0 Hz and averaged over one period of 6 kHz (8 samples), which removes the
        image at 12 kHz exactly; then the peak of its 3rd difference over its RMS, in dB. A sine's
        own 3rd difference at 6 kHz is −4 dB of it, so the 330 Hz gate cannot see a cut there; the
        envelope of a steady sine has none, and a gain that steps or bends shows. Not the plan's
        gate: the detector's hop-to-hop ripple alone reads about −47 dB in Split and Wide, so the
        report compares the shapes (spectral/NOTES.md, Clicks).
    */
    double worstEnvelopeCrest (Shape shape, const Scenario& scenario)
    {
        const auto n = samplesFor (seconds, rate);
        const Channels in { sine (6000.0, n, rate, 0.1), sine (6000.0, n, rate, 0.08, 1.1) };
        const auto key = keySignal (n);

        ChainRun run;
        run.rate = rate;
        run.settings.shape = shape;
        run.settings.detector.key = KeySource::external;
        run.settings.detector.harshOn = scenario.harsh;
        run.settings.detector.thresholdAbsDb = -36.0f;
        run.key = &key;
        run.blocks = { 256 };
        const auto base = run.settings;
        run.automate = [&] (int pos, dsp::ChainSettings& s)
        {
            s = base;
            scenario.automate (pos, s);
        };
        const auto out = runChain (in, run);
        const auto from = samplesFor (settle, rate) + dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);

        auto worst = -300.0;
        for (const auto& x : out)
        {
            std::vector<std::complex<double>> e ((size_t) n);
            std::complex<double> sum {};
            for (int i = 0; i < n; ++i)
            {
                sum += (double) x[(size_t) i] * std::polar (1.0, -2.0 * std::numbers::pi * 6000.0 * i / rate);
                if (i >= 8)
                    sum -= (double) x[(size_t) i - 8] * std::polar (1.0, -2.0 * std::numbers::pi * 6000.0 * (i - 8) / rate);
                e[(size_t) i] = sum / 8.0;
            }
            auto peak = 0.0, power = 0.0;
            for (int i = from; i < n; ++i)
            {
                peak = std::max (peak, std::abs (e[(size_t) i] - 3.0 * e[(size_t) i - 1] + 3.0 * e[(size_t) i - 2] - e[(size_t) i - 3]));
                power += std::norm (e[(size_t) i]);
            }
            worst = std::max (worst, 20.0 * std::log10 (std::max (peak, 1.0e-30) / std::sqrt (power / (n - from))));
        }
        return worst;
    }
}

TEST_CASE ("Clicks: report", "[.report]")
{
    std::printf ("\n3rd-difference crest (dB), 330 Hz sines, key holding a reduction, %0.f kHz:\n", rate / 1000.0);
    std::printf ("  %-42s %8s %8s %8s %8s %8s\n", "", "Split N", "Wide N", "Spectr N", "Split L", "Wide L");
    auto all = scenarios();
    all.push_back ({ "nothing changes", [] (int, dsp::ChainSettings&) {} });
    for (const auto& scenario : all)
        std::printf ("  %-42s %8.1f %8.1f %8.1f %8.1f %8.1f\n", scenario.name,
                     worstCrest (Shape::split, LatencyMode::normal, scenario), worstCrest (Shape::wide, LatencyMode::normal, scenario),
                     worstCrest (Shape::spectral, LatencyMode::normal, scenario), worstCrest (Shape::split, LatencyMode::live, scenario),
                     worstCrest (Shape::wide, LatencyMode::live, scenario));

    std::printf ("\nIn the band: 3rd-difference crest (dB) of the 6 kHz envelope, Normal latency:\n");
    std::printf ("  %-42s %8s %8s %8s\n", "", "Split", "Wide", "Spectral");
    for (const auto& scenario : all)
        std::printf ("  %-42s %8.1f %8.1f %8.1f\n", scenario.name, worstEnvelopeCrest (Shape::split, scenario),
                     worstEnvelopeCrest (Shape::wide, scenario), worstEnvelopeCrest (Shape::spectral, scenario));
}
