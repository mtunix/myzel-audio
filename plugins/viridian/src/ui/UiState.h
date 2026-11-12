#pragma once

#include "common/plugin/UiState.h"

namespace viridian::ui::uistate
{

// Viridian's own keys inside ViridianProcessor::getUiState(), next to the shared ones. These are
// saved with the session, so never rename them. The analyser is a view setting, not part of the
// sound: no parameter, no preset, no undo (as Umber's meter mode).
using namespace myzel::plugin::uistate;

inline const juce::Identifier analyserMode  { "analyserMode" };   // int, 0 = off, 1 = post, 2 = pre + post; absent = post
inline const juce::Identifier analyserSpeed { "analyserSpeed" };  // int, 0 = slow, 1 = medium, 2 = fast; absent = medium

} // namespace viridian::ui::uistate
