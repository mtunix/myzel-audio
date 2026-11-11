#pragma once

#include "ui/Theme.h"

namespace umber::ui
{

/**
    The value read-out shown while hovering or dragging a control. It sits over the control's
    printed legend, so the legend appears to light up with the current value.
*/
class ValueBubble final : public juce::Component
{
public:
    ValueBubble();

    /** Shows text centred on anchor (in the coordinate space of the bubble's parent). */
    void show (juce::Point<float> anchor, const juce::String& text);
    void hide();

    void paint (juce::Graphics& g) override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    juce::String text;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueBubble)
};

} // namespace umber::ui
