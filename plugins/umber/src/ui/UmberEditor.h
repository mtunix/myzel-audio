#pragma once

#include "common/ui/PluginEditor.h"
#include "ui/MainPanel.h"
#include "ui/StudioPanel.h"
#include "ui/Theme.h"

namespace umber
{

class UmberProcessor;

/**
    Umber's editor: hosts one of the two views (the procedurally drawn panel, or the flat studio
    view) and scales it (75-200 %, fixed aspect ratio). Both views share one base size, so
    switching does not resize the host window.

    The view and the scale are stored in the processor's UI state, so they survive sessions; a
    session with no stored view opens in the user's default view (the one chosen last).
*/
class UmberEditor final : public myzel::ui::PluginEditor
{
public:
    explicit UmberEditor (UmberProcessor& p);

    /** The panel view; only valid while it is shown. */
    ui::MainPanel& getMainPanel() noexcept;

    /** The studio view; only valid while it is shown. */
    ui::StudioPanel& getStudioPanel() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UmberEditor)
};

} // namespace umber
