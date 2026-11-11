#pragma once

#include "ui/Theme.h"

namespace cerulean::ui
{

/**
    The value read-out shown while hovering or dragging a control. On the panel's large knobs it
    sits over the top of the printed scale, so the legend (the control's name) stays readable;
    on the drawer's small knobs it covers their printed value.
*/
class ValueBubble final : public juce::Component
{
public:
    ValueBubble();

    /** Shows text centred on anchor (in the coordinate space of the bubble's parent), at least
        minWidth wide so it fully covers the printed legend underneath. */
    void show (juce::Point<float> anchor, const juce::String& text, float minWidth = 46.0f);
    void hide();

    void paint (juce::Graphics& g) override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    juce::String text;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueBubble)
};

} // namespace cerulean::ui
