#pragma once

#include "ui/Drawing.h"

namespace umber::ui
{

/**
    A console-style key with a thin amber LED bar. Used for the model selector, Slam, Weight
    and Auto.

    The LED fades towards the toggle state (~80 ms) when tick() is called by the editor's
    frame clock. The key face is cached; only the LED and the label are drawn per frame.
*/
class LampButton final : public juce::Button
{
public:
    enum class Shape
    {
        square,     // horizontal LED above the label
        wide        // vertical LED left of the label
    };

    LampButton (const juce::String& label, Shape shape);

    /** Extra space around the key for the shadow and focus ring (component bounds = key + margin). */
    static constexpr float margin = 14.0f;

    void setKeyBounds (juce::Rectangle<float> keyBounds);

    /** Weight with Low Cut off: the LED is shown dimmed because the control has no effect. */
    void setDimmed (bool shouldBeDimmed);

    /** Advances the LED fade; returns true while something is still moving. */
    bool tick (float dtSeconds);

    /** Snaps the LED to its target (no fade), e.g. after construction. */
    void snapLamp();

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    void resized() override;

private:
    void renderKey (juce::Graphics& g, juce::Rectangle<float> key) const;
    void drawLed (juce::Graphics& g, juce::Rectangle<float> key, const juce::Path& face, float lit) const;
    juce::Rectangle<float> keyArea() const;
    juce::Rectangle<float> ledArea (juce::Rectangle<float> key) const;
    juce::Rectangle<float> labelArea (juce::Rectangle<float> key) const;

    juce::SharedResourcePointer<Fonts> fonts;
    juce::String label;
    Shape shape;
    bool dimmed = false;

    float level = 0.0f;      // smoothed LED level 0..1
    float shownLevel = -1.0f;

    drawing::CachedLayer keyLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LampButton)
};

} // namespace umber::ui
