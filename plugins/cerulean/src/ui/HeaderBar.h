#pragma once

#include "ui/AbCompare.h"
#include "ui/HeaderButton.h"
#include "ui/InlineDialog.h"
#include "ui/PresetDisplay.h"
#include "ui/PresetManager.h"
#include "ui/ViewMode.h"

namespace cerulean
{
class CeruleanProcessor;
}

namespace cerulean::ui
{

/**
    The header rail: name, preset browser (with SAVE while there are unsaved changes), bypass,
    A/B compare, undo/redo, the view switch (PANEL | STUDIO) and the settings menu (window size).

    Both views use the same header with the same layout: nothing moves when the view changes,
    so Bypass and A/B stay where the hand expects them. View-specific keys (the panel's
    ADVANCED key) live in the view, next to what they open.
*/
class HeaderBar final : public juce::Component
{
public:
    HeaderBar (CeruleanProcessor& processor, PresetManager& presets, AbCompare& ab, InlineDialog& dialog);
    ~HeaderBar() override;

    /** Pulls state that can change behind the UI's back (undo history, preset dirty flag). */
    void refresh();

    std::function<void (float)> onScaleChosen;
    std::function<float()> getScale;

    // Test hooks.
    HeaderButton& getBypassButton() noexcept { return bypassButton; }
    HeaderButton& getSaveButton() noexcept { return saveButton; }
    HeaderButton& getSettingsButton() noexcept { return settingsButton; }

    /** The view switch: lights the current view (the layout never changes). */
    void setView (ViewMode mode);
    std::function<void (ViewMode)> onViewChosen;
    HeaderButton& getViewButton (ViewMode mode) noexcept { return mode == ViewMode::studio ? studioViewButton : panelViewButton; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void showPresetMenu();
    void showSettingsMenu();
    void saveCurrent();
    void promptSaveAs();
    void promptRename (int index);
    void promptDelete (int index);
    juce::PopupMenu::Options menuOptionsFor (juce::Component& target) const;

    CeruleanProcessor& processor;
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
    HeaderButton saveButton { "SAVE" };
    HeaderButton panelViewButton { "PANEL" }, studioViewButton { "STUDIO" };
    ViewMode view = ViewMode::panel;
    HeaderButton settingsButton { "Settings", HeaderButton::Icon::settings };

    juce::SharedResourcePointer<Fonts> fonts;
    drawing::CachedLayer background;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace cerulean::ui
