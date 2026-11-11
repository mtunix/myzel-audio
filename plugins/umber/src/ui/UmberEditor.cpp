#include "ui/UmberEditor.h"

#include "plugin/PluginProcessor.h"
#include "ui/Header.h"

namespace umber
{

UmberEditor::UmberEditor (UmberProcessor& p)
    : PluginEditor (p, std::make_unique<myzel::ui::EditorController> (p, p.getPresets()), ui::metrics::baseWidth, ui::headerConfig (p),
                    [&p] (ui::ViewMode mode, myzel::ui::EditorController& c) -> std::unique_ptr<myzel::ui::EditorView>
                    {
                        if (mode == ui::ViewMode::studio)
                            return std::make_unique<ui::StudioPanel> (c, p);

                        return std::make_unique<ui::MainPanel> (c, p);
                    })
{
}

ui::MainPanel& UmberEditor::getMainPanel() noexcept
{
    auto* panel = dynamic_cast<ui::MainPanel*> (&getView());
    jassert (panel != nullptr);
    return *panel;
}

ui::StudioPanel& UmberEditor::getStudioPanel() noexcept
{
    auto* studio = dynamic_cast<ui::StudioPanel*> (&getView());
    jassert (studio != nullptr);
    return *studio;
}

} // namespace umber
