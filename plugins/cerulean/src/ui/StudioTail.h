#pragma once

#include "ui/Drawing.h"
#include "ui/StudioTheme.h"
#include "ui/TailModel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>
#include <vector>

namespace cerulean::ui
{

/**
    The studio view's tail graph: the decay envelope of the low, mid and high bands drawn on
    labelled time and dB axes, with the target RT60 of each band, the pre-delay and the early
    reflections (or Echo's repeats), and the live wet level running across it.

    Like the panel's tail display it is a picture of the settings (TailModel), not a measurement.
    The axes and curves are cached and re-rendered only when the shape changes; the live level
    is drawn per frame, and only while it moves.

    Two handles make the picture editable: drag the end of the mid line (where it meets the
    floor, one RT60 after the pre-delay) to set Decay, and the pre-delay marker to set Pre-delay.
    Shift drags finely, double-click resets; each drag is one host gesture and one undo step.
    The time axis holds still while a handle is dragged and rescales on release.

    Bypassed, the graph dims and an amber badge says so.
*/
class StudioTail final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    StudioTail();

    void setShape (const TailShape& shape);

    /** Binds the two drag handles. Pre-delay can be dragged only while it is in ms (not synced). */
    void attachHandles (juce::RangedAudioParameter& decay, juce::RangedAudioParameter& preDelay, juce::UndoManager* undoManager);
    void setPreDelayDraggable (bool canDrag);

    /** Leaves the head of the card free left of this x (the Decay value) and right of readoutRight (Freeze). */
    void setHeadLayout (float readoutLeft, float readoutRight);

    // Test hooks: where the handles are drawn (local), or an empty optional while hidden.
    std::optional<juce::Point<float>> getDecayHandle() const;
    std::optional<juce::Point<float>> getPreDelayHandle() const;
    const TailShape& getShape() const noexcept { return shape; }

    /** Advances the live trace by dt with the current wet level (linear RMS). */
    void tick (float dtSeconds, float wetRms);

    /** Test hook: fills the history with a synthetic decaying note. */
    void fillHistoryForSnapshot (float peakDb, float holdSeconds);

    /** The read-outs as text (tests, accessibility), e.g. "Low 4.20 s, Mid 3.00 s, High 2.40 s". */
    juce::String getReadoutText() const;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr float historyRate = 60.0f;
    static constexpr int historySize = 60 * 64;

private:
    juce::Rectangle<float> plotArea() const;
    float xForTime (float t) const noexcept;
    float yForDb (float db) const noexcept;
    float historyAt (long long entry) const noexcept;

    void renderStatic (juce::Graphics& g, juce::Rectangle<float> area) const;
    void drawLive (juce::Graphics& g) const;
    void drawHandles (juce::Graphics& g) const;

    enum class Handle { none, decay, preDelay };
    Handle handleAt (juce::Point<float> p) const;
    juce::ParameterAttachment* attachmentFor (Handle h) const;
    void setHovered (Handle h);

    juce::SharedResourcePointer<Fonts> fonts;
    TailShape shape;
    float span = 4.0f;

    std::vector<float> history;
    long long written = 0;
    float pending = 0.0f;
    float smoothedDb = tail::floorDb;
    long long shownColumn = -1;
    bool liveVisible = false;

    std::unique_ptr<juce::ParameterAttachment> decayAttachment, preDelayAttachment;
    juce::RangedAudioParameter* decayParam = nullptr;
    juce::RangedAudioParameter* preDelayParam = nullptr;
    bool preDelayDraggable = true;
    Handle hovered = Handle::none, dragged = Handle::none;
    float dragStartValue = 0.0f, dragStartTime = 0.0f;
    float headLeft = 0.0f, headRight = -1.0f;

    drawing::CachedLayer staticLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioTail)
};

} // namespace cerulean::ui
