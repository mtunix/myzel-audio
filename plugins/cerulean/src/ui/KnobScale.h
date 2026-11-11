#pragma once

#include "ui/Knob.h"

namespace cerulean::ui
{

/**
    The printed scale and title of a knob whose meaning changes: Param A/B (per algorithm),
    Pre-delay (ms or note value with Sync) and Echo time (ms or note).

    It sits under the knob (mouse-transparent) and crossfades from the old legend to the new one
    when the style changes (metrics::legendFadeSeconds), driven by the panel's frame clock. Both
    legends are cached layers, so a frame of the fade is two image blits.
*/
class KnobScale final : public juce::Component
{
public:
    KnobScale();

    /** Positions the overlay around a knob centre (parent coordinates). */
    void setKnobCentre (juce::Point<float> centre);

    /** Shows a new legend; fades from the previous one unless animate is false. */
    void setStyle (const KnobStyle& style, bool animate);

    /** Advances the crossfade; returns true while it is still moving. */
    bool tick (float dtSeconds);

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr float width = 220.0f, above = 74.0f, below = 74.0f;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    KnobStyle current, previous;
    bool hasPrevious = false;
    float fade = 1.0f;
    drawing::CachedLayer currentLayer, previousLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobScale)
};

} // namespace cerulean::ui
