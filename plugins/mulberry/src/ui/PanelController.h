#pragma once

#include "common/ui/EditorController.h"
#include "plugin/Parameters.h"
#include "ui/EchoModel.h"
#include "ui/FactoryPresets.h"
#include "ui/Theme.h"

namespace mulberry
{
class MulberryProcessor;
}

namespace mulberry::ui
{

/**
    What the meaning-changing parameters currently say: Param A/B relabel with the machine, Time
    is a note value with Sync, the mode knob (Ⓜ) is Width, Right time (or note) or Steps with the
    mode, and Hold Mode makes Hold momentary.
*/
struct Meanings
{
    Machine machine = Machine::reel;
    Mode mode = Mode::single;
    bool synced = true;
    HoldMode holdMode = HoldMode::latch;

    bool operator== (const Meanings&) const = default;
};

/**
    The editor's model, shared by both views: presets, A/B, the parameter meanings, value texts,
    the echo shape and Tap. It outlives the views. Message thread only.
*/
class PanelController : public EditorController
{
public:
    explicit PanelController (MulberryProcessor& processor);

    MulberryProcessor& getProcessor() noexcept { return processor; }

    Meanings readMeanings() const;

    /** The parameter the Time control edits: time, or note with Sync. */
    static const char* timeParameterId (const Meanings& m) noexcept;

    /** The parameter the mode knob edits: width (Single, Ping-Pong), timeRight / noteRight (Dual), steps (Pattern). */
    static const char* modeAmountParameterId (const Meanings& m) noexcept;

    /** The mode knob's legend ("WIDTH", "RIGHT TIME", "STEPS"). */
    static juce::String modeAmountLegend (Mode m);

    static juce::String characterLegend (Machine m, bool isParamB);
    static int characterSteps (Machine m, bool isParamB) noexcept;

    juce::String characterText (const Meanings& m, bool isParamB, float value01) const;

    /** A note value with its length at the current tempo, e.g. "1/8D  375 ms" (folded: "1/1D  3.0 s / 2"). */
    juce::String noteText (int noteIndex) const;
    static juce::String noteName (int noteIndex);
    juce::String noteLengthText (int noteIndex) const;
    static int noteIndexFromText (const juce::String& text);

    double currentBpm() const;
    bool hostHasTempo() const;

    /** The echo map's picture, from the current settings. */
    EchoShape currentShape() const;

    /**
        Tap: two or more taps set Time (free, in ms) and turn Sync off; the average of the last
        four intervals; a 2 s pause starts over. Returns true when it set the time.
    */
    bool tap (double nowSeconds);

    static juce::String machineDescription (Machine m);
    static juce::String machineTagline (Machine m) { return info (m).tagline; }
    static juce::String modeDescription (Mode m);

private:
    MulberryProcessor& processor;
    std::vector<double> taps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelController)
};

} // namespace mulberry::ui
