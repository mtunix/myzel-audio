#include "common/ui/EditorController.h"

#include "common/plugin/UiState.h"

namespace myzel::ui
{

EditorController::EditorController (plugin::PresetHost& h, plugin::PresetManager& p)
    : host (h),
      presets (p),
      ab (h, p)
{
}

juce::RangedAudioParameter& EditorController::parameter (const char* id) const
{
    auto* param = host.getState().getParameter (id);
    jassert (param != nullptr);
    return *param;
}

bool EditorController::flag (const char* id) const
{
    return host.getState().getRawParameterValue (id)->load (std::memory_order_relaxed) >= 0.5f;
}

int EditorController::choice (const char* id) const
{
    return juce::roundToInt (host.getState().getRawParameterValue (id)->load (std::memory_order_relaxed));
}

ViewMode EditorController::getView() const
{
    const auto stored = host.getUiState().getProperty (plugin::uistate::view).toString();
    return viewModeFromString (stored, viewPreference::getDefault (presets.getPluginName()));
}

void EditorController::setView (ViewMode mode)
{
    host.getUiState().setProperty (plugin::uistate::view, toString (mode), nullptr);
    viewPreference::setDefault (presets.getPluginName(), mode);
}

} // namespace myzel::ui
