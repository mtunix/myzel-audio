#include "ui/ViewMode.h"

namespace cerulean::ui
{

namespace
{
    const juce::String defaultViewKey { "defaultView" };

    juce::File& settingsOverride()
    {
        static juce::File file;
        return file;
    }

    juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        o.millisecondsBeforeSaving = -1;     // saved explicitly
        return o;
    }
}

juce::String toString (ViewMode mode)
{
    return mode == ViewMode::studio ? "studio" : "panel";
}

ViewMode viewModeFromString (const juce::String& text, ViewMode fallback)
{
    if (text == "studio")
        return ViewMode::studio;

    if (text == "panel")
        return ViewMode::panel;

    return fallback;
}

namespace viewPreference
{
    juce::File getSettingsFile()
    {
        if (settingsOverride() != juce::File())
            return settingsOverride();

        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Myzel Audio")
            .getChildFile ("Cerulean")
            .getChildFile ("Cerulean.settings");
    }

    void setSettingsFileOverride (const juce::File& file)
    {
        settingsOverride() = file;
    }

    ViewMode getDefault()
    {
        const auto file = getSettingsFile();

        if (! file.existsAsFile())
            return ViewMode::panel;

        juce::PropertiesFile props (file, settingsOptions());
        return viewModeFromString (props.getValue (defaultViewKey), ViewMode::panel);
    }

    void setDefault (ViewMode mode)
    {
        const auto file = getSettingsFile();
        file.getParentDirectory().createDirectory();

        juce::PropertiesFile props (file, settingsOptions());

        if (props.getValue (defaultViewKey) == toString (mode))
            return;

        props.setValue (defaultViewKey, toString (mode));
        props.saveIfNeeded();
    }
}

} // namespace cerulean::ui
