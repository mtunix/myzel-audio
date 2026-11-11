#pragma once

#include "common/ui/EditorController.h"
#include "common/ui/EditorView.h"
#include "common/ui/KitLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace myzel::ui
{

/**
    The editor window: shows one view at a time (the panel or the studio view), scales it, and
    follows the session (scale and view are stored in the UI state).

    The views are made by the plugin's factory and share the controller, which the editor owns
    and which outlives them. Switching views replaces the view; the header keeps its layout, so
    nothing under the mouse moves.
*/
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::ValueTree::Listener
{
public:
    using ViewFactory = std::function<std::unique_ptr<EditorView> (ViewMode, EditorController&)>;

    PluginEditor (juce::AudioProcessor& processor, std::unique_ptr<EditorController> controller,
                  int baseWidth, ViewFactory makeView);
    ~PluginEditor() override;

    void setScale (float newScale);
    float getScale() const noexcept { return scale; }

    /** Switches to a view now (stored in the session and as the default for new instances). */
    void setView (ViewMode mode);
    ViewMode getViewMode() const noexcept { return view->getMode(); }

    EditorView& getView() noexcept { return *view; }
    EditorController& getController() noexcept { return *controller; }

    /** One animation frame of the current view (the snapshot tool drives frames directly). */
    void animateFrame (double timeSeconds) { view->animateFrame (timeSeconds); }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void showView (ViewMode mode);
    void applyBaseSize (float newScale);
    void valueTreeRedirected (juce::ValueTree&) override;

    plugin::PresetHost& host;
    KitLookAndFeel lookAndFeel;                       // declared first: outlives every child
    std::unique_ptr<EditorController> controller;     // outlives the views
    ViewFactory makeView;
    std::unique_ptr<EditorView> view;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    const int baseWidth;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace myzel::ui
