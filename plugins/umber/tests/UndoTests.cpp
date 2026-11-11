#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace umber;
using Catch::Matchers::WithinAbs;

namespace
{
    juce::RangedAudioParameter& param (UmberProcessor& p, const char* id)
    {
        auto* rp = p.getState().getParameter (id);
        REQUIRE (rp != nullptr);
        return *rp;
    }

    /** What our editor does: a gesture-wrapped edit. */
    void userEdit (UmberProcessor& p, const char* id, float plainValue)
    {
        auto& rp = param (p, id);
        rp.beginChangeGesture();
        rp.setValueNotifyingHost (rp.convertTo0to1 (plainValue));
        rp.endChangeGesture();
    }

    float plain (UmberProcessor& p, const char* id)
    {
        return p.getState().getRawParameterValue (id)->load();
    }
}

TEST_CASE ("Host automation and state saves never enter the undo history", "[undo]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    for (int i = 0; i < 200; ++i)
    {
        auto& drive = param (p, params::drive);
        drive.setValueNotifyingHost ((float) (i % 10) / 10.0f); // automation: no gestures

        juce::MemoryBlock state;
        p.getStateInformation (state);
    }

    CHECK_FALSE (p.getUndoManager().canUndo());
}

TEST_CASE ("User edits are undoable and redoable", "[undo]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;
    auto& um = p.getUndoManager();

    um.beginNewTransaction();
    userEdit (p, params::drive, 7.0f);
    um.beginNewTransaction();
    userEdit (p, params::mix, 40.0f);

    REQUIRE (um.canUndo());

    um.undo();
    CHECK_THAT (plain (p, params::mix), WithinAbs (100.0, 0.01));
    CHECK_THAT (plain (p, params::drive), WithinAbs (7.0, 0.01));

    um.undo();
    CHECK_THAT (plain (p, params::drive), WithinAbs (3.0, 0.01));
    CHECK_FALSE (um.canUndo());

    um.redo();
    CHECK_THAT (plain (p, params::drive), WithinAbs (7.0, 0.01));
}

TEST_CASE ("A grouped change (preset load, A/B) undoes in one step", "[undo]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;
    auto& um = p.getUndoManager();

    um.beginNewTransaction ("Load preset");
    userEdit (p, params::drive, 9.0f);
    userEdit (p, params::model, (float) Model::fuzz);
    userEdit (p, params::slam, 1.0f);

    um.undo();
    CHECK_THAT (plain (p, params::drive), WithinAbs (3.0, 0.01));
    CHECK (juce::roundToInt (plain (p, params::model)) == (int) Model::classA);
    CHECK (plain (p, params::slam) < 0.5f);
    CHECK_FALSE (um.canUndo());
}

TEST_CASE ("Restoring a session clears the undo history", "[undo]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    juce::MemoryBlock state;
    p.getStateInformation (state);

    userEdit (p, params::drive, 8.0f);
    REQUIRE (p.getUndoManager().canUndo());

    p.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_FALSE (p.getUndoManager().canUndo());
}

TEST_CASE ("Bypass and Delta are listening switches, not undoable edits", "[undo]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;
    auto& um = p.getUndoManager();

    um.beginNewTransaction();
    userEdit (p, params::bypass, 1.0f);
    um.beginNewTransaction();
    userEdit (p, params::delta, 1.0f);
    CHECK_FALSE (um.canUndo());

    um.beginNewTransaction();
    userEdit (p, params::emphasis, 5.0f);
    CHECK (um.canUndo());
}
