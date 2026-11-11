#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace cerulean
{

/**
    Records user edits — and only user edits — into an UndoManager.

    The APVTS is deliberately built WITHOUT an undo manager: it would also record host automation
    and every getStateInformation() call. Instead this recorder listens for change *gestures*
    (begin/end), which only interactive edits produce (our editor, presets, A/B, a host's generic
    UI), and records one action per parameter from its value at gesture start to its value at
    gesture end. Host automation playback sends no gestures, so it never lands in the history.

    Callers group several parameter changes into one step with UndoManager::beginNewTransaction()
    (the editor's attachments and the preset manager already do).

    Message thread only: gestures arriving on any other thread are ignored, because UndoManager
    is not thread-safe.
*/
class ParameterUndoRecorder final : private juce::AudioProcessorParameter::Listener
{
public:
    /** Parameters listed in excludedIds (e.g. monitoring switches like Bypass) are never recorded. */
    ParameterUndoRecorder (juce::AudioProcessor& processor, juce::UndoManager& undoManager,
                           const juce::StringArray& excludedIds = {});
    ~ParameterUndoRecorder() override;

private:
    class ParameterChange;

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool gestureIsStarting) override;

    void apply (int index, float normalisedValue);

    juce::UndoManager& undoManager;
    juce::Array<juce::AudioProcessorParameter*> parameters;
    std::vector<float> gestureStartValue;
    std::vector<int> gestureDepth;
    std::vector<bool> excluded;
    bool applying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterUndoRecorder)
};

} // namespace cerulean
