#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cerulean::ui
{

/**
    Every colour of the Cerulean look, as named tokens.

    A deep cerulean powder-coat panel with pale silkscreen, graphite knobs with turned
    aluminium caps, a navy header rail, and a single ice-blue accent that is reserved for
    things that are on (key LEDs, the selected A/B slot, focus rings, the live tail display).
    See src/ui/DESIGN.md.
*/
struct Palette
{
    // Panel (deep cerulean powder coat)
    juce::Colour panelTop, panelBottom, panelGrain, panelVignette;
    juce::Colour panelEdgeLight, panelEdgeDark;

    // Legends (pale ink on the blue panel; legendLight is the drop shadow drawn under the ink)
    juce::Colour legend, legendDim, legendShadow, legendLight;
    juce::Colour grooveDark, grooveLight;

    // Wells (recesses the keys sit in)
    juce::Colour wellFill, wellShadow, wellLip;

    // Header rail and other dark surfaces (header keys, menus, dialogs)
    juce::Colour headerTop, headerBottom, headerEdge;
    juce::Colour headerLegend, headerLegendDim;
    juce::Colour displayGlass, displayText, displayTextDim;
    juce::Colour keyTop, keyBottom, keyText, keyTextActive;

    // Knobs
    juce::Colour skirtLight, skirtDark;
    juce::Colour capLight, capDark;
    juce::Colour pointer;
    juce::Colour tickMajor, tickMinor;

    // The accent (ice blue): lamps and active states only
    juce::Colour iceCore, ice, iceDeep;
    juce::Colour lensOff, lensOffRim;

    // Tail display
    juce::Colour bezel;
    juce::Colour screenTop, screenBottom, screenGrid, screenText, screenTextDim;
    juce::Colour traceMid, traceBand, liveFill, liveLine;

    // Advanced drawer
    juce::Colour drawerTop, drawerBottom, drawerLegend, drawerLegendDim, drawerValue;

    // Feedback
    juce::Colour focus, focusOnDark, bubbleFill, bubbleText, bubbleEdge, overlayDim, danger;

    // Caution (amber): states that silence or bypass the sound (Bypass). Never means "on".
    juce::Colour caution, cautionDeep;
};

/** Dimensions in base (100 %) editor coordinates. */
namespace metrics
{
    inline constexpr int baseWidth  = 1088;
    inline constexpr int baseHeight = 580;
    inline constexpr float headerHeight = 50.0f;

    inline constexpr float minScale = 0.75f;
    inline constexpr float maxScale = 2.0f;

    // Light comes from the top-left: every shadow is offset by this (scaled by elevation).
    inline constexpr float shadowDx = 0.45f;
    inline constexpr float shadowDy = 1.0f;

    inline constexpr float legendSize      = 12.0f;
    inline constexpr float legendTracking  = 0.14f;   // extra kerning for small caps legends
    inline constexpr float scaleNumberSize = 9.5f;
    inline constexpr float keyLabelSize    = 10.5f;

    // Advanced drawer: smaller type, still >= 7.5 device px at 75 %.
    inline constexpr float drawerLegendSize = 11.0f;
    inline constexpr float drawerValueSize  = 11.5f;

    inline constexpr float knobStartAngle = -2.35619449f;   // -135 degrees
    inline constexpr float knobEndAngle   =  2.35619449f;   // +135 degrees

    inline constexpr float lampFadeSeconds    = 0.08f;
    inline constexpr float legendFadeSeconds  = 0.18f;   // Param A/B relabel crossfade
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

/** Shared typefaces (Inter, SIL OFL 1.1), loaded once and shared via SharedResourcePointer. */
class Fonts
{
public:
    Fonts();

    juce::Font legend (float height) const;     // Inter SemiBold
    juce::Font label (float height) const;      // Inter Medium
    juce::Font display (float height) const;    // Inter Display Black (display headline)

    juce::Typeface::Ptr getMedium() const { return medium; }

private:
    juce::Typeface::Ptr medium, semiBold, black;

    JUCE_DECLARE_NON_COPYABLE (Fonts)
};

struct StudioPalette;   // StudioTheme.h

/** The palettes: the panel view's, and the studio view's. */
struct Theme
{
    static const Palette& palette() noexcept;
    static const StudioPalette& studio() noexcept;
};

} // namespace cerulean::ui
