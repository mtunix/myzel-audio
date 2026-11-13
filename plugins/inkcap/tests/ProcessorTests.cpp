/*
    The processor: channel layouts, the reported latency, an exact pass-through at Damage 0, the
    host's song position reaching the chain, the session state (seed included), and an undo
    history of user edits only.
*/

#include "support/ProcessorHelpers.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <thread>

using namespace inkcap;
using namespace inkcap::testing;

namespace
{
    juce::AudioProcessor::BusesLayout layout (juce::AudioChannelSet in, juce::AudioChannelSet out)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (in);
        l.outputBuses.add (out);
        return l;
    }

    float source (int channel, int i)
    {
        return 0.4f * std::sin (0.003f * (float) i * (float) (channel + 1)) + 0.1f * std::sin (0.21f * (float) i);
    }
}

TEST_CASE ("Processor: mono -> mono, mono -> stereo and stereo -> stereo", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    using Set = juce::AudioChannelSet;

    CHECK (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::create5point1(), Set::create5point1())));
}

TEST_CASE ("Processor: reports its constant latency (the 2x pair and Warble's rest), whatever the modules do", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;

    for (const auto rate : { 44100.0, 96000.0 })
    {
        p.prepareToPlay (rate, 512);
        CHECK (p.getLatencySamples() == dsp::InkcapChain::fixedLatencySamples());
    }

    for (const auto* id : params::onIds)
        automate (p, id, 0.0f);
    p.prepareToPlay (48000.0, 512);
    CHECK (p.getLatencySamples() == 9);
}

TEST_CASE ("Processor: at Damage 0 every layout passes the input through, latency-aligned, to the bit", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    using Set = juce::AudioChannelSet;

    for (const auto& [in, out] : { std::pair { Set::mono(), Set::mono() }, std::pair { Set::mono(), Set::stereo() },
                                   std::pair { Set::stereo(), Set::stereo() } })
    {
        for (const auto modulesOn : { false, true })
        {
            InkcapProcessor p;
            REQUIRE (p.setBusesLayout (layout (in, out)));
            p.prepareToPlay (48000.0, 333);
            const auto latency = p.getLatencySamples();

            for (const auto* id : params::onIds)
                automate (p, id, modulesOn ? 1.0f : 0.0f);
            for (const auto* id : params::amountIds)
                automate (p, id, 100.0f);
            automate (p, params::damage, 0.0f);
            automate (p, params::chaos, 100.0f);
            automate (p, params::mix, 40.0f);

            juce::MidiBuffer midi;
            bool exact = true;
            int sample = 0;

            for (int block = 0; block < 30; ++block)
            {
                const auto n = 1 + (block * 97) % 333;     // every block a different length
                juce::AudioBuffer<float> buffer (out.size(), n);
                for (int c = 0; c < in.size(); ++c)
                    for (int i = 0; i < n; ++i)
                        buffer.setSample (c, i, source (c, sample + i));

                p.processBlock (buffer, midi);

                for (int c = 0; c < out.size(); ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        const auto t = sample + i - latency;
                        const auto expected = t < 0 ? 0.0f : source (in.size() == 1 ? 0 : c, t);
                        exact = exact && juce::exactlyEqual (buffer.getSample (c, i), expected);
                    }
                sample += n;
            }

            INFO (in.getDescription() << " -> " << out.getDescription() << ", modules " << (modulesOn ? "on" : "off"));
            CHECK (exact);
        }
    }
}

namespace
{
    /** A host transport for the processor: playing from a position at a tempo, advanced by the test. */
    struct TestPlayHead final : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 120.0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setPpqPosition (ppq);
            info.setBpm (bpm);
            info.setTimeInSeconds (ppq * 60.0 / bpm);
            info.setTimeSignature (TimeSignature { 3, 4 });
            return info;
        }
    };
}

TEST_CASE ("Processor: full Damage changes the sound, keeps the latency and stays finite", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    p.prepareToPlay (48000.0, 256);
    for (const auto* id : params::onIds)
        automate (p, id, 1.0f);
    automate (p, params::damage, 100.0f);
    automate (p, params::chaos, 100.0f);
    automate (p, params::glitchType, (float) GlitchType::any);

    juce::MidiBuffer midi;
    double difference = 0.0;
    bool finite = true;
    for (int block = 0; block < 400; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
                buffer.setSample (c, i, source (c, block * 256 + i));
        p.processBlock (buffer, midi);
        for (int i = 0; i < 256; ++i)
        {
            const auto t = block * 256 + i - p.getLatencySamples();
            const auto y = buffer.getSample (0, i);
            finite = finite && std::isfinite (y);
            difference = std::max (difference, (double) std::abs (y - (t < 0 ? 0.0f : source (0, t))));
        }
    }
    CHECK (finite);
    CHECK (difference > 0.01);
    CHECK (p.getLatencySamples() == 9);
    CHECK (p.getTailLengthSeconds() >= 2.0);
}

TEST_CASE ("Processor: the host's song position reaches the chain, and the display sees it", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    TestPlayHead head;
    head.ppq = 32.0;
    head.bpm = 96.0;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 512);
    automate (p, params::glitchOn, 1.0f);
    automate (p, params::glitchAmount, 100.0f);
    automate (p, params::damage, 100.0f);

    juce::MidiBuffer midi;
    bool sawEvent = false;
    for (int block = 0; block < 200; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (c, i, source (c, block * 512 + i));
        p.processBlock (buffer, midi);
        head.ppq += 512.0 * head.bpm / 60.0 / 48000.0;
        sawEvent = sawEvent || p.getTelemetry().glitchEventType.load() >= 0;
    }

    const auto& t = p.getTelemetry();
    CHECK (t.followingHost.load());
    CHECK (juce::exactlyEqual (t.bpm.load(), 96.0));
    CHECK (std::abs (t.ppq.load() - head.ppq) < 1.0e-6);
    CHECK (t.timeSigNumerator.load() == 3);
    CHECK (sawEvent);
    CHECK (t.glitchSeed.load() == p.getSeeds().current());
    CHECK (t.modules[(size_t) Module::glitch].intensity.load() > 0.99f);

    // Stopped: the grid runs free at the host's tempo.
    head.playing = false;
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    p.processBlock (buffer, midi);
    CHECK_FALSE (t.followingHost.load());
    p.setPlayHead (nullptr);
}

TEST_CASE ("Processor: the settings carry every parameter and the seed", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;

    automate (p, params::damage, 80.0f);
    automate (p, params::glitchType, (float) GlitchType::any);
    automate (p, params::glitchRate, 10.0f);
    automate (p, params::glitchGroove, 62.5f);
    automate (p, params::crushTone, -50.0f);
    automate (p, params::hissFollow, 100.0f);
    automate (p, params::bitsType, (float) BitsType::phone);
    automate (p, params::roomOn, 1.0f);
    automate (p, params::bpm, 90.0f);
    automate (p, params::bypass, 1.0f);
    REQUIRE (p.getSeeds().paste ("0x00C0FFEE") == SeedState::Paste::code);

    const auto s = p.readSettings();
    CHECK (std::abs (s.damage - 0.8f) < 1.0e-6f);
    CHECK (s.glitch.type == GlitchType::any);
    CHECK (std::string (glitchRates[(size_t) s.glitch.rate].name) == "1/32");
    CHECK (std::abs (s.glitch.groove - 0.625f) < 1.0e-6f);
    CHECK (std::abs (s.crush.tone + 0.5f) < 1.0e-6f);
    CHECK (std::abs (s.hiss.follow - 1.0f) < 1.0e-6f);
    CHECK (s.bits.type == BitsType::phone);
    CHECK (s.room.on);
    CHECK (std::abs (s.fallbackBpm - 90.0) < 1.0e-4);
    CHECK (s.bypass);
    CHECK (s.seed == 0x00C0FFEEu);
}

TEST_CASE ("Processor: state round-trips, seed, lock and history included", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    InkcapProcessor a;

    automate (a, params::damage, 77.0f);
    automate (a, params::glitchOn, 1.0f);
    automate (a, params::roomType, (float) RoomType::spring);
    automate (a, params::warbleSync, 1.0f);
    automate (a, params::match, 0.0f);
    a.getSeeds().paste ("Thrift Shop");
    a.getSeeds().reroll();
    a.getSeeds().paste ("3F2A-91C7");
    a.setSeedLocked (true);

    const auto state = saveState (a);
    InkcapProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    for (auto* parameter : a.getParameters())
    {
        const auto id = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter)->getParameterID();
        INFO (id);
        CHECK (juce::exactlyEqual (b.getState().getParameter (id)->getValue(), parameter->getValue()));
    }

    CHECK (b.getSeeds().current() == 0x3F2A91C7u);
    CHECK (b.getSeeds().code() == "3F2A-91C7");
    CHECK (b.getSeeds().label().isEmpty());
    CHECK (b.readSettings().seed == 0x3F2A91C7u);
    CHECK (b.isSeedLocked());

    const auto history = b.getSeeds().history();
    REQUIRE (history.size() == a.getSeeds().history().size());
    for (size_t i = 0; i < history.size(); ++i)
    {
        CHECK (history[i].seed == a.getSeeds().history()[i].seed);
        CHECK (history[i].label == a.getSeeds().history()[i].label);
    }
}

TEST_CASE ("Processor: a session without a valid seed restores the fresh instance's", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor a;
    a.getSeeds().paste ("12345678");

    for (const auto& damaged : { juce::var(), juce::var ("not a code"), juce::var (42) })
    {
        auto tree = a.getState().copyState();
        if (damaged.isVoid())
            tree.removeProperty (seedstate::seed, nullptr);
        else
            tree.setProperty (seedstate::seed, damaged, nullptr);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), block);

        InkcapProcessor b;
        b.getSeeds().paste ("ABCDEF01");
        b.setStateInformation (block.getData(), (int) block.getSize());

        INFO (damaged.toString());
        CHECK (b.getSeeds().current() == SeedState::initial().seed);
        CHECK (b.getSeeds().label() == "inkcap");
        CHECK (b.getState().state.getProperty (seedstate::seed).toString() == "4393-0A3D");
    }
}

TEST_CASE ("Processor: garbage state is ignored", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    p.getSeeds().paste ("CAFE-F00D");
    automate (p, params::damage, 12.0f);

    const char garbage[] = "definitely not a session";
    p.setStateInformation (garbage, (int) sizeof (garbage));
    p.setStateInformation (nullptr, 0);

    CHECK (p.getSeeds().current() == 0xCAFEF00Du);
    CHECK (std::abs (plain (p, params::damage) - 12.0f) < 1.0e-3f);
}

TEST_CASE ("Undo: host automation and state saves never enter the history", "[undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;

    for (int i = 0; i < 200; ++i)
    {
        automate (p, params::damage, (float) (i % 10) * 10.0f);
        saveState (p);
    }

    CHECK_FALSE (p.getUndoManager().canUndo());
}

TEST_CASE ("Undo: user edits are undoable and redoable", "[undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    auto& um = p.getUndoManager();

    userEdit (p, params::damage, 90.0f);
    userEdit (p, params::crushType, (float) CrushType::fuzz);

    um.undo();
    CHECK (juce::roundToInt (plain (p, params::crushType)) == (int) CrushType::tape);
    CHECK (std::abs (plain (p, params::damage) - 90.0f) < 0.01f);
    um.undo();
    CHECK (std::abs (plain (p, params::damage) - 50.0f) < 0.01f);
    CHECK_FALSE (um.canUndo());
    um.redo();
    CHECK (std::abs (plain (p, params::damage) - 90.0f) < 0.01f);
}

TEST_CASE ("Undo: Bypass, Match and BPM are session settings, not undoable edits", "[undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;

    userEdit (p, params::bypass, 1.0f);
    userEdit (p, params::match, 0.0f);
    userEdit (p, params::bpm, 95.0f);
    CHECK_FALSE (p.getUndoManager().canUndo());

    userEdit (p, params::mix, 40.0f);
    CHECK (p.getUndoManager().canUndo());
}

TEST_CASE ("Undo: restoring a session clears the history, also off the message thread", "[undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    const auto state = saveState (p);

    userEdit (p, params::damage, 10.0f);
    p.getSeeds().reroll();
    REQUIRE (p.getUndoManager().canUndo());
    p.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_FALSE (p.getUndoManager().canUndo());

    // A host's loader thread: the history clears on the message thread.
    userEdit (p, params::damage, 10.0f);
    std::thread loader ([&] { p.setStateInformation (state.getData(), (int) state.getSize()); });
    loader.join();
    p.processPendingRequests();
    CHECK_FALSE (p.getUndoManager().canUndo());
    CHECK (p.getSeeds().current() == SeedState::initial().seed);
}
