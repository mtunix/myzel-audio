#pragma once

#include "ui/Drawing.h"

namespace cerulean::ui
{

/**
    A console-style key with a thin ice-blue LED bar. Used for the algorithm keys, Freeze, Duck,
    Wet and the switches in the Advanced drawer.

    The LED fades towards the toggle state (~80 ms) when tick() is called by the editor's
    frame clock. The key face is cached; only the LED and the label are drawn per frame.
*/
class LampButton final : public juce::Button
{
public:
    enum class Shape
    {
        square,     // horizontal LED above the label
        wide,       // vertical LED left of the label
        compact     // small key, short vertical LED left of a small label (SYNC)
    };

    LampButton (const juce::String& label, Shape shape);

    /** Extra space around the key for the shadow and focus ring (component bounds = key + margin). */
    static constexpr float margin = 14.0f;

    void setKeyBounds (juce::Rectangle<float> keyBounds);

    /** The LED and label are shown dimmed because the control has no effect right now. */
    void setDimmed (bool shouldBeDimmed);

    /**
        Momentary keys (Freeze in momentary mode) report press and release through onPress
        instead of toggling. The click that follows a mouse press is swallowed: see
        takeMomentaryClick(). Keyboard and accessibility clicks still arrive as onClick.
    */
    void setMomentary (bool shouldBeMomentary);
    bool isMomentary() const noexcept { return momentary; }
    std::function<void (bool isDown)> onPress;

    /** True (once) if the current click is the release of a momentary mouse press. */
    bool takeMomentaryClick() noexcept { return std::exchange (pressedMomentarily, false); }

    void setLabel (const juce::String& newLabel);

    /** Advances the LED fade; returns true while something is still moving. */
    bool tick (float dtSeconds);

    /** Snaps the LED to its target (no fade), e.g. after construction. */
    void snapLamp();

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    void resized() override;
    void buttonStateChanged() override;

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
    bool momentary = false, momentaryDown = false, pressedMomentarily = false;

    float level = 0.0f;      // smoothed LED level 0..1
    float shownLevel = -1.0f;

    drawing::CachedLayer keyLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LampButton)
};

} // namespace cerulean::ui
