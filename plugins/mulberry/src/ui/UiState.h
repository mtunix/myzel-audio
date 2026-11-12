#pragma once

#include "common/plugin/UiState.h"

namespace mulberry::ui::uistate
{

// Mulberry's own keys inside MulberryProcessor::getUiState(), next to the shared ones. These are
// saved with the session, so never rename them.
using namespace myzel::plugin::uistate;

inline const juce::Identifier advancedOpen { "advancedOpen" };   // bool, the Advanced deck

} // namespace mulberry::ui::uistate
