#pragma once

#include "common/ui/EditorController.h"
#include "common/ui/EditorView.h"
#include "common/ui/HeaderBar.h"
#include "common/ui/InlineDialog.h"
#include "common/ui/KitLookAndFeel.h"
#include "common/ui/LockBadge.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace myzel::ui
{

/**
    The editor window: shows one view at a time (the panel or the studio view), scales it, and
    follows the session (scale and view are stored in the UI state).

    The views are made by the plugin's factory and share the controller, which the editor owns
    and which outlives them. The editor also owns what both views show the same way: the header
    (over the top of the view), the in-editor dialog, the mix-lock padlock and the frame clock.
    Switching views replaces only the view, so nothing in the header moves.
*/
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::ValueTree::Listener
{
public:
    using ViewFactory = std::function<std::unique_ptr<EditorView> (ViewMode, EditorController&)>;

    PluginEditor (juce::AudioProcessor& processor, std::unique_ptr<EditorController> controller,
                  int baseWidth, HeaderConfig headerConfig, ViewFactory makeView);
    ~PluginEditor() override;

    void setScale (float newScale);
    float getScale() const noexcept { return scale; }

    /** Switches to a view now (stored in the session and as the default for new instances). */
    void setView (ViewMode mode);
    ViewMode getViewMode() const noexcept { return view->getMode(); }

    EditorView& getView() noexcept { return *view; }
    EditorController& getController() noexcept { return *controller; }
    HeaderBar& getHeader() noexcept { return header; }
    InlineDialog& getDialog() noexcept { return dialog; }

    /** One frame of the frame clock (called on vblank; the snapshot tool drives it directly). */
    void animateFrame (double timeSeconds);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void showView (ViewMode mode);
    void applyBaseSize (float newScale);
    void updateMixLockBadge();
    void valueTreeRedirected (juce::ValueTree&) override;

    plugin::PresetHost& host;
    KitLookAndFeel lookAndFeel;                       // declared first: outlives every child
    std::unique_ptr<EditorController> controller;     // outlives the views and the header
    ViewFactory makeView;

    juce::Component content;                          // base size, scaled; the view and the chrome over it
    InlineDialog dialog;
    HeaderBar header;
    LockBadge mixLockBadge;
    std::unique_ptr<EditorView> view;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    const int baseWidth;
    float scale = 1.0f;

    juce::VBlankAttachment vblank;
    double lastFrameTime = -1.0;
    int framesSinceRefresh = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace myzel::ui
