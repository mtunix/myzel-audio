#include "ui/HeaderButton.h"

#include "ui/Drawing.h"

namespace cerulean::ui
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

void HeaderButton::setCaution (bool shouldUseCaution)
{
    if (caution != shouldUseCaution)
    {
        caution = shouldUseCaution;
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
    const auto pressed = area.translated (0.0f, down ? 0.6f : 0.0f);
    const auto leftRound = segment != Segment::right, rightRound = segment != Segment::left;
    juce::Path key, shadow;
    key.addRoundedRectangle (pressed.getX(), pressed.getY(), pressed.getWidth(), pressed.getHeight(), corner, corner,
                             leftRound, rightRound, leftRound, rightRound);
    shadow.addRoundedRectangle (area.getX() + 0.4f, area.getY() + 1.2f, area.getWidth(), area.getHeight(), corner, corner,
                                leftRound, rightRound, leftRound, rightRound);

    g.setColour (juce::Colour (0x70000000));
    g.fillPath (shadow);

    const auto amber = lit && caution;
    auto top = pal.keyTop.darker (0.35f), bottom = pal.keyBottom.darker (0.45f);

    if (amber)
    {
        // A warm cap, so a bypassed plugin is obvious from across the room.
        top = pal.cautionDeep.brighter (0.25f);
        bottom = pal.cautionDeep;
    }

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

    auto ink = amber ? pal.caution.brighter (0.5f) : (lit ? pal.displayText : pal.keyText);

    if (! enabled)
        ink = ink.withMultipliedAlpha (0.32f);
    else if (highlighted && ! lit)
        ink = ink.brighter (0.2f);

    if (lit)
    {
        // A thin bar under the legend: ice when on, amber for caution states.
        const auto bar = juce::Rectangle<float> (area.getWidth() * (amber ? 0.62f : 0.45f), 1.6f)
                             .withCentre ({ area.getCentreX(), area.getBottom() - 3.0f });
        g.setColour ((amber ? pal.caution : pal.ice).withAlpha (0.9f));
        g.fillRoundedRectangle (bar, 0.8f);

        if (amber)
            drawing::fillGlow (g, bar.getCentre(), bar.getWidth() * 0.6f, pal.caution, 0.35f);
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
        g.setColour (pal.focusOnDark.withAlpha (0.9f));
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

        case Icon::settings:
        {
            // A small gear: a ring with eight teeth.
            const auto r = 4.2f;
            g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c), 1.5f);

            for (int i = 0; i < 8; ++i)
            {
                const auto a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                const auto p0 = drawing::polar (c, r + 0.6f, a), p1 = drawing::polar (c, r + 2.8f, a);
                g.drawLine ({ p0, p1 }, 2.0f);
            }

            break;
        }

        case Icon::none:
        default:
            break;
    }
}

} // namespace cerulean::ui
