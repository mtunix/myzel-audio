#pragma once

#include "ui/Drawing.h"
#include "ui/StudioTheme.h"

namespace cerulean
{
struct MeterValues;
}

namespace cerulean::ui
{

/**
    The studio view's meters: input, wet and output level (RMS bar with a peak-hold line and a
    peak read-out above), and the ducker's gain reduction.

    Levels come from the processor's lock-free MeterValues (RMS per block, peaks as "max since
    last read"). The bars use a fast attack and a ~300 ms release; peaks hold for 1.5 s, then fall.
    A peak over 0 dBFS turns its read-out red until the meters are clicked. The scale is cached;
    the bars repaint only when they move.
*/
class StudioMeters final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    explicit StudioMeters (MeterValues& meters);

    /** Reads the processor's levels and advances the ballistics. */
    void tick (float dtSeconds);

    /** Duck off: the reduction meter is shown greyed out. */
    void setDuckEnabled (bool enabled);

    /** Bypassed: the wet meter turns grey (the reverb does not reach the output). */
    void setWetMuted (bool muted);

    /** Test hook: fixed levels in dBFS (gain reduction in dB, positive). */
    void setLevelsForSnapshot (float inDb, float wetDb, float outDb, float reductionDb);

    /** The read-outs as text (tests, accessibility). */
    juce::String getReadoutText() const;

    void resetPeaks();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr float topDb = 6.0f, floorDb = -60.0f, reductionRangeDb = 24.0f;

private:
    struct Channel
    {
        float levelDb = -120.0f, peakDb = -120.0f, holdSeconds = 0.0f;
        bool clipped = false;
        float shownLevel = -999.0f, shownPeak = -999.0f;
        bool shownClipped = false;
    };

    bool wetMuted = false;

    juce::Rectangle<float> barArea (int index) const;
    float yForDb (float db, juce::Rectangle<float> bar) const noexcept;
    void advance (Channel& c, float rms, float peak, float dt);
    void renderScale (juce::Graphics& g) const;

    MeterValues& meters;
    juce::SharedResourcePointer<Fonts> fonts;
    std::array<Channel, 3> channels;           // input, wet, output
    float reductionDb = 0.0f, shownReduction = -1.0f;
    bool duckEnabled = false;
    drawing::CachedLayer scaleLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioMeters)
};

} // namespace cerulean::ui
