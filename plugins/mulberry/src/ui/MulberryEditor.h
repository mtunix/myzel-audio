#pragma once

#include "common/ui/PluginEditor.h"
#include "ui/MainPanel.h"
#include "ui/PanelController.h"
#include "ui/StudioPanel.h"
#include "ui/Theme.h"

namespace mulberry
{

class MulberryProcessor;

/**
    Mulberry's editor: hosts the panel or the studio view and scales it (75-200 %, fixed aspect
    ratio). The PanelController lives here and outlives the views.
*/
class MulberryEditor final : public myzel::ui::PluginEditor
{
public:
    explicit MulberryEditor (MulberryProcessor& p);

    ui::PanelController& getController() noexcept { return static_cast<ui::PanelController&> (PluginEditor::getController()); }
    ui::MainPanel& getMainPanel() noexcept;
    ui::StudioPanel& getStudioPanel() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MulberryEditor)
};

} // namespace mulberry
