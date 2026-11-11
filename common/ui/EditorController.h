#pragma once

#include "common/plugin/AbCompare.h"
#include "common/plugin/PresetManager.h"
#include "common/ui/ViewMode.h"

namespace myzel::ui
{

/**
    The model an editor's views share: presets, A/B, undo and the stored view. Owned by the
    editor (PluginEditor), it outlives the views. A plugin derives from it for its own meanings.
*/
class EditorController
{
public:
    EditorController (plugin::PresetHost& host, plugin::PresetManager& presets);
    virtual ~EditorController() = default;

    plugin::PresetHost& getHost() noexcept        { return host; }
    plugin::PresetManager& getPresets() noexcept  { return presets; }
    plugin::AbCompare& getAb() noexcept           { return ab; }
    juce::UndoManager& getUndoManager() noexcept  { return host.getUndoManager(); }

    juce::RangedAudioParameter& parameter (const char* id) const;
    bool flag (const char* id) const;
    int choice (const char* id) const;

    /** The view stored in the session, or the user's default view if the session has none. */
    ViewMode getView() const;

    /** Stores the view in the session and makes it the default for new instances. */
    void setView (ViewMode mode);

private:
    plugin::PresetHost& host;
    plugin::PresetManager& presets;   // the processor's (it outlives every editor)
    plugin::AbCompare ab;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorController)
};

} // namespace myzel::ui
