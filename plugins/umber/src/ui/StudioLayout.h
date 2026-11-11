#pragma once

#include "common/ui/StudioTheme.h"

namespace umber::ui::studio
{

// Umber's studio view, in base (100 %) editor coordinates. Same base size as the panel, and the
// same columns as every plugin's studio view: a selector column, the centre, the output column.
using namespace myzel::ui::studio;
using R = juce::Rectangle<float>;

inline R circuitCard() { return { 12.0f, 62.0f, 184.0f, 224.0f }; }
inline R listenCard()  { return { 12.0f, 298.0f, 184.0f, 270.0f }; }
inline R driveCard()   { return { 208.0f, 62.0f, 640.0f, 228.0f }; }
inline R inputCard()   { return { 208.0f, 302.0f, 314.0f, 266.0f }; }
inline R toneCard()    { return { 534.0f, 302.0f, 314.0f, 266.0f }; }
inline R outputCard()  { return { 860.0f, 62.0f, 216.0f, 506.0f }; }

// The circuit list.
inline constexpr float circuitRowPitch = 36.0f;
inline R circuitRow (int index)
{
    const auto c = circuitCard();
    return { c.getX() + 6.0f, c.getY() + cardTitleHeight + 2.0f + (float) index * circuitRowPitch, c.getWidth() - 12.0f, circuitRowPitch - 2.0f };
}

// The Drive card's head: Drive as its headline value (left) and Slam (right). Under it Bias and
// Emphasis side by side, then a note on the selected circuit.
inline constexpr float driveHeadHeight = 62.0f;
inline R driveHero()  { const auto c = driveCard(); return { c.getX() + cardPad, c.getY() + 8.0f, 112.0f, 46.0f }; }
inline R slamButton() { const auto c = driveCard(); return { c.getRight() - cardPad - 132.0f, c.getY() + 16.0f, 132.0f, 30.0f }; }
inline R driveRow (int column)
{
    const auto c = driveCard();
    const auto w = (c.getWidth() - 3.0f * cardPad) * 0.5f;
    return { c.getX() + cardPad + (float) column * (w + cardPad), c.getY() + driveHeadHeight + 8.0f, w, rowHeight };
}
inline R circuitNote()
{
    const auto c = driveCard();
    const auto top = driveRow (0).getBottom() + 18.0f;
    return { c.getX() + cardPad, top, c.getWidth() - 2.0f * cardPad, c.getBottom() - cardPad - top };
}

// Slider rows in the Input and Tone cards.
inline R cardRow (R card, int row)
{
    return { card.getX() + cardPad, card.getY() + cardTitleHeight + 6.0f + (float) row * rowPitch, card.getWidth() - 2.0f * cardPad, rowHeight };
}

// A small key at the right end of a slider's bar (Weight on Low Cut).
inline constexpr float chipWidth = 56.0f;
inline R rowChip (R row) { return { row.getRight() - chipWidth, row.getY() + 19.0f, chipWidth, 18.0f }; }

// The Listen card: a label left, its switch right, a short note under each.
inline R listenLabel (int row)  { const auto c = listenCard(); return { c.getX() + cardPad, c.getY() + 36.0f + (float) row * 116.0f, 80.0f, 22.0f }; }
inline R listenSwitch (int row) { const auto l = listenLabel (row); const auto c = listenCard(); return { c.getRight() - cardPad - 58.0f, l.getY(), 58.0f, 22.0f }; }
inline R listenNote (int row)   { const auto l = listenLabel (row); const auto c = listenCard(); return { c.getX() + cardPad, l.getBottom() + 8.0f, c.getWidth() - 2.0f * cardPad, 70.0f }; }

// The Output card: tall meters, then Mix and Output.
inline constexpr float outputRowPitch = 56.0f;
inline R meters()
{
    const auto c = outputCard();
    return { c.getX() + cardPad, c.getY() + cardTitleHeight + 4.0f, c.getWidth() - 2.0f * cardPad,
             c.getHeight() - cardTitleHeight - 4.0f - 12.0f - 2.0f * outputRowPitch - cardPad };
}
inline R outputRow (int row)
{
    const auto c = outputCard();
    return { c.getX() + cardPad, meters().getBottom() + 12.0f + (float) row * outputRowPitch, c.getWidth() - 2.0f * cardPad, rowHeight };
}

} // namespace umber::ui::studio
