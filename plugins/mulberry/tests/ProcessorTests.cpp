#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace mulberry;

namespace
{
    void setParam (MulberryProcessor& p, const juce::String& id, float plainValue)
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

TEST_CASE ("Processor: mono -> mono, mono -> stereo and stereo -> stereo", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    MulberryProcessor p;
    using Set = juce::AudioChannelSet;

    CHECK (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::mono())));
    CHECK (p.getLatencySamples() == 0);
}

TEST_CASE ("Processor: every layout echoes, with no NaN", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    using Set = juce::AudioChannelSet;

    for (const auto& [in, out] : { std::pair { Set::mono(), Set::mono() }, std::pair { Set::mono(), Set::stereo() },
                                   std::pair { Set::stereo(), Set::stereo() } })
    {
        MulberryProcessor p;
        p.setBusesLayout (layout (in, out));
        p.prepareToPlay (48000.0, 512);
        setParam (p, params::wetOnly, 1.0f);
        setParam (p, params::sync, 0.0f);
        setParam (p, params::time, 100.0f);

        juce::AudioBuffer<float> buffer (out.size(), 512);
        juce::MidiBuffer midi;
        double energy = 0.0;
        bool finite = true;

        for (int block = 0; block < 40; ++block)
        {
            buffer.clear();
            if (block == 0)
                buffer.setSample (0, 0, 1.0f);
            p.processBlock (buffer, midi);
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                for (int i = 0; i < 512; ++i)
                {
                    const auto v = buffer.getSample (c, i);
                    finite = finite && std::isfinite (v);
                    energy += (double) v * v;
                }
        }

        INFO (in.getDescription() << " -> " << out.getDescription());
        CHECK (finite);
        CHECK (energy > 1.0e-4);
    }
}

TEST_CASE ("Processor: state round-trips", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    MulberryProcessor a;
    setParam (a, params::machine, (float) Machine::loop);
    setParam (a, params::mode, (float) Mode::pattern);
    setParam (a, params::feedback, 110.0f);
    setParam (a, params::stepId (6), 25.0f);
    setParam (a, params::note, 3.0f);
    setParam (a, params::trails, 0.0f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    MulberryProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    for (auto* param : a.getParameters())
    {
        const auto id = dynamic_cast<juce::HostedAudioProcessorParameter*> (param)->getParameterID();
        INFO (id);
        CHECK (b.getState().getParameter (id)->getValue() == param->getValue());
    }
}

TEST_CASE ("Processor: tail follows Time and Feedback, infinite above 100 % and while held", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    MulberryProcessor p;
    setParam (p, params::sync, 0.0f);
    setParam (p, params::time, 500.0f);
    setParam (p, params::feedback, 50.0f);
    const auto finite = p.getTailLengthSeconds();
    CHECK (finite > 3.0);
    CHECK (finite < 60.0);

    setParam (p, params::feedback, 110.0f);
    CHECK (std::isinf (p.getTailLengthSeconds()));
    setParam (p, params::feedback, 50.0f);
    setParam (p, params::hold, 1.0f);
    CHECK (std::isinf (p.getTailLengthSeconds()));
}
