#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halite::ui
{

/** The name and one sentence the status line shows for a control (PLAN.md, Parts, Status line). */
struct HelpText
{
    juce::String title, text;
};

/**
    Where a control reports that it is under the pointer or has keyboard focus: the editor, which
    the control finds among its parents. A control outside an editor (a test) reports nowhere.
*/
class HelpHost
{
public:
    virtual ~HelpHost() = default;

    virtual void showHelp (const juce::Component* source, const HelpText&, bool fromFocus) = 0;
    virtual void hideHelp (const juce::Component* source, bool fromFocus) = 0;
};

/** The help of a control by its parameter ID, or by a name for controls that are not parameters ("learn", "bypass"). */
HelpText helpFor (const juce::String& id);

/** The same sentence with how to act on it after a semicolon ("…; drag the line, double-click to type."). */
HelpText withHint (HelpText, const juce::String& hint);

/** Every id helpFor() knows (the self-test reads each text). */
juce::StringArray helpIds();

} // namespace halite::ui
