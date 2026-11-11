#include "ui/ValueBubble.h"

#include "ui/Drawing.h"

namespace cerulean::ui
{

namespace
{
    constexpr float textHeight = 12.5f;
    constexpr float padX = 9.0f;
    constexpr float height = 21.0f;
}

ValueBubble::ValueBubble()
{
    setInterceptsMouseClicks (false, false);
    setVisible (false);
    setAlwaysOnTop (true);
}

void ValueBubble::show (juce::Point<float> anchor, const juce::String& newText, float minWidth)
{
    const auto width = juce::GlyphArrangement::getStringWidth (drawing::tracked (fonts->label (textHeight), 0.02f), newText)
                       + 2.0f * padX;
    const auto bounds = juce::Rectangle<float> (juce::jmax (minWidth, width), height).withCentre (anchor);

    if (newText != text || bounds.toNearestInt() != getBounds())
    {
        text = newText;
        setBounds (bounds.toNearestInt());
        repaint();
    }

    if (! isVisible())
        setVisible (true);

    toFront (false);
}

void ValueBubble::hide()
{
    setVisible (false);
}

void ValueBubble::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palette();
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = area.getHeight() * 0.5f;

    g.setColour (pal.bubbleFill);
    g.fillRoundedRectangle (area, corner);

    g.setGradientFill (juce::ColourGradient (pal.bubbleEdge, area.getX(), area.getY(),
                                             pal.bubbleEdge.withMultipliedAlpha (0.35f), area.getX(), area.getBottom(), false));
    g.drawRoundedRectangle (area, corner, 1.0f);

    g.setColour (pal.displayText);
    g.setFont (drawing::tracked (fonts->label (textHeight), 0.02f));
    g.drawText (text, area, juce::Justification::centred, false);
}

} // namespace cerulean::ui
