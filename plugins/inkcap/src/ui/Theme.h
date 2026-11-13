#pragma once

#include "common/ui/Theme.h"

namespace inkcap::ui
{

// The shared kit (dialog, menus, tooltips, fonts, drawing helpers) is used unqualified.
using namespace myzel::ui;

/**
    The glass face's own colours (src/ui/DESIGN.md). The kit's Palette (Theme::palette()) only
    styles what Inkcap borrows from the kit: the inline dialog, popup menus, tooltips and text
    fields. Amber is the display's accent and lives inside the glass only; caution is Bypass.
*/
struct Glass
{
    // The appliance body and its strip
    juce::Colour body, bodyEdgeLight, bodyEdgeShade;
    juce::Colour ink, inkDim, seam;
    juce::Colour keyOff, keyOffLip, keyOn;
    juce::Colour led, ledOff;
    juce::Colour knobLight, knobDark;
    juce::Colour pill, pillText, pillDark, pillDarkText;
    juce::Colour focusOnBody;

    // The dark glass display
    juce::Colour glass, glassEdge;
    juce::Colour text, textDim, textFaint;
    juce::Colour line, track, gridDot, rowSelected, rowHover;
    juce::Colour amber;
    juce::Colour caution;
};

struct Theme : myzel::ui::Theme
{
    static const Glass& glass() noexcept;
};

/**
    The display's monospaced face (JetBrains Mono, SIL OFL 1.1, src/ui/assets/fonts) and the
    kit's Inter for names on the body. Sizes are CSS pixels (the em), as the mock gives them.
*/
class GlassFonts
{
public:
    GlassFonts();

    juce::Font mono (float px) const;
    juce::Font monoMedium (float px) const;
    juce::Font sans (float px) const;        // Inter SemiBold: the name, labels on the body

private:
    juce::Typeface::Ptr regular, medium;
    juce::SharedResourcePointer<Fonts> inter;

    JUCE_DECLARE_NON_COPYABLE (GlassFonts)
};

namespace metrics
{
    using namespace myzel::ui::metrics;

    inline constexpr int baseWidth  = 928;
    inline constexpr int baseHeight = 568;

    // Type, in CSS pixels
    inline constexpr float textSize     = 12.0f;
    inline constexpr float smallSize    = 10.5f;
    inline constexpr float nameSize     = 18.0f;
    inline constexpr float stripLabelSize = 11.0f;

    inline constexpr float displayCorner = 18.0f;
    inline constexpr float padCorner = 8.0f;
    inline constexpr float rowCorner = 4.0f;

    inline constexpr float fineFactor = 0.2f;              // Shift while dragging
    inline constexpr float glideSeconds = 0.12f;           // display values ease to a new value
}

/**
    Every area of the face, in base (100 %) coordinates (the grid in DESIGN.md). The face is the
    whole window; the display and the strip sit inside its 14 px margin.
*/
namespace layout
{
    using R = juce::Rectangle<float>;

    inline constexpr float margin = 14.0f;

    inline R display() { return { margin, margin, 900.0f, 390.0f }; }
    inline R strip()   { return { margin, 418.0f, 900.0f, 136.0f }; }

    // Inside the display (display coordinates): 18 px sides, 16 px top and bottom, 12 px under the header.
    inline R header()      { return { 18.0f, 16.0f, 864.0f, 24.0f }; }
    inline R main()        { return { 18.0f, 52.0f, 864.0f, 322.0f }; }
    inline R pad()         { return { 18.0f, 52.0f, 420.0f, 300.0f }; }
    inline R statusLine()  { return { 18.0f, 358.0f, 420.0f, 16.0f }; }
    inline R rightColumn() { return { 456.0f, 52.0f, 426.0f, 322.0f }; }

    // The right column (its own coordinates): six module rows, the wear matrix at the bottom.
    inline constexpr float rowHeight = 30.0f, rowPitch = 34.0f;
    inline constexpr int wearColumns = 32, wearRows = 3;
    inline constexpr float wearDot = 5.0f, wearGap = 4.0f;
    inline R wear() { return { 0.0f, 278.0f, 426.0f, 44.0f }; }

    // A row's columns (row coordinates), as the mock's flex row: 8 | dot 12 | name 62 | type 70 | bar | value 24 | 8, gaps 10.
    inline constexpr float rowPadX = 8.0f, rowGap = 10.0f;
    inline constexpr float dotWidth = 12.0f, nameWidth = 62.0f, typeWidth = 70.0f, amountWidth = 24.0f;

    // The strip (strip coordinates): keys and knobs of 42 with a label under them, on one line.
    inline constexpr float keyDiameter = 42.0f;
    inline constexpr float stripPadX = 14.0f, stripGap = 18.0f;
    inline constexpr float labelGap = 6.0f, labelHeight = 13.0f;
    inline constexpr float itemTop = (136.0f - (keyDiameter + labelGap + labelHeight)) * 0.5f;
    inline constexpr float pillHeight = 42.0f, pillGap = 8.0f;
    inline constexpr float seamHeight = 52.0f;
}

} // namespace inkcap::ui
