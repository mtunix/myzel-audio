#pragma once

#include "ui/Theme.h"

namespace halite::ui
{

/**
    The user's own settings, shared by every Halite instance and never part of a session or preset:
    the theme and the window size (PLAN.md, Colours and themes; Behaviour). Kept in the family's
    settings file (userApplicationDataDirectory/Myzel Audio/Halite/Halite.settings, which the snapshot
    tool's scratch session redirects) and re-read on every call, so open instances agree.
*/
namespace userSettings
{
    ThemeChoice theme();
    void setTheme (ThemeChoice);

    /** The window size last used; empty when none is stored. */
    juce::Rectangle<int> windowSize();
    void setWindowSize (juce::Rectangle<int>);
}

} // namespace halite::ui
