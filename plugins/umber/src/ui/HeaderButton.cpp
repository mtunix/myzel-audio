#include "ui/HeaderButton.h"

#include "ui/Drawing.h"

namespace umber::ui
{

HeaderButton::HeaderButton (const juce::String& t, Icon i)
    : juce::Button (t),
      text (t),
      icon (i)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

void HeaderButton::setLit (bool shouldBeLit)
{
    if (lit != shouldBeLit)
    {
        lit = shouldBeLit;
        repaint();
    }
}

void HeaderButton::setLabel (const juce::String& newText)
{
    if (text != newText)
    {
        text = newText;
        repaint();
    }
}

void HeaderButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& pal = Theme::palette();
    auto area = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = 4.0f;
    const auto enabled = isEnabled();

    // Key: dark rubberised cap on the rail, lit from the top-left.
    juce::Path key;
    key.addRoundedRectangle (area.translated (0.0f, down ? 0.6f : 0.0f), corner);

    g.setColour (juce::Colour (0x70000000));
    g.fillRoundedRectangle (area.translated (0.4f, 1.2f), corner);

    auto top = pal.keyTop.darker (0.35f), bottom = pal.keyBottom.darker (0.45f);

    if (highlighted && enabled)
    {
        top = top.brighter (0.25f);
        bottom = bottom.brighter (0.2f);
    }

    g.setGradientFill (juce::ColourGradient (top, area.getX(), area.getY(), bottom, area.getX(), area.getBottom(), false));
    g.fillPath (key);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0x38ffffff), area.getX(), area.getY(),
                                             juce::Colour (0x00ffffff), area.getX(), area.getY() + 8.0f, false));
    g.strokePath (key, juce::PathStrokeType (0.9f));

    auto ink = lit ? pal.displayText : pal.keyText;

    if (! enabled)
        ink = ink.withMultipliedAlpha (0.32f);
    else if (highlighted && ! lit)
        ink = ink.brighter (0.2f);

    if (lit)
    {
        // A thin amber bar under the legend.
        g.setColour (pal.ember.withAlpha (0.9f));
        g.fillRoundedRectangle (juce::Rectangle<float> (area.getWidth() * 0.45f, 1.6f)
                                    .withCentre ({ area.getCentreX(), area.getBottom() - 3.0f }), 0.8f);
    }

    auto content = area.reduced (6.0f, 0.0f);

    if (showsCaret)
    {
        auto caretArea = content.removeFromRight (7.0f);
        juce::Path caret;
        const auto c = caretArea.getCentre().translated (0.0f, 0.5f);
        caret.addTriangle (c.x - 3.0f, c.y - 1.5f, c.x + 3.0f, c.y - 1.5f, c.x, c.y + 2.0f);
        g.setColour (ink.withMultipliedAlpha (0.7f));
        g.fillPath (caret);
        content.removeFromRight (2.0f);
    }

    if (icon != Icon::none)
    {
        drawIcon (g, content, ink);
    }
    else
    {
        g.setColour (ink);
        g.setFont (drawing::tracked (fonts->legend (11.5f), 0.06f));
        g.drawText (text, content, juce::Justification::centred, false);
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (pal.focus.withAlpha (0.9f));
        g.drawRoundedRectangle (area.expanded (1.5f), corner + 1.5f, 1.2f);
    }
}

void HeaderButton::drawIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) const
{
    const auto c = area.getCentre();
    g.setColour (colour);
    juce::Path p;

    switch (icon)
    {
        case Icon::previous:
        case Icon::next:
        {
            const auto dir = icon == Icon::next ? 1.0f : -1.0f;
            p.startNewSubPath (c.x - 2.5f * dir, c.y - 5.0f);
            p.lineTo (c.x + 2.5f * dir, c.y);
            p.lineTo (c.x - 2.5f * dir, c.y + 5.0f);
            g.strokePath (p, juce::PathStrokeType (1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }

        case Icon::undo:
        case Icon::redo:
        {
            const auto dir = icon == Icon::redo ? -1.0f : 1.0f;
            const auto r = 5.0f;
            // A hooked arrow: an arc over the top, arrowhead at the start.
            p.addCentredArc (c.x, c.y + 1.5f, r, r, 0.0f, -1.9f * dir, 1.6f * dir, true);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const auto tip = drawing::polar ({ c.x, c.y + 1.5f }, r, -1.9f * dir);
            juce::Path head;
            head.addTriangle (tip.x - 3.2f * dir, tip.y - 3.0f, tip.x + 1.8f * dir, tip.y - 0.4f, tip.x - 2.6f * dir, tip.y + 2.6f);
            g.fillPath (head);
            break;
        }

        case Icon::none:
        default:
            break;
    }
}

} // namespace umber::ui
