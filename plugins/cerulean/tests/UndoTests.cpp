#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace cerulean;
using Catch::Matchers::WithinAbs;

namespace
{
    juce::RangedAudioParameter& param (CeruleanProcessor& p, const char* id)
    {
        auto* rp = p.getState().getParameter (id);
        REQUIRE (rp != nullptr);
        return *rp;
    }

    /** What our editor does: a gesture-wrapped edit. */
    void userEdit (CeruleanProcessor& p, const char* id, float plainValue)
    {
        auto& rp = param (p, id);
        rp.beginChangeGesture();
        rp.setValueNotifyingHost (rp.convertTo0to1 (plainValue));
        rp.endChangeGesture();
    }

    float plain (CeruleanProcessor& p, const char* id)
    {
        return p.getState().getRawParameterValue (id)->load();
    }
}

TEST_CASE ("Undo: host automation and state saves never enter the history", "[undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;

    for (int i = 0; i < 200; ++i)
    {
        param (p, params::decay).setValueNotifyingHost ((float) (i % 10) / 10.0f);   // automation: no gestures

        juce::MemoryBlock state;
        p.getStateInformation (state);
    }

    CHECK_FALSE (p.getUndoManager().canUndo());
}

TEST_CASE ("Undo: user edits are undoable and redoable", "[undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    auto& um = p.getUndoManager();

    um.beginNewTransaction();
    userEdit (p, params::decay, 8.0f);
    um.beginNewTransaction();
    userEdit (p, params::mix, 60.0f);

    REQUIRE (um.canUndo());

    um.undo();
    CHECK_THAT (plain (p, params::mix), WithinAbs (35.0, 0.01));
    CHECK_THAT (plain (p, params::decay), WithinAbs (8.0, 0.01));

    um.undo();
    CHECK_THAT (plain (p, params::decay), WithinAbs (3.0, 0.01));
    CHECK_FALSE (um.canUndo());

    um.redo();
    CHECK_THAT (plain (p, params::decay), WithinAbs (8.0, 0.01));
}

TEST_CASE ("Undo: a grouped change (preset load, A/B) undoes in one step", "[undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    auto& um = p.getUndoManager();

    um.beginNewTransaction ("Load preset");
    userEdit (p, params::decay, 12.0f);
    userEdit (p, params::algorithm, (float) (int) Algorithm::halo);
    userEdit (p, params::duck, 1.0f);
    userEdit (p, params::lowEnd, 1.6f);

    um.undo();
    CHECK_THAT (plain (p, params::decay), WithinAbs (3.0, 0.01));
    CHECK (juce::roundToInt (plain (p, params::algorithm)) == (int) Algorithm::hall);
    CHECK (plain (p, params::duck) < 0.5f);
    CHECK_THAT (plain (p, params::lowEnd), WithinAbs (1.0, 0.01));
    CHECK_FALSE (um.canUndo());
}

TEST_CASE ("Undo: restoring a session clears the history", "[undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;

    juce::MemoryBlock state;
    p.getStateInformation (state);

    userEdit (p, params::decay, 8.0f);
    REQUIRE (p.getUndoManager().canUndo());

    p.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_FALSE (p.getUndoManager().canUndo());
}

TEST_CASE ("Undo: Freeze, Bypass and BPM are performance controls, not undoable edits", "[undo]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    auto& um = p.getUndoManager();

    um.beginNewTransaction();
    userEdit (p, params::freeze, 1.0f);
    um.beginNewTransaction();
    userEdit (p, params::bypass, 1.0f);
    um.beginNewTransaction();
    userEdit (p, params::bpm, 90.0f);
    CHECK_FALSE (um.canUndo());

    // A momentary Freeze press is one long gesture: still nothing recorded.
    auto& freeze = param (p, params::freeze);
    freeze.beginChangeGesture();
    freeze.setValueNotifyingHost (1.0f);
    freeze.setValueNotifyingHost (0.0f);
    freeze.endChangeGesture();
    CHECK_FALSE (um.canUndo());

    um.beginNewTransaction();
    userEdit (p, params::trails, 0.0f);
    CHECK (um.canUndo());
}
