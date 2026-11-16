#pragma once

#include "ui/DisplayModel.h"
#include "ui/DrawCache.h"
#include "ui/Theme.h"

#include <tuple>

namespace halite::ui
{

/**
    In, Out and Cut at the display's right edge (PLAN.md, Parts, Meters; 48 px: PLAN's ≈ 44, room for
    the three labels). In and Out: the peak bar, the RMS bar and the hold line, 0 … −60 dBFS. Cut hangs
    from the top, the Ess cut and the Harsh cut side by side over Range's 24 dB, with the deepest since
    the hold was reset as a line; under the labels the Ess cut now. A click on a meter resets its hold.
*/
class MeterStrip final : public juce::Component
{
public:
    explicit MeterStrip (DisplayModel&);

    void update (double dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** The three tracks (In, Out, Cut). */
    juce::Rectangle<float> track (int which) const;

    /** Which meter a point belongs to (0 In, 1 Out, 2 Cut). */
    int meterAt (float x) const noexcept;

    /** Repaints the frame clock asked for (the snapshot tool's timing paints only those). */
    std::uint32_t frameRepaints() const noexcept { return repaintsAsked; }

    static constexpr int width = 48;

private:
    std::array<float, 8> shownPositions() const;
    void paintStatic (juce::Graphics&);

    DisplayModel& model;
    juce::SharedResourcePointer<Fonts> fonts;
    std::array<float, 8> shown {};
    juce::String shownReadout;
    double sinceReadout = 1.0, sinceRepaint = 1.0;
    std::uint32_t repaintsAsked = 0;
    juce::Image cached;                                     // panel, tracks and labels
    ChipCache readouts;
    std::tuple<int, int, float, ThemeName> cachedFor {};
    ThemeName shownTheme = ThemeName::dark;
};

} // namespace halite::ui
