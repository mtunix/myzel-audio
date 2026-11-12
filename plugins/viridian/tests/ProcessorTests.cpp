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

TEST_CASE ("Processor: the scaffold passes audio through bit for bit, any block size", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto channels : { 1, 2 })
        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 2 }, { 3 }, { 7 }, { 64 }, { 333 }, { 4096 },
                                                                     { 1, 4096, 17, 512, 2, 999 } })
            {
                INFO (channels << " ch, " << rate << " Hz, first block " << sizes.front());
                ViridianProcessor p;
                const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
                REQUIRE (p.setBusesLayout (layout (set, set)));
                p.prepareToPlay (rate, 4096);
                CHECK (p.getLatencySamples() == 0);

                const auto in = noise (channels, 12000, 7u);
                CHECK (identical (run (p, in, sizes), in));
            }
}

TEST_CASE ("Processor: the scaffold chain passes through on every model at its flat settings", "[processor][scaffold]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    for (int m = 0; m < numModels; ++m)
        for (const auto alt : { false, true })
        {
            // The scaffold chain ignores the slots: M1 replaces this with PLAN's flat-is-flat gate
            // (within 0.1 dB of the amps-only response).
            INFO (m << (alt ? " Alt" : ""));
            ViridianProcessor p;
            setParam (p, params::model, (float) m);
            setParam (p, params::alt, alt ? 1.0f : 0.0f);
            const auto flat = flatPositions (static_cast<Model> (m));
            for (int c = 0; c < numControls; ++c)
                setParam (p, params::controlId (static_cast<Control> (c)), flat[(size_t) c]);

            p.prepareToPlay (48000.0, 512);
            const auto in = noise (2, 4096, 11u);
            CHECK (identical (run (p, in, { 512 }), in));
        }
}

TEST_CASE ("Processor: Output gain is applied, Bypass returns the input", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;
    constexpr double rate = 48000.0;
    p.prepareToPlay (rate, 512);

    // The first block takes its settings at once; later changes glide.
    setParam (p, params::output, -6.0f);
    const auto in = noise (2, 512, 3u);
    const auto out = run (p, in, { 512 });
    const auto g = juce::Decibels::decibelsToGain (-6.0f);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            REQUIRE (juce::exactlyEqual (out.getSample (ch, i), in.getSample (ch, i) * g));

    setParam (p, params::bypass, 1.0f);
    const auto settle = noise (2, (int) (rate * 0.05), 4u);
    run (p, settle, { 256 });
    const auto in2 = noise (2, 2048, 5u);
    CHECK (identical (run (p, in2, { 256 }), in2));
    CHECK (p.getBypassParameter() == p.getState().getParameter (params::bypass));
}

TEST_CASE ("Processor: Bypass and Output glide without steps", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;
    constexpr double rate = 48000.0;
    p.prepareToPlay (rate, 512);

    juce::AudioBuffer<float> dc (2, (int) (rate * 0.2));
    dc.clear();
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, dc.getNumSamples());

    setParam (p, params::output, -24.0f);
    run (p, dc, { 512 });    // settled at -24 dB
    setParam (p, params::bypass, 1.0f);
    const auto out = run (p, dc, { 512 });

    // The 20 ms fade moves 0.5 * (1 - g) in equal steps: no step is larger than one share.
    const auto g = juce::Decibels::decibelsToGain (-24.0f);
    const auto share = 0.5f * (1.0f - g) / (float) (rate * dsp::EqChain::bypassSeconds);
    auto largest = 0.0f;
    for (int i = 1; i < out.getNumSamples(); ++i)
        largest = std::max (largest, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
    CHECK (largest <= share * 1.01f);
    CHECK (juce::exactlyEqual (out.getSample (0, out.getNumSamples() - 1), 0.5f));
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
