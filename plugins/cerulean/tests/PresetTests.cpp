/*
    Factory presets and the preset plumbing: valid values, Init = the parameter defaults, every
    sound parameter stored, a sane render, fresh engines (trails) on load and on A/B, user
    preset files.
*/

#include "plugin/PluginProcessor.h"
#include "ui/AbCompare.h"
#include "ui/FactoryPresets.h"
#include "ui/PresetManager.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <set>
#include <thread>

using namespace cerulean;
using Catch::Matchers::WithinAbs;

namespace
{
    float plain (CeruleanProcessor& p, const char* id)
    {
        auto* param = p.getState().getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }

    void set (CeruleanProcessor& p, const char* id, float value)
    {
        auto* param = p.getState().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** A plain value lies inside the parameter's range (it survives a round trip). */
    bool inRange (CeruleanProcessor& p, const char* id, float value)
    {
        auto* param = p.getState().getParameter (id);
        const auto& range = param->getNormalisableRange();
        return value >= range.start - 1.0e-4f && value <= range.end + 1.0e-4f;
    }

    /** Redirects user presets to a scratch folder for the lifetime of a test. */
    struct ScratchPresetFolder
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("cerulean-preset-tests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));

        ScratchPresetFolder()  { ui::PresetManager::setUserPresetDirectoryOverride (dir); }
        ~ScratchPresetFolder() { dir.deleteRecursively(); ui::PresetManager::setUserPresetDirectoryOverride ({}); }
    };

    int indexOf (const ui::PresetManager& pm, const juce::String& name)
    {
        const auto& e = pm.getEntries();

        for (size_t i = 0; i < e.size(); ++i)
            if (e[i].name == name)
                return (int) i;

        return -1;
    }
}

TEST_CASE ("Presets: factory presets are valid", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;

    const std::set<juce::String> categories { "Utility", "Rooms", "Halls & Plates", "Springs", "Ambient",
                                               "Shimmer", "Swells & Blooms", "Voices", "Echoes" };
    std::set<juce::String> names, usedCategories;

    REQUIRE (ui::factoryPresets().size() >= 10);

    for (const auto& f : ui::factoryPresets())
    {
        INFO (f.name);
        CHECK (names.insert (f.name).second);   // unique names
        CHECK (categories.count (f.category) == 1);
        usedCategories.insert (f.category);

        CHECK (f.algorithm >= 0);
        CHECK (f.algorithm < numAlgorithms);
        CHECK (inRange (p, params::decay, f.decay));
        CHECK (inRange (p, params::preDelay, f.preDelay));
        CHECK (inRange (p, params::size, f.size));
        CHECK (inRange (p, params::mod, f.mod));
        CHECK (inRange (p, params::tone, f.tone));
        CHECK (inRange (p, params::paramA, f.paramA));
        CHECK (inRange (p, params::paramB, f.paramB));
        CHECK (inRange (p, params::mix, f.mix));
        CHECK (inRange (p, params::output, f.output));
        CHECK (inRange (p, params::lowEnd, f.lowEnd));
        CHECK (inRange (p, params::diffusion, f.diffusion));
        CHECK (inRange (p, params::modRate, f.modRate));
        CHECK (inRange (p, params::width, f.width));
        CHECK (inRange (p, params::inLowCut, f.inLowCut));
        CHECK (inRange (p, params::inHighCut, f.inHighCut));
        CHECK (inRange (p, params::duckAmount, f.duckAmount));
        CHECK (inRange (p, params::duckThreshold, f.duckThreshold));
        CHECK (inRange (p, params::duckRelease, f.duckRelease));
        CHECK (f.preDelayNote >= 0);
        CHECK (f.preDelayNote < numNoteValues);
        CHECK (f.echoNote >= 0);
        CHECK (f.echoNote < numNoteValues);
    }

    CHECK (usedCategories == categories);   // every category has at least one entry
}

TEST_CASE ("Presets: Init is the parameter defaults, and every sound parameter is stored", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    CeruleanProcessor p;
    ui::PresetManager pm (p);

    const auto init = indexOf (pm, "Init");
    REQUIRE (init == 0);

    for (const auto& entry : pm.getEntries())
        for (const auto& id : params::soundParameterIds())
        {
            INFO (entry.name << ": " << id);
            CHECK (entry.values.hasProperty (juce::Identifier (id)));
        }

    const auto& values = pm.getEntries()[(size_t) init].values;

    for (const auto& id : params::soundParameterIds())
    {
        INFO (id);
        auto* param = p.getState().getParameter (id);
        const auto defaultPlain = param->convertFrom0to1 (param->getDefaultValue());
        CHECK_THAT ((double) (float) values.getProperty (juce::Identifier (id)), WithinAbs (defaultPlain, 1.0e-3));
    }

    // Performance and session switches are not part of a preset.
    for (const auto* id : { params::freeze, params::bypass, params::bpm, params::freezeMode })
        CHECK_FALSE (values.hasProperty (juce::Identifier (id)));
}

TEST_CASE ("Presets: every factory preset loads and renders sanely", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    CeruleanProcessor reference;
    ui::PresetManager referencePresets (reference);
    const auto count = (int) referencePresets.getEntries().size();

    for (int i = 0; i < count; ++i)
    {
        CeruleanProcessor p;
        p.setPlayConfigDetails (2, 2, rate, block);
        p.prepareToPlay (rate, block);
        ui::PresetManager pm (p);
        const auto& entry = pm.getEntries()[(size_t) i];
        INFO (entry.name);

        pm.load (i);
        CHECK (pm.getCurrentName() == entry.name);
        CHECK_FALSE (pm.isModified());
        CHECK (juce::roundToInt (plain (p, params::algorithm)) == (int) entry.values.getProperty (params::algorithm));
        CHECK_THAT (plain (p, params::decay), WithinAbs ((float) entry.values.getProperty (params::decay), 1.0e-3));

        // Half a second of decaying noise bursts, then the tail.
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        juce::Random rng (7);
        bool finite = true;
        float peak = 0.0f;

        for (int b = 0; b < (int) (1.0 * rate) / block; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < block; ++s)
                    buffer.setSample (ch, s, b * block < (int) (0.5 * rate) ? 0.25f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f);

            p.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < block; ++s)
                {
                    finite = finite && std::isfinite (buffer.getSample (ch, s));
                    peak = std::max (peak, std::abs (buffer.getSample (ch, s)));
                }
        }

        CHECK (finite);
        CHECK (peak < 2.0f);
    }
}

TEST_CASE ("Presets: loading is one undo step and starts a fresh engine (trails)", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    CeruleanProcessor p;
    ui::PresetManager pm (p);
    auto& um = p.getUndoManager();
    um.clearUndoHistory();

    const auto generation = p.readSettings().presetGeneration;
    pm.load (indexOf (pm, "Octave Halo"));

    CHECK (p.readSettings().presetGeneration != generation);
    CHECK (juce::roundToInt (plain (p, params::algorithm)) == (int) Algorithm::halo);
    REQUIRE (um.canUndo());

    um.undo();
    CHECK (juce::roundToInt (plain (p, params::algorithm)) == (int) Algorithm::hall);
    CHECK_THAT (plain (p, params::decay), WithinAbs (3.0, 1.0e-3));
    CHECK_FALSE (um.canUndo());
}

TEST_CASE ("Presets: A/B switching recalls each slot and starts a fresh engine", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    CeruleanProcessor p;
    ui::PresetManager pm (p);
    ui::AbCompare ab (p, pm);

    set (p, params::decay, 2.0f);
    ab.selectSlot (1);   // B starts as a copy of A
    set (p, params::decay, 12.0f);
    set (p, params::algorithm, (float) (int) Algorithm::nimbus);

    const auto generation = p.readSettings().presetGeneration;
    ab.selectSlot (0);
    CHECK (p.readSettings().presetGeneration != generation);
    CHECK_THAT (plain (p, params::decay), WithinAbs (2.0, 1.0e-3));
    CHECK (juce::roundToInt (plain (p, params::algorithm)) == (int) Algorithm::hall);

    ab.selectSlot (1);
    CHECK_THAT (plain (p, params::decay), WithinAbs (12.0, 1.0e-2));
    CHECK (juce::roundToInt (plain (p, params::algorithm)) == (int) Algorithm::nimbus);
}

TEST_CASE ("Presets: user presets round-trip through XML files", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    CeruleanProcessor p;
    ui::PresetManager pm (p);

    set (p, params::algorithm, (float) (int) Algorithm::plate);
    set (p, params::decay, 4.5f);
    set (p, params::echoNote, 9.0f);
    set (p, params::trails, 0.0f);
    REQUIRE (pm.saveUserPreset ("My Plate").wasOk());

    const auto file = scratch.dir.getChildFile ("My Plate.xml");
    REQUIRE (file.existsAsFile());
    CHECK (file.loadFileAsString().contains ("CeruleanPreset"));

    // Change everything, then load it back.
    pm.load (0);
    CHECK (juce::roundToInt (plain (p, params::algorithm)) == (int) Algorithm::hall);

    ui::PresetManager fresh (p);   // rescans the folder
    const auto index = indexOf (fresh, "My Plate");
    REQUIRE (index >= 0);
    CHECK_FALSE (fresh.getEntries()[(size_t) index].isFactory);
    fresh.load (index);
    CHECK (juce::roundToInt (plain (p, params::algorithm)) == (int) Algorithm::plate);
    CHECK_THAT (plain (p, params::decay), WithinAbs (4.5, 1.0e-3));
    CHECK (juce::roundToInt (plain (p, params::echoNote)) == 9);
    CHECK (plain (p, params::trails) < 0.5f);

    // A file that only mentions some parameters loads the rest at their defaults.
    juce::ValueTree partial ("CeruleanPreset");
    partial.setProperty ("name", "Partial", nullptr);
    partial.setProperty (params::decay, 7.0f, nullptr);
    REQUIRE (partial.createXml()->writeTo (scratch.dir.getChildFile ("Partial.xml")));
    fresh.scanUserPresets();
    fresh.load (indexOf (fresh, "Partial"));
    CHECK_THAT (plain (p, params::decay), WithinAbs (7.0, 1.0e-3));
    CHECK (juce::roundToInt (plain (p, params::algorithm)) == (int) Algorithm::hall);
    CHECK (plain (p, params::trails) > 0.5f);

    CHECK (fresh.deleteUserPreset (indexOf (fresh, "My Plate")).wasOk());
    CHECK_FALSE (file.existsAsFile());
}

TEST_CASE ("Presets: the mix lock keeps Mix and Wet on preset loads and A/B", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    CeruleanProcessor p;
    auto& pm = p.getPresets();
    const auto cathedral = indexOf (pm, "Cathedral");
    REQUIRE (cathedral >= 0);
    const auto presetMix = (float) pm.getEntries()[(size_t) cathedral].values.getProperty (params::mix);

    // A send: Wet on, Mix all the way up.
    set (p, params::mix, 100.0f);
    set (p, params::wetOnly, 1.0f);
    pm.setMixLocked (true);
    pm.load (cathedral);

    CHECK_THAT (plain (p, params::mix), WithinAbs (100.0, 1.0e-3));
    CHECK (plain (p, params::wetOnly) > 0.5f);
    CHECK_THAT (plain (p, params::decay), WithinAbs ((float) pm.getEntries()[(size_t) cathedral].values.getProperty (params::decay), 1.0e-2));
    CHECK_FALSE (pm.isModified());   // the locked controls don't count as changes

    // A/B keeps them too.
    ui::AbCompare ab (p, pm);
    ab.selectSlot (1);
    set (p, params::mix, 20.0f);
    ab.selectSlot (0);
    CHECK_THAT (plain (p, params::mix), WithinAbs (20.0, 1.0e-3));

    // Unlocked, the preset's own values come back.
    pm.setMixLocked (false);
    pm.load (cathedral);
    CHECK_THAT (plain (p, params::mix), WithinAbs (presetMix, 1.0e-3));
    CHECK (plain (p, params::wetOnly) < 0.5f);

    // The lock is part of the session.
    pm.setMixLocked (true);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    CeruleanProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.getPresets().isMixLocked());
}

TEST_CASE ("Presets: host programs are the factory presets, then the user presets", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresetFolder scratch;
    const auto factory = ui::factoryPresets();
    const auto numFactory = (int) factory.size();

    // A user preset that exists before the instance is created.
    {
        CeruleanProcessor setup;
        set (setup, params::decay, 7.5f);
        REQUIRE (setup.getPresets().saveUserPreset ("Verse").wasOk());
    }

    CeruleanProcessor p;
    REQUIRE (p.getNumPrograms() == numFactory + 1);
    CHECK (p.getProgramName (0) == "Init");
    CHECK (p.getProgramName (numFactory) == "User: Verse");
    CHECK (p.getCurrentProgram() == 0);

    for (int i : { 5, numFactory - 1 })
    {
        CHECK (p.getProgramName (i) == juce::String (factory[(size_t) i].name));

        const auto generation = p.readSettings().presetGeneration;
        p.setCurrentProgram (i);   // on the message thread: loads at once

        CHECK (p.getCurrentProgram() == i);
        CHECK (p.getPresets().getCurrentName() == juce::String (factory[(size_t) i].name));
        CHECK_THAT (plain (p, params::decay), WithinAbs (factory[(size_t) i].decay, 1.0e-2));
        CHECK (p.readSettings().presetGeneration != generation);   // a fresh engine: trails
    }

    // The user preset, by program number.
    p.setCurrentProgram (numFactory);
    CHECK (p.getPresets().getCurrentName() == "Verse");
    CHECK (p.getPresets().isCurrentUserPreset());
    CHECK_THAT (plain (p, params::decay), WithinAbs (7.5, 1.0e-2));

    // From another thread (e.g. a program change on the audio thread): the sound changes at
    // once, the browser shows it on the message thread.
    {
        const auto generation = p.readSettings().presetGeneration;
        std::thread ([&p] { p.setCurrentProgram (3); }).join();
        CHECK_THAT (plain (p, params::decay), WithinAbs (factory[3].decay, 1.0e-2));
        CHECK (p.getCurrentProgram() == 3);
        CHECK (p.readSettings().presetGeneration != generation);
        p.processPendingRequests();
        CHECK (p.getPresets().getCurrentName() == juce::String (factory[3].name));
        CHECK_FALSE (p.getPresets().isModified());
    }

    // ... and it keeps the mix lock.
    p.getPresets().setMixLocked (true);
    set (p, params::mix, 77.0f);
    std::thread ([&p] { p.setCurrentProgram (4); }).join();
    p.processPendingRequests();
    CHECK_THAT (plain (p, params::mix), WithinAbs (77.0, 1.0e-3));
    CHECK_THAT (plain (p, params::decay), WithinAbs (factory[4].decay, 1.0e-2));
    p.getPresets().setMixLocked (false);

    // Choosing a preset in the browser moves the host program along. A preset saved after the
    // instance was created is not a program (hosts cache the list): it reports program 0.
    p.getPresets().load (7);
    CHECK (p.getCurrentProgram() == 7);
    REQUIRE (p.getPresets().saveUserPreset ("Chorus").wasOk());
    CHECK (p.getNumPrograms() == numFactory + 1);
    CHECK (p.getCurrentProgram() == 0);

    // A restored session reports its preset, and wins over a program change still queued.
    p.setCurrentProgram (9);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    CeruleanProcessor restored;
    std::thread ([&restored] { restored.setCurrentProgram (2); }).join();
    restored.setStateInformation (state.getData(), (int) state.getSize());
    restored.processPendingRequests();
    CHECK (restored.getCurrentProgram() == 9);
    CHECK (restored.getPresets().getCurrentName() == juce::String (factory[9].name));
}
