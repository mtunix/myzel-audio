#pragma once

#include "dsp/EqParams.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace viridian
{

namespace params
{
    // Parameter IDs are part of saved state and host automation: never rename.
    inline constexpr auto model     = "model";
    inline constexpr auto hpfFreq   = "hpfFreq";
    inline constexpr auto lowFreq   = "lowFreq";
    inline constexpr auto lowGain   = "lowGain";
    inline constexpr auto lowQ      = "lowQ";
    inline constexpr auto lowShape  = "lowShape";
    inline constexpr auto lmFreq    = "lmFreq";
    inline constexpr auto lmGain    = "lmGain";
    inline constexpr auto lmQ       = "lmQ";
    inline constexpr auto hmFreq    = "hmFreq";
    inline constexpr auto hmGain    = "hmGain";
    inline constexpr auto hmQ       = "hmQ";
    inline constexpr auto highFreq  = "highFreq";
    inline constexpr auto highGain  = "highGain";
    inline constexpr auto highQ     = "highQ";
    inline constexpr auto highShape = "highShape";
    inline constexpr auto lpfFreq   = "lpfFreq";
    inline constexpr auto alt       = "alt";
    inline constexpr auto drive     = "drive";
    inline constexpr auto eqIn      = "eqIn";
    inline constexpr auto output    = "output";
    inline constexpr auto stereo    = "stereo";
    inline constexpr auto tolerance = "tolerance";
    inline constexpr auto noise     = "noise";
    inline constexpr auto bypass    = "bypass";

    /** The slot controls' IDs, indexed by Control. */
    inline constexpr std::array<const char*, numControls> controlIds {
        hpfFreq, lowFreq, lowGain, lowQ, lowShape, lmFreq, lmGain, lmQ,
        hmFreq, hmGain, hmQ, highFreq, highGain, highQ, highShape, lpfFreq
    };

    inline const char* controlId (Control c) noexcept { return controlIds[(size_t) c]; }

    // Saved in sessions and preset files: never rename.
    inline constexpr auto stateTag  = "ViridianState";
    inline constexpr auto presetTag = "ViridianPreset";

    /** Version hints, see juce::ParameterID: 1 for the v1 set. Later sections (dynamics, `dyn…`) take 2. */
    inline constexpr int versionHint = 1;

    inline constexpr float driveMinDb    = -12.0f;
    inline constexpr float driveMaxDb    = 24.0f;
    inline constexpr float outputRangeDb = 24.0f;

    /** The parameter set. The slot parameters read out (and parse) through the layout's own Model and Alt. */
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::StringArray modelNames();
    juce::StringArray stereoModeNames();

    /** A slot control's read-out on a model: "110 Hz", "+4 dB", "7.5", "Bell", "Off", "Unused". */
    juce::String controlText (Model, bool altOn, Control, float position);

    /** Reads a typed value ("1.6k", "-4 dB", "off", "bell") to its knob position; -1 if not understood. */
    float controlPositionFromText (Model, bool altOn, Control, const juce::String& text);

    /** Parameters that make up the sound (stored in presets, A/B, recorded for undo). */
    const juce::StringArray& soundParameterIds();
}

/** Cached raw parameter pointers for lock-free reads on the audio thread. */
struct ParameterRefs
{
    explicit ParameterRefs (juce::AudioProcessorValueTreeState& apvts);

    std::atomic<float>* model {};
    std::array<std::atomic<float>*, numControls> controls {};
    std::atomic<float>* alt {};
    std::atomic<float>* drive {};
    std::atomic<float>* eqIn {};
    std::atomic<float>* output {};
    std::atomic<float>* stereo {};
    std::atomic<float>* tolerance {};
    std::atomic<float>* noise {};

    juce::AudioParameterBool* bypass {};
};

} // namespace viridian
