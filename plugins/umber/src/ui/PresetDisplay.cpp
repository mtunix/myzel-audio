#include "ui/PresetDisplay.h"

namespace umber::ui
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
        setDescription ("Current preset: " + name + (modified ? " (modified)" : ""));
        repaint();
    }
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

    // Category tag on the left.
    if (category.isNotEmpty())
    {
        const auto font = drawing::tracked (fonts->legend (8.5f), 0.22f);
        const auto tag = category.toUpperCase();
        const auto w = juce::GlyphArrangement::getStringWidth (font, tag) + 4.0f;
        auto tagArea = content.removeFromLeft (juce::jmin (w, 96.0f));
        g.setColour (pal.displayTextDim);
        g.setFont (font);
        g.drawText (tag, tagArea, juce::Justification::centredLeft, true);
        content.removeFromLeft (6.0f);
    }

    const auto shown = name + (modified ? " *" : "");
    const auto font = fonts->label (15.0f);
    const auto textArea = content.withTrimmedRight (4.0f);

    g.setFont (font);
    g.setColour (highlighted ? pal.displayText.brighter (0.15f) : pal.displayText);
    g.drawText (shown, textArea, juce::Justification::centred, true);

    if (hasKeyboardFocus (false))
    {
        g.setColour (pal.focus.withAlpha (0.9f));
        g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.2f);
    }
}

} // namespace umber::ui
