// The identity gate (PLAN.md, Measurement and quality gates): with no esses the output is the
// latency-aligned input bit for bit, in every shape, stereo mode and latency, Harsh on or off.

#include "ChainRun.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace halite;
using namespace halite::testing;

namespace
{
    struct Idle
    {
        const char* name;
        Channels signal;
        float thresholdDb;      // Relative; pink noise reads about −5 dB in both bands, so it sits under 0 dB
    };

    std::vector<Idle> idleSignals (double rate)
    {
        const auto n = samplesFor (1.5, rate);
        auto tones = sum (sine (110.0, n, rate, 0.2), sum (sine (440.0, n, rate, 0.1), sine (1000.0, n, rate, 0.05)));
        return {
            { "vowels", { vowel (150.0, n, rate, 0.1, 1), harmonic ({ .f0 = 196.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -6.0, .rms = 0.08 }, n, rate) }, -17.6f },
            { "sines", { tones, scaled (tones, -0.7) }, -17.6f },
            { "pink noise under the threshold", { pinkNoise (n, 0.05, 3), pinkNoise (n, 0.05, 4) }, 0.0f },
            { "silence", { Samples ((size_t) n, 0.0f), Samples ((size_t) n, 0.0f) }, -17.6f },
        };
    }
}

TEST_CASE ("Identity: no esses, every shape, stereo mode and latency: the aligned input bit for bit", "[identity]")
{
    const auto rate = 48000.0;

    for (const auto& idle : idleSignals (rate))
        for (const auto shape : { Shape::split, Shape::wide, Shape::spectral })
            for (const auto stereo : { StereoMode::linked, StereoMode::unlinked, StereoMode::mid, StereoMode::side })
                for (const auto latency : { LatencyMode::normal, LatencyMode::live })
                    for (const auto harsh : { false, true })
                    {
                        ChainRun run;
                        run.rate = rate;
                        run.settings.shape = shape;
                        run.settings.detector.stereo = stereo;
                        run.settings.detector.latency = latency;
                        run.settings.detector.harshOn = harsh;
                        run.settings.detector.thresholdDb = idle.thresholdDb;
                        run.settings.detector.harshThresholdDb = std::max (idle.thresholdDb, run.settings.detector.harshThresholdDb);
                        run.blocks = { 512, 33, 1, 4096 };

                        dsp::HaliteChain chain;
                        chain.prepare (rate, 4096, 2, latency);
                        const auto out = runChain (idle.signal, run, &chain);
                        const auto delay = dsp::HaliteChain::latencySamples (latency, rate);

                        INFO (idle.name << ", shape " << (int) shape << ", stereo " << (int) stereo << ", "
                                        << (latency == LatencyMode::live ? "Live" : "Normal") << ", Harsh " << harsh);
                        REQUIRE (chain.events().count() == 0);
                        CHECK (juce::exactlyEqual (chain.meters().essReductionDb[0].load(), 0.0f));
                        for (size_t c = 0; c < 2; ++c)
                            CHECK (firstDifference (out[c], idle.signal[c], delay) == -1);
                    }
}

TEST_CASE ("Identity: a mono bus, with and without a stereo key", "[identity]")
{
    const auto rate = 44100.0;
    const auto n = samplesFor (1.0, rate);
    const Channels in { vowel (220.0, n, rate, 0.1, 5) };
    const Channels key { vowel (150.0, n, rate, 0.1, 6), vowel (160.0, n, rate, 0.1, 7) };

    for (const auto shape : { Shape::split, Shape::wide })
        for (const auto external : { false, true })
            for (const auto latency : { LatencyMode::normal, LatencyMode::live })
            {
                ChainRun run;
                run.rate = rate;
                run.settings.shape = shape;
                run.settings.detector.key = external ? KeySource::external : KeySource::internal;
                run.settings.detector.latency = latency;
                run.settings.detector.harshOn = true;
                run.key = &key;
                const auto out = runChain (in, run);
                INFO ("shape " << (int) shape << ", external " << external << ", Live " << (latency == LatencyMode::live));
                CHECK (firstDifference (out[0], in[0], dsp::HaliteChain::latencySamples (latency, rate)) == -1);
            }
}

TEST_CASE ("Identity: the output returns to the input bit for bit once a reduction has let go", "[identity]")
{
    // The release is exponential in dB; a depth under 10⁻⁶ (−120 dB) counts as none, so identity
    // comes back once an 8 dB reduction has released by ln (8 / 8.7e-6) ≈ 13.7 time constants,
    // not when the float underflows (about 90 of them).
    const auto rate = 48000.0;
    const auto n = samplesFor (2.0, rate);
    const auto voice = vowel (150.0, n, rate, 0.1, 1);
    const auto ess = gated (bandNoise (4500.0, 12000.0, n, rate, 3, 0.05), samplesFor (0.3, rate), samplesFor (0.45, rate), rate);
    const Channels in { sum (voice, ess), sum (voice, ess) };

    for (const auto shape : { Shape::split, Shape::wide })
    {
        ChainRun run;
        run.rate = rate;
        run.settings.shape = shape;
        run.settings.detector.harshOn = true;
        run.settings.detector.speedAuto = false;
        run.settings.detector.releaseMs = 40.0f;
        dsp::HaliteChain chain;
        chain.prepare (rate, 512, 2, LatencyMode::normal);
        const auto out = runChain (in, run, &chain);
        REQUIRE (chain.events().count() > 0);

        const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
        auto last = 0;
        for (int i = delay; i < n; ++i)
            if (! sameBits (out[0][(size_t) i], in[0][(size_t) (i - delay)]))
                last = i - delay;
        const auto afterEss = (last - samplesFor (0.45, rate)) / rate;
        INFO ("shape " << (int) shape << ": the last changed sample is " << afterEss * 1000.0 << " ms after the ess ends");
        CHECK (last > samplesFor (0.3, rate));
        CHECK (afterEss < 14.0 * 0.040 + 0.02);
    }
}

TEST_CASE ("Identity: Shape and Stereo changes keep it, denormals included", "[identity]")
{
    // A crossfade between two lanes that both remove nothing must not touch the samples: under
    // flush-to-zero even a subtraction of 0 would zero a denormal.
    const auto rate = 48000.0;
    const auto n = samplesFor (1.0, rate);
    auto left = vowel (150.0, n, rate, 0.1, 1);
    for (int i = 0; i < n; i += 7)
        left[(size_t) i] = (i / 7) % 2 == 0 ? std::numeric_limits<float>::denorm_min() : -1.0e-40f;
    const Channels in { left, scaled (left, -0.5) };

    ChainRun run;
    run.rate = rate;
    run.automate = [&] (int pos, dsp::ChainSettings& s)
    {
        s.shape = (pos / 4096) % 2 == 0 ? Shape::split : Shape::wide;
        s.detector.stereo = static_cast<StereoMode> ((pos / 6144) % numStereoModes);
    };
    const auto out = runChain (in, run);
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
    for (size_t c = 0; c < 2; ++c)
        CHECK (firstDifference (out[c], in[c], delay) == -1);
}
