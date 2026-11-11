#pragma once

#include "common/plugin/UiState.h"
#include "common/testing/ScratchSession.h"
#include "common/testing/UiTestKit.h"
#include "common/ui/PluginEditor.h"

namespace myzel::testing
{

/** What the shared editor checks need to know about a plugin. */
struct EditorCheckSpec
{
    juce::String pluginName;          // the default-view setting's name
    const char* parameter = nullptr;  // a sound parameter the checks edit; not a lockable one
    juce::StringArray lockableIds;    // the plugin's PresetFormat::lockableIds
};

namespace detail
{
    inline juce::RangedAudioParameter& param (plugin::PresetHost& host, const juce::String& id)
    {
        return *host.getState().getParameter (id);
    }

    /** Moves a parameter well away from where it is (works for bools and choices too). */
    inline void nudge (plugin::PresetHost& host, const juce::String& id)
    {
        auto& p = param (host, id);
        p.setValueNotifyingHost (p.getValue() < 0.5f ? 0.9f : 0.1f);
    }

    inline std::vector<float> valuesOf (plugin::PresetHost& host, const juce::StringArray& ids)
    {
        std::vector<float> values;

        for (const auto& id : ids)
            values.push_back (param (host, id).getValue());

        return values;
    }

    inline juce::String namesOf (plugin::PresetHost& host, const juce::StringArray& ids)
    {
        juce::StringArray names;

        for (const auto& id : ids)
            names.add (param (host, id).getName (64));

        return names.joinIntoString (" and ");
    }

    inline void pumpMessages()
    {
        // Needs JUCE_MODAL_LOOPS_PERMITTED=1 in the tool's build.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
}

/** User presets go to the scratch folder: save, rename, delete; SAVE in the header; stepping. */
inline void checkUserPresets (plugin::PresetHost& host, ui::PluginEditor& editor, const ScratchSession& scratch,
                              const EditorCheckSpec& spec)
{
    std::printf ("User presets\n");
    auto& presets = editor.getController().getPresets();
    auto& header = editor.getHeader();
    const auto folder = scratch.getPresetDirectory();

    check (presets.getUserPresetDirectory() == folder, "user presets live in the scratch folder");

    presets.load (0);
    header.refresh();
    check (! presets.isModified(), "freshly loaded preset is not modified");
    check (! header.getSaveButton().isVisible(), "no SAVE without changes");

    detail::nudge (host, spec.parameter);
    header.refresh();
    check (presets.isModified(), "editing marks the preset modified");
    check (header.getSaveButton().isVisible(), "SAVE appears with unsaved changes");

    const auto saved = presets.saveUserPreset ("Selftest Preset");
    check (saved.wasOk() && presets.isCurrentUserPreset(), "save user preset");
    check (folder.getChildFile ("Selftest Preset.xml").existsAsFile(), "user preset written to the scratch folder");

    const auto idx = presets.getCurrentIndex();
    check (presets.renameUserPreset (idx, "Selftest Renamed").wasOk() && presets.getCurrentName() == "Selftest Renamed",
           "rename user preset");
    check (folder.getChildFile ("Selftest Renamed.xml").existsAsFile() && ! folder.getChildFile ("Selftest Preset.xml").exists(),
           "the file is renamed in the scratch folder");
    check (presets.deleteUserPreset (presets.getCurrentIndex()).wasOk(), "delete user preset");
    check (! presets.userPresetExists ("Selftest Renamed")
           && folder.findChildFiles (juce::File::findFiles, false, "*" + plugin::PresetManager::fileExtension).isEmpty(),
           "file is gone");

    presets.load (0);
    presets.step (1);
    check (presets.getCurrentIndex() == 1, "next preset");
    presets.step (-2);
    check (presets.getCurrentIndex() == (int) presets.getEntries().size() - 1, "previous preset wraps around");
    presets.load (0);
}

/** A/B: each slot recalls its own sound, and the active slot copies to the other. */
inline void checkAbCompare (plugin::PresetHost& host, ui::PluginEditor& editor, const EditorCheckSpec& spec)
{
    std::printf ("A/B compare\n");
    auto& ab = editor.getController().getAb();
    auto& p = detail::param (host, spec.parameter);
    auto near = [&p] (float v) { return std::abs (p.getValue() - v) < 0.01f; };

    ab.selectSlot (0);
    p.setValueNotifyingHost (0.2f);
    const auto a = p.getValue();
    ab.selectSlot (1);
    p.setValueNotifyingHost (0.8f);
    const auto b = p.getValue();
    ab.selectSlot (0);
    check (near (a), "A recalls its own setting");
    ab.selectSlot (1);
    check (near (b), "B recalls its own setting");
    ab.copyActiveToOther();
    ab.selectSlot (0);
    check (near (b), "copy B to A");
}

/** The mix lock: preset loads and A/B leave the lockable parameters alone, and so does the modified dot. */
inline void checkMixLock (plugin::PresetHost& host, ui::PluginEditor& editor, const EditorCheckSpec& spec)
{
    std::printf ("Mix lock\n");
    auto& presets = editor.getController().getPresets();
    auto& ab = editor.getController().getAb();
    const auto& ids = spec.lockableIds;
    const auto names = detail::namesOf (host, ids);
    auto nudgeAll = [&] { for (const auto& id : ids) detail::nudge (host, id); };

    check (presets.canLockMix() && ! ids.isEmpty(), "the plugin has lockable parameters");

    presets.setMixLocked (false);
    presets.load (1);
    const auto loaded = detail::valuesOf (host, ids);
    nudgeAll();
    presets.load (1);
    check (detail::valuesOf (host, ids) == loaded, ("unlocked, a preset load sets " + names).toRawUTF8());

    nudgeAll();
    const auto kept = detail::valuesOf (host, ids);
    presets.setMixLocked (true);
    check (presets.isMixLocked() && (bool) host.getUiState().getProperty (plugin::uistate::mixLock), "the lock is stored in the session");
    presets.load (1);
    check (detail::valuesOf (host, ids) == kept, ("locked, a preset load leaves " + names + " alone").toRawUTF8());

    nudgeAll();
    check (! presets.isModified(), ("the modified dot ignores " + names + " while locked").toRawUTF8());

    const auto beforeAb = detail::valuesOf (host, ids);
    ab.selectSlot (1);
    const auto onB = detail::valuesOf (host, ids);
    ab.selectSlot (0);
    check (onB == beforeAb && detail::valuesOf (host, ids) == beforeAb, "and so does an A/B switch");

    presets.setMixLocked (false);
    check (! presets.isMixLocked(), "unlocked again");
    presets.load (0);
}

/**
    The two views: synchronous and header switching, the header keys, the session's view, the
    default view for new instances, and following a restored session. Starts and ends in the panel.
*/
template <typename Processor, typename Editor>
void checkTwoViews (Processor& processor, Editor& editor, const EditorCheckSpec& spec)
{
    using ui::ViewMode;
    std::printf ("Two views\n");
    auto& presets = editor.getController().getPresets();
    auto& ab = editor.getController().getAb();
    auto storedView = [&] (plugin::PresetHost& h) { return h.getUiState().getProperty (plugin::uistate::view).toString(); };

    check (editor.getViewMode() == ViewMode::panel, "starts in the panel view");
    auto& header = editor.getHeader();
    check (header.getViewButton (ViewMode::panel).isLit() && ! header.getViewButton (ViewMode::studio).isLit(),
           "PANEL is lit in the panel view");

    presets.load (2);
    const auto presetName = presets.getCurrentName();
    const auto abSlot = ab.getActiveSlot();
    const auto sizeBefore = editor.getWidth();
    const auto bypassBounds = header.getBypassButton().getBounds();
    const auto settingsBounds = header.getSettingsButton().getBounds();

    editor.setView (ViewMode::studio);
    check (editor.getViewMode() == ViewMode::studio, "switches to the studio view");
    check (storedView (processor) == ui::toString (ViewMode::studio), "view stored in the session");
    check (ui::viewPreference::getDefault (spec.pluginName) == ViewMode::studio, "the chosen view becomes the default for new instances");
    check (editor.getWidth() == sizeBefore, "switching does not resize the window");
    check (presets.getCurrentName() == presetName && ab.getActiveSlot() == abSlot, "preset and A/B state survive the switch");

    check (header.getBypassButton().getBounds() == bypassBounds
           && header.getSettingsButton().getBounds() == settingsBounds, "the header keys keep their places in both views");
    check (header.getViewButton (ViewMode::studio).isLit() && ! header.getViewButton (ViewMode::panel).isLit(),
           "STUDIO is lit in the studio view");

    // A new editor follows the session's view, or the default when the session has none.
    {
        Processor fresh;
        auto e2 = std::make_unique<Editor> (fresh);
        check (e2->getViewMode() == ViewMode::studio, "a new instance opens in the default view (studio)");
        check (storedView (fresh) == ui::toString (ViewMode::studio), "and writes it into its session");
        fresh.getUiState().setProperty (plugin::uistate::view, ui::toString (ViewMode::panel), nullptr);
        e2.reset();
        e2 = std::make_unique<Editor> (fresh);
        check (e2->getViewMode() == ViewMode::panel, "a session's stored view wins over the default");
    }

    // A host restoring a session while the editor is open: the editor follows it.
    juce::MemoryBlock studioSession;
    processor.getStateInformation (studioSession);
    editor.setView (ViewMode::panel);
    check (editor.getViewMode() == ViewMode::panel, "switches back synchronously");
    processor.setStateInformation (studioSession.getData(), (int) studioSession.getSize());
    detail::pumpMessages();
    check (editor.getViewMode() == ViewMode::studio, "a restored session's view is followed");

    // The header belongs to the editor, not to the view it replaces, so a click switches at once.
    header.getViewButton (ViewMode::panel).onClick();
    check (editor.getViewMode() == ViewMode::panel, "a header click switches to the panel straight away");
    check (ui::viewPreference::getDefault (spec.pluginName) == ViewMode::panel, "and the panel is the default again");
}

/** Every check of the shared editor code, in order. Starts and ends in the panel view. */
template <typename Processor, typename Editor>
void runEditorChecks (Processor& processor, Editor& editor, const ScratchSession& scratch, const EditorCheckSpec& spec)
{
    checkUserPresets (processor, editor, scratch, spec);
    checkAbCompare (processor, editor, spec);
    checkMixLock (processor, editor, spec);
    checkTwoViews (processor, editor, spec);
}

} // namespace myzel::testing
