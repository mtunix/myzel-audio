#pragma once

#include "common/plugin/UiState.h"

namespace halite::ui::uistate
{

// Halite's own keys inside HaliteProcessor::getUiState(), next to the shared ones (preset, A/B).
// Saved with the session: never rename them or change their types (PLAN.md, Behaviour; DESIGN.md, UI state keys).
using namespace myzel::plugin::uistate;

inline const juce::Identifier displayView  { "displayView" };    // string: "timeline" (default) or "cloud"
inline const juce::Identifier selectedBand { "selectedBand" };   // string: "ess" (default) or "harsh"
inline const juce::Identifier eventsShown  { "eventsShown" };    // bool, default true: the events sidebar
inline const juce::Identifier advancedOpen { "advancedOpen" };   // bool, default false: the Advanced row
inline const juce::Identifier timelineZoom { "timelineZoom" };   // float seconds, 2 … 10, default 6
inline const juce::Identifier cloudZoom    { "cloudZoom" };      // float seconds, 10 … 60, default 30

} // namespace halite::ui::uistate
