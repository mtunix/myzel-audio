#pragma once

#include "ui/StudioTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace cerulean::ui
{

/**
    The studio view's parameter control: a row with the name and the exact value on top and a
    flat bar below.

    Drag anywhere on the row (up or right raises, Shift = fine), double-click = default, mouse
    wheel, arrow keys / Page / Home / End / Delete. Click the value (or press Return, or
    Alt/Ctrl/right-click) to type a value with units ("4.2 s", "85 ms", "1/8D", "+7").
    Every edit is a host gesture and an undo transaction.
*/
class StudioSlider final : public juce::Component,
                           public juce::SettableTooltipClient,
                           private juce::Timer
{
public:
    StudioSlider (juce::RangedAudioParameter& parameter, juce::UndoManager* undoManager, const juce::String& label);
    ~StudioSlider() override;

    juce::RangedAudioParameter& getParameter() const noexcept { return *parameter; }

    /** Binds to another parameter (e.g. Pre-delay ms <-> note value). Any open edit is closed first. */
    void rebind (juce::RangedAudioParameter& newParameter);

    void setLabel (const juce::String& newLabel);
    const juce::String& getLabel() const noexcept { return label; }

    /** Fill from the middle instead of the left (Tone, Output, Low End, Mod Rate). */
    void setBipolar (bool shouldBeBipolar);

    /** Fill from the right end (High Cut: the fill is the part that is cut, so Off is empty). */
    void setFillFromRight (bool shouldFillFromRight);

    /** A small boxed letter before the name (Param A / B), as on the panel. */
    void setBadge (const juce::String& newBadge);
    const juce::String& getBadge() const noexcept { return badge; }

    /** A faint line under the bar (e.g. a synced note's length: "= 375 ms at 120 BPM"). */
    void setCaption (std::function<juce::String()> fn);
    juce::String getCaption() const;

    /** Row: name and value over a bar. Hero: the name over a large value, no bar (Decay). */
    enum class Look { row, hero };
    void setLook (Look newLook);

    /** A stepped control with this many positions (notches; wheel and arrows move one step). 0 = continuous. */
    void setSteps (int numSteps);

    /** Overrides the value text. Receives the normalised value. */
    void setValueToText (std::function<juce::String (float value01)> fn);

    /** Overrides how typed text is read. Returns the normalised value, or a negative number if the text is not understood. */
    void setTextToValue (std::function<float (const juce::String&)> fn);

    /** Keeps space at the right end of the bar for a small key (e.g. SYNC). */
    void setTrailingSpace (float width);

    /** Shown greyed out (e.g. BPM while the host provides the tempo). */
    void setDimmed (bool shouldBeDimmed);

    float getNormalisedValue() const noexcept { return value01; }
    juce::String getValueText() const;
    void setNormalisedValueAsGesture (float newValue01);

    /** Reads typed text as the text field would; returns false if it was not understood. */
    bool commitText (const juce::String& text);

    void showTextEntry();
    bool isTextEntryOpen() const noexcept { return textEntry != nullptr; }

    /** Where the bar is drawn (local). */
    juce::Rectangle<float> barArea() const;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    static constexpr float fullRangePixels = 300.0f;

private:
    void timerCallback() override;
    void valueChanged (float denormalised);
    void finishWheelGesture();
    void nudge (float amount);
    float stepSize (bool fine) const noexcept;
    float quantise (float v01) const noexcept;
    juce::Rectangle<float> valueArea() const;
    float textToValue01 (const juce::String& text) const;

    juce::RangedAudioParameter* parameter;
    juce::UndoManager* undoManager;
    juce::String label;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    std::function<juce::String (float)> valueToText;
    std::function<float (const juce::String&)> textToValue;
    std::function<juce::String()> caption;
    juce::String badge;
    Look look = Look::row;
    std::unique_ptr<juce::TextEditor> textEntry;
    juce::SharedResourcePointer<Fonts> fonts;

    float value01 = 0.0f, dragValue = 0.0f;
    juce::Point<float> lastDrag;
    float travelled = 0.0f, wheelAccumulator = 0.0f, trailing = 0.0f;
    int steps = 0;
    bool bipolar = false, fromRight = false, dimmed = false;
    bool hovering = false, dragging = false, inWheelGesture = false, keyboardFocused = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioSlider)
};

//==============================================================================
/**
    A flat toggle. Styles: a small chip (SYNC), a switch with a sliding thumb that says OFF or ON
    (Duck, Trails), a block with an indicator (FREEZE, WET ONLY), or one half of a segmented
    pair (Latch | Hold).

    Momentary toggles (Freeze in momentary mode) report press and release through onPress; the
    click that follows a mouse press is swallowed (takeMomentaryClick()), as on the panel's keys.
*/
class StudioToggle final : public juce::Button
{
public:
    enum class Style
    {
        chip,
        toggleSwitch,
        block,
        segmentLeft,
        segmentRight
    };

    StudioToggle (const juce::String& label, Style style);

    void setLabel (const juce::String& newLabel);
    void setDimmed (bool shouldBeDimmed);

    void setMomentary (bool shouldBeMomentary);
    bool isMomentary() const noexcept { return momentary; }
    std::function<void (bool isDown)> onPress;
    bool takeMomentaryClick() noexcept { return std::exchange (pressedMomentarily, false); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    void buttonStateChanged() override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    juce::String label;
    Style style;
    bool dimmed = false;
    bool momentary = false, momentaryDown = false, pressedMomentarily = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioToggle)
};

//==============================================================================
/**
    One row of the algorithm list: number, name and a few words on its character. Radio-grouped.
    Up/Down arrows ask for the previous/next algorithm through onStep.
*/
class AlgorithmRow final : public juce::Button
{
public:
    AlgorithmRow (int index, const juce::String& name, const juce::String& tagline);

    std::function<void (int delta)> onStep;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    int index;
    juce::String name, tagline;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlgorithmRow)
};

} // namespace cerulean::ui
