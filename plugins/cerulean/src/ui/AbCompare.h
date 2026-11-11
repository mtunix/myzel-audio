#pragma once

#include "ui/PresetManager.h"

namespace cerulean::ui
{

/**
    A/B compare: two parameter snapshots kept in the session's UI state.

    The active slot is always the live sound; switching stores the live sound into the active
    slot and recalls the other one (an undoable step). The first switch to an empty slot simply
    clones the current sound.
*/
class AbCompare
{
public:
    AbCompare (CeruleanProcessor& processor, PresetManager& presets);

    int getActiveSlot() const;
    void selectSlot (int slot);
    void copyActiveToOther();

    std::function<void()> onChange;

private:
    juce::ValueTree slotTree (int slot) const;
    void storeInto (int slot);

    CeruleanProcessor& processor;
    PresetManager& presets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbCompare)
};

} // namespace cerulean::ui
