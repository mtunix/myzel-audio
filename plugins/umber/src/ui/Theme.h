#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace umber::ui
{

/**
    Every colour of the Umber look, as named tokens.

    A matte dark umber (deep brown) panel with cream silkscreen, black knobs with turned aluminium
    caps and a single amber accent that is reserved for lamps and active states.
    See src/ui/DESIGN.md.
*/
struct Palette
{
    // Panel (powder-coated)
    juce::Colour panelTop, panelBottom, panelGrain, panelVignette;
    juce::Colour panelEdgeLight, panelEdgeDark;

    // Legends (printed / engraved)
    juce::Colour legend, legendDim, legendShadow, legendLight;
    juce::Colour rule;

    // Header rail
    juce::Colour headerTop, headerBottom, headerEdge;
    juce::Colour displayGlass, displayText, displayTextDim;
    juce::Colour keyTop, keyBottom, keyText, keyTextActive;

    // Knobs
    juce::Colour skirtLight, skirtDark;
    juce::Colour capLight, capDark;
    juce::Colour pointer;
    juce::Colour tickMajor, tickMinor;

    // Lamps and the accent colour
    juce::Colour emberCore, ember, emberDeep;
    juce::Colour lensOff, lensOffRim;

    // Meter
    juce::Colour meterFaceCentre, meterFaceEdge, meterInk, meterRed, needle, meterBezel;

    // Feedback
    juce::Colour focus, bubbleFill, bubbleText, bubbleEdge, overlayDim;
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

    inline constexpr float knobStartAngle = -2.35619449f;   // -135 degrees
    inline constexpr float knobEndAngle   =  2.35619449f;   // +135 degrees

    inline constexpr float lampFadeSeconds = 0.08f;
}

/** Fixed positions of every control, in base coordinates (the layout grid). */
namespace layout
{
    using R = juce::Rectangle<float>;

    // Columns: left shaping (x 28..428), centre drive (444..772), right meter/output (788..1060),
    // separated by grooves at x 436 and 780.
    inline constexpr float leftGrooveX = 436.0f, rightGrooveX = 780.0f;
    inline constexpr float centreX = 608.0f;

    inline R header()        { return { 0.0f, 0.0f, (float) metrics::baseWidth, metrics::headerHeight }; }

    inline juce::Point<float> driveCentre()  { return { centreX, 204.0f }; }
    inline constexpr float driveDiameter = 196.0f;

    // Left block, a 3 x 2 grid. Top row feeds the circuit (stereo mode, emphasis, bias: closest
    // to Drive); bottom row is the EQ.
    inline constexpr float colA = 100.0f, colB = 228.0f, colC = 356.0f;
    inline constexpr float topRowY = 178.0f, knobRowY = 372.0f;

    inline juce::Point<float> stereoCentre()   { return { colA, topRowY }; }
    inline juce::Point<float> emphasisCentre() { return { colB, topRowY }; }
    inline juce::Point<float> biasCentre()     { return { colC, topRowY }; }

    inline juce::Point<float> lowCutCentre()  { return { colA, knobRowY }; }
    inline juce::Point<float> highCutCentre() { return { colB, knobRowY }; }
    inline juce::Point<float> toneCentre()    { return { colC, knobRowY }; }
    inline juce::Point<float> mixCentre()     { return { 860.0f, knobRowY }; }
    inline juce::Point<float> outputCentre()  { return { 988.0f, knobRowY }; }
    inline constexpr float smallKnobDiameter = 76.0f;

    inline R meter()          { return { 796.0f, 84.0f, 256.0f, 168.0f }; }

    inline R slam()           { return { centreX - 56.0f, 372.0f, 112.0f, 44.0f }; }

    inline constexpr float railCentreY = 498.0f;
    inline constexpr float keyWidth = 58.0f, keyHeight = 58.0f, keyGap = 8.0f;
    inline R modelKey (int index)
    {
        const auto total = 5.0f * keyWidth + 4.0f * keyGap;
        const auto x0 = centreX - total * 0.5f;
        return { x0 + (float) index * (keyWidth + keyGap), railCentreY - keyHeight * 0.5f, keyWidth, keyHeight };
    }
    inline R railKeyUnder (juce::Point<float> knob) { return { knob.x - keyWidth * 0.5f, railCentreY - keyHeight * 0.5f, keyWidth, keyHeight }; }
    inline R weightKey()      { return railKeyUnder (lowCutCentre()); }
    inline R deltaKey()       { return railKeyUnder (mixCentre()); }
    inline R autoKey()        { return railKeyUnder (outputCentre()); }

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
    juce::Font display (float height) const;    // Inter Display Black (VU legend)

    juce::Typeface::Ptr getMedium() const { return medium; }

private:
    juce::Typeface::Ptr medium, semiBold, black;

    JUCE_DECLARE_NON_COPYABLE (Fonts)
};

/** The palette. */
struct Theme
{
    static const Palette& palette() noexcept;
};

} // namespace umber::ui
