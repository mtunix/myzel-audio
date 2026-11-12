#pragma once

#include "common/ui/Knob.h"
#include "ui/Theme.h"

namespace mulberry::ui
{

/**
    The printed scale and title of a knob whose meaning changes: Param A/B (per machine), Time
    (ms, or a note value with Sync) and the mode knob (Width, Right time, Steps).

    It sits under the knob (mouse-transparent) and crossfades from the old legend to the new one
    when the style changes (metrics::legendFadeSeconds), driven by the panel's frame clock. Both
    legends are cached layers, so a frame of the fade is two image blits.
*/
class KnobScale final : public juce::Component
{
public:
    KnobScale();

    /** Positions the overlay around a knob centre (parent coordinates), sized for its diameter. */
    void setKnobCentre (juce::Point<float> centre, float diameter);

    /** Shows a new legend; fades from the previous one unless animate is false. */
    void setStyle (const KnobStyle& style, bool animate);

    /** Advances the crossfade; returns true while it is still moving. */
    bool tick (float dtSeconds);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    float width = 220.0f, above = 74.0f;

    juce::SharedResourcePointer<Fonts> fonts;
    KnobStyle current, previous;
    bool hasPrevious = false;
    float fade = 1.0f;
    drawing::CachedLayer currentLayer, previousLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobScale)
};

} // namespace mulberry::ui
