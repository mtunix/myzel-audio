#include "plugin/PluginProcessor.h"
#include "support/SignalAnalysis.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace umber;
using Catch::Matchers::WithinAbs;

namespace
{
    void setParam (UmberProcessor& p, const char* id, float plainValue)
    {
        auto* rp = p.getState().getParameter (id);
        REQUIRE (rp != nullptr);
        rp->setValueNotifyingHost (rp->convertTo0to1 (plainValue));
    }

    juce::AudioBuffer<float> makeSine (int channels, int samples, double sampleRate, float freq, float amp)
    {
        juce::AudioBuffer<float> buffer (channels, samples);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < samples; ++i)
                buffer.setSample (ch, i, amp * std::sin (juce::MathConstants<float>::twoPi * freq * (float) i / (float) sampleRate));

        return buffer;
    }
}

TEST_CASE ("Supports mono and stereo, matched in/out only", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    using Set = juce::AudioChannelSet;
    auto layout = [] (Set in, Set out)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (in);
        l.outputBuses.add (out);
        return l;
    };

    CHECK (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::create5point1(), Set::create5point1())));
}

TEST_CASE ("Output gain is applied and latency is reported", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    constexpr double sr = 48000.0;
    constexpr int block = 512;

    setParam (p, params::output, -6.0f);
    setParam (p, params::mix, 0.0f); // dry only: isolates the output stage
    p.prepareToPlay (sr, block);

    CHECK (p.getLatencySamples() >= 0);

    // One continuous sine, processed block by block (restarting the sine per block would
    // create discontinuities that the oversampling filters rightly ring on).
    constexpr int numBlocks = 40;
    const auto signal = makeSine (2, block * numBlocks, sr, 1000.0f, 0.5f);
    std::vector<float> out;

    juce::MidiBuffer midi;
    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, block);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, signal, ch, b * block, block);
        p.processBlock (buffer, midi);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    const int window = block * 16; // well after smoothing has settled
    const auto amplitude = umber::test::toneAmplitude (out.data() + out.size() - window, window, 1000.0, sr);
    CHECK_THAT (amplitude / 0.5, WithinAbs (juce::Decibels::decibelsToGain (-6.0), 1e-3));
}

TEST_CASE ("Arbitrary block sizes do not break processing", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;
    p.prepareToPlay (44100.0, 4096);

    juce::MidiBuffer midi;

    for (int size : { 1, 7, 64, 333, 4096 })
    {
        INFO (size);
        auto buffer = makeSine (2, size, 44100.0, 440.0f, 1.0f);
        p.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < size; ++i)
                REQUIRE (std::isfinite (buffer.getSample (ch, i)));
    }
}

TEST_CASE ("State round-trips through get/setStateInformation", "[processor][state]")
{
    juce::ScopedJuceInitialiser_GUI juce;

    juce::MemoryBlock saved;
    {
        UmberProcessor p;
        setParam (p, params::drive, 7.25f);
        setParam (p, params::model, (float) Model::valve);
        setParam (p, params::slam, 1.0f);
        setParam (p, params::lowCut, 150.0f);
        setParam (p, params::mix, 42.0f);
        p.getStateInformation (saved);
    }

    UmberProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    auto value = [&] (const char* id) { return restored.getState().getRawParameterValue (id)->load(); };

    CHECK_THAT (value (params::drive), WithinAbs (7.25, 1e-3));
    CHECK (juce::roundToInt (value (params::model)) == (int) Model::valve);
    CHECK (value (params::slam) > 0.5f);
    CHECK_THAT (value (params::lowCut), WithinAbs (150.0, 0.1));
    CHECK_THAT (value (params::mix), WithinAbs (42.0, 0.1));
}

TEST_CASE ("Garbage state is ignored", "[processor][state]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    const char junk[] = "definitely not a plugin state";
    p.setStateInformation (junk, (int) sizeof (junk));

    CHECK_THAT (p.getState().getRawParameterValue (params::drive)->load(), WithinAbs (3.0, 1e-4));
}
