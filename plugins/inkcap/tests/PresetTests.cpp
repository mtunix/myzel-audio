/*
    The factory presets and host programs (PLAN.md, Presets): the set itself, Init as the
    defaults, every preset playing, and programs that carry the seed from any thread.
*/

#include "support/ProcessorHelpers.h"

#include "dsp/common/SeedCode.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <thread>

using namespace inkcap;
using namespace inkcap::testing;

namespace
{
    uint32_t seedOf (const FactoryPreset& p)
    {
        const auto parsed = dsp::seedcode::parse (p.seed);
        REQUIRE (parsed.has_value());
        return parsed->seed;
    }

    /** A programme in both channels: noise, two tones and a swell, about -14 dBFS RMS. */
    juce::AudioBuffer<float> programme (int numSamples)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        juce::Random r (17);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = (double) i / 48000.0;
            const auto swell = 0.6 + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.7 * t);
            const auto v = (float) (swell * (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                                             + 0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 1375.0 * t)))
                           + 0.05f * (r.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return b;
    }

    double rmsDb (const juce::AudioBuffer<float>& b, int from)
    {
        double sum = 0.0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = from; i < b.getNumSamples(); ++i)
                sum += (double) b.getSample (c, i) * b.getSample (c, i);
        return 10.0 * std::log10 (sum / (double) (b.getNumChannels() * (b.getNumSamples() - from)) + 1.0e-30);
    }
}

TEST_CASE ("Presets: about fifty, in the plan's categories, each with its own seed", "[presets]")
{
    const auto presets = factoryPresets();
    CHECK (presets.size() >= 50);

    // The categories in the plan's order, each one used.
    const std::vector<juce::String> categories { "Utility", "Tape & Cassette", "Vinyl", "Lo-fi beats", "Broken",
                                                 "Spaces", "Drums", "Keys", "Vocals", "Guitar" };
    std::vector<juce::String> seen;
    std::set<juce::String> names;
    std::set<uint32_t> seeds;
    for (const auto& p : presets)
    {
        INFO (p.name);
        CHECK (names.insert (p.name).second);
        CHECK (seeds.insert (seedOf (p)).second);
        CHECK (std::find (categories.begin(), categories.end(), juce::String (p.category)) != categories.end());
        if (seen.empty() || seen.back() != p.category)
        {
            CHECK (std::find (seen.begin(), seen.end(), juce::String (p.category)) == seen.end());     // grouped
            seen.push_back (p.category);
        }

        // Our own names: no product, maker or hardware names.
        for (const auto* banned : { "RC-20", "RC20", "XLN", "Freakshow", "SP-1200", "SP1200", "MPC", "Akai", "Sony", "TASCAM", "Studer",
                                    "Ampex", "Fender", "Marshall", "Walkman", "Discman", "Microcassette", "Dictaphone",
                                    "Technics", "Roland", "Boss" })
        {
            CHECK_FALSE (juce::String (p.name).containsIgnoreCase (banned));
            CHECK_FALSE (juce::String (p.seed).containsIgnoreCase (banned));
        }
    }
    CHECK (seen == categories);
    CHECK (juce::String (presets.front().name) == "Init");
    CHECK (seedOf (presets.front()) == SeedState::initial().seed);
}

TEST_CASE ("Presets: every typed control written as text is understood", "[presets]")
{
    for (const auto& p : factoryPresets())
    {
        INFO (p.name);
        auto check = [] (TypedControl c, auto type, bool synced, const char* text)
        {
            if (text != nullptr)
            {
                INFO (text);
                CHECK (typedControlFromText (c, (int) type, synced, text) >= 0.0f);
            }
        };
        check (TypedControl::glitchLength, p.glitch.type, false, p.glitch.length);
        check (TypedControl::warbleRate, p.warble.type, p.warble.sync, p.warble.rate);
        check (TypedControl::crushDrive, p.crush.type, false, p.crush.drive);
        check (TypedControl::bitsRate, p.bits.type, false, p.bits.rate);
        check (TypedControl::bitsDepth, p.bits.type, false, p.bits.depth);
        check (TypedControl::roomSize, p.room.type, false, p.room.size);
        CHECK (p.glitch.rate >= 0);
        CHECK (p.glitch.rate < numGlitchRates);
    }
}

TEST_CASE ("Presets: Init is every parameter's default and the fresh instance's seed", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    auto p = std::make_unique<InkcapProcessor>();
    auto& pm = p->getPresets();

    for (const auto& id : params::soundParameterIds())
        automate (*p, id, param (*p, id).convertFrom0to1 (0.83f));
    p->getSeeds().paste ("not the default");

    pm.load (indexOf (pm, "Init"));
    for (const auto& id : params::soundParameterIds())
    {
        INFO (id);
        CHECK (std::abs (param (*p, id).getValue() - param (*p, id).getDefaultValue()) < 1.0e-6f);
    }
    CHECK (p->getSeeds().current() == SeedState::initial().seed);
    CHECK (p->getSeeds().label() == SeedState::initial().label);
    CHECK_FALSE (pm.isModified());

    // A fresh instance is Init, unmodified.
    InkcapProcessor fresh;
    CHECK (fresh.getPresets().getCurrentName() == "Init");
    CHECK_FALSE (fresh.getPresets().isModified());
}

TEST_CASE ("Presets: every factory preset plays, finite and near the input's level", "[presets]")
{
    // Match keeps each module's loudness; the presets stay within a few dB of the input on a
    // programme at -14 dBFS (Match is calibrated at -18: a hotter mix saturates more).
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    auto p = std::make_unique<InkcapProcessor>();
    auto& pm = p->getPresets();
    constexpr int block = 512, blocks = 280;
    const auto source = programme (block * blocks);
    const auto inDb = rmsDb (source, block * 90);

    for (const auto& preset : factoryPresets())
    {
        INFO (preset.name);
        pm.load (indexOf (pm, preset.name));
        CHECK_FALSE (pm.isModified());
        p->prepareToPlay (48000.0, block);

        auto out = source;
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, b * block, block);
            p->processBlock (view, midi);
        }

        auto finite = true;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < out.getNumSamples(); ++i)
                finite = finite && std::isfinite (out.getSample (c, i));
        CHECK (finite);

        const auto change = rmsDb (out, block * 90) - inDb;
        INFO ("level " << change << " dB against the input");
        CHECK (change > -6.0);
        CHECK (change < 4.0);
    }
}

TEST_CASE ("Programs: the host lists the factory set, and a program loads like the browser", "[presets][programs]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    auto p = std::make_unique<InkcapProcessor>();
    const auto presets = factoryPresets();

    REQUIRE (p->getNumPrograms() == (int) presets.size());
    for (int i = 0; i < p->getNumPrograms(); ++i)
        CHECK (p->getProgramName (i) == presets[(size_t) i].name);
    CHECK (p->getCurrentProgram() == 0);

    // On the message thread: the browser's load (seed and label, one undo step).
    const auto pocket = (int) (std::find_if (presets.begin(), presets.end(), [] (const auto& e) { return juce::String (e.name) == "Pocket Cassette"; })
                                - presets.begin());
    p->getUndoManager().clearUndoHistory();
    p->setCurrentProgram (pocket);
    CHECK (p->getCurrentProgram() == pocket);
    CHECK (p->getPresets().getCurrentName() == "Pocket Cassette");
    CHECK (p->getSeeds().current() == seedOf (presets[(size_t) pocket]));
    CHECK (p->getSeeds().label() == "pocket");
    CHECK (std::abs (plain (*p, params::damage) - 65.0f) < 0.01f);
    CHECK (p->getUndoManager().canUndo());

    // The browser moves the host program along.
    p->getPresets().load (indexOf (p->getPresets(), "Shellac"));
    CHECK (p->getProgramName (p->getCurrentProgram()) == "Shellac");
}

TEST_CASE ("Programs: off the message thread a program sets parameters and seed at once and keeps both locks", "[presets][programs]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    auto p = std::make_unique<InkcapProcessor>();
    const auto presets = factoryPresets();
    auto indexOfPreset = [&] (const char* name)
    {
        return (int) (std::find_if (presets.begin(), presets.end(), [name] (const auto& e) { return juce::String (e.name) == name; })
                      - presets.begin());
    };

    auto fromLoader = [&p] (int index) { std::thread ([&p, index] { p->setCurrentProgram (index); }).join(); };

    const auto phone = indexOfPreset ("Phone Call");
    p->getUndoManager().clearUndoHistory();
    fromLoader (phone);

    // In place when the call returns: the audio thread already plays the program and its seed.
    CHECK (std::abs (plain (*p, params::damage) - 70.0f) < 0.01f);
    CHECK (p->readSettings().bits.on);
    CHECK (p->readSettings().seed == seedOf (presets[(size_t) phone]));
    CHECK (p->getCurrentProgram() == phone);

    // The message thread catches up: the seed in the state tree (saved with the session), the browser, no undo step.
    p->processPendingRequests();
    CHECK (p->getSeeds().code() == juce::String (dsp::seedcode::format (seedOf (presets[(size_t) phone]))));
    CHECK (p->getSeeds().label() == "hello");
    CHECK (p->getPresets().getCurrentName() == "Phone Call");
    CHECK_FALSE (p->getPresets().isModified());
    CHECK_FALSE (p->getUndoManager().canUndo());

    // The seed Lock and the mix lock hold, off the message thread too.
    p->setSeedLocked (true);
    p->getPresets().setMixLocked (true);
    automate (*p, params::mix, 40.0f);
    const auto locked = p->getSeeds().current();
    const auto shellac = indexOfPreset ("Shellac");
    fromLoader (shellac);
    CHECK (p->readSettings().seed == locked);
    CHECK (std::abs (plain (*p, params::mix) - 40.0f) < 0.01f);
    CHECK (std::abs (plain (*p, params::damage) - 75.0f) < 0.01f);
    p->processPendingRequests();
    CHECK (p->getSeeds().current() == locked);
    CHECK (p->getSeeds().label() == "hello");
    CHECK (p->getPresets().getCurrentName() == "Shellac");
    CHECK_FALSE (p->getPresets().isModified());

    // A session restore wins over a program the browser has not shown yet, and brings its own Lock.
    p->setSeedLocked (false);
    const auto saved = saveState (*p);
    fromLoader (indexOfPreset ("Fuzz Box"));
    p->setStateInformation (saved.getData(), (int) saved.getSize());
    p->processPendingRequests();
    CHECK (p->getPresets().getCurrentName() == "Shellac");
    CHECK (p->getCurrentProgram() == shellac);
    CHECK (p->getSeeds().current() == locked);
}
