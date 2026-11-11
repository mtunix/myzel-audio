/*
    Delta, Stereo mode (Mid/Side) and Emphasis at chain level. Bias is tested per model.
*/

#include "dsp/chain/SaturatorChain.h"
#include "support/SignalAnalysis.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <random>

using namespace umber;
using umber::dsp::ChainSettings;
using umber::dsp::SaturatorChain;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    ChainSettings driven()
    {
        ChainSettings s;
        s.driveKnob = 7.0f;
        s.model = Model::valve;
        return s;
    }

    /** Stereo programme: different tones left and right plus a little noise. */
    juce::AudioBuffer<float> programme (int n, float leftAmp = 0.3f, float rightAmp = 0.25f)
    {
        juce::AudioBuffer<float> b (2, n);
        const auto l = test::sine (220.0, leftAmp, rate, n);
        const auto r = test::sine (330.0, rightAmp, rate, n);
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> noise (-0.01f, 0.01f);

        for (int i = 0; i < n; ++i)
        {
            b.setSample (0, i, l[(size_t) i] + noise (rng));
            b.setSample (1, i, r[(size_t) i] + noise (rng));
        }
        return b;
    }

    template <typename SettingsForBlock>
    void runWith (juce::AudioBuffer<float>& buffer, SettingsForBlock&& settingsForBlock, int channels = 2)
    {
        SaturatorChain chain;
        chain.prepare (rate, block, channels);
        dsp::ChainMeters meters;

        for (int start = 0, index = 0; start < buffer.getNumSamples(); start += block, ++index)
        {
            const int len = std::min (block, buffer.getNumSamples() - start);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
            chain.process (view, settingsForBlock (index), false, meters);
        }
    }

    juce::AudioBuffer<float> render (juce::AudioBuffer<float> input, const ChainSettings& s)
    {
        runWith (input, [&] (int) { return s; }, input.getNumChannels());
        return input;
    }

    double maxAbsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
    {
        double m = 0.0;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = from; i < a.getNumSamples(); ++i)
                m = std::max (m, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return m;
    }

    double maxSecondDifference (const juce::AudioBuffer<float>& b, int from)
    {
        double m = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* x = b.getReadPointer (ch);
            for (int i = std::max (from, 2); i < b.getNumSamples(); ++i)
                m = std::max (m, (double) std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
        }
        return m;
    }
}

TEST_CASE ("Delta is exactly what the circuit adds", "[chain][delta]")
{
    const int n = 16384;
    const auto input = programme (n);

    for (float mix : { 1.0f, 0.4f })
    {
        INFO ("mix " << mix);
        auto s = driven();
        s.mix = mix;
        const auto normal = render (input, s);

        s.delta = true;
        const auto delta = render (input, s);

        s.delta = false;
        s.mix = 0.0f;
        const auto dry = render (input, s);   // the dry path, through the same resampling filters

        // normal = dry + added, delta = added
        double err = 0.0, level = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 2048; i < n; ++i)
            {
                err = std::max (err, (double) std::abs (normal.getSample (ch, i) - delta.getSample (ch, i) - dry.getSample (ch, i)));
                level = std::max (level, (double) std::abs (delta.getSample (ch, i)));
            }

        INFO ("residual " << err << ", delta peak " << level);
        CHECK (err < 1.0e-5);
        CHECK (level > 0.01);   // the circuit really adds something at this drive
    }
}

TEST_CASE ("Delta engages without clicks", "[chain][delta]")
{
    const int n = 48 * block;
    auto steady = programme (n);
    runWith (steady, [] (int) { return driven(); });

    auto toggled = programme (n);
    runWith (toggled, [] (int index)
    {
        auto s = driven();
        s.delta = (index / 6) % 2 == 1;
        return s;
    });

    const auto reference = maxSecondDifference (steady, 4 * block);
    const auto switching = maxSecondDifference (toggled, 4 * block);
    INFO ("steady " << reference << ", toggling " << switching);
    CHECK (switching < 1.5 * reference);
}

TEST_CASE ("Mid and Side modes leave the other half untouched", "[chain][stereo]")
{
    const int n = 16384;
    const auto sine = test::sine (180.0, 0.5, rate, n);

    juce::AudioBuffer<float> pureMid (2, n), pureSide (2, n);
    for (int i = 0; i < n; ++i)
    {
        pureMid.setSample (0, i, sine[(size_t) i]);
        pureMid.setSample (1, i, sine[(size_t) i]);
        pureSide.setSample (0, i, sine[(size_t) i]);
        pureSide.setSample (1, i, -sine[(size_t) i]);
    }

    auto dryOf = [] (const juce::AudioBuffer<float>& in)
    {
        auto s = driven();
        s.mix = 0.0f;
        return render (in, s);
    };

    SECTION ("Mid mode: a pure side signal passes dry")
    {
        auto s = driven();
        s.stereoMode = StereoMode::mid;
        CHECK (maxAbsDifference (render (pureSide, s), dryOf (pureSide), 2048) < 1.0e-5);
        CHECK (maxAbsDifference (render (pureMid, s), dryOf (pureMid), 2048) > 0.01);
    }

    SECTION ("Side mode: a pure mid signal passes dry")
    {
        auto s = driven();
        s.stereoMode = StereoMode::side;
        CHECK (maxAbsDifference (render (pureMid, s), dryOf (pureMid), 2048) < 1.0e-5);
        CHECK (maxAbsDifference (render (pureSide, s), dryOf (pureSide), 2048) > 0.01);
    }

    SECTION ("Stereo mode processes both")
    {
        const auto s = driven();
        CHECK (maxAbsDifference (render (pureMid, s), dryOf (pureMid), 2048) > 0.01);
        CHECK (maxAbsDifference (render (pureSide, s), dryOf (pureSide), 2048) > 0.01);
    }

    SECTION ("Mono input ignores the mode")
    {
        juce::AudioBuffer<float> mono (1, n);
        mono.copyFrom (0, 0, sine.data(), n);
        auto s = driven();
        const auto stereoResult = render (mono, s);
        s.stereoMode = StereoMode::side;
        CHECK (maxAbsDifference (render (mono, s), stereoResult) < 1.0e-6);
    }
}

TEST_CASE ("Switching the stereo mode is click-free", "[chain][stereo]")
{
    const int n = 64 * block;
    auto steady = programme (n);
    runWith (steady, [] (int) { return driven(); });

    auto switching = programme (n);
    runWith (switching, [] (int index)
    {
        auto s = driven();
        s.stereoMode = static_cast<StereoMode> ((index / 5) % 3);   // every ~27 ms
        return s;
    });

    const auto reference = maxSecondDifference (steady, 4 * block);
    const auto result = maxSecondDifference (switching, 4 * block);
    INFO ("steady " << reference << ", switching " << result);
    CHECK (result < 1.5 * reference);
}

TEST_CASE ("Emphasis is transparent where the circuit is linear", "[chain][emphasis]")
{
    // At low level the circuit is (nearly) linear, so pre- and de-emphasis cancel.
    auto gainAt = [] (float emphasis, double freq)
    {
        const int n = (int) (0.6 * rate);
        juce::AudioBuffer<float> b (1, n);
        const auto s = test::sine (freq, 0.003, rate, n);
        b.copyFrom (0, 0, s.data(), n);

        ChainSettings cs;
        cs.driveKnob = 0.0f;
        cs.model = Model::classA;
        cs.emphasis = emphasis;
        b = render (b, cs);

        const int window = (int) (0.25 * rate);
        return 20.0 * std::log10 (test::toneAmplitude (b.getReadPointer (0, n - window), window, freq, rate) / 0.003);
    };

    for (double freq : { 60.0, 1000.0, 12000.0 })
        for (float e : { -1.0f, 1.0f })
        {
            INFO ("freq " << freq << ", emphasis " << e);
            CHECK (std::abs (gainAt (e, freq) - gainAt (0.0f, freq)) < 0.05);
        }
}

TEST_CASE ("Emphasis decides which frequencies saturate", "[chain][emphasis]")
{
    // A 4 kHz tone at moderate drive: emphasising the highs drives it harder (more of its
    // 3rd harmonic), emphasising the lows drives it less.
    auto harmonicRatio = [] (float emphasis)
    {
        const int n = (int) (0.5 * rate);
        juce::AudioBuffer<float> b (1, n);
        const auto s = test::sine (4000.0, 0.25, rate, n);
        b.copyFrom (0, 0, s.data(), n);

        auto cs = driven();
        cs.model = Model::fuzz;
        cs.driveKnob = 4.0f;
        cs.emphasis = emphasis;
        b = render (b, cs);

        const int window = (int) (0.25 * rate);
        const auto* x = b.getReadPointer (0, n - window);
        return test::toneAmplitude (x, window, 12000.0, rate) / test::toneAmplitude (x, window, 4000.0, rate);
    };

    const auto lows = harmonicRatio (-1.0f), flat = harmonicRatio (0.0f), highs = harmonicRatio (1.0f);
    INFO ("H3/H1 lows " << lows << ", flat " << flat << ", highs " << highs);
    CHECK (highs > flat * 1.3);
    CHECK (lows < flat / 1.3);
}

TEST_CASE ("Emphasis automation does not click", "[chain][emphasis]")
{
    const int n = 48 * block;
    auto steady = programme (n);
    runWith (steady, [] (int) { return driven(); });

    auto moving = programme (n);
    runWith (moving, [] (int index)
    {
        auto s = driven();
        s.emphasis = (index / 4) % 2 == 0 ? -1.0f : 1.0f;
        return s;
    });

    const auto reference = maxSecondDifference (steady, 4 * block);
    const auto result = maxSecondDifference (moving, 4 * block);
    INFO ("steady " << reference << ", automated " << result);
    CHECK (result < 1.5 * reference);
}

TEST_CASE ("Bias reaches every circuit through the chain, and automates cleanly", "[chain][bias]")
{
    const int n = 32 * block;

    for (int m = 0; m < numModels; ++m)
    {
        INFO (params::modelNames()[m]);
        auto s = driven();
        s.model = static_cast<Model> (m);
        s.driveKnob = 5.0f;

        const auto input = programme (n);
        const auto nominal = render (input, s);
        s.bias = 1.0f;
        const auto hot = render (input, s);
        CHECK (maxAbsDifference (nominal, hot, 4096) > 0.005);

        // automate the bias hard (full range every ~43 ms) against the worst steady state
        double reference = 0.0;
        for (float b : { -1.0f, 0.0f, 1.0f })
        {
            s.bias = b;
            reference = std::max (reference, maxSecondDifference (render (input, s), 4 * block));
        }

        auto moving = input;
        runWith (moving, [&] (int index)
        {
            auto ms = driven();
            ms.model = static_cast<Model> (m);
            ms.driveKnob = 5.0f;
            ms.bias = (index / 8) % 2 == 0 ? -1.0f : 1.0f;
            return ms;
        });

        INFO ("worst steady " << reference << ", automated " << maxSecondDifference (moving, 4 * block));
        CHECK (maxSecondDifference (moving, 4 * block) < 1.5 * reference);
    }
}
