#pragma once

#include "dsp/Modes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace halite
{

namespace params
{
    // Parameter IDs are part of saved state and host automation: never rename (PLAN.md, Parameter IDs).
    inline constexpr auto threshold         = "threshold";
    inline constexpr auto thresholdAbs      = "thresholdAbs";
    inline constexpr auto range             = "range";
    inline constexpr auto detectLow         = "detectLow";
    inline constexpr auto detectHigh        = "detectHigh";
    inline constexpr auto shape             = "shape";
    inline constexpr auto detection         = "detection";
    inline constexpr auto focus             = "focus";
    inline constexpr auto speedAuto         = "speedAuto";
    inline constexpr auto release           = "release";
    inline constexpr auto lookahead         = "lookahead";
    inline constexpr auto stereo            = "stereo";
    inline constexpr auto key               = "key";
    inline constexpr auto keyHpf            = "keyHpf";
    inline constexpr auto output            = "output";
    inline constexpr auto bypass            = "bypass";
    inline constexpr auto listen            = "listen";
    inline constexpr auto harshOn           = "harshOn";
    inline constexpr auto harshThreshold    = "harshThreshold";
    inline constexpr auto harshThresholdAbs = "harshThresholdAbs";
    inline constexpr auto harshRange        = "harshRange";
    inline constexpr auto harshFreq         = "harshFreq";
    inline constexpr auto harshWidth        = "harshWidth";
    inline constexpr auto harshSpeedAuto    = "harshSpeedAuto";
    inline constexpr auto harshRelease      = "harshRelease";

    // Saved in sessions and preset files: never rename.
    inline constexpr auto stateTag  = "HaliteState";
    inline constexpr auto presetTag = "HalitePreset";

    /** Version hint of every parameter, see juce::ParameterID: 1 for the v1 set. */
    inline constexpr int versionHint = 1;

    // Ranges and defaults (PLAN.md, Controls), shared by the layout, the DSP and the tests.
    // Thresholds in dB: relative over the voice, absolute in dBFS.
    // The relative ranges come from the corpus (NOTES.md, Measured (M1) and M1b) and are frozen. The
    // threshold defaults are what Learn sets over a whole corpus (M3, NOTES.md, M3: Learn): relative,
    // the tune split's speech; absolute, synthetic cymbals (Threshold) and speech (Harsh) at -18 dBFS RMS.
    inline constexpr float thresholdMinDb         = -36.0f;
    inline constexpr float thresholdMaxDb         = 24.0f;
    inline constexpr float thresholdDefaultDb     = -17.0f;
    inline constexpr float absThresholdMinDb      = -60.0f;
    inline constexpr float absThresholdDefaultDb  = -16.0f;
    inline constexpr float rangeMaxDb             = 24.0f;
    inline constexpr float rangeDefaultDb         = 8.0f;
    inline constexpr float detectMinHz            = 1500.0f;
    inline constexpr float detectMaxHz            = 20000.0f;
    inline constexpr float detectLowDefaultHz     = 4500.0f;
    inline constexpr float detectHighDefaultHz    = 12000.0f;
    inline constexpr float focusDefault           = 70.0f;    // %
    inline constexpr float releaseMinMs           = 5.0f;
    inline constexpr float releaseMaxMs           = 200.0f;
    inline constexpr float releaseDefaultMs       = 20.0f;    // used only with Speed Auto off
    inline constexpr float lookaheadMaxMs         = 10.0f;
    inline constexpr float lookaheadDefaultMs     = 5.0f;
    inline constexpr float keyHpfMinHz            = 20.0f;    // minimum position means "Off"
    inline constexpr float keyHpfMaxHz            = 2000.0f;
    inline constexpr float outputRangeDb          = 12.0f;
    inline constexpr float harshThresholdMinDb    = -36.0f;
    inline constexpr float harshThresholdMaxDb    = 12.0f;
    inline constexpr float harshThresholdDefaultDb = -9.9f;
    inline constexpr float harshAbsThresholdDefaultDb = -22.4f;
    inline constexpr float harshRangeMaxDb        = 12.0f;
    inline constexpr float harshRangeDefaultDb    = 4.0f;
    inline constexpr float harshFreqMinHz         = 1500.0f;
    inline constexpr float harshFreqMaxHz         = 6000.0f;
    inline constexpr float harshFreqDefaultHz     = 3200.0f;
    inline constexpr float harshWidthMinOct       = 0.3f;
    inline constexpr float harshWidthMaxOct       = 2.0f;
    inline constexpr float harshWidthDefaultOct   = 1.0f;
    inline constexpr float harshReleaseMinMs      = 10.0f;
    inline constexpr float harshReleaseMaxMs      = 300.0f;
    inline constexpr float harshReleaseDefaultMs  = 80.0f;    // used only with Harsh Speed Auto off

    /** Off wherever the read-out would round to 20 Hz, so the text and the filter agree. */
    inline bool isKeyHpfOff (float hz) noexcept { return juce::roundToInt (hz) <= juce::roundToInt (keyHpfMinHz); }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::StringArray shapeNames();
    juce::StringArray detectionNames();
    juce::StringArray stereoModeNames();
    juce::StringArray keySourceNames();
    juce::StringArray listenNames();

    /** Every parameter ID in the frozen order (PLAN.md, Parameter IDs), which is also the host's order. */
    const juce::StringArray& allParameterIds();

    /** Parameters that make up the sound (stored in presets, A/B, recorded for undo): all but Bypass and Listen. */
    const juce::StringArray& soundParameterIds();
}

/** Session settings that are not parameters, stored as properties of the state tree's root. */
namespace sessionKeys
{
    // Saved with the session: never rename. Not in presets or A/B.
    inline const juce::Identifier latencyMode { "latencyMode" };   // int, 0 = Normal, 1 = Live; absent = Normal
}

/** Cached raw parameter pointers for lock-free reads on the audio thread. */
struct ParameterRefs
{
    explicit ParameterRefs (juce::AudioProcessorValueTreeState& apvts);

    std::atomic<float>* threshold {};
    std::atomic<float>* thresholdAbs {};
    std::atomic<float>* range {};
    std::atomic<float>* detectLow {};
    std::atomic<float>* detectHigh {};
    std::atomic<float>* shape {};
    std::atomic<float>* detection {};
    std::atomic<float>* focus {};
    std::atomic<float>* speedAuto {};
    std::atomic<float>* release {};
    std::atomic<float>* lookahead {};
    std::atomic<float>* stereo {};
    std::atomic<float>* key {};
    std::atomic<float>* keyHpf {};
    std::atomic<float>* output {};
    std::atomic<float>* listen {};
    std::atomic<float>* harshOn {};
    std::atomic<float>* harshThreshold {};
    std::atomic<float>* harshThresholdAbs {};
    std::atomic<float>* harshRange {};
    std::atomic<float>* harshFreq {};
    std::atomic<float>* harshWidth {};
    std::atomic<float>* harshSpeedAuto {};
    std::atomic<float>* harshRelease {};

    juce::AudioParameterBool* bypass {};
};

} // namespace halite
