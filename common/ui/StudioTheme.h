#pragma once

#include "common/ui/Theme.h"

namespace myzel::ui
{

/**
    Every colour of the studio view, as named tokens (each plugin defines Theme::studio() in its
    ui/Theme.cpp, next to its panel colours).

    A flat workstation surface: cards on a darker ground, light text, hairline edges, no lighting
    or texture. The plugin's accent stays the one colour for things that are on (selection, lit
    toggles, slider fills, focus). Meter colours are only used inside the meters.
*/
struct StudioPalette
{
    // Surfaces
    juce::Colour background, card, cardEdge, inset, insetEdge, hover;

    // Text
    juce::Colour text, textDim, textFaint, title;

    // Controls
    juce::Colour track, trackFill, trackFillEdge, marker, chipOff, chipOffText;

    // Accent: on states, selection, focus
    juce::Colour accent, accentDeep, accentSoft, accentText;

    // Scales and grids
    juce::Colour grid, gridText;

    // Meters
    juce::Colour meterBack, meterFill, meterHot, meterClip, meterPeak, reduction;

    // Caution: Bypass
    juce::Colour caution, cautionDeep;
};

/** Studio view geometry shared by every plugin, in base (100 %) editor coordinates. */
namespace studio
{
    inline constexpr float gutter = 12.0f;
    inline constexpr float cardCorner = 6.0f;
    inline constexpr float cardTitleHeight = 30.0f;
    inline constexpr float cardPad = 12.0f;

    // Type
    inline constexpr float titleSize = 10.5f, titleTracking = 0.16f;
    inline constexpr float labelSize = 10.5f, labelTracking = 0.10f;
    inline constexpr float valueSize = 12.5f;
    inline constexpr float smallSize = 10.0f;

    // A slider row: label and value on top, the bar below.
    inline constexpr float rowHeight = 44.0f, rowPitch = 58.0f;

    /** A card: a rounded surface with a hairline edge and an optional title at its top left. */
    void drawCard (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, const Fonts& fonts);
}

} // namespace myzel::ui
