#include "dsp/chain/SaturatorChain.h"
#include "support/SignalAnalysis.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <chrono>
#include <random>

using namespace umber;
using umber::dsp::ChainSettings;
using umber::dsp::SaturatorChain;

namespace
{
    /** Linear, clean settings: lowest drive, everything neutral. */
    ChainSettings cleanSettings()
    {
        ChainSettings s;
        s.driveKnob = 0.0f;
        s.model = Model::classA;
        s.autoGain = true;
        return s;
    }

    juce::AudioBuffer<float> toneBuffer (int channels, int samples, double rate, double freq, double amp)
    {
        juce::AudioBuffer<float> b (channels, samples);
        const auto s = test::sine (freq, amp, rate, samples);
        for (int ch = 0; ch < channels; ++ch)
            b.copyFrom (ch, 0, s.data(), samples);
        return b;
    }

    /** Runs the chain over the whole buffer in fixed-size blocks, optionally changing settings per block. */
    template <typename SettingsForBlock>
    void runWith (SaturatorChain& chain, juce::AudioBuffer<float>& buffer, int blockSize,
              SettingsForBlock&& settingsForBlock, bool nonRealtime = false)
    {
        dsp::ChainMeters meters;
        for (int start = 0, index = 0; start < buffer.getNumSamples(); start += blockSize, ++index)
        {
            const int len = std::min (blockSize, buffer.getNumSamples() - start);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
            chain.process (view, settingsForBlock (index), nonRealtime, meters);
        }
    }

    void run (SaturatorChain& chain, juce::AudioBuffer<float>& buffer, int blockSize, const ChainSettings& s)
    {
        runWith (chain, buffer, blockSize, [&] (int) { return s; });
    }

    /** Steady-state gain (dB) of the chain at one frequency. */
    double gainAt (const ChainSettings& s, double freq, double rate = 48000.0, double amp = 0.01)
    {
        SaturatorChain chain;
        chain.prepare (rate, 512, 1);

        const int n = (int) (0.6 * rate);
        auto buffer = toneBuffer (1, n, rate, freq, amp);
        run (chain, buffer, 512, s);

        const int window = (int) (0.25 * rate);
        const auto out = test::toneAmplitude (buffer.getReadPointer (0, n - window), window, freq, rate);
        return 20.0 * std::log10 (out / amp);
    }

    /** Largest second difference: a click/discontinuity detector. */
    double maxSecondDifference (const float* x, int start, int end)
    {
        double m = 0.0;
        for (int i = std::max (start, 2); i < end; ++i)
            m = std::max (m, (double) std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
        return m;
    }
}

TEST_CASE ("Latency is constant across every quality and render mode", "[chain]")
{
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        INFO ("rate " << rate);
        SaturatorChain chain;
        chain.prepare (rate, 512, 2);
        const int latency = chain.getLatencySamples();
        CHECK (latency >= 0);
        CHECK (latency < 64);

        // The oversampled rate never exceeds the cap (except the minimum 2x).
        for (int f : { 2, 4, 8, 16 })
        {
            const int eff = SaturatorChain::effectiveFactor (f, rate);
            CHECK ((eff == 2 || rate * eff <= SaturatorChain::maxOversampledRate));
            CHECK (eff <= f);
        }
    }
}

TEST_CASE ("Bypass is sample-exact and latency-aligned", "[chain]")
{
    constexpr double rate = 48000.0;
    SaturatorChain chain;
    chain.prepare (rate, 256, 2);
    const int latency = chain.getLatencySamples();

    juce::AudioBuffer<float> buffer (2, 8192);
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, dist (rng));

    juce::AudioBuffer<float> input (buffer);

    auto s = cleanSettings();
    s.bypass = true;
    s.driveKnob = 10.0f;
    s.slam = true;
    run (chain, buffer, 256, s);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i + latency < buffer.getNumSamples(); ++i)
            REQUIRE (juce::exactlyEqual (buffer.getSample (ch, i + latency), input.getSample (ch, i)));
}

TEST_CASE ("Mix at 0 % returns the dry signal regardless of drive", "[chain]")
{
    constexpr double rate = 48000.0;
    auto s = cleanSettings();
    s.mix = 0.0f;
    s.driveKnob = 10.0f;
    s.slam = true;

    for (double freq : { 100.0, 1000.0, 10000.0 })
    {
        INFO ("freq " << freq);
        CHECK (std::abs (gainAt (s, freq, rate, 0.5)) < 0.05);
    }
}

TEST_CASE ("Dry and wet blend without comb filtering", "[chain]")
{
    // At a clean setting the wet path is (near) unity, so a 50 % blend must be flat:
    // mixing inside the oversampled domain keeps dry and wet phase-coherent.
    auto s = cleanSettings();
    s.mix = 0.5f;

    for (double freq : { 200.0, 1000.0, 5000.0, 12000.0, 18000.0 })
    {
        INFO ("freq " << freq);
        CHECK (std::abs (gainAt (s, freq, 48000.0, 0.003)) < 0.5);
    }
}

TEST_CASE ("Low Cut, Weight and High Cut shape the wet signal", "[chain]")
{
    auto s = cleanSettings();

    SECTION ("filters off are flat")
    {
        for (double freq : { 40.0, 1000.0, 15000.0 })
        {
            INFO ("freq " << freq);
            CHECK (std::abs (gainAt (s, freq, 48000.0, 0.003)) < 0.5);
        }
    }

    SECTION ("low cut")
    {
        s.lowCutHz = 200.0f;
        CHECK (gainAt (s, 50.0, 48000.0, 0.003) < -18.0);
        CHECK (std::abs (gainAt (s, 2000.0, 48000.0, 0.003)) < 0.5);
    }

    SECTION ("weight adds a resonant bump at the cutoff")
    {
        s.lowCutHz = 120.0f;
        const auto plain = gainAt (s, 120.0, 48000.0, 0.003);
        s.weight = true;
        const auto bumped = gainAt (s, 120.0, 48000.0, 0.003);
        INFO ("plain " << plain << " dB, weight " << bumped << " dB");
        CHECK (bumped - plain > 5.0);
        CHECK (bumped > 2.0);
    }

    SECTION ("high cut")
    {
        s.highCutHz = 4000.0f;
        CHECK (gainAt (s, 16000.0, 48000.0, 0.003) < -18.0);
        CHECK (std::abs (gainAt (s, 300.0, 48000.0, 0.003)) < 0.5);
    }
}

TEST_CASE ("Tone tilts around its pivot", "[chain]")
{
    auto s = cleanSettings();

    s.tone = 1.0f;
    const auto brightTilt = gainAt (s, 12000.0, 48000.0, 0.003) - gainAt (s, 60.0, 48000.0, 0.003);
    s.tone = -1.0f;
    const auto darkTilt = gainAt (s, 12000.0, 48000.0, 0.003) - gainAt (s, 60.0, 48000.0, 0.003);

    INFO ("bright " << brightTilt << " dB, dark " << darkTilt << " dB");
    CHECK (brightTilt > 8.0);
    CHECK (brightTilt < 10.5);
    CHECK (darkTilt < -8.0);
    CHECK (darkTilt > -10.5);
}

TEST_CASE ("Switching models mid-stream is click-free", "[chain]")
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    const int n = 48 * block;

    auto base = cleanSettings();
    base.driveKnob = 6.0f;

    // Reference: worst steady-state second difference of each model alone
    double reference = 0.0;
    for (int m = 0; m < numModels; ++m)
    {
        SaturatorChain chain;
        chain.prepare (rate, block, 1);
        auto s = base;
        s.model = static_cast<Model> (m);
        auto buffer = toneBuffer (1, n / 4, rate, 220.0, 0.3);
        run (chain, buffer, block, s);
        reference = std::max (reference, maxSecondDifference (buffer.getReadPointer (0), n / 8, n / 4));
    }

    SaturatorChain chain;
    chain.prepare (rate, block, 1);
    auto buffer = toneBuffer (1, n, rate, 220.0, 0.3);
    runWith (chain, buffer, block, [&] (int index)
    {
        auto s = base;
        s.model = static_cast<Model> ((index / 3) % numModels); // switch every 3 blocks (~32 ms)
        return s;
    });

    const auto switching = maxSecondDifference (buffer.getReadPointer (0), 4 * block, n);
    INFO ("steady " << reference << ", while switching " << switching);
    CHECK (switching < 1.5 * reference);
}

TEST_CASE ("Changing quality and entering offline render is click-free", "[chain]")
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    const int n = 64 * block;

    auto base = cleanSettings();
    base.driveKnob = 5.0f;

    SaturatorChain steadyChain;
    steadyChain.prepare (rate, block, 1);
    auto steady = toneBuffer (1, n, rate, 330.0, 0.3);
    run (steadyChain, steady, block, base);
    const auto reference = maxSecondDifference (steady.getReadPointer (0), n / 2, n);

    SaturatorChain chain;
    chain.prepare (rate, block, 1);
    auto buffer = toneBuffer (1, n, rate, 330.0, 0.3);
    runWith (chain, buffer, block, [&] (int index)
    {
        auto s = base;
        constexpr int factors[] = { 4, 2, 8, 4 };
        s.realtimeFactor = factors[(index / 8) % 4];
        return s;
    });

    const auto switching = maxSecondDifference (buffer.getReadPointer (0), 4 * block, n);
    INFO ("steady " << reference << ", while switching " << switching);
    CHECK (switching < 1.5 * reference);
}

TEST_CASE ("Offline rendering uses the render quality", "[chain]")
{
    auto check = [] (double rate, bool nonRealtime, int renderFactor, int expected)
    {
        SaturatorChain chain;
        chain.prepare (rate, 256, 2);
        auto s = cleanSettings();
        s.realtimeFactor = 4;
        s.renderFactor = renderFactor;

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        dsp::ChainMeters meters;
        chain.process (buffer, s, nonRealtime, meters);
        INFO ("rate " << rate << " nonRealtime " << nonRealtime << " render " << renderFactor);
        CHECK (meters.oversamplingFactor == expected);
    };

    check (48000.0, false, 16, 4);
    check (48000.0, true, 16, 16);
    check (48000.0, true, 0, 4);
    check (96000.0, true, 16, 8);
    check (192000.0, true, 16, 4);
}

TEST_CASE ("Fast drive automation does not zipper", "[chain]")
{
    constexpr double rate = 48000.0;
    constexpr int block = 64;
    const int n = 200 * block;

    auto base = cleanSettings();

    SaturatorChain chain;
    chain.prepare (rate, block, 1);
    auto buffer = toneBuffer (1, n, rate, 150.0, 0.05);
    runWith (chain, buffer, block, [&] (int index)
    {
        auto s = base;
        // jump between extremes every ~10 ms
        s.driveKnob = ((index / 8) % 2 == 0) ? 0.0f : 10.0f;
        s.autoGain = true;
        return s;
    });

    // Every sample stays finite and the signal never jumps by more than a smooth
    // 150 Hz tone at the loudest level could.
    const float* y = buffer.getReadPointer (0);
    double maxStep = 0.0;
    for (int i = 1; i < n; ++i)
    {
        REQUIRE (std::isfinite (y[i]));
        maxStep = std::max (maxStep, (double) std::abs (y[i] - y[i - 1]));
    }
    INFO ("max step " << maxStep);
    CHECK (maxStep < 0.25);
}

TEST_CASE ("Survives every sample rate, block size and channel count", "[chain]")
{
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            INFO ("rate " << rate << " channels " << channels);
            SaturatorChain chain;
            chain.prepare (rate, 512, channels);

            auto s = cleanSettings();
            s.driveKnob = 10.0f;
            s.slam = true;
            s.model = Model::fuzz;

            for (int block : { 1, 17, 512, 1500 }) // 1500 > prepared size: chain must split it
            {
                auto buffer = toneBuffer (channels, block * 4, rate, 1000.0, 1.0);
                run (chain, buffer, block, s);

                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const auto v = buffer.getSample (ch, i);
                        REQUIRE (std::isfinite (v));
                        REQUIRE (std::abs (v) < 16.0f);
                    }
            }
        }
}

TEST_CASE ("Chain performance report", "[.perf]")
{
    for (int factor : { 2, 4, 8, 16 })
        for (int m = 0; m < numModels; ++m)
        {
            constexpr double rate = 48000.0;
            constexpr int block = 256;
            const int n = (int) rate * 2;

            SaturatorChain chain;
            chain.prepare (rate, block, 2);

            auto s = cleanSettings();
            s.driveKnob = 7.0f;
            s.model = static_cast<Model> (m);
            s.realtimeFactor = factor;
            s.renderFactor = factor;

            auto buffer = toneBuffer (2, n, rate, 220.0, 0.5);
            const auto start = std::chrono::steady_clock::now();
            run (chain, buffer, block, s);
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

            WARN (params::modelNames()[m] << " @ " << factor << "x: " << 100.0 * seconds / 2.0
                                         << " % of one core (stereo, 48 kHz)");
        }
}

TEST_CASE ("NaN or Inf input never poisons the plugin", "[chain]")
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    for (int m = 0; m < numModels; ++m)
    {
        INFO (params::modelNames()[m]);
        SaturatorChain chain;
        chain.prepare (rate, block, 2);

        auto s = cleanSettings();
        s.model = static_cast<Model> (m);
        s.driveKnob = 5.0f;

        auto buffer = toneBuffer (2, block * 40, rate, 440.0, 0.3);
        buffer.setSample (0, block * 2 + 5, std::numeric_limits<float>::quiet_NaN());
        buffer.setSample (1, block * 2 + 9, std::numeric_limits<float>::infinity());
        buffer.setSample (0, block * 3, -std::numeric_limits<float>::infinity());
        run (chain, buffer, block, s);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                REQUIRE (std::isfinite (buffer.getSample (ch, i)));

        // and the signal is back (not muted) shortly afterwards
        const int tailStart = block * 30;
        CHECK (buffer.getMagnitude (0, tailStart, block * 10) > 0.05f);
        CHECK (buffer.getMagnitude (1, tailStart, block * 10) > 0.05f);
    }
}

TEST_CASE ("Preparation time report", "[.perf]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        SaturatorChain chain;
        const auto start = std::chrono::steady_clock::now();
        chain.prepare (rate, 512, 2);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
        std::printf ("prepare at %.0f Hz, stereo: %.1f ms\n", rate, ms);
    }
}
