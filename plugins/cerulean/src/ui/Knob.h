#pragma once

#include "ui/Drawing.h"
#include "ui/ValueBubble.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace cerulean::ui
{

/** A labelled position on a knob's printed scale (position is the normalised parameter value). */
struct ScaleMark
{
    float position = 0.0f;
    juce::String text;
    bool major = true;
};

struct KnobStyle
{
    juce::String title;
    float diameter = 76.0f;
    bool hero = false;          // Decay: larger cap, finer knurling, major/minor ticks
    bool centreDetent = false;  // Tone: sticky at 12 o'clock
    int ticks = 11;             // evenly spaced scale ticks (0 = none)
    std::vector<float> tickPositions;   // explicit ticks instead (e.g. a log scale); minor where not marked
    std::vector<ScaleMark> marks;
    float dragPixelsForFullRange = 260.0f;
    int steps = 0;              // > 1: a rotary switch with this many positions (wheel/arrows move one step)
    float legendOffset = 0.0f;  // centre -> printed title distance (0 = the panel default for the size)
    juce::String badge;         // Param A/B: a small boxed letter printed before the title
};

/**
    A rotary control bound to one plugin parameter.

    Vertical drag (Shift = fine), double-click = default, Alt/Ctrl/Cmd-click or right-click =
    type a value, mouse wheel, arrow keys. Every edit is a host gesture and an undo transaction.
    The static artwork (shadow, skirt, cap) is cached per physical scale; only the knurling and
    the pointer are drawn per frame, so the lighting never rotates with the knob.
*/
class Knob final : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
{
public:
    Knob (juce::RangedAudioParameter& parameter, juce::UndoManager* undoManager, KnobStyle style);
    ~Knob() override;

    const KnobStyle& getStyle() const noexcept     { return style; }
    juce::RangedAudioParameter& getParameter() const noexcept { return *parameter; }

    /**
        Binds the knob to another parameter (e.g. Pre-delay ms <-> note value when Sync is on).
        Any open gesture is closed first. The artwork is kept; only the behaviour changes.
    */
    void rebind (juce::RangedAudioParameter& newParameter, KnobStyle newStyle);

    /** Changes the style (legend, marks, detent) but keeps the binding. */
    void setStyle (KnobStyle newStyle);

    /** Overrides the value text (read-out, accessibility). Receives the normalised value. */
    void setValueToText (std::function<juce::String (float value01)> fn);

    /** Re-reads the text (e.g. Param A/B after an algorithm change). */
    void refreshValueText();

    /** Point of the title relative to the knob centre, as printed on the panel. */
    static float defaultLegendOffset (const KnobStyle& s) noexcept;

    void setValueBubble (ValueBubble* bubble) noexcept { valueBubble = bubble; }

    /** The printed title, in parent coordinates. */
    juce::Point<float> getLegendAnchor() const;

    /** Where a large knob's value read-out goes (above the knob), in parent coordinates. */
    juce::Point<float> getValueAnchor() const;

    /** Test hook: show the hover state as if the mouse were over the knob. */
    void setHoverForSnapshot (bool shouldHover);

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

    // Used by the accessibility handler.
    float getNormalisedValue() const noexcept { return value01; }
    void setNormalisedValueAsGesture (float newValue01);
    juce::String getValueText() const;

private:
    void timerCallback() override;

    void valueChanged (float denormalised);
    void beginEdit();
    void endEdit();
    void finishWheelGesture();
    void setFromDrag (float newRaw);
    float rawFromValue (float v) const noexcept;
    float valueFromRaw (float raw) const noexcept;
    void nudge (float amount);
    void showTextEntry();
    void updateBubble();

    float radius() const noexcept { return style.diameter * 0.5f; }
    bool isSmall() const noexcept { return style.diameter < 56.0f; }
    juce::Point<float> centre() const noexcept { return getLocalBounds().toFloat().getCentre(); }

    void renderBody (juce::Graphics& g, juce::Rectangle<float> area) const;
    void renderCap (juce::Graphics& g, juce::Rectangle<float> area) const;
    void drawKnurling (juce::Graphics& g, float angle) const;
    void drawPointer (juce::Graphics& g, float angle) const;

    juce::RangedAudioParameter* parameter;
    juce::UndoManager* undoManager;
    KnobStyle style;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    std::function<juce::String (float)> valueToText;

    ValueBubble* valueBubble = nullptr;
    std::unique_ptr<juce::TextEditor> textEntry;

    drawing::CachedLayer bodyLayer, capLayer;

    float value01 = 0.0f;
    float dragRaw = 0.0f;
    float lastDragY = 0.0f;
    float wheelAccumulator = 0.0f;
    bool hovering = false, dragging = false, inWheelGesture = false, keyboardFocused = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace cerulean::ui
