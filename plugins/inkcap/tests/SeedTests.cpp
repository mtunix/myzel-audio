/*
    The seed in the processor (PLAN.md, Seeds): paste, words, reroll and history as undo steps,
    presets and A/B that carry the seed, and the Lock that keeps it.
*/

#include "support/ProcessorHelpers.h"

#include "dsp/common/SeedCode.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace inkcap;
using namespace inkcap::testing;

namespace
{
    std::vector<uint32_t> historySeeds (InkcapProcessor& p)
    {
        std::vector<uint32_t> seeds;
        for (const auto& e : p.getSeeds().history())
            seeds.push_back (e.seed);
        return seeds;
    }

    /** A user preset with its own seed, saved from p's current sound. */
    int saveWithSeed (InkcapProcessor& p, const juce::String& name, const juce::String& seedText)
    {
        p.getSeeds().paste (seedText);
        REQUIRE (p.getPresets().saveUserPreset (name).wasOk());
        return indexOf (p.getPresets(), name);
    }
}

TEST_CASE ("Seeds: a fresh instance has the word inkcap as its seed", "[seeds]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    InkcapProcessor p;
    auto& seeds = p.getSeeds();

    CHECK (seeds.current() == 0x43930A3Du);
    CHECK (seeds.code() == "4393-0A3D");
    CHECK (seeds.label() == "inkcap");
    CHECK (seeds.history().empty());
    CHECK_FALSE (p.isSeedLocked());
    CHECK_FALSE (p.getPresets().isModified());     // the fresh instance is Init, seed included
}

TEST_CASE ("Seeds: paste takes codes as codes, any other text as a word, and empty text not at all", "[seeds]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    auto& seeds = p.getSeeds();
    auto& um = p.getUndoManager();

    for (const auto* text : { "3F2A-91C7", "3f2a91c7", "0x3F2A91C7" })
    {
        seeds.paste ("something else");
        INFO (text);
        CHECK (seeds.paste (text) == SeedState::Paste::code);
        CHECK (seeds.current() == 0x3F2A91C7u);
        CHECK (seeds.label().isEmpty());
    }

    CHECK (seeds.paste ("  Thrift Shop ") == SeedState::Paste::word);
    CHECK (seeds.current() == dsp::seedcode::wordHash ("thrift shop"));
    CHECK (seeds.label() == "Thrift Shop");
    CHECK (seeds.code() == "F99E-6E2A");

    // Unicode survives the trip through juce::String.
    CHECK (seeds.paste (juce::String::fromUTF8 ("\xC3\x84pfel")) == SeedState::Paste::word);
    CHECK (seeds.current() == 0x8393A579u);
    CHECK (seeds.label() == juce::String::fromUTF8 ("\xC3\x84pfel"));

    um.clearUndoHistory();
    for (const auto* nothing : { "", "   ", "\t\n" })
    {
        CHECK (seeds.paste (nothing) == SeedState::Paste::nothing);
        CHECK (seeds.current() == 0x8393A579u);
    }
    CHECK_FALSE (um.canUndo());

    // The same seed again is not a change, and not an undo step.
    CHECK (seeds.paste (juce::String::fromUTF8 ("\xC3\x84pfel")) == SeedState::Paste::word);
    CHECK_FALSE (um.canUndo());
}

TEST_CASE ("Seeds: every seed action is one undo step, and the audio thread follows", "[seeds][undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    auto& seeds = p.getSeeds();
    auto& um = p.getUndoManager();

    seeds.paste ("thrift");
    seeds.paste ("0x00000001");
    seeds.reroll();
    const auto rerolled = seeds.current();
    CHECK (rerolled != 1u);
    CHECK (seeds.label().isEmpty());
    CHECK (p.readSettings().seed == rerolled);

    um.undo();
    CHECK (seeds.current() == 1u);
    CHECK (p.readSettings().seed == 1u);
    um.undo();
    CHECK (seeds.current() == 0x87198E02u);
    CHECK (seeds.label() == "thrift");
    um.undo();
    CHECK (seeds.current() == SeedState::initial().seed);
    CHECK (seeds.label() == "inkcap");
    CHECK_FALSE (um.canUndo());

    um.redo();
    um.redo();
    um.redo();
    CHECK (seeds.current() == rerolled);
    CHECK (p.readSettings().seed == rerolled);

    // A seed action between parameter edits stays its own step.
    userEdit (p, params::damage, 70.0f);
    seeds.paste ("ABCD-1234");
    userEdit (p, params::chaos, 70.0f);
    um.undo();
    CHECK (seeds.current() == 0xABCD1234u);
    um.undo();
    CHECK (seeds.current() == rerolled);
    CHECK (std::abs (plain (p, params::damage) - 70.0f) < 0.01f);
}

TEST_CASE ("Seeds: reroll draws fresh seeds from system entropy", "[seeds]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    std::set<uint32_t> seen { p.getSeeds().current() };

    for (int i = 0; i < 200; ++i)
    {
        const auto before = p.getSeeds().current();
        p.getSeeds().reroll();
        CHECK (p.getSeeds().current() != before);
        seen.insert (p.getSeeds().current());
    }

    CHECK (seen.size() > 195u);    // 32 bits: a repeat among 200 is a one-in-200,000 event
}

TEST_CASE ("Seeds: the history keeps the last 8 seeds the seed actions touched, newest first", "[seeds]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    InkcapProcessor p;
    auto& seeds = p.getSeeds();

    seeds.paste ("00000001");
    CHECK (historySeeds (p) == std::vector<uint32_t> { 1u, SeedState::initial().seed });
    CHECK (seeds.history()[1].label == "inkcap");

    for (uint32_t s = 2; s <= 12; ++s)
        seeds.paste (dsp::seedcode::format (s));
    CHECK (historySeeds (p) == std::vector<uint32_t> { 12u, 11u, 10u, 9u, 8u, 7u, 6u, 5u });

    // A pick moves the seed to the front, the one it replaced right behind it; no duplicates.
    CHECK (seeds.pickFromHistory (5));
    CHECK (seeds.current() == 7u);
    CHECK (historySeeds (p) == std::vector<uint32_t> { 7u, 12u, 11u, 10u, 9u, 8u, 6u, 5u });
    CHECK_FALSE (seeds.pickFromHistory (8));
    CHECK_FALSE (seeds.pickFromHistory (-1));

    // A word keeps its label in the history.
    seeds.paste ("thrift");
    CHECK (seeds.history().front().label == "thrift");

    // Undo, presets and A/B change the seed, never the history: a reroll is never lost.
    seeds.reroll();
    const auto rerolled = seeds.current();
    const auto before = historySeeds (p);
    p.getUndoManager().undo();
    p.getPresets().load (indexOf (p.getPresets(), "Init"));
    AbCompare ab (p, p.getPresets());
    ab.selectSlot (1);
    ab.selectSlot (0);
    CHECK (historySeeds (p) == before);
    CHECK (historySeeds (p).front() == rerolled);
}

TEST_CASE ("Seeds: presets carry their seed, and a load is one undo step with it", "[seeds][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    InkcapProcessor p;
    auto& pm = p.getPresets();
    auto& seeds = p.getSeeds();

    automate (p, params::damage, 88.0f);
    const auto mine = saveWithSeed (p, "Thrifted", "thrift");
    REQUIRE (mine >= 0);

    // The file stores the seed and its word, not the session's Lock.
    const auto xml = scratch.dir.getChildFile ("Thrifted.xml").loadFileAsString();
    CHECK (xml.contains ("InkcapPreset"));
    CHECK (xml.contains ("seed=\"8719-8E02\""));
    CHECK (xml.contains ("seedLabel=\"thrift\""));
    CHECK_FALSE (xml.contains ("seedLock"));

    pm.load (indexOf (pm, "Init"));
    CHECK (seeds.current() == SeedState::initial().seed);
    CHECK (seeds.label() == "inkcap");
    CHECK (std::abs (plain (p, params::damage) - 50.0f) < 0.01f);
    CHECK_FALSE (pm.isModified());

    p.getUndoManager().clearUndoHistory();
    pm.load (indexOf (pm, "Thrifted"));
    CHECK (seeds.current() == 0x87198E02u);
    CHECK (seeds.label() == "thrift");
    CHECK (p.readSettings().seed == 0x87198E02u);
    CHECK_FALSE (pm.isModified());

    // A different seed is a change to the preset.
    seeds.reroll();
    CHECK (pm.isModified());

    // One undo step takes back the whole load, seed and parameters together.
    p.getUndoManager().undo();
    CHECK_FALSE (pm.isModified());
    p.getUndoManager().undo();
    CHECK (seeds.current() == SeedState::initial().seed);
    CHECK (std::abs (plain (p, params::damage) - 50.0f) < 0.01f);
    CHECK_FALSE (p.getUndoManager().canUndo());

    // A preset file without a seed (hand-written) loads the fresh instance's seed, without its word.
    juce::ValueTree partial (params::presetTag);
    partial.setProperty ("name", "Partial", nullptr);
    partial.setProperty (params::chaos, 60.0f, nullptr);
    REQUIRE (partial.createXml()->writeTo (scratch.dir.getChildFile ("Partial.xml")));
    pm.scanUserPresets();
    seeds.paste ("ABCD-ABCD");
    pm.load (indexOf (pm, "Partial"));
    CHECK (seeds.current() == SeedState::initial().seed);
    CHECK (seeds.label().isEmpty());
    CHECK (std::abs (plain (p, params::chaos) - 60.0f) < 0.01f);
}

TEST_CASE ("Seeds: the Lock keeps the seed through preset loads and A/B, like the mix lock keeps Mix", "[seeds][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    InkcapProcessor p;
    auto& pm = p.getPresets();
    auto& seeds = p.getSeeds();

    automate (p, params::chaos, 90.0f);
    saveWithSeed (p, "Chaotic", "0x11111111");

    seeds.paste ("keeper");
    const auto kept = seeds.current();
    p.setSeedLocked (true);
    CHECK (p.isSeedLocked());

    pm.load (indexOf (pm, "Chaotic"));
    CHECK (seeds.current() == kept);
    CHECK (seeds.label() == "keeper");
    CHECK (std::abs (plain (p, params::chaos) - 90.0f) < 0.01f);    // the sound loads, the seed stays
    CHECK_FALSE (pm.isModified());                                  // a kept seed is no change

    pm.load (indexOf (pm, "Init"));
    CHECK (seeds.current() == kept);

    // A/B keeps it too.
    AbCompare ab (p, pm);
    ab.selectSlot (1);
    seeds.paste ("22222222");
    ab.selectSlot (0);
    CHECK (seeds.current() == 0x22222222u);

    // Unlocked, presets and A/B bring their own seeds again (A is live: it took the seed it kept).
    p.setSeedLocked (false);
    seeds.paste ("33333333");
    ab.selectSlot (1);
    CHECK (seeds.current() == 0x22222222u);
    ab.selectSlot (0);
    CHECK (seeds.current() == 0x33333333u);
    pm.load (indexOf (pm, "Chaotic"));
    CHECK (seeds.current() == 0x11111111u);

    // The two locks are independent.
    pm.setMixLocked (true);
    CHECK_FALSE (p.isSeedLocked());
    pm.load (indexOf (pm, "Init"));
    CHECK (seeds.current() == SeedState::initial().seed);
}

TEST_CASE ("Seeds: switching A/B carries each slot's seed as one undo step", "[seeds][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;
    auto& seeds = p.getSeeds();
    AbCompare ab (p, p.getPresets());

    seeds.paste ("AAAA-AAAA");
    ab.selectSlot (1);
    seeds.paste ("BBBB-BBBB");
    automate (p, params::damage, 10.0f);

    p.getUndoManager().clearUndoHistory();
    ab.selectSlot (0);
    CHECK (seeds.current() == 0xAAAAAAAAu);
    CHECK (std::abs (plain (p, params::damage) - 50.0f) < 0.01f);

    p.getUndoManager().undo();
    CHECK (seeds.current() == 0xBBBBBBBBu);
    CHECK (std::abs (plain (p, params::damage) - 10.0f) < 0.01f);
}

TEST_CASE ("Seeds: a preset whose seed is not a code loads the fresh seed without a word", "[seeds][presets]")
{
    // A shared or hand-edited preset with a letter O typed for a zero (PLAN.md, Settled 16).
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder folder;
    {
        auto p = std::make_unique<InkcapProcessor>();
        p->getSeeds().paste ("thrift");
        REQUIRE (p->getPresets().saveUserPreset ("Shared").wasOk());
    }
    auto file = folder.dir.getChildFile ("Shared.xml");
    if (! file.existsAsFile())
        for (const auto& f : folder.dir.findChildFiles (juce::File::findFiles, false))
            file = f;
    auto text = file.loadFileAsString();
    REQUIRE (text.contains ("8719-8E02"));
    REQUIRE (file.replaceWithText (text.replace ("8719-8E02", "8719-8EO2")));

    auto p = std::make_unique<InkcapProcessor>();
    p->getSeeds().paste ("pocket");
    const auto index = indexOf (p->getPresets(), "Shared");
    REQUIRE (index >= 0);
    p->getPresets().load (index);

    CHECK (p->getSeeds().current() == SeedState::initial().seed);
    CHECK (p->getSeeds().label().isEmpty());
    CHECK (p->getState().state.getProperty ("seed").toString() == p->getSeeds().code());

    // The session reopens as it was saved.
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = std::make_unique<InkcapProcessor>();
    q->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q->getSeeds().current() == p->getSeeds().current());
    CHECK (q->getSeeds().label() == p->getSeeds().label());

    // One undo step takes the load back, repair included.
    p->getUndoManager().undo();
    CHECK (p->getSeeds().label() == "pocket");
}
