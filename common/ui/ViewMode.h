#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace myzel::ui
{

/**
    The two faces of the editor. Both show the same parameters, presets and state; only the
    presentation differs.

    - panel:  the hardware-style instrument face (knobs, keys, the Advanced drawer).
    - studio: a flat, software-style workstation view with every control visible, exact
              numeric entry and metering.
*/
enum class ViewMode
{
    panel = 0,
    studio
};

/** Stored names (session UI state and the settings file). Never rename. */
juce::String toString (ViewMode mode);
ViewMode viewModeFromString (const juce::String& text, ViewMode fallback);

/**
    The view a new editor opens in when its session has none stored yet: the view the user chose
    last, in any instance of the plugin. Kept in userApplicationDataDirectory/Myzel Audio/<plugin>/<plugin>.settings,
    re-read on every call so all open instances agree.
*/
namespace viewPreference
{
    ViewMode getDefault (const juce::String& pluginName);
    void setDefault (const juce::String& pluginName, ViewMode mode);

    juce::File getSettingsFile (const juce::String& pluginName);

    /** Tests: redirect the settings file (an empty File restores the default). */
    void setSettingsFileOverride (const juce::File& file);
}

} // namespace myzel::ui
