#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <random>

using namespace viridian;
using Catch::Matchers::WithinAbs;

namespace
{
    void setParam (ViridianProcessor& p, const char* id, float plainValue)
    {
        auto* rp = p.getState().getParameter (id);
        REQUIRE (rp != nullptr);
        rp->setValueNotifyingHost (rp->convertTo0to1 (plainValue));
    }

    juce::AudioProcessor::BusesLayout layout (juce::AudioChannelSet in, juce::AudioChannelSet out)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (in);
        l.outputBuses.add (out);
        return l;
    }

    juce::AudioBuffer<float> noise (int channels, int samples, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        juce::AudioBuffer<float> b (channels, samples);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < samples; ++i)
                b.setSample (ch, i, dist (rng));
        return b;
    }

    /** Runs the signal through in blocks of the given sizes (cycled) and returns the output. */
    juce::AudioBuffer<float> run (ViridianProcessor& p, const juce::AudioBuffer<float>& in, const std::vector<int>& sizes)
    {
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        int pos = 0;
        size_t k = 0;

        while (pos < in.getNumSamples())
        {
            const auto n = std::min (sizes[k++ % sizes.size()], in.getNumSamples() - pos);
            juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
            p.processBlock (block, midi);
            pos += n;
        }
        return out;
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
                return false;
        return true;
    }

    juce::MemoryBlock stateOf (ViridianProcessor& p)
    {
        juce::MemoryBlock m;
        p.getStateInformation (m);
        return m;
    }

    void restore (ViridianProcessor& p, const juce::MemoryBlock& m)
    {
        p.setStateInformation (m.getData(), (int) m.getSize());
    }

    juce::String idOf (juce::AudioProcessorParameter* param)
    {
        return dynamic_cast<juce::HostedAudioProcessorParameter*> (param)->getParameterID();
    }
}

TEST_CASE ("Processor: mono -> mono and stereo -> stereo only", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;

    using Set = juce::AudioChannelSet;
    CHECK (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::create5point1(), Set::create5point1())));
}

TEST_CASE ("Processor: the output does not depend on the host's block sizes", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto channels : { 1, 2 })
        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto render = [&] (const std::vector<int>& sizes)
            {
                ViridianProcessor p;
                const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
                REQUIRE (p.setBusesLayout (layout (set, set)));
                setParam (p, params::model, (float) Model::console);
                setParam (p, params::controlId (Control::highGain), 0.8f);
                setParam (p, params::controlId (Control::lmGain), 0.3f);
                setParam (p, params::controlId (Control::hpfFreq), 0.4f);
                setParam (p, params::drive, 12.0f);
                p.prepareToPlay (rate, 4096);
                CHECK (p.getLatencySamples() == 30);
                return run (p, noise (channels, 12000, 7u), sizes);
            };
            const auto reference = render ({ 4096 });
            for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 3 }, { 64 }, { 333 }, { 1, 4096, 17, 512, 2, 999 } })
            {
                INFO (channels << " ch, " << rate << " Hz, first block " << sizes.front());
                CHECK (identical (render (sizes), reference));
            }
        }
}

TEST_CASE ("Processor: Output gain is applied, Bypass returns the input delayed by the latency", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;
    constexpr double rate = 48000.0;
    // prepareToPlay() takes the settings as they are (PLAN.md Settled 19); later changes glide.
    setParam (p, params::output, -6.0f);
    p.prepareToPlay (rate, 512);
    const auto latency = p.getLatencySamples();
    juce::AudioBuffer<float> sine (2, 4800);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < sine.getNumSamples(); ++i)
            sine.setSample (ch, i, 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / (float) rate));
    const auto out = run (p, sine, { 512 });
    double in2 = 0.0, out2 = 0.0;
    for (int i = 2400; i < 4800; ++i)
    {
        in2 += sine.getSample (0, i - latency) * sine.getSample (0, i - latency);
        out2 += out.getSample (0, i) * out.getSample (0, i);
    }
    // The default model is a placeholder: flat but for the chain's three band-limits' ripple.
    const auto bandLimits = 3.0 * 20.0 * std::log10 (std::abs (viridian::dsp::ChainBandLimit::response (1000.0 / rate)));
    CHECK_THAT (10.0 * std::log10 (out2 / in2), WithinAbs (-6.0 + bandLimits, 1.0e-3));

    setParam (p, params::bypass, 1.0f);
    const auto settle = noise (2, (int) (rate * 0.05), 4u);
    run (p, settle, { 256 });
    const auto in = noise (2, 2048, 5u);
    const auto bypassed = run (p, in, { 256 });
    for (int ch = 0; ch < 2; ++ch)
        for (int i = latency; i < in.getNumSamples(); ++i)
            REQUIRE (juce::exactlyEqual (bypassed.getSample (ch, i), in.getSample (ch, i - latency)));
    CHECK (p.getBypassParameter() == p.getState().getParameter (params::bypass));
}

TEST_CASE ("Processor: Bypass and Output glide without clicks", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;
    constexpr double rate = 48000.0;
    setParam (p, params::model, (float) Model::console);
    setParam (p, params::controlId (Control::highGain), 1.0f);
    p.prepareToPlay (rate, 512);

    juce::AudioBuffer<float> sine (2, (int) (rate * 0.3));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < sine.getNumSamples(); ++i)
            sine.setSample (ch, i, 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 330.0f * (float) i / (float) rate));

    setParam (p, params::output, -24.0f);
    run (p, sine, { 512 });
    setParam (p, params::bypass, 1.0f);
    setParam (p, params::output, 0.0f);
    const auto out = run (p, sine, { 512 });

    // The 3rd difference of a 330 Hz sine is -79 dB re its RMS; a step would stand out above -60.
    double peak = 0.0, sum = 0.0;
    for (int i = 3; i < out.getNumSamples(); ++i)
    {
        const auto* y = out.getReadPointer (0);
        peak = std::max (peak, (double) std::abs (y[i] - 3.0f * y[i - 1] + 3.0f * y[i - 2] - y[i - 3]));
        sum += (double) y[i] * y[i];
    }
    CHECK (20.0 * std::log10 (peak / std::sqrt (sum / out.getNumSamples())) <= -60.0);
    CHECK (juce::exactlyEqual (out.getSample (0, out.getNumSamples() - 1), sine.getSample (0, out.getNumSamples() - 1 - p.getLatencySamples())));
}

TEST_CASE ("Processor: state round-trips, including the extremes", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    auto roundTrip = [] (auto&& setUp)
    {
        ViridianProcessor a;
        setUp (a);
        const auto saved = stateOf (a);

        ViridianProcessor b;
        restore (b, saved);

        // Compared as plain values: a switch a host left at a raw 0.37 is saved as what it means, off.
        for (auto* param : a.getParameters())
        {
            auto* source = dynamic_cast<juce::RangedAudioParameter*> (param);
            auto* restored = b.getState().getParameter (idOf (param));
            INFO (idOf (param));
            CHECK (juce::exactlyEqual (restored->convertFrom0to1 (restored->getValue()),
                                       source->convertFrom0to1 (source->getValue())));
        }
    };

    SECTION ("everything at its minimum")
    {
        roundTrip ([] (ViridianProcessor& p) { for (auto* param : p.getParameters()) param->setValueNotifyingHost (0.0f); });
    }

    SECTION ("everything at its maximum")
    {
        roundTrip ([] (ViridianProcessor& p) { for (auto* param : p.getParameters()) param->setValueNotifyingHost (1.0f); });
    }

    SECTION ("random positions")
    {
        roundTrip ([] (ViridianProcessor& p)
        {
            std::mt19937 rng (2026u);
            std::uniform_real_distribution<float> dist (0.0f, 1.0f);
            for (auto* param : p.getParameters())
                param->setValueNotifyingHost (dist (rng));
        });
    }

    SECTION ("a typical setting")
    {
        roundTrip ([] (ViridianProcessor& p)
        {
            setParam (p, params::model, (float) Model::console);
            setParam (p, params::alt, 1.0f);
            setParam (p, params::lmFreq, 0.37f);
            setParam (p, params::lmGain, 0.81f);
            setParam (p, params::drive, 13.5f);
            setParam (p, params::output, -7.25f);
            setParam (p, params::stereo, (float) StereoMode::side);
            setParam (p, params::tolerance, 30.0f);
        });
    }
}

TEST_CASE ("Processor: restoring a state undoes raw host values on switches", "[processor][state]")
{
    // Hosts (and pluginval) may set a bool to any value, e.g. 0.76: the session's 1 must give 1.
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;
    setParam (p, params::lowShape, 1.0f);
    const auto saved = stateOf (p);

    for (const auto* id : { params::lowShape, params::highShape, params::alt, params::eqIn })
    {
        auto* param = p.getState().getParameter (id);
        const auto value = param->getValue();
        param->setValueNotifyingHost (value > 0.5f ? 0.76f : 0.24f);
        restore (p, saved);
        INFO (id);
        CHECK (juce::exactlyEqual (param->getValue(), value));
    }
}

TEST_CASE ("Processor: sessions from other versions load", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    ViridianProcessor source;
    setParam (source, params::lowGain, 0.8f);
    const auto saved = stateOf (source);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    const auto tree = juce::ValueTree::fromXml (*xml);
    REQUIRE (tree.isValid());

    auto save = [] (const juce::ValueTree& t)
    {
        juce::MemoryBlock m;
        juce::AudioProcessor::copyXmlToBinary (*t.createXml(), m);
        return m;
    };

    SECTION ("a newer session's parameters are ignored")
    {
        auto newer = tree.createCopy();
        newer.appendChild (juce::ValueTree ("PARAM", { { "id", "dynThreshold" }, { "value", -20.0f } }), nullptr);

        ViridianProcessor p;
        restore (p, save (newer));
        CHECK (juce::exactlyEqual (p.getState().getParameter (params::lowGain)->getValue(), 0.8f));
    }

    SECTION ("a parameter missing from an older session takes its default")
    {
        auto older = tree.createCopy();
        older.removeChild (older.getChildWithProperty ("id", params::output), nullptr);

        ViridianProcessor p;
        setParam (p, params::output, 12.0f);
        restore (p, save (older));
        CHECK (juce::exactlyEqual (p.getState().getRawParameterValue (params::output)->load(), 0.0f));
        CHECK (juce::exactlyEqual (p.getState().getParameter (params::lowGain)->getValue(), 0.8f));
    }
}

TEST_CASE ("Processor: a value that is not finite in a session restores as the default", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto* bad : { "nan", "inf", "-inf" })
    {
        INFO (bad);
        ViridianProcessor source;
        auto tree = source.getState().copyState();
        for (auto child : tree)
            if (child.hasType ("PARAM"))
                child.setProperty ("value", bad, nullptr);

        juce::MemoryBlock m;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), m);

        ViridianProcessor p;
        for (auto* param : p.getParameters())
            param->setValueNotifyingHost (0.7f);
        restore (p, m);

        for (auto* param : p.getParameters())
        {
            INFO (idOf (param));
            CHECK (juce::exactlyEqual (param->getValue(), param->getDefaultValue()));
        }

        const auto s = p.readSettings();
        CHECK (juce::exactlyEqual (s.outputDb, 0.0f));
        CHECK (juce::exactlyEqual (s.driveDb, 0.0f));
        CHECK (s.positions == defaultPositions());
    }
}

TEST_CASE ("Processor: the UI state is saved with the session", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    // Frozen keys: sessions store them.
    CHECK (ui::uistate::analyserMode.toString() == "analyserMode");
    CHECK (ui::uistate::analyserSpeed.toString() == "analyserSpeed");

    ViridianProcessor a;
    a.getUiState().setProperty (ui::uistate::analyserMode, 2, nullptr);
    a.getUiState().setProperty (ui::uistate::scale, 1.25f, nullptr);

    ViridianProcessor b;
    restore (b, stateOf (a));
    CHECK ((int) b.getUiState().getProperty (ui::uistate::analyserMode) == 2);
    CHECK (juce::exactlyEqual ((float) b.getUiState().getProperty (ui::uistate::scale), 1.25f));
}

TEST_CASE ("Processor: foreign or broken state is ignored", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;
    setParam (p, params::drive, 6.0f);

    const char junk[] = "definitely not a plugin state";
    p.setStateInformation (junk, (int) sizeof (junk));
    CHECK_THAT (p.getState().getRawParameterValue (params::drive)->load(), WithinAbs (6.0, 1e-4));

    juce::ValueTree other ("UmberState");
    other.appendChild (juce::ValueTree ("PARAM", { { "id", params::drive }, { "value", -3.0f } }), nullptr);
    juce::MemoryBlock m;
    juce::AudioProcessor::copyXmlToBinary (*other.createXml(), m);
    restore (p, m);
    CHECK_THAT (p.getState().getRawParameterValue (params::drive)->load(), WithinAbs (6.0, 1e-4));
}
