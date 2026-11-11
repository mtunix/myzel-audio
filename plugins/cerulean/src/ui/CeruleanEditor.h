#pragma once

#include "common/ui/PluginEditor.h"
#include "ui/MainPanel.h"
#include "ui/PanelController.h"
#include "ui/StudioPanel.h"
#include "ui/Theme.h"

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
class CeruleanEditor final : public myzel::ui::PluginEditor
{
public:
    explicit CeruleanEditor (CeruleanProcessor& p);

    ui::PanelController& getController() noexcept { return static_cast<ui::PanelController&> (PluginEditor::getController()); }

    /** The panel view; only valid while it is shown. */
    ui::MainPanel& getMainPanel() noexcept;

    /** The studio view; only valid while it is shown. */
    ui::StudioPanel& getStudioPanel() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeruleanEditor)
};

} // namespace cerulean
