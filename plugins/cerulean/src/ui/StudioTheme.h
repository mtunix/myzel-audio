#pragma once

#include "ui/Theme.h"

namespace cerulean::ui
{

/**
    Every colour of the studio view, as named tokens (defined in Theme.cpp with the panel's).

    A flat deep-navy workstation surface: cards on a darker ground, light text, hairline edges,
    no lighting or texture. The ice blue stays the one accent for things that are on (selected
    algorithm, lit toggles, slider fills, focus). Data colours (the three decay bands, meters)
    are only used inside the graph and the meters, and never alone: every band is also labelled
    and drawn with its own line style. See src/ui/DESIGN.md.
*/
struct StudioPalette
{
    // Surfaces
    juce::Colour background, card, cardEdge, inset, insetEdge, hover;

    // Text
    juce::Colour text, textDim, textFaint, title;

    // Controls
    juce::Colour track, trackFill, trackFillEdge, marker, chipOff, chipOffText;

    // Accent (ice): on states, selection, focus
    juce::Colour accent, accentDeep, accentSoft, accentText;

    // Graph
    juce::Colour grid, gridText, bandLow, bandMid, bandHigh, early, liveFill, liveLine;

    // Meters
    juce::Colour meterBack, meterFill, meterHot, meterClip, meterPeak, reduction;

    // Caution (amber): Bypass
    juce::Colour caution, cautionDeep;
};

/** Studio view geometry, in base (100 %) editor coordinates. Same base size as the panel. */
namespace studio
{
    using R = juce::Rectangle<float>;

    inline constexpr float gutter = 12.0f;
    inline constexpr float cardCorner = 6.0f;
    inline constexpr float cardTitleHeight = 30.0f;
    inline constexpr float cardPad = 12.0f;

    // Type
    inline constexpr float titleSize = 10.5f, titleTracking = 0.16f;
    inline constexpr float labelSize = 10.5f, labelTracking = 0.10f;
    inline constexpr float valueSize = 12.5f;
    inline constexpr float smallSize = 10.0f;

    // Columns
    inline R algorithmCard()   { return { 12.0f, 62.0f, 184.0f, 310.0f }; }
    inline R behaviourCard()   { return { 12.0f, 384.0f, 184.0f, 184.0f }; }
    inline R displayCard()     { return { 208.0f, 62.0f, 640.0f, 228.0f }; }
    inline R outputCard()      { return { 860.0f, 62.0f, 216.0f, 506.0f }; }

    // The tail card's head: Decay as its headline value (left) and Freeze (right), with the
    // graph's read-outs between them. The graph starts below.
    inline constexpr float tailHeadHeight = 62.0f;
    inline R decayHero()      { const auto c = displayCard(); return { c.getX() + cardPad, c.getY() + 8.0f, 112.0f, 46.0f }; }
    inline R freezeButton()   { const auto c = displayCard(); return { c.getRight() - cardPad - 132.0f, c.getY() + 16.0f, 132.0f, 30.0f }; }

    // Four parameter cards under the display: Space, Character, Tone, Duck.
    inline constexpr int numParamCards = 4;
    inline constexpr float paramCardWidth = 151.0f;
    inline R paramCard (int index) { return { 208.0f + (float) index * (paramCardWidth + gutter), 302.0f, paramCardWidth, 266.0f }; }

    // A slider row: label and value on top, the bar below.
    inline constexpr float rowHeight = 44.0f, rowPitch = 58.0f;
    inline R paramRow (int card, int row)
    {
        const auto c = paramCard (card);
        return { c.getX() + cardPad, c.getY() + cardTitleHeight + 6.0f + (float) row * rowPitch, c.getWidth() - 2.0f * cardPad, rowHeight };
    }

    // Algorithm list
    inline constexpr float algorithmRowHeight = 27.0f;
    inline R algorithmRow (int index)
    {
        const auto c = algorithmCard();
        return { c.getX() + 6.0f, c.getY() + cardTitleHeight + 2.0f + (float) index * algorithmRowHeight, c.getWidth() - 12.0f, algorithmRowHeight - 2.0f };
    }

    // Behaviour card: settings you set once (Trails, Freeze mode, Tempo). A label on the left,
    // its control on the right.
    inline R behaviourLabel (int row) { const auto c = behaviourCard(); return { c.getX() + cardPad, c.getY() + 36.0f + (float) row * 34.0f, 80.0f, 22.0f }; }
    inline R trailsSwitch()        { const auto l = behaviourLabel (0); const auto c = behaviourCard(); return { c.getRight() - cardPad - 58.0f, l.getY(), 58.0f, 22.0f }; }
    inline R freezeModeLatch()     { const auto l = behaviourLabel (1); const auto c = behaviourCard(); return { c.getRight() - cardPad - 104.0f, l.getY(), 52.0f, 22.0f }; }
    inline R freezeModeMomentary() { const auto r = freezeModeLatch(); return r.translated (r.getWidth(), 0.0f); }
    inline R layerSwitch()         { const auto l = behaviourLabel (2); const auto c = behaviourCard(); return { c.getRight() - cardPad - 58.0f, l.getY(), 58.0f, 22.0f }; }
    inline R tempoRow()       { const auto c = behaviourCard(); return { c.getX() + cardPad, c.getBottom() - 8.0f - 38.0f, c.getWidth() - 2.0f * cardPad, 38.0f }; }

    // Output card: tall meters, then Mix, Output and the Wet key.
    inline constexpr float outputRowPitch = 56.0f;
    inline R meters()         { const auto c = outputCard(); return { c.getX() + cardPad, c.getY() + cardTitleHeight + 4.0f, c.getWidth() - 2.0f * cardPad,
                                                                    c.getHeight() - cardTitleHeight - 4.0f - 12.0f - 2.0f * outputRowPitch - 40.0f - cardPad }; }
    inline R outputRow (int row)
    {
        const auto c = outputCard();
        return { c.getX() + cardPad, meters().getBottom() + 12.0f + (float) row * outputRowPitch, c.getWidth() - 2.0f * cardPad, rowHeight };
    }
    inline R wetButton()      { const auto r = outputRow (1); return { r.getX(), r.getBottom() + 16.0f, r.getWidth(), 24.0f }; }

    // Small toggle that sits at the right end of a card title (e.g. the Duck switch).
    inline R titleChip (R card, float width = 40.0f)
    {
        return { card.getRight() - cardPad - width, card.getY() + (cardTitleHeight - 18.0f) * 0.5f + 1.0f, width, 18.0f };
    }
}

} // namespace cerulean::ui
