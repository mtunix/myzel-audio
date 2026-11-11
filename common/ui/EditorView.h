#pragma once

#include "common/ui/ViewMode.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace myzel::ui
{

/**
    One face of the editor (the panel or the studio view), laid out once at base size and scaled
    by the editor (PluginEditor). The editor owns the chrome both views share (header, dialog,
    mix-lock badge, frame clock) and draws it over the view; the model they share (presets, A/B,
    the stored view) is the EditorController, which outlives the views.
*/
class EditorView : public juce::Component
{
public:
    ~EditorView() override = default;

    virtual ViewMode getMode() const noexcept = 0;

    /** Height at 100 % (may change, e.g. a panel that grows while a drawer is open). */
    virtual int getBaseHeight() const noexcept = 0;

    /** Wired by the editor: called when getBaseHeight() changes, so the window follows. */
    std::function<void()> onBaseSizeChanged;

    /** One animation frame, from the editor's frame clock; dt is in seconds, at most 0.1. */
    virtual void animateFrame (float dt) = 0;

    /** The host restored a session while the view is shown: re-read the view's UI state. */
    virtual void sessionRestored() {}

    /** Where the mix-lock padlock is centred, in view coordinates (none: no badge). */
    virtual std::optional<juce::Point<int>> getMixLockAnchor() const { return std::nullopt; }

    /** Wired by the editor: for a view that changes what the header shows. */
    std::function<void()> refreshHeader;
};

} // namespace myzel::ui
