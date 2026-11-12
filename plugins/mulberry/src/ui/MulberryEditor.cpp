#include "ui/MulberryEditor.h"

#include "plugin/PluginProcessor.h"
#include "ui/Header.h"

namespace mulberry
{

MulberryEditor::MulberryEditor (MulberryProcessor& p)
    : PluginEditor (p, std::make_unique<ui::PanelController> (p), ui::metrics::baseWidth, ui::headerConfig (p),
                    [] (ui::ViewMode mode, myzel::ui::EditorController& c) -> std::unique_ptr<myzel::ui::EditorView>
                    {
                        auto& panelController = static_cast<ui::PanelController&> (c);
                        if (mode == ui::ViewMode::studio)
                            return std::make_unique<ui::StudioPanel> (panelController);
                        return std::make_unique<ui::MainPanel> (panelController);
                    })
{
}

ui::MainPanel& MulberryEditor::getMainPanel() noexcept
{
    auto* panel = dynamic_cast<ui::MainPanel*> (&getView());
    jassert (panel != nullptr);
    return *panel;
}

ui::StudioPanel& MulberryEditor::getStudioPanel() noexcept
{
    auto* studio = dynamic_cast<ui::StudioPanel*> (&getView());
    jassert (studio != nullptr);
    return *studio;
}

} // namespace mulberry
