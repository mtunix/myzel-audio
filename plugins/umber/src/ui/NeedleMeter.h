#pragma once

#include "ui/Drawing.h"
#include "ui/VuBallistics.h"

namespace umber::ui
{

/**
    Backlit moving-coil VU meter with a true voltage-linear VU scale, glass and bezel.

    Shows either the Output level or the Drive level (into the circuit); click (or Space) toggles.
    Face, glass and bezel are cached; per frame only the needle and peak lamp
    are drawn, and only when they change.
*/
class NeedleMeter final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    enum class Mode
    {
        output = 0,
        drive
    };

    NeedleMeter();

    void setMode (Mode newMode);
    Mode getMode() const noexcept { return mode; }
    std::function<void (Mode)> onModeChanged;

    /**
        Advances the needle. level is linear relative to 0 VU (1.0 = 0 VU); peak lights the clip
        lamp.
    */
    void tick (float dtSeconds, float level, bool peak);

    /** Test hook: jumps the needle to a level without ballistics. */
    void setNeedleImmediately (float level);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    void toggleMode();

    static constexpr float bezelWidth = 9.0f;

private:
    juce::Rectangle<float> faceArea() const;
    juce::Point<float> pivot() const;
    float scaleRadius() const;
    float angleForPosition (float linear) const noexcept;

    void renderBezel (juce::Graphics& g, juce::Rectangle<float> area) const;
    void renderFace (juce::Graphics& g, juce::Rectangle<float> area) const;
    void renderGlass (juce::Graphics& g, juce::Rectangle<float> area) const;

    juce::SharedResourcePointer<Fonts> fonts;
    Mode mode = Mode::output;
    VuBallistics ballistics;

    float needle = 0.0f, shownNeedle = -1.0f;
    float peakLamp = 0.0f, peakHold = 0.0f, shownPeak = -1.0f;

    drawing::CachedLayer bezelLayer, faceLayer, glassLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeedleMeter)
};

} // namespace umber::ui
