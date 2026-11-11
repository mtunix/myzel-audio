#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace cerulean;

namespace
{
    void setParam (CeruleanProcessor& p, const char* id, float plainValue)
    {
        auto* param = p.getState().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    juce::AudioProcessor::BusesLayout layout (juce::AudioChannelSet in, juce::AudioChannelSet out)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (in);
        l.outputBuses.add (out);
        return l;
    }
}

TEST_CASE ("Processor: mono -> stereo and stereo -> stereo only", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    using Set = juce::AudioChannelSet;

    CHECK (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::mono(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::mono())));
}

TEST_CASE ("Processor: a mono input gives a wide stereo tail", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    p.setBusesLayout (layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo()));
    p.prepareToPlay (48000.0, 512);
    setParam (p, params::wetOnly, 1.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    double sumL = 0, sumR = 0, sumLR = 0;

    for (int block = 0; block < 200; ++block)
    {
        buffer.clear();
        if (block == 0)
            buffer.setSample (0, 0, 1.0f);   // impulse on the (only) input channel
        p.processBlock (buffer, midi);

        if (block > 10)
            for (int i = 0; i < 512; ++i)
            {
                const double l = buffer.getSample (0, i), r = buffer.getSample (1, i);
                sumL += l * l; sumR += r * r; sumLR += l * r;
            }
    }

    const auto correlation = sumLR / std::sqrt (sumL * sumR);
    INFO ("L energy " << sumL << ", R energy " << sumR << ", correlation " << correlation);
    CHECK (sumL > 1.0e-6);
    CHECK (sumR > 1.0e-6);
    CHECK (std::abs (correlation) < 0.6);
}

TEST_CASE ("Processor: state round-trips", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor a;
    setParam (a, params::decay, 7.5f);
    setParam (a, params::algorithm, (float) Algorithm::halo);
    setParam (a, params::paramA, 80.0f);
    setParam (a, params::trails, 0.0f);
    setParam (a, params::echoNote, (float) NoteValue::n1_4T);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    CeruleanProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    for (auto* param : a.getParameters())
    {
        const auto id = dynamic_cast<juce::HostedAudioProcessorParameter*> (param)->getParameterID();
        INFO (id);
        CHECK (std::abs (b.getState().getParameter (id)->getValue() - param->getValue()) < 1.0e-6f);
    }
}

TEST_CASE ("Processor: restoring a state undoes raw host values on switches", "[processor]")
{
    // Hosts (and pluginval) may set a bool to any value, e.g. 0.76. The session says 1, so
    // restoring it must give exactly 1, although 0.76 already "means" on.
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    juce::MemoryBlock state;
    p.getStateInformation (state);

    for (const auto* id : { params::trails, params::duck, params::freezeLayer })
    {
        auto* param = p.getState().getParameter (id);
        const auto saved = param->getValue();
        param->setValueNotifyingHost (saved > 0.5f ? 0.76f : 0.24f);

        p.setStateInformation (state.getData(), (int) state.getSize());
        INFO (id);
        CHECK (param->getValue() == saved);
    }
}

TEST_CASE ("Processor: tempo sync resolves note values", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;

    setParam (p, params::preDelaySync, 1.0f);
    setParam (p, params::preDelayNote, (float) NoteValue::n1_8D);
    setParam (p, params::echoSync, 1.0f);
    setParam (p, params::echoNote, (float) NoteValue::n1_4);

    const auto atHost = p.readSettings (120.0);
    CHECK (std::abs (atHost.preDelayMs - 375.0f) < 0.01f);       // dotted eighth at 120 BPM
    CHECK (std::abs (atHost.engine.echoTimeMs - 500.0f) < 0.01f);

    setParam (p, params::bpm, 60.0f);
    const auto noHost = p.readSettings (0.0);                     // Standalone: the BPM parameter
    CHECK (std::abs (noHost.preDelayMs - 750.0f) < 0.01f);
    CHECK (std::abs (noHost.engine.echoTimeMs - 1000.0f) < 0.01f);

    setParam (p, params::echoNote, (float) NoteValue::n1_1);     // 4 s at 60 BPM: clamped
    CHECK (p.readSettings (0.0).engine.echoTimeMs <= 2500.0f);
}

TEST_CASE ("Processor: tail length follows Decay and is infinite when frozen", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    setParam (p, params::decay, 10.0f);
    CHECK (p.getTailLengthSeconds() >= 15.0);
    setParam (p, params::freeze, 1.0f);
    CHECK (std::isinf (p.getTailLengthSeconds()));
}

TEST_CASE ("Processor: preset notifications reach the chain settings", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    const auto before = p.readSettings().presetGeneration;
    p.notifyPresetLoaded();
    CHECK (p.readSettings().presetGeneration == before + 1);
}

TEST_CASE ("Processor: survives every block size and stays finite", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    p.prepareToPlay (44100.0, 4096);
    juce::MidiBuffer midi;
    juce::Random random (3);

    for (const auto n : { 1, 2, 31, 32, 33, 480, 4096 })
    {
        juce::AudioBuffer<float> buffer (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
        p.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                REQUIRE (std::isfinite (buffer.getSample (ch, i)));
    }
}
