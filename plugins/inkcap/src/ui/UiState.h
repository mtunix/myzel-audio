#pragma once

#include "common/plugin/UiState.h"
#include "plugin/SeedState.h"

namespace inkcap::ui::uistate
{

// Inkcap's own keys inside InkcapProcessor::getUiState(), next to the shared ones and the seed's
// (seedstate::seedLock, seedstate::history). Saved with the session: never rename them.
using namespace myzel::plugin::uistate;

inline const juce::Identifier modulePage { "modulePage" };   // int: the module whose page is open, -1: the module list

} // namespace inkcap::ui::uistate
