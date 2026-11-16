#pragma once

#include "plugin/LearnControl.h"
#include "ui/ControlBar.h"
#include "ui/DisplayParts.h"
#include "ui/DisplayToolbar.h"
#include "ui/Header.h"
#include "ui/StatusLine.h"
#include "ui/WorkbenchLook.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace halite
{

class HaliteProcessor;

namespace ui
{
    /** Shared by every open Halite editor in the process: a theme change in one restyles them all. */
    struct ThemeEvents final : juce::ChangeBroadcaster {};
}

/**
    Halite's editor: the workbench (src/ui/DESIGN.md). Its own editor on the shared EditorController
    (PLAN.md, Settled 63), not on the shared PluginEditor: its own header and a free layout computed
    from the window's real size, from 720 × 420 with no fixed aspect. It brings what PluginEditor
    would: the frame clock, the session listener and an in-editor dialog. The window size and the theme
    are the user's settings (UserSettings), never the session's.
*/
class HaliteEditor final : public juce::AudioProcessorEditor,
                           public ui::HelpHost,
                           private ui::HaliteController::Listener,
                           private juce::ValueTree::Listener,
                           private juce::ChangeListener,
                           private juce::DarkModeSettingListener,
                           private juce::Timer,
                           private juce::AsyncUpdater
{
public:
    explicit HaliteEditor (HaliteProcessor&);
    ~HaliteEditor() override;

    /** Every area, computed from the window's size (DESIGN.md, Layout; Breakpoints). */
    struct Layout
    {
        bool compact = false, sidebarDocked = false, sidebarOverlay = false;
        ui::ControlBar::Mode barMode = ui::ControlBar::Mode::wide;
        juce::Rectangle<int> header, toolbar, main, spectrum, meters, sidebar, advanced, controlBar, status, display;
    };

    const Layout& getLayout() const noexcept { return layout; }

    static constexpr int minWidth = 720, minHeight = 420, maxWidth = 3840, maxHeight = 2400;
    static constexpr int dockWidth = 1000;          // narrower: the sidebar collapses, the toggle opens it as an overlay
    static constexpr int compactHeight = 600;       // lower: smaller knobs and rails
    static constexpr int minSpectrum = 120;

    struct WindowSize
    {
        const char* name;
        int width, height;
    };

    static constexpr WindowSize windowSizes[] { { "S", 960, 576 }, { "M", 1200, 720 }, { "L", 1440, 864 }, { "XL", 1680, 1008 } };

    /** One frame of the frame clock (called on vblank; the snapshot tool drives it directly). */
    void animateFrame (double timeSeconds);

    ui::HaliteController& getController() noexcept { return *controller; }
    ui::Header& getHeader() noexcept { return header; }
    ui::DisplayToolbar& getToolbar() noexcept { return toolbar; }
    ui::MainView& getMainView() noexcept { return mainView; }
    ui::SpectrumView& getSpectrum() noexcept { return spectrum; }
    ui::MeterStrip& getMeters() noexcept { return meters; }
    ui::DisplayModel& getModel() noexcept { return model; }
    ui::EventsSidebar& getSidebar() noexcept { return sidebar; }
    ui::AdvancedRow& getAdvanced() noexcept { return advanced; }
    ui::ControlBar& getControlBar() noexcept { return controlBar; }
    ui::StatusLine& getStatusLine() noexcept { return statusLine; }
    ui::Dialog& getDialog() noexcept { return dialog; }

    // The menus, and what choosing an item does (the self-test calls both).
    juce::PopupMenu settingsMenu() const;
    void performSettingsItem (int itemId);
    juce::PopupMenu presetMenu() const;
    void performPresetItem (int itemId);
    juce::PopupMenu zoomMenu() const;
    void performZoomItem (int itemId);
    juce::PopupMenu abMenu() const;
    void performAbItem (int itemId);

    static constexpr int latencyItem = 100, themeItem = 200, sizeItem = 300, helpItem = 400, abCopyItem = 500;
    static constexpr int saveAsItem = 10001, saveItem = 10002, renameItem = 10003, deleteItem = 10004;

    /** The events toggle: the stored choice when the sidebar can dock, a passing overlay when it cannot. */
    void toggleSidebar();
    bool isSidebarShowing() const noexcept { return layout.sidebarDocked || layout.sidebarOverlay; }

    void setThemeChoice (ui::ThemeChoice);
    void setWindowSize (int width, int height);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // HelpHost
    void showHelp (const juce::Component*, const ui::HelpText&, bool fromFocus) override;
    void hideHelp (const juce::Component*, bool fromFocus) override;

private:
    void uiChanged() override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void darkModeSettingChanged() override;
    void timerCallback() override;
    void handleAsyncUpdate() override;

    void applyTheme();
    void followRestoredSession();
    void computeLayout();
    void showMenu (juce::Component& target, juce::PopupMenu, std::function<void (int)>);
    void openHelp();
    void savePresetAs();
    void followLearn();

    HaliteProcessor& processor;
    ui::WorkbenchLook look;                                 // declared first: outlives every child
    std::unique_ptr<ui::HaliteController> controller;      // outlives the parts
    ui::DisplayModel model;                                 // the parts' one reader of the display feed

    ui::Header header;
    ui::DisplayToolbar toolbar;
    ui::MainView mainView;
    ui::SpectrumView spectrum;
    ui::MeterStrip meters;
    ui::EventsSidebar sidebar;
    ui::AdvancedRow advanced;
    ui::ControlBar controlBar;
    ui::StatusLine statusLine;
    ui::Dialog dialog;

    juce::SharedResourcePointer<ui::ThemeEvents> themeEvents;
    Layout layout;
    bool overlayOpen = false;
    LearnState shownLearn = LearnState::idle;

    juce::VBlankAttachment vblank;
    double lastFrameTime = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HaliteEditor)
};

} // namespace halite
