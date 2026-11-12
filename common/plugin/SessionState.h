#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace myzel::plugin
{

/**
    The session state every plugin saves: the APVTS tree (parameters plus the "UI" child, see
    UiState.h) with a version number, as binary XML.
*/
namespace sessionState
{
    inline const juce::Identifier versionId { "stateVersion" };

    /** Writes the current state, tagged with its version. */
    void write (juce::AudioProcessorValueTreeState& apvts, int version, juce::MemoryBlock& destData);

    /** Reads saved state; an invalid tree if the data is not this plugin's state. */
    juce::ValueTree read (const juce::AudioProcessorValueTreeState& apvts, const void* data, int sizeInBytes);

    /**
        Restores a tree from read(). replaceState() skips a parameter whose restored value equals
        its cached *snapped* value, so a bool a host set to a raw 0.76 would stay 0.76 when the
        session says 1: every parameter whose raw value is not what the session says is written.
        A stored value that is not finite (a damaged session) restores as the parameter's default.
    */
    void restore (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& state);
}

} // namespace myzel::plugin
