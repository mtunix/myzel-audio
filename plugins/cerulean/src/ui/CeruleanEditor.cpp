#include "ui/CeruleanEditor.h"

#include "plugin/PluginProcessor.h"

namespace cerulean
{

CeruleanEditor::CeruleanEditor (CeruleanProcessor& p)
    : PluginEditor (p, std::make_unique<ui::PanelController> (p), ui::metrics::baseWidth,
                    [] (ui::ViewMode mode, myzel::ui::EditorController& c) -> std::unique_ptr<myzel::ui::EditorView>
                    {
                        auto& panelController = static_cast<ui::PanelController&> (c);

                        if (mode == ui::ViewMode::studio)
                            return std::make_unique<ui::StudioPanel> (panelController);

                        return std::make_unique<ui::MainPanel> (panelController);
                    })
{
}

ui::MainPanel& CeruleanEditor::getMainPanel() noexcept
{
    auto* panel = dynamic_cast<ui::MainPanel*> (&getView());
    jassert (panel != nullptr);
    return *panel;
}

ui::StudioPanel& CeruleanEditor::getStudioPanel() noexcept
{
    auto* studio = dynamic_cast<ui::StudioPanel*> (&getView());
    jassert (studio != nullptr);
    return *studio;
}

} // namespace cerulean
