#pragma once

#include "dsp/Modes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>

namespace basalt
{

namespace params
{
    // Parameter IDs are part of saved state and host automation: never rename (PLAN.md, Parameter IDs).
    inline constexpr auto model       = "model";
    inline constexpr auto vcaDetector = "vcaDetector";
    inline constexpr auto threshold   = "threshold";
    inline constexpr auto ratio       = "ratio";
    inline constexpr auto knee        = "knee";
    inline constexpr auto attack      = "attack";
    inline constexpr auto release     = "release";
    inline constexpr auto autoRelease = "autoRelease";
    inline constexpr auto range       = "range";
    inline constexpr auto floor       = "floor";
    inline constexpr auto gain        = "gain";
    inline constexpr auto autoGain    = "autoGain";
    inline constexpr auto mix         = "mix";
    inline constexpr auto character   = "character";
    inline constexpr auto drive       = "drive";
    inline constexpr auto charPost    = "charPost";
    inline constexpr auto driveLink   = "driveLink";
    inline constexpr auto target      = "target";
    inline constexpr auto lookahead   = "lookahead";
    inline constexpr auto stereo      = "stereo";
    inline constexpr auto link        = "link";
    inline constexpr auto key         = "key";
    inline constexpr auto keyHpf      = "keyHpf";
    inline constexpr auto keyLpf      = "keyLpf";
    inline constexpr auto keyBellFreq = "keyBellFreq";
    inline constexpr auto keyBellGain = "keyBellGain";
    inline constexpr auto keyBellQ    = "keyBellQ";
    inline constexpr auto listen      = "listen";
    inline constexpr auto output      = "output";
    inline constexpr auto bypass      = "bypass";

    // Saved in sessions and preset files: never rename.
    inline constexpr auto stateTag  = "BasaltState";
    inline constexpr auto presetTag = "BasaltPreset";

    /** Version hint of every parameter, see juce::ParameterID: 1 for the v1 set. */
    inline constexpr int versionHint = 1;

    // Ranges and defaults (PLAN.md, Controls), shared by the layout, the DSP and the tests. Frozen
    // with the IDs: hosts store automation normalised, so no range or mapping may move.
    inline constexpr float thresholdMinDb     = -60.0f;
    inline constexpr float thresholdDefaultDb = -18.0f;
    inline constexpr float ratioMin           = 1.0f;
    inline constexpr float ratioMax           = 20.0f;
    inline constexpr float ratioDefault       = 4.0f;
    inline constexpr float kneeMaxDb          = 24.0f;
    inline constexpr float kneeDefaultDb      = 6.0f;
    inline constexpr float attackMinMs        = 0.02f;
    inline constexpr float attackMaxMs        = 300.0f;
    inline constexpr float attackDefaultMs    = 10.0f;
    inline constexpr float releaseMinMs       = 5.0f;
    inline constexpr float releaseMaxMs       = 5000.0f;
    inline constexpr float releaseDefaultMs   = 150.0f;
    inline constexpr float rangeMaxDb         = 40.0f;
    inline constexpr float rangeDefaultDb     = 40.0f;
    inline constexpr float floorMinDb         = -90.0f;
    inline constexpr float floorMaxDb         = -30.0f;
    inline constexpr float floorDefaultDb     = -70.0f;
    inline constexpr float gainRangeDb        = 24.0f;
    inline constexpr float driveMaxDb         = 24.0f;
    inline constexpr float driveDefaultDb     = 6.0f;
    inline constexpr float targetMinDb        = 1.0f;
    inline constexpr float targetMaxDb        = 12.0f;
    inline constexpr float targetDefaultDb    = 3.0f;
    inline constexpr float lookaheadMaxMs     = 10.0f;
    inline constexpr float keyHpfMinHz        = 20.0f;      // the lowest position is Off
    inline constexpr float keyHpfMaxHz        = 2000.0f;
    inline constexpr float keyLpfMinHz        = 2000.0f;
    inline constexpr float keyLpfMaxHz        = 20000.0f;   // the highest position is Off
    inline constexpr float keyBellMinHz       = 40.0f;
    inline constexpr float keyBellMaxHz       = 16000.0f;
    inline constexpr float keyBellDefaultHz   = 1000.0f;
    inline constexpr float keyBellRangeDb     = 18.0f;
    inline constexpr float keyBellQMin        = 0.3f;
    inline constexpr float keyBellQMax        = 6.0f;
    inline constexpr float keyBellQDefault    = 1.0f;
    inline constexpr float outputRangeDb      = 24.0f;

    /**
        Ratio's knob: 1 … 20 logarithmic over the first ratioLogTravel of the travel, then one step
        to ∞, stored as ratioInfinity. Positions inside the step snap to 20 or ∞, whichever is
        nearer. A plain value, not a sentinel far away, so that an LV2 host's linear slider
        (lv2:minimum 1, lv2:maximum 21) still reads 1 … 20 and then ∞.
    */
    inline constexpr float ratioInfinity  = 21.0f;
    inline constexpr float ratioLogTravel = 0.9f;

    inline bool isRatioInfinite (float stored) noexcept { return stored >= 0.5f * (ratioMax + ratioInfinity); }

    /** 1 / ratio, the gain computer's slope term: 1 at Ratio 1, exactly 0 at ∞. */
    inline float inverseRatio (float stored) noexcept
    {
        return isRatioInfinite (stored) ? 0.0f : 1.0f / juce::jlimit (ratioMin, ratioMax, stored);
    }

    /** Off wherever the read-out would round to 20 Hz, so the text and the filter agree. */
    inline bool isKeyHpfOff (float hz) noexcept { return juce::roundToInt (hz) <= juce::roundToInt (keyHpfMinHz); }

    /** Off wherever the read-out would round to 20.0 kHz. */
    inline bool isKeyLpfOff (float hz) noexcept { return std::round (hz / 100.0f) >= keyLpfMaxHz / 100.0f; }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::StringArray modelNames();
    juce::StringArray vcaDetectorNames();
    juce::StringArray characterNames();
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

    std::atomic<float>* model {};
    std::atomic<float>* vcaDetector {};
    std::atomic<float>* threshold {};
    std::atomic<float>* ratio {};
    std::atomic<float>* knee {};
    std::atomic<float>* attack {};
    std::atomic<float>* release {};
    std::atomic<float>* autoRelease {};
    std::atomic<float>* range {};
    std::atomic<float>* floor {};
    std::atomic<float>* gain {};
    std::atomic<float>* autoGain {};
    std::atomic<float>* mix {};
    std::atomic<float>* character {};
    std::atomic<float>* drive {};
    std::atomic<float>* charPost {};
    std::atomic<float>* driveLink {};
    std::atomic<float>* target {};
    std::atomic<float>* lookahead {};
    std::atomic<float>* stereo {};
    std::atomic<float>* link {};
    std::atomic<float>* key {};
    std::atomic<float>* keyHpf {};
    std::atomic<float>* keyLpf {};
    std::atomic<float>* keyBellFreq {};
    std::atomic<float>* keyBellGain {};
    std::atomic<float>* keyBellQ {};
    std::atomic<float>* listen {};
    std::atomic<float>* output {};

    juce::AudioParameterBool* bypass {};
};

} // namespace basalt
