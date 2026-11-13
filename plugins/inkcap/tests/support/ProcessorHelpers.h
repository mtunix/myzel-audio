#pragma once

#include "plugin/FactoryPresets.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

/** What the processor tests share: parameter access the way our editor and hosts do it, a scratch preset folder. */
namespace inkcap::testing
{

inline juce::RangedAudioParameter& param (InkcapProcessor& p, const juce::String& id)
{
    auto* rp = p.getState().getParameter (id);
    REQUIRE (rp != nullptr);
    return *rp;
}

inline float plain (InkcapProcessor& p, const juce::String& id)
{
    auto& rp = param (p, id);
    return rp.convertFrom0to1 (rp.getValue());
}

/** Host automation: no gesture. */
inline void automate (InkcapProcessor& p, const juce::String& id, float plainValue)
{
    auto& rp = param (p, id);
    rp.setValueNotifyingHost (rp.convertTo0to1 (plainValue));
}

/** What an editor does: a gesture-wrapped edit, one undo step. */
inline void userEdit (InkcapProcessor& p, const juce::String& id, float plainValue)
{
    p.getUndoManager().beginNewTransaction();
    auto& rp = param (p, id);
    rp.beginChangeGesture();
    rp.setValueNotifyingHost (rp.convertTo0to1 (plainValue));
    rp.endChangeGesture();
}

inline int indexOf (const PresetManager& pm, const juce::String& name)
{
    const auto& e = pm.getEntries();
    for (size_t i = 0; i < e.size(); ++i)
        if (e[i].name == name)
            return (int) i;
    return -1;
}

inline juce::MemoryBlock saveState (InkcapProcessor& p)
{
    juce::MemoryBlock state;
    p.getStateInformation (state);
    return state;
}

/**
    Redirects user presets to a fresh folder for the lifetime of a test. Create it before the
    processor. ctest runs test cases as parallel processes: the name is random and the folder
    exists from the start, so no two of them ever share one.
*/
struct ScratchPresetFolder
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("inkcap-preset-tests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));

    ScratchPresetFolder()
    {
        dir.createDirectory();
        PresetManager::setUserPresetDirectoryOverride (dir);
    }
    ~ScratchPresetFolder()
    {
        dir.deleteRecursively();
        PresetManager::setUserPresetDirectoryOverride ({});
    }
};

} // namespace inkcap::testing
