#pragma once

#include "common/plugin/UiState.h"

namespace cerulean::ui::uistate
{

// Cerulean's own keys inside CeruleanProcessor::getUiState(), next to the shared ones. These are
// saved with the session, so never rename them.
using namespace myzel::plugin::uistate;

inline const juce::Identifier advancedOpen { "advancedOpen" };   // bool, the Advanced drawer

} // namespace cerulean::ui::uistate
