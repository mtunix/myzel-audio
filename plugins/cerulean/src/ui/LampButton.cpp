#include "ui/LampButton.h"

namespace cerulean::ui
{

namespace
{
    constexpr float corner = 5.0f;
}

LampButton::LampButton (const juce::String& labelText, Shape s)
    : juce::Button (labelText),
      label (labelText),
      shape (s)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setTitle (labelText);
}

void LampButton::setKeyBounds (juce::Rectangle<float> keyBounds)
{
    setBounds (keyBounds.expanded (margin).toNearestInt());
}

void LampButton::setLabel (const juce::String& newLabel)
{
    if (label != newLabel)
    {
        label = newLabel;
        repaint();
    }
}

void LampButton::setMomentary (bool shouldBeMomentary)
{
    // Switching to latching while held releases the press, so no gesture is left open.
    if (! shouldBeMomentary && momentaryDown)
    {
        momentaryDown = false;

        if (onPress != nullptr)
            onPress (false);
    }

    momentary = shouldBeMomentary;
}

void LampButton::buttonStateChanged()
{
    const auto down = getState() == buttonDown;

    if (! momentary || down == momentaryDown)
        return;

    momentaryDown = down;

    if (down)
        pressedMomentarily = true;

    if (onPress != nullptr)
        onPress (down);
}

void LampButton::setDimmed (bool shouldBeDimmed)
{
    if (dimmed != shouldBeDimmed)
    {
        dimmed = shouldBeDimmed;
        repaint();
    }
}

bool LampButton::tick (float dt)
{
    const auto target = getToggleState() ? 1.0f : 0.0f;
    const auto tau = metrics::lampFadeSeconds / 3.0f;
    level += (target - level) * (1.0f - std::exp (-dt / tau));

    const auto settled = std::abs (target - level) < 0.002f;

    if (settled)
        level = target;

    if (std::abs (level - shownLevel) > 0.003f)
    {
        shownLevel = level;
        repaint();
    }

    return ! settled;
}

void LampButton::snapLamp()
{
    level = getToggleState() ? 1.0f : 0.0f;
    shownLevel = -1.0f;
    repaint();
}

void LampButton::resized()
{
    keyLayer.invalidate();
}

juce::Rectangle<float> LampButton::keyArea() const
{
    return getLocalBounds().toFloat().reduced (margin);
}

juce::Rectangle<float> LampButton::ledArea (juce::Rectangle<float> key) const
{
    if (shape == Shape::wide)
        return juce::Rectangle<float> (3.0f, juce::jmin (16.0f, key.getHeight() - 22.0f)).withCentre ({ key.getX() + 23.0f, key.getCentreY() });

    if (shape == Shape::compact)
        return juce::Rectangle<float> (3.0f, juce::jmin (10.0f, key.getHeight() - 12.0f)).withCentre ({ key.getX() + 11.0f, key.getCentreY() });

    const auto w = juce::jmin (22.0f, key.getWidth() - 24.0f);
    return juce::Rectangle<float> (w, 3.0f).withCentre ({ key.getCentreX(), key.getY() + juce::jmax (9.0f, key.getHeight() * 0.27f) });
}

juce::Rectangle<float> LampButton::labelArea (juce::Rectangle<float> key) const
{
    if (shape == Shape::wide)
        return key.withTrimmedLeft (38.0f).withTrimmedRight (10.0f);

    if (shape == Shape::compact)
        return key.withTrimmedLeft (17.0f).withTrimmedRight (5.0f);

    const auto led = ledArea (key);
    const auto top = led.getBottom() + 2.0f;
    return { key.getX() + 2.0f, top, key.getWidth() - 4.0f, juce::jmin (18.0f, key.getBottom() - top - 3.0f) };
}

void LampButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& pal = Theme::palette();
    const auto key = keyArea();
    const auto lit = juce::jlimit (0.0f, 1.0f, level * (dimmed ? 0.28f : 1.0f));

    juce::Path face;
    face.addRoundedRectangle (key, corner);

    {
        juce::Graphics::ScopedSaveState pressed (g);
        g.addTransform (juce::AffineTransform::translation (0.0f, down ? 1.0f : 0.0f));

        keyLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float>, float)
        {
            renderKey (lg, keyArea());
        });

        if (highlighted || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.02f : 0.04f));
            g.fillPath (face);
        }

        drawLed (g, key, face, lit);

        // Label, printed on the key: brightens slightly when on.
        const auto font = drawing::tracked (fonts->legend (shape == Shape::wide ? 13.0f
                                                                                  : (key.getHeight() < 36.0f ? 9.5f : metrics::keyLabelSize)),
                                            shape == Shape::wide ? 0.22f : 0.10f);
        const auto area = labelArea (key);
        g.setFont (font);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawText (label, area.translated (0.0f, -0.7f), juce::Justification::centred, false);
        g.setColour (pal.keyText.interpolatedWith (pal.keyTextActive, lit).withMultipliedAlpha (dimmed ? 0.55f : 1.0f));
        g.drawText (label, area, juce::Justification::centred, false);
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (pal.focusOnDark);
        g.drawRoundedRectangle (key.expanded (3.0f), corner + 3.0f, 1.6f);
    }
}

void LampButton::drawLed (juce::Graphics& g, juce::Rectangle<float> key, const juce::Path& face, float lit) const
{
    const auto& pal = Theme::palette();
    const auto led = ledArea (key);
    const auto wide = shape != Shape::square;

    // The LED window set into the key: a dark slot when off.
    g.setColour (pal.lensOffRim);
    g.fillRoundedRectangle (led.expanded (1.0f), 2.0f);
    g.setColour (pal.lensOff);
    g.fillRoundedRectangle (led, 1.5f);

    if (lit <= 0.01f)
        return;

    // A little light on the key face around the window.
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (face);
        juce::ColourGradient spill (pal.ice.withAlpha (0.16f * lit), led.getCentre(),
                                    pal.ice.withAlpha (0.0f), led.getCentre().translated (0.0f, wide ? 0.0f : 14.0f), true);
        g.setGradientFill (spill);
        g.fillRect (led.expanded (wide ? 12.0f : 14.0f, wide ? 14.0f : 10.0f));
    }

    g.setColour (pal.ice.withAlpha (lit));
    g.fillRoundedRectangle (led, 1.5f);
    g.setColour (pal.iceCore.withAlpha (0.7f * lit));
    g.fillRoundedRectangle (led.reduced (wide ? 0.8f : 3.0f, wide ? 3.0f : 0.8f), 1.0f);
}

void LampButton::renderKey (juce::Graphics& g, juce::Rectangle<float> key) const
{
    const auto& pal = Theme::palette();

    juce::Path face;
    face.addRoundedRectangle (key, corner);
    drawing::drawContactShadow (g, face, 3.5f);

    // Face: matte black-anodised cap, lit from the top-left.
    g.setGradientFill (juce::ColourGradient (pal.keyTop, key.getX(), key.getY(),
                                             pal.keyBottom, key.getX() + key.getWidth() * 0.35f, key.getBottom(), false));
    g.fillPath (face);

    // Subtle dished top: darker towards the bottom edge.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00000000), key.getCentreX(), key.getCentreY(),
                                             juce::Colour (0x38000000), key.getCentreX(), key.getBottom(), false));
    g.fillPath (face);

    // Bevel: lit upper-left edge, shadowed lower-right edge.
    juce::Path edge;
    edge.addRoundedRectangle (key.reduced (0.5f), corner - 0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x60ffffff), key.getX(), key.getY(),
                                             juce::Colour (0x00ffffff), key.getX() + 18.0f, key.getY() + 18.0f, false));
    g.strokePath (edge, juce::PathStrokeType (1.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00000000), key.getRight() - 18.0f, key.getBottom() - 18.0f,
                                             juce::Colour (0xb0000000), key.getRight(), key.getBottom(), false));
    g.strokePath (edge, juce::PathStrokeType (1.2f));
}

} // namespace cerulean::ui
