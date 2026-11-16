#pragma once

#include "ui/Theme.h"

namespace halite::ui
{

/** Styles the stock JUCE pieces Halite uses (popup menus, text fields, the corner resizer) in the workbench theme. */
class WorkbenchLook final : public juce::LookAndFeel_V4
{
public:
    WorkbenchLook();

    /** Re-reads the theme's colours (a theme change). */
    void refresh();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth,
                                    int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 5; }

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

private:
    juce::SharedResourcePointer<Fonts> fonts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WorkbenchLook)
};

} // namespace halite::ui
