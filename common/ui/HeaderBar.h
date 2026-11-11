#pragma once

#include "common/plugin/AbCompare.h"
#include "common/plugin/PresetManager.h"
#include "common/ui/HeaderButton.h"
#include "common/ui/InlineDialog.h"
#include "common/ui/PresetDisplay.h"
#include "common/ui/ViewMode.h"

namespace myzel::ui
{

/** What a plugin's header says and offers. */
struct HeaderConfig
{
    juce::String name;                  // printed on the rail, e.g. "UMBER"
    juce::String productName;           // e.g. "Umber", for the settings menu
    juce::String version;               // e.g. "0.1.0"
    juce::String bypassId = "bypass";
    juce::String bypassTooltip;
    juce::String bypassedText, activeText;      // the bypass state, for screen readers
    juce::String settingsTooltip = "Settings: mix lock, window size";
    juce::String mixLockLabel = "Lock Mix";
    juce::String mixLockTooltip;        // on the padlock after Mix while the lock is on
    bool hasViews = false;              // show the PANEL | STUDIO switch

    /** Adds the plugin's own entries at the top of the settings menu. Each item carries its own
        action and an ID from firstPluginItemId up (JUCE only runs actions of items with an ID). */
    std::function<void (juce::PopupMenu&)> addSettings;
    static constexpr int firstPluginItemId = 30000;   // the header's own IDs are below
};

/**
    The header rail: name, preset browser (with SAVE while there are unsaved changes), bypass,
    A/B compare, undo/redo, the view switch (PANEL | STUDIO, plugins with two views) and the
    settings menu (the plugin's own settings, mix lock, window size).

    The editor owns one header and shows it over both views: nothing moves when the view
    changes, so Bypass and A/B stay where the hand expects them. View-specific keys (the panel's
    ADVANCED key) live in the view, next to what they open.
*/
class HeaderBar final : public juce::Component
{
public:
    HeaderBar (plugin::PresetHost& host, plugin::PresetManager& presets, plugin::AbCompare& ab, InlineDialog& dialog, HeaderConfig config);
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

    plugin::PresetHost& host;
    plugin::PresetManager& presets;
    plugin::AbCompare& ab;
    InlineDialog& dialog;
    const HeaderConfig config;

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

} // namespace myzel::ui
