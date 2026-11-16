#include "ui/WorkbenchLook.h"

#include "ui/Widgets.h"

namespace halite::ui
{

WorkbenchLook::WorkbenchLook()
{
    refresh();
}

void WorkbenchLook::refresh()
{
    const auto& t = theme::current();

    setColour (juce::PopupMenu::backgroundColourId, t.raised);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::headerTextColourId, t.secondary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.hover);
    setColour (juce::PopupMenu::highlightedTextColourId, t.text);

    setColour (juce::TextEditor::backgroundColourId, t.raised);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::highlightColourId, t.essFill.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.line);
    setColour (juce::TextEditor::focusedOutlineColourId, t.focus);
    setColour (juce::CaretComponent::caretColourId, t.focus);
}

juce::Typeface::Ptr WorkbenchLook::getTypefaceForFont (const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return fonts->defaultSans();

    return LookAndFeel_V4::getTypefaceForFont (font);
}

juce::Font WorkbenchLook::getPopupMenuFont()
{
    return fonts->sans (type::preset);
}

void WorkbenchLook::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto& t = theme::current();
    g.fillAll (t.raised);
    g.setColour (t.line);
    g.drawRect (0, 0, width, height, 1);
}

void WorkbenchLook::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                       bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                       const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour*)
{
    const auto& t = theme::current();

    if (isSeparator)
    {
        g.setColour (t.lineSoft);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (t.hover);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }

    // A tick in the accent marks what is chosen.
    auto tickArea = r.removeFromLeft (22).toFloat();
    if (isTicked)
    {
        g.setColour (t.essText);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (tickArea.getCentre()));
    }

    g.setColour (isActive ? t.text : t.faint);
    g.setFont (getPopupMenuFont());
    g.drawText (text, r.reduced (2, 0), juce::Justification::centredLeft, true);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (t.secondary);
        g.setFont (fonts->mono (type::label));
        g.drawText (shortcutKeyText, r.reduced (8, 0), juce::Justification::centredRight, true);
    }

    if (hasSubMenu)
        draw::icon (g, Icon::chevronRight, r.removeFromRight (16).toFloat().withSizeKeepingCentre (12.0f, 12.0f), t.secondary);
}

void WorkbenchLook::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    const auto& t = theme::current();
    g.setColour (t.secondary);
    g.setFont (fonts->sansMedium (type::caps).withExtraKerningFactor (0.08f));
    g.drawText (sectionName.toUpperCase(), area.reduced (12, 0).withTrimmedTop (4), juce::Justification::centredLeft, true);
}

void WorkbenchLook::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 40;
        idealHeight = 9;
        return;
    }

    idealHeight = 26;
    idealWidth = (int) std::ceil (textWidth (getPopupMenuFont(), text)) + 64;
}

void WorkbenchLook::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height), 5.0f);
}

void WorkbenchLook::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto focused = editor.hasKeyboardFocus (true);
    g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), 5.0f, focused ? 1.5f : 1.0f);
}

void WorkbenchLook::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    const auto& t = theme::current();
    g.setColour (isMouseOver || isMouseDragging ? t.secondary : t.faint);

    // Two short diagonals in the corner, clear of the status line's text.
    for (const auto inset : { 4.0f, 8.0f })
        g.drawLine ((float) w - inset, (float) h - 2.0f, (float) w - 2.0f, (float) h - inset, 1.0f);
}

} // namespace halite::ui
