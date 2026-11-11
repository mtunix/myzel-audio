#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace umber::ui::uistate
{

// Property and child names inside UmberProcessor::getUiState(). These are saved with the
// session, so never rename them. UI state is always changed without the undo manager.
inline const juce::Identifier scale       { "scale" };          // float, 0.75 .. 2.0
inline const juce::Identifier meterMode   { "meterMode" };      // int, 0 = output, 1 = drive
inline const juce::Identifier presetName  { "presetName" };     // string
inline const juce::Identifier presetUser  { "presetIsUser" };   // bool
inline const juce::Identifier abActive    { "abActive" };       // int, 0 = A, 1 = B
inline const juce::Identifier abSlot0     { "SlotA" };          // child: parameter snapshot
inline const juce::Identifier abSlot1     { "SlotB" };          // child: parameter snapshot

} // namespace umber::ui::uistate
