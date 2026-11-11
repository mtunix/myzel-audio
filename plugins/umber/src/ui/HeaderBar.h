#pragma once

#include "ui/AbCompare.h"
#include "ui/HeaderButton.h"
#include "ui/InlineDialog.h"
#include "ui/PresetDisplay.h"
#include "ui/PresetManager.h"

namespace umber
{
class UmberProcessor;
}

namespace umber::ui
{

/**
    The header rail: name, preset browser, bypass, A/B compare, undo/redo, quality and scale menus.
*/
class HeaderBar final : public juce::Component
{
public:
    HeaderBar (UmberProcessor& processor, PresetManager& presets, AbCompare& ab, InlineDialog& dialog);
    ~HeaderBar() override;

    /** Pulls state that can change behind the UI's back (undo history, oversampling, preset dirty flag). */
    void refresh();

    std::function<void (float)> onScaleChosen;
    std::function<float()> getScale;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void showPresetMenu();
    void showQualityMenu();
    void showScaleMenu();
    void promptSaveAs();
    void promptRename (int index);
    void promptDelete (int index);
    void setChoice (const char* parameterId, int index);
    juce::PopupMenu::Options menuOptionsFor (juce::Component& target) const;

    UmberProcessor& processor;
    PresetManager& presets;
    AbCompare& ab;
    InlineDialog& dialog;

    HeaderButton prevButton { "Previous preset", HeaderButton::Icon::previous };
    HeaderButton nextButton { "Next preset", HeaderButton::Icon::next };
    PresetDisplay presetDisplay;
    HeaderButton bypassButton { "BYPASS" };
    std::unique_ptr<juce::ParameterAttachment> bypassAttachment;
    HeaderButton slotA { "A" }, slotB { "B" }, copyButton { "A>B" };
    HeaderButton undoButton { "Undo", HeaderButton::Icon::undo };
    HeaderButton redoButton { "Redo", HeaderButton::Icon::redo };
    HeaderButton qualityButton { "4x" };
    HeaderButton scaleButton { "100%" };

    juce::SharedResourcePointer<Fonts> fonts;
    drawing::CachedLayer background;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace umber::ui
