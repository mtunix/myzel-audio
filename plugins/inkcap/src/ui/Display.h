#pragma once

#include "common/ui/Drawing.h"
#include "common/ui/InlineDialog.h"
#include "ui/GlassWidgets.h"
#include "ui/InkcapController.h"
#include "ui/ModuleList.h"
#include "ui/ModulePage.h"
#include "ui/PresetsPage.h"
#include "ui/XyPad.h"

namespace inkcap::ui
{

/**
    The status line under the pad: Damage and Chaos (amber, double-click to type), and on the
    right the seed line: whether the clock follows the host, the seed's word and code, and the
    padlock while it is locked. A click on the seed line opens the seed menu. Short messages
    ("seed copied") and the strip's read-outs take the seed line's place for a moment.
*/
class StatusLine final : public juce::Component
{
public:
    explicit StatusLine (InkcapController&);

    void update();

    /** The seed line, a button of its own (keyboard, screen readers). */
    juce::Button& getSeedButton() noexcept;
    std::function<void()> onSeedClicked;

    void openEntry (const char* parameterId);
    TypedEntry& getEntry() noexcept { return entry; }

    juce::String getSeedText() const { return seedText; }
    juce::String getMessage() const { return message; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr float chaosX = 124.0f;

private:
    class SeedButton;

    juce::Rectangle<float> valueArea (const char* parameterId) const;

    InkcapController& controller;
    juce::SharedResourcePointer<GlassFonts> fonts;
    std::unique_ptr<SeedButton> seedButton;
    TypedEntry entry;

    int damage = -1, chaos = -1;
    juce::String seedText, message;
    bool locked = false, following = false, messageIsHint = false;
};

/**
    The dark glass display: the header (name, preset, soft keys), and the page shown below it:
    the pad with the module list and the wear matrix, a module's page beside the pad, or the
    preset browser over the whole display. Owns the seed menu and the settings (⚙) menu.
    Keyboard while it has focus: Ctrl/Cmd+C and +V copy and paste the seed, Ctrl/Cmd+Z undoes,
    Shift+Ctrl/Cmd+Z redoes, Escape closes a page.
*/
class Display final : public juce::Component
{
public:
    Display (InkcapController&, myzel::ui::InlineDialog&);
    ~Display() override;

    /** One frame: pull every value that may have changed and repaint only what did. */
    void update();

    /** The host restored a session: follow its page. */
    void sessionRestored();

    void openModulePage (int moduleIndex);   // -1: the module list
    void setPresetsOpen (bool open);

    std::function<float()> getScale;
    std::function<void (float)> onScaleChosen;

    // Settings and seed menu items: what each does is performMenuItem's, so the self-test runs them.
    enum MenuItem
    {
        matchItem = 1,
        bpmItem,
        mixLockItem,
        seedLockItem,
        seedCopyItem,
        seedPasteItem,
        seedWordItem,
        seedRerollItem,
        abCopyItem,
        redoItem,
        scaleItem = 100,          // + index into scales
        historyItem = 200         // + index into the seed history
    };
    void performMenuItem (int itemId);
    juce::PopupMenu settingsMenu();
    juce::PopupMenu seedMenu();

    static constexpr std::array<float, 6> scales { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

    // Parts, for the editor's self-test.
    XyPad& getPad() noexcept { return pad; }
    StatusLine& getStatusLine() noexcept { return status; }
    ModuleList& getModuleList() noexcept { return modules; }
    ModulePage& getModulePage() noexcept { return page; }
    WearMatrix& getWear() noexcept { return wear; }
    PresetsPage& getPresetsPage() noexcept { return presetsPage; }
    SoftKey& getPresetsKey() noexcept { return presetsKey; }
    SoftKey& getAbKey() noexcept { return abKey; }
    SoftKey& getUndoKey() noexcept { return undoKey; }
    SoftKey& getBypassKey() noexcept { return bypassKey; }
    SoftKey& getSettingsKey() noexcept { return settingsKey; }
    juce::Button& getPresetButton() noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType cause) override;
    void focusLost (FocusChangeType) override;

private:
    class PresetButton;

    void showPage();
    void showMenu (juce::PopupMenu, juce::Component& target, int minimumWidth);
    void promptWord();
    void promptBpm();
    void refreshHeader();

    InkcapController& controller;
    myzel::ui::InlineDialog& dialog;
    juce::SharedResourcePointer<GlassFonts> fonts;
    drawing::CachedLayer glassLayer;

    std::unique_ptr<PresetButton> presetButton;
    SoftKey presetsKey { "presets", "Presets" };
    SoftKey abKey { "a/b", "Compare A and B" };
    SoftKey undoKey { "undo", "Undo" };
    SoftKey bypassKey { "bypass", "Bypass" };
    SoftKey settingsKey { "", "Settings", SoftKey::Icon::gear };

    XyPad pad;
    StatusLine status;
    ModuleList modules;
    WearMatrix wear;
    ModulePage page;
    PresetsPage presetsPage;

    int shownPage = -2;
    bool shownPresets = false;
    juce::String presetName, presetNumber;
    bool presetModified = false;
    bool keyboardFocused = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Display)
};

} // namespace inkcap::ui
