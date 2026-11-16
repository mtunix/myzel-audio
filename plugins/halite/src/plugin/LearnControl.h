#pragma once

#include "dsp/detector/Learn.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <functional>

namespace halite
{

/**
    What Learn is doing, for the UI (PLAN.md, Learn). The state follows Threshold; whether Harsh
    Threshold moved too is result().harshSet, in either finished state.
*/
enum class LearnState
{
    idle = 0,
    listening,      // the capture runs
    learned,        // Threshold was set; the captured histogram stays, marked Learned
    tooFew          // too few esses: Threshold stayed, and the display says so
};

/**
    Learn on the message thread (PLAN.md, Learn). start() asks the chain for an 8 s capture; when it
    is done, Threshold (and Harsh Threshold, if Harsh was on) is set as one host-visible change:
    every gesture begins before any value moves and ends after, so the undo recorder records one
    step inside the transaction the processor opens in beforeApplying. A capture with too few esses
    keeps Threshold. Its outcome stays until the next edit (a gesture on any sound parameter: a
    control, a preset, an undo; not the host's automation, Bypass or Listen) or dismiss(). Nothing
    here runs on its own, and nothing is stored.
*/
class LearnControl final : private juce::Timer,
                           private juce::AudioProcessorParameter::Listener
{
public:
    LearnControl (juce::AudioProcessorValueTreeState&, dsp::Learn&);
    ~LearnControl() override;

    /** Message thread: capture the next 8 s. Pressed again, the capture starts over. */
    void start();

    /** Message thread: applies a finished capture. The timer calls it while listening; tests may too. */
    void service();

    /** Message thread: the Learned histogram leaves the display (the next edit, or a click on it). */
    void dismiss() noexcept;

    LearnState state() const noexcept { return current; }

    /** The last finished capture's conclusion (counts, and what was set). */
    const dsp::LearnResult& result() const noexcept { return last; }

    /** 0 … 1 of the capture done while listening. */
    float progress() const noexcept;

    /** Called just before Learn changes the thresholds: the processor opens its undo transaction here. */
    std::function<void()> beforeApplying;

    static constexpr int pollHz = 20;

private:
    void timerCallback() override { service(); }
    void apply (dsp::LearnResult&, Detection);

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool starting) override;

    juce::AudioProcessorValueTreeState& apvts;
    dsp::Learn& learn;
    std::vector<bool> edits;        // per parameter index: a gesture on it is an edit
    bool applying = false;
    std::uint32_t pending = 0;      // the capture asked for; 0: none
    LearnState current = LearnState::idle;
    dsp::LearnResult last;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LearnControl)
};

} // namespace halite
