#pragma once

#include "ui/CeruleanLookAndFeel.h"
#include "ui/EditorView.h"
#include "ui/MainPanel.h"
#include "ui/PanelController.h"
#include "ui/StudioPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace cerulean
{

class CeruleanProcessor;

/**
    Cerulean's editor: hosts one of the two views (the procedurally drawn panel, or the flat
    studio view) and scales it (75-200 %, fixed aspect ratio). Both views share one base size,
    so switching does not resize the host window, except that the panel grows by a strip while
    its Advanced deck is open.

    The PanelController (presets, A/B, meanings) lives here and outlives the views. The view and
    the scale are stored in the processor's UI state, so they survive sessions; a session with no
    stored view opens in the user's default view (the one chosen last).
*/
class CeruleanEditor final : public juce::AudioProcessorEditor,
                           private juce::ValueTree::Listener
{
public:
    explicit CeruleanEditor (CeruleanProcessor& p);
    ~CeruleanEditor() override;

    void setScale (float newScale);
    float getScale() const noexcept { return scale; }

    /** Switches to a view now (stored in the session and as the default for new instances). */
    void setView (ui::ViewMode mode);
    ui::ViewMode getViewMode() const noexcept { return view->getMode(); }

    ui::EditorView& getView() noexcept { return *view; }
    ui::PanelController& getController() noexcept { return controller; }

    /** The panel view; only valid while it is shown. */
    ui::MainPanel& getMainPanel() noexcept;

    /** The studio view; only valid while it is shown. */
    ui::StudioPanel& getStudioPanel() noexcept;

    /** One animation frame of the current view (the snapshot tool drives frames directly). */
    void animateFrame (double timeSeconds) { view->animateFrame (timeSeconds); }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void showView (ui::ViewMode mode);
    void applyBaseSize (float newScale);
    void valueTreeRedirected (juce::ValueTree&) override;

    CeruleanProcessor& processor;
    ui::CeruleanLookAndFeel lookAndFeel;     // declared first: outlives every child
    ui::PanelController controller;        // outlives the views
    std::unique_ptr<ui::EditorView> view;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeruleanEditor)
};

} // namespace cerulean
