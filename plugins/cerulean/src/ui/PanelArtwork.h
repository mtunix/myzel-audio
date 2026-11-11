#pragma once

#include "ui/Knob.h"

#include <vector>

namespace cerulean::ui
{

/** Everything the static panel artwork needs to know about the controls sitting on it. */
struct PanelArtworkSpec
{
    struct KnobEntry
    {
        juce::Point<float> centre;
        KnobStyle style;
    };

    std::vector<KnobEntry> knobs;
    struct Legend
    {
        juce::String text;
        juce::Rectangle<float> area;
    };

    std::vector<juce::Rectangle<float>> wells;     // recesses the keys sit in
    std::vector<Legend> legends;                   // small section legends
};

/**
    Renders the static front panel (ground, legends, scales, wells, screws) into g.
    Called only when the editor size or scale changes; the result is cached by the caller.
*/
void renderPanelArtwork (juce::Graphics& g, juce::Rectangle<float> area, const PanelArtworkSpec& spec,
                         const Fonts& fonts, float physicalScale);

/** Draws a knob's printed scale (ticks, numerals) and its title. */
void renderKnobScale (juce::Graphics& g, juce::Point<float> centre, const KnobStyle& style, const Fonts& fonts);

} // namespace cerulean::ui
