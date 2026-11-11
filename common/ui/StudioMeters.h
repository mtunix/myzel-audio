#pragma once

#include "common/ui/Drawing.h"
#include "common/ui/StudioTheme.h"

#include <optional>
#include <vector>

namespace myzel::ui
{

/**
    The studio view's meters: a few level bars (RMS bar with a peak-hold line and a peak read-out
    above), and optionally a gain-reduction bar to their right.

    Levels come from the processor's lock-free meter values (RMS per block, peaks as "max since
    last read", which takePeak() resets). The bars use a fast attack and a ~300 ms release; peaks
    hold for 1.5 s, then fall. A peak over 0 dBFS turns its read-out red until the meters are
    clicked. The scale is cached; the bars repaint only when they move.
*/
class StudioMeters final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    struct Channel
    {
        juce::String name;                    // printed under the bar, e.g. "IN"
        std::function<float()> rms;           // linear RMS
        std::function<float()> takePeak;      // linear peak since the last call (and reset); none: the RMS
    };

    struct Reduction
    {
        juce::String name;                    // e.g. "DUCK"
        std::function<float()> gain;          // linear gain, 1 = no reduction
    };

    StudioMeters (std::vector<Channel> channels, std::optional<Reduction> reduction, const juce::String& tooltip);

    /** Reads the processor's levels and advances the ballistics. */
    void tick (float dtSeconds);

    /** The reduction meter is shown greyed out (e.g. the ducker is off). */
    void setReductionEnabled (bool enabled);

    /** The bar turns grey (e.g. the wet signal while bypassed: it does not reach the output). */
    void setChannelMuted (int index, bool muted);

    /** Test hook: fixed levels in dBFS, one per channel (gain reduction in dB, positive). */
    void setLevelsForSnapshot (const std::vector<float>& levelsDb, float reductionDb = 0.0f);

    /** The read-outs as text (tests, accessibility), e.g. "In -12.0 dB, Out -6.0 dB". */
    juce::String getReadoutText() const;

    void resetPeaks();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr float topDb = 6.0f, floorDb = -60.0f, reductionRangeDb = 24.0f;

private:
    struct Level
    {
        Channel source;
        float levelDb = -120.0f, peakDb = -120.0f, holdSeconds = 0.0f;
        bool clipped = false, muted = false;
        float shownLevel = -999.0f, shownPeak = -999.0f;
        bool shownClipped = false;
    };

    int numBars() const noexcept { return (int) levels.size(); }
    juce::Rectangle<float> barArea (int index) const;
    juce::Rectangle<float> reductionArea() const;
    float yForDb (float db, juce::Rectangle<float> bar) const noexcept;
    void advance (Level& c, float rms, float peak, float dt);
    void renderScale (juce::Graphics& g) const;

    std::vector<Level> levels;
    std::optional<Reduction> reduction;
    juce::SharedResourcePointer<Fonts> fonts;
    float reductionDb = 0.0f, shownReduction = -1.0f;
    bool reductionEnabled = false;
    drawing::CachedLayer scaleLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioMeters)
};

} // namespace myzel::ui
