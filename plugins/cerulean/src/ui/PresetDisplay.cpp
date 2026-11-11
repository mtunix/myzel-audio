#include "ui/PresetDisplay.h"

namespace cerulean::ui
{

PresetDisplay::PresetDisplay()
    : juce::Button ("Preset")
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setTitle ("Preset");
    setTooltip ("Presets: click for the preset menu (load, save, rename, delete).");
}

void PresetDisplay::setPreset (const juce::String& newName, const juce::String& newCategory, bool isModified)
{
    if (newName != name || newCategory != category || isModified != modified)
    {
        name = newName;
        category = newCategory;
        modified = isModified;
        setDescription ("Current preset: " + name + (category.isNotEmpty() ? " (" + category + ")" : juce::String()) + (modified ? ", modified" : ""));
        repaint();
    }
}

void PresetDisplay::setReservedRight (float width)
{
    if (! juce::exactlyEqual (reservedRight, width))
    {
        reservedRight = width;
        repaint();
    }
}

juce::Rectangle<float> PresetDisplay::reservedArea() const
{
    // Just inside the caret, vertically centred.
    auto area = getLocalBounds().toFloat().reduced (12.0f, 0.0f).withTrimmedRight (10.0f + 6.0f);
    return area.removeFromRight (reservedRight).withSizeKeepingCentre (reservedRight, 20.0f);
}

void PresetDisplay::resized()
{
    glassLayer.invalidate();
}

void PresetDisplay::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto& pal = Theme::palette();
    const auto area = getLocalBounds().toFloat();

    glassLayer.draw (g, area, [] (juce::Graphics& lg, juce::Rectangle<float> a, float)
    {
        const auto& p = Theme::palette();
        const auto glass = a.reduced (1.0f);

        drawing::drawRecess (lg, glass, 4.0f, 2.5f);
        lg.setColour (p.displayGlass.withAlpha (0.92f));
        lg.fillRoundedRectangle (glass.reduced (1.0f), 3.0f);
    });

    auto content = area.reduced (12.0f, 0.0f);

    // Caret on the right.
    {
        auto caretArea = content.removeFromRight (10.0f);
        const auto c = caretArea.getCentre();
        juce::Path caret;
        caret.addTriangle (c.x - 3.5f, c.y - 1.8f, c.x + 3.5f, c.y - 1.8f, c.x, c.y + 2.4f);
        g.setColour (pal.displayTextDim.brighter (highlighted ? 0.6f : 0.0f));
        g.fillPath (caret);
    }

    if (reservedRight > 0.0f)
        content.removeFromRight (reservedRight + 10.0f);

    // Category on the left, as a breadcrumb: "HALLS & PLATES ›".
    if (category.isNotEmpty())
    {
        const auto font = drawing::tracked (fonts->legend (9.5f), 0.16f);
        const auto tag = category.toUpperCase() + juce::String::fromUTF8 ("  \xe2\x80\xba");
        const auto tagWidth = juce::jmin (juce::GlyphArrangement::getStringWidth (font, tag) + 4.0f, 128.0f);
        auto tagArea = content.removeFromLeft (tagWidth);
        g.setColour (pal.displayTextDim.brighter (0.25f));
        g.setFont (font);
        g.drawText (tag, tagArea, juce::Justification::centredLeft, true);
        content.removeFromLeft (8.0f);
    }

    // The name, centred in the field (not in what is left), unless that would collide.
    const auto font = fonts->label (15.0f);
    const auto nameWidth = juce::GlyphArrangement::getStringWidth (font, name);
    const auto dotSpace = modified ? 14.0f : 0.0f;
    auto textArea = juce::Rectangle<float> (nameWidth + dotSpace + 2.0f, area.getHeight())
                        .withCentre ({ area.getCentreX(), area.getCentreY() });

    if (textArea.getX() < content.getX() || textArea.getRight() > content.getRight())
        textArea = textArea.withX (content.getX() + juce::jmax (0.0f, (content.getWidth() - textArea.getWidth()) * 0.5f));

    textArea = textArea.getIntersection (content);

    g.setFont (font);
    g.setColour (highlighted ? pal.displayText.brighter (0.15f) : pal.displayText);
    g.drawText (name, textArea.withTrimmedRight (dotSpace), juce::Justification::centredLeft, true);

    // Unsaved changes: a small lit ice dot after the name.
    if (modified)
    {
        const auto shownWidth = juce::jmin (nameWidth, textArea.getWidth() - dotSpace);
        const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ textArea.getX() + shownWidth + 9.0f, area.getCentreY() });
        drawing::fillGlow (g, dot.getCentre(), 7.0f, pal.ice, 0.45f);
        g.setColour (pal.ice);
        g.fillEllipse (dot);
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (pal.focusOnDark.withAlpha (0.9f));
        g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.2f);
    }
}

} // namespace cerulean::ui
