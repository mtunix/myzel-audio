#pragma once

#include "common/plugin/PresetManager.h"
#include "common/ui/ViewMode.h"

/**
    Redirects every file a plugin writes for the user (user presets, the default-view setting) to
    a fresh temporary folder, and deletes it again. Create it before any processor exists: the
    PresetManager scans the preset folder in its constructor.
*/
namespace myzel::testing
{

class ScratchSession
{
public:
    explicit ScratchSession (const juce::String& pluginName)
        : directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile (pluginName.toLowerCase() + "-ui-scratch", {}, false))
    {
        directory.createDirectory();
        plugin::PresetManager::setUserPresetDirectoryOverride (getPresetDirectory());
        ui::viewPreference::setSettingsFileOverride (directory.getChildFile (pluginName + ".settings"));
    }

    ~ScratchSession()
    {
        plugin::PresetManager::setUserPresetDirectoryOverride ({});
        ui::viewPreference::setSettingsFileOverride ({});
        directory.deleteRecursively();
    }

    const juce::File& getDirectory() const noexcept { return directory; }
    juce::File getPresetDirectory() const { return directory.getChildFile ("Presets"); }

private:
    const juce::File directory;

    JUCE_DECLARE_NON_COPYABLE (ScratchSession)
};

} // namespace myzel::testing
