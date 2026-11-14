#pragma once

#include "common/ui/StudioTheme.h"
#include "ui/Theme.h"

namespace mulberry::ui
{

/** The studio echo graph's colours (the kit's studio colours are Theme::studio()). */
struct StudioExtras
{
    juce::Colour lineLeft, lineRight, feedbackSlope, liveFill, liveLine, step;
};

/** Mulberry's studio layout, in base (100 %) editor coordinates. Same base size as the panel. */
namespace studio
{
    using namespace myzel::ui::studio;
    using R = juce::Rectangle<float>;

    inline R machineCard()   { return { 12.0f, 62.0f, 184.0f, 228.0f }; }
    inline R modeCard()      { return { 12.0f, 302.0f, 184.0f, 266.0f }; }
    inline R echoCard()      { return { 208.0f, 62.0f, 640.0f, 228.0f }; }
    inline R outputCard()    { return { 860.0f, 62.0f, 216.0f, 506.0f }; }

    // The echo card's head: Time as its headline, Right time beside it (Dual), Sync / Tap / Hold
    // on the right; the graph below.
    inline R timeHero()      { const auto c = echoCard(); return { c.getX() + cardPad, c.getY() + 8.0f, 132.0f, 46.0f }; }
    inline R rightTimeHero() { const auto r = timeHero(); return r.translated (r.getWidth() + 12.0f, 0.0f); }
    inline R syncChip()      { const auto c = echoCard(); return { c.getRight() - cardPad - 228.0f, c.getY() + 20.0f, 48.0f, 22.0f }; }
    inline R tapChip()       { return syncChip().translated (56.0f, 0.0f); }
    inline R holdBlock()     { const auto c = echoCard(); return { c.getRight() - cardPad - 112.0f, c.getY() + 16.0f, 112.0f, 30.0f }; }
    inline R graph()         { const auto c = echoCard(); return { c.getX() + cardPad, c.getY() + 62.0f, c.getWidth() - 2.0f * cardPad, c.getHeight() - 62.0f - cardPad }; }

    // Five parameter cards under the graph: Loop, Machine, Stereo, Time, Duck.
    inline constexpr int numParamCards = 5;
    inline constexpr float paramCardWidth = 118.4f, paramRowPitch = 46.0f;
    inline R paramCard (int index) { return { 208.0f + (float) index * (paramCardWidth + gutter), 302.0f, paramCardWidth, 266.0f }; }
    inline R paramRow (int card, int row)
    {
        const auto c = paramCard (card);
        return { c.getX() + 8.0f, c.getY() + cardTitleHeight + 2.0f + (float) row * paramRowPitch, c.getWidth() - 16.0f, rowHeight };
    }

    inline constexpr float listRowHeight = 30.0f, machineRowHeight = 28.0f;   // seven machines fill their card
    /** The row at a slot of machineOrder (MachineInfo.h). */
    inline R machineRow (int slot)
    {
        const auto c = machineCard();
        return { c.getX() + 6.0f, c.getY() + cardTitleHeight + 2.0f + (float) slot * machineRowHeight, c.getWidth() - 12.0f, machineRowHeight - 2.0f };
    }
    inline R modeRow (int index)
    {
        const auto c = modeCard();
        return { c.getX() + 6.0f, c.getY() + cardTitleHeight + 2.0f + (float) index * listRowHeight, c.getWidth() - 12.0f, listRowHeight - 2.0f };
    }

    // Under the modes: Trails and Hold mode (set once).
    inline R behaviourLabel (int row) { const auto c = modeCard(); return { c.getX() + cardPad, c.getY() + 166.0f + (float) row * 34.0f, 80.0f, 22.0f }; }
    inline R trailsSwitch()       { const auto l = behaviourLabel (0); const auto c = modeCard(); return { c.getRight() - cardPad - 58.0f, l.getY(), 58.0f, 22.0f }; }
    inline R holdModeLatch()      { const auto l = behaviourLabel (1); const auto c = modeCard(); return { c.getRight() - cardPad - 104.0f, l.getY(), 52.0f, 22.0f }; }
    inline R holdModeMomentary()  { const auto r = holdModeLatch(); return r.translated (r.getWidth(), 0.0f); }

    inline constexpr float outputRowPitch = 56.0f;
    inline R meters()        { const auto c = outputCard(); return { c.getX() + cardPad, c.getY() + cardTitleHeight + 4.0f, c.getWidth() - 2.0f * cardPad,
                                                                   c.getHeight() - cardTitleHeight - 4.0f - 12.0f - 2.0f * outputRowPitch - 40.0f - cardPad }; }
    inline R outputRow (int row)
    {
        const auto c = outputCard();
        return { c.getX() + cardPad, meters().getBottom() + 12.0f + (float) row * outputRowPitch, c.getWidth() - 2.0f * cardPad, rowHeight };
    }
    inline R wetButton()     { const auto r = outputRow (1); return { r.getX(), r.getBottom() + 16.0f, r.getWidth(), 24.0f }; }

    inline R titleChip (R card, float width = 40.0f)
    {
        return { card.getRight() - 8.0f - width, card.getY() + (cardTitleHeight - 18.0f) * 0.5f + 1.0f, width, 18.0f };
    }
}

} // namespace mulberry::ui
