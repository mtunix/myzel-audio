#pragma once

#include "common/plugin/UiState.h"

namespace umber::ui::uistate
{

// Umber's own keys inside UmberProcessor::getUiState(), next to the shared ones. These are
// saved with the session, so never rename them.
using namespace myzel::plugin::uistate;

inline const juce::Identifier meterMode { "meterMode" };   // int, 0 = output, 1 = drive

} // namespace umber::ui::uistate
