#include "ui/CeruleanLookAndFeel.h"

#include "ui/Drawing.h"

namespace cerulean::ui
{

namespace
{
    juce::TextLayout layoutTooltip (const juce::String& text, const Fonts& fonts)
    {
        const auto& pal = Theme::palette();
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (text, fonts.label (12.5f), pal.headerLegend);

        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 280.0f);
        return tl;
    }
}

CeruleanLookAndFeel::CeruleanLookAndFeel()
{
    const auto& pal = Theme::palette();

    setColour (juce::PopupMenu::backgroundColourId, pal.headerTop);
    setColour (juce::PopupMenu::textColourId, pal.keyText);
    setColour (juce::PopupMenu::headerTextColourId, pal.headerLegendDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal.ice.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, pal.keyTextActive);

    setColour (juce::TooltipWindow::backgroundColourId, pal.bubbleFill);
    setColour (juce::TooltipWindow::textColourId, pal.headerLegend);
    setColour (juce::TooltipWindow::outlineColourId, pal.bubbleEdge);

    setColour (juce::TextEditor::backgroundColourId, pal.displayGlass);
    setColour (juce::TextEditor::textColourId, pal.displayText);
    setColour (juce::TextEditor::highlightColourId, pal.ice.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, juce::Colour (0x40ffffff));
    setColour (juce::TextEditor::focusedOutlineColourId, pal.ice);
    setColour (juce::CaretComponent::caretColourId, pal.ice);
}

juce::Typeface::Ptr CeruleanLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return fonts->getMedium();

    return LookAndFeel_V4::getTypefaceForFont (font);
}

juce::Font CeruleanLookAndFeel::getPopupMenuFont()
{
    return fonts->label (14.0f);
}

void CeruleanLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto& pal = Theme::palette();
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setGradientFill (juce::ColourGradient (pal.headerTop.brighter (0.12f), 0.0f, 0.0f, pal.headerBottom, 0.0f, (float) height, false));
    g.fillRect (area);
    g.setColour (juce::Colour (0x26ffffff));
    g.drawRect (area, 1.0f);
    g.setColour (pal.ice.withAlpha (0.5f));
    g.fillRect (area.getX() + 1.0f, 0.0f, area.getWidth() - 2.0f, 1.5f);
}

void CeruleanLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight,
                                                     int& idealWidth, int& idealHeight)
{
    LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardHeight, idealWidth, idealHeight);

    if (! isSeparator)
        idealHeight = juce::jmax (idealHeight, 26);

    idealWidth += 18;
}

void CeruleanLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                             bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                             const juce::String& text, const juce::String& shortcutKeyText,
                                             const juce::Drawable*, const juce::Colour* textColour)
{
    const auto& pal = Theme::palette();
    auto r = area.toFloat();

    if (isSeparator)
    {
        const auto y = r.getCentreY();
        g.setColour (juce::Colour (0x80000000));
        g.drawHorizontalLine ((int) y, r.getX() + 10.0f, r.getRight() - 10.0f);
        g.setColour (juce::Colour (0x14ffffff));
        g.drawHorizontalLine ((int) y + 1, r.getX() + 10.0f, r.getRight() - 10.0f);
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (pal.ice.withAlpha (0.16f));
        g.fillRoundedRectangle (r.reduced (3.0f, 1.0f), 3.0f);
        g.setColour (pal.ice);
        g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, r.getHeight() - 10.0f).withPosition (r.getX() + 3.0f, r.getY() + 5.0f), 1.0f);
    }

    auto ink = textColour != nullptr ? *textColour : pal.keyText;

    if (! isActive)
        ink = pal.headerLegendDim.withMultipliedAlpha (0.8f);
    else if (isHighlighted)
        ink = pal.keyTextActive;
    else if (isTicked)
        ink = pal.displayText;

    // Tick: an ice lamp dot.
    auto tickArea = r.removeFromLeft (26.0f);

    if (isTicked)
    {
        const auto c = tickArea.getCentre().translated (2.0f, 0.0f);
        drawing::fillGlow (g, c, 9.0f, pal.ice, 0.6f);
        g.setColour (pal.iceCore);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));
    }

    if (hasSubMenu)
    {
        auto arrowArea = r.removeFromRight (18.0f);
        const auto c = arrowArea.getCentre();
        juce::Path arrow;
        arrow.startNewSubPath (c.x - 2.0f, c.y - 4.0f);
        arrow.lineTo (c.x + 2.0f, c.y);
        arrow.lineTo (c.x - 2.0f, c.y + 4.0f);
        g.setColour (ink.withMultipliedAlpha (0.8f));
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (ink);
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, r.withTrimmedRight (8.0f).toNearestInt(), juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (pal.headerLegendDim);
        g.setFont (fonts->label (12.0f));
        g.drawText (shortcutKeyText, r.withTrimmedRight (10.0f), juce::Justification::centredRight, true);
    }
}

void CeruleanLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    const auto& pal = Theme::palette();
    g.setColour (pal.headerLegendDim);
    g.setFont (drawing::tracked (fonts->legend (10.0f), 0.22f));
    g.drawText (name.toUpperCase(), area.toFloat().withTrimmedLeft (12.0f).withTrimmedTop (4.0f),
                juce::Justification::centredLeft, true);
}

juce::Rectangle<int> CeruleanLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                            juce::Rectangle<int> parentArea)
{
    const auto tl = layoutTooltip (tipText, *fonts);
    const auto w = (int) std::ceil (tl.getWidth() + 22.0f);
    const auto h = (int) std::ceil (tl.getHeight() + 14.0f);

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
        .constrainedWithin (parentArea);
}

void CeruleanLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto& pal = Theme::palette();
    const auto area = juce::Rectangle<float> ((float) width, (float) height);

    g.setColour (pal.bubbleFill);
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (pal.bubbleEdge.withMultipliedAlpha (0.8f));
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

    layoutTooltip (text, *fonts).draw (g, area.reduced (11.0f, 7.0f));
}

void CeruleanLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height), 4.0f);
}

void CeruleanLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
    g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), 4.0f, focused ? 1.4f : 1.0f);
}

} // namespace cerulean::ui
