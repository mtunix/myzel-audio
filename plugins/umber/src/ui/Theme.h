#pragma once

#include "common/ui/Theme.h"

namespace umber::ui
{

using myzel::ui::Fonts;
using myzel::ui::Palette;

/**
    Colours of Umber's own displays: the VU meter. The kit colours (a matte dark umber panel with
    cream silkscreen, black knobs and a single amber accent) are Theme::palette(). See src/ui/DESIGN.md.
*/
struct Extras
{
    juce::Colour meterFaceCentre, meterFaceEdge, meterInk, meterRed, needle, meterBezel;
};

struct Theme : myzel::ui::Theme
{
    static const Extras& extras() noexcept;
};

/** Dimensions in base (100 %) editor coordinates (the shared ones come from common/ui/Theme.h). */
namespace metrics
{
    using namespace myzel::ui::metrics;

    inline constexpr int baseWidth  = 1088;
    inline constexpr int baseHeight = 580;
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

} // namespace umber::ui
