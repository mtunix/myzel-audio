#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace myzel::plugin::uistate
{

// Property and child names inside the processor's UI state (PresetHost::getUiState()), shared by
// every plugin. They are saved with the session, so never rename them. UI state is always
// changed without the undo manager. Plugins add their own keys next to these.
inline const juce::Identifier scale      { "scale" };          // float, 0.75 .. 2.0
inline const juce::Identifier view       { "view" };           // string, "panel" or "studio"
inline const juce::Identifier presetName { "presetName" };     // string
inline const juce::Identifier presetUser { "presetIsUser" };   // bool
inline const juce::Identifier abActive   { "abActive" };       // int, 0 = A, 1 = B
inline const juce::Identifier abSlot0    { "SlotA" };          // child: parameter snapshot
inline const juce::Identifier abSlot1    { "SlotB" };          // child: parameter snapshot
inline const juce::Identifier mixLock    { "mixLock" };        // bool, presets and A/B keep the mix

} // namespace myzel::plugin::uistate
