#pragma once

#include "common/ui/Theme.h"

namespace cerulean::ui
{

using myzel::ui::Fonts;
using myzel::ui::Palette;

/**
    Colours of Cerulean's own displays: the tail display and the Advanced drawer. The kit colours
    (a deep cerulean panel with pale silkscreen, graphite knobs, a navy header rail and a single
    ice-blue accent) are Theme::palette(). See src/ui/DESIGN.md.
*/
struct Extras
{
    // Tail display
    juce::Colour bezel;
    juce::Colour screenTop, screenBottom, screenGrid, screenText, screenTextDim;
    juce::Colour traceMid, traceBand, liveFill, liveLine;

    // Advanced drawer
    juce::Colour drawerValue;
};

struct StudioPalette;

/** The palettes: the kit's (Theme::palette()), the panel displays' and the studio view's. */
struct Theme : myzel::ui::Theme
{
    static const Extras& extras() noexcept;
    static const StudioPalette& studio() noexcept;
};

/** Dimensions in base (100 %) editor coordinates (the shared ones come from common/ui/Theme.h). */
namespace metrics
{
    using namespace myzel::ui::metrics;

    inline constexpr int baseWidth  = 1088;
    inline constexpr int baseHeight = 580;

    // Advanced drawer: smaller type, still >= 7.5 device px at 75 %.
    inline constexpr float drawerLegendSize = 11.0f;
    inline constexpr float drawerValueSize  = 11.5f;
}

/** Fixed positions of every control, in base coordinates (the layout grid). */
namespace layout
{
    using R = juce::Rectangle<float>;

    // Columns: left space (x 28..428), centre decay (444..772), right display/output (788..1060),
    // separated by grooves at x 436 and 780.
    inline constexpr float leftGrooveX = 436.0f, rightGrooveX = 780.0f;
    inline constexpr float grooveTop = 80.0f, grooveBottom = 404.0f;
    inline constexpr float centreX = 608.0f;

    inline R header()        { return { 0.0f, 0.0f, (float) metrics::baseWidth, metrics::headerHeight }; }

    inline juce::Point<float> decayCentre()  { return { centreX, 196.0f }; }
    inline constexpr float decayDiameter = 196.0f;

    // Left block, a 3 x 2 grid. Top row is the space (pre-delay, size, modulation), bottom row
    // the character (tone and the two per-algorithm knobs, next to the algorithm keys).
    inline constexpr float colA = 100.0f, colB = 228.0f, colC = 356.0f;
    inline constexpr float topRowY = 196.0f, knobRowY = 404.0f;

    inline juce::Point<float> preDelayCentre() { return { colA, topRowY }; }
    inline juce::Point<float> sizeCentre()     { return { colB, topRowY }; }
    inline juce::Point<float> modCentre()      { return { colC, topRowY }; }

    inline juce::Point<float> toneCentre()     { return { colA, knobRowY }; }
    inline juce::Point<float> paramACentre()   { return { colB, knobRowY }; }
    inline juce::Point<float> paramBCentre()   { return { colC, knobRowY }; }
    inline juce::Point<float> mixCentre()      { return { 860.0f, knobRowY }; }
    inline juce::Point<float> outputCentre()   { return { 988.0f, knobRowY }; }
    inline constexpr float smallKnobDiameter = 76.0f;

    inline R tailDisplay()    { return { 796.0f, 80.0f, 256.0f, 192.0f }; }

    inline R freeze()         { return { centreX - 56.0f, 348.0f, 112.0f, 40.0f }; }

    // Algorithm rail: 2 x 5 keys (5 x 58 + 4 x 8 = 322 wide).
    inline constexpr float keyWidth = 58.0f, keyHeight = 48.0f, keyGap = 8.0f;
    inline constexpr float railTopY = 432.0f;
    inline R algorithmKey (int index)
    {
        const auto row = index / 5, col = index % 5;
        const auto total = 5.0f * keyWidth + 4.0f * keyGap;
        const auto x0 = centreX - total * 0.5f;
        return { x0 + (float) col * (keyWidth + keyGap), railTopY + (float) row * (keyHeight + keyGap), keyWidth, keyHeight };
    }
    inline R railKeyUnder (juce::Point<float> knob) { return { knob.x - keyWidth * 0.5f, railTopY + keyHeight + keyGap, keyWidth, keyHeight }; }
    inline R duckKey()        { return railKeyUnder (mixCentre()); }
    inline R wetKey()         { return railKeyUnder (outputCentre()); }

    // Sync keys: under the legend of the control they change (Pre-delay; Param A while Echo is selected).
    inline R syncKeyUnder (juce::Point<float> knob) { return { knob.x - 30.0f, knob.y + 76.0f, 60.0f, 24.0f }; }
    inline R preDelaySyncKey() { return syncKeyUnder (preDelayCentre()); }
    inline R echoSyncKey()     { return syncKeyUnder (paramACentre()); }

    // Advanced deck: a lower section of the instrument, below the panel. Opening it grows the
    // window by deckHeight; nothing on the panel is covered. ADVANCED is engraved on the bottom
    // edge: of the panel while closed, of the deck while open.
    inline constexpr float deckHeight = 132.0f;
    inline R deck()           { return { 0.0f, (float) metrics::baseHeight, (float) metrics::baseWidth, deckHeight }; }
    inline R advancedTab (bool open)
    {
        const auto bottom = (float) metrics::baseHeight + (open ? deckHeight : 0.0f);
        return { (float) metrics::baseWidth * 0.5f - 208.0f, bottom - 28.0f, 416.0f, 20.0f };
    }

    /** Knob component bounds include room for the legend underneath. */
    inline R knobBounds (juce::Point<float> centre, float diameter)
    {
        const auto pad = diameter * 0.30f;
        return { centre.x - diameter * 0.5f - pad, centre.y - diameter * 0.5f - pad,
                 diameter + 2.0f * pad, diameter + 2.0f * pad };
    }
}

} // namespace cerulean::ui
