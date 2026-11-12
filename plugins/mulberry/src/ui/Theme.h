#pragma once

#include "common/ui/Theme.h"

namespace mulberry::ui
{

// The shared UI kit (theme, knobs, keys, header parts) is used unqualified throughout the UI.
using namespace myzel::ui;

/**
    Colours of Mulberry's own displays: the echo map and the Advanced deck. The kit colours (a deep
    mulberry panel with pale silkscreen, graphite knobs, a plum header rail and a single leaf-green
    accent) are Theme::palette(). See src/ui/DESIGN.md.
*/
struct Extras
{
    // Echo map (glass window)
    juce::Colour bezel;
    juce::Colour screenTop, screenBottom, screenGrid, screenGridBeat, screenText, screenTextDim;
    juce::Colour spike, spikeDim, spikeRight, liveFill, liveLine, stepFill, stepEdge;

    // Feedback scale: the RUN zone above 100 % (engraved hatching, never the caution colour)
    juce::Colour runHatch;

    // Advanced deck
    juce::Colour drawerValue;
};

struct StudioExtras;

struct Theme : myzel::ui::Theme
{
    static const Extras& extras() noexcept;
    static const StudioExtras& studioExtras() noexcept;   // StudioTheme.h
};

namespace metrics
{
    using namespace myzel::ui::metrics;

    inline constexpr int baseWidth  = 1088;
    inline constexpr int baseHeight = 580;

    inline constexpr float drawerLegendSize = 11.0f;
    inline constexpr float drawerValueSize  = 11.5f;
}

/** Fixed positions of every control, in base coordinates (the layout grid, see DESIGN.md). */
namespace layout
{
    using R = juce::Rectangle<float>;
    using P = juce::Point<float>;

    // Columns: the loop and its colour (x 20..456), time and machine (476..784), the result
    // (800..1068), separated by grooves at x 468 and 792. Knob columns sit 112 apart so the
    // end labels of neighbours never meet.
    inline constexpr float leftGrooveX = 468.0f, rightGrooveX = 792.0f;
    inline constexpr float grooveTop = 80.0f, grooveBottom = 512.0f;
    inline constexpr float centreX = 630.0f;

    // Left block, 4 x 2 knobs of 64: the loop on top, its colour below.
    inline constexpr float smallKnobDiameter = 64.0f;
    inline constexpr float col0 = 66.0f, col1 = 178.0f, col2 = 290.0f, col3 = 402.0f;
    inline constexpr float topRowY = 188.0f, bottomRowY = 400.0f;

    inline P feedbackCentre() { return { col0, topRowY }; }
    inline P driveCentre()    { return { col1, topRowY }; }
    inline P wobbleCentre()   { return { col2, topRowY }; }
    inline P diffuseCentre()  { return { col3, topRowY }; }
    inline P lowCutCentre()   { return { col0, bottomRowY }; }
    inline P highCutCentre()  { return { col1, bottomRowY }; }
    inline P paramACentre()   { return { col2, bottomRowY }; }
    inline P paramBCentre()   { return { col3, bottomRowY }; }

    // Centre: Time with its three keys, then the machine keys (2 x 3).
    inline P timeCentre() { return { centreX, 170.0f }; }
    inline constexpr float timeDiameter = 160.0f;
    inline constexpr float timeKeyWidth = 72.0f, timeKeyHeight = 32.0f, timeKeyGap = 12.0f, timeKeyY = 306.0f;
    inline R timeKey (int index)
    {
        const auto total = 3.0f * timeKeyWidth + 2.0f * timeKeyGap;
        return { centreX - total * 0.5f + (float) index * (timeKeyWidth + timeKeyGap), timeKeyY, timeKeyWidth, timeKeyHeight };
    }
    inline R syncKey() { return timeKey (0); }
    inline R tapKey()  { return timeKey (1); }
    inline R holdKey() { return timeKey (2); }

    inline constexpr float machineKeyWidth = 92.0f, machineKeyHeight = 44.0f, machineKeyGap = 8.0f, machineTopY = 384.0f;
    inline R machineKey (int index)
    {
        const auto row = index / 3, col = index % 3;
        const auto total = 3.0f * machineKeyWidth + 2.0f * machineKeyGap;
        return { centreX - total * 0.5f + (float) col * (machineKeyWidth + machineKeyGap),
                 machineTopY + (float) row * (machineKeyHeight + machineKeyGap), machineKeyWidth, machineKeyHeight };
    }

    // Right column: the echo map, Mode and its knob, Mix and Output, Duck and Wet.
    inline R echoMap() { return { 808.0f, 76.0f, 256.0f, 156.0f }; }
    inline P modeCentre()       { return { 874.0f, 296.0f }; }
    inline P modeAmountCentre() { return { 998.0f, 296.0f }; }
    inline P mixCentre()        { return { 874.0f, 418.0f }; }
    inline P outputCentre()     { return { 998.0f, 418.0f }; }
    inline constexpr float keyWidth = 58.0f, keyHeight = 30.0f;
    inline R keyUnder (P knob) { return { knob.x - keyWidth * 0.5f, 486.0f, keyWidth, keyHeight }; }
    inline R duckKey() { return keyUnder (mixCentre()); }
    inline R wetKey()  { return keyUnder (outputCentre()); }

    // Advanced deck: a lower section of the instrument; ADVANCED sits on the bottom edge.
    inline constexpr float deckHeight = 132.0f;
    inline R deck() { return { 0.0f, (float) metrics::baseHeight, (float) metrics::baseWidth, deckHeight }; }
    inline R advancedTab (bool open)
    {
        const auto bottom = (float) metrics::baseHeight + (open ? deckHeight : 0.0f);
        return { (float) metrics::baseWidth * 0.5f - 208.0f, bottom - 28.0f, 416.0f, 20.0f };
    }

    /** Knob component bounds include room for the legend underneath. */
    inline R knobBounds (P centre, float diameter)
    {
        const auto pad = diameter * 0.30f;
        return { centre.x - diameter * 0.5f - pad, centre.y - diameter * 0.5f - pad,
                 diameter + 2.0f * pad, diameter + 2.0f * pad };
    }
}

} // namespace mulberry::ui
