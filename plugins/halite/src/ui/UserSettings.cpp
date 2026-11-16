#include "ui/UserSettings.h"

#include "common/ui/ViewMode.h"

namespace halite::ui::userSettings
{

namespace
{
    // Keys in the settings file: never rename them (a user's choice would be forgotten).
    const juce::String themeKey { "theme" };
    const juce::String widthKey { "windowWidth" };
    const juce::String heightKey { "windowHeight" };

    juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options o;
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        o.millisecondsBeforeSaving = -1;     // saved explicitly
        return o;
    }

    juce::File file() { return myzel::ui::viewPreference::getSettingsFile ("Halite"); }

    template <typename Fn>
    void write (Fn&& change)
    {
        const auto f = file();
        f.getParentDirectory().createDirectory();
        juce::PropertiesFile props (f, options());
        change (props);
        props.saveIfNeeded();
    }
}

ThemeChoice theme()
{
    const auto f = file();
    if (! f.existsAsFile())
        return ThemeChoice::dark;

    juce::PropertiesFile props (f, options());
    return theme::choiceFromString (props.getValue (themeKey), ThemeChoice::dark);
}

void setTheme (ThemeChoice choice)
{
    write ([choice] (juce::PropertiesFile& p) { p.setValue (themeKey, theme::toString (choice)); });
}

juce::Rectangle<int> windowSize()
{
    const auto f = file();
    if (! f.existsAsFile())
        return {};

    juce::PropertiesFile props (f, options());
    return { props.getIntValue (widthKey, 0), props.getIntValue (heightKey, 0) };
}

void setWindowSize (juce::Rectangle<int> size)
{
    const auto stored = windowSize();
    if (stored.getWidth() == size.getWidth() && stored.getHeight() == size.getHeight())
        return;

    write ([size] (juce::PropertiesFile& p)
    {
        p.setValue (widthKey, size.getWidth());
        p.setValue (heightKey, size.getHeight());
    });
}

} // namespace halite::ui::userSettings
