#pragma once

#include "dsp/MachineInfo.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace mulberry
{

namespace params
{
    // Parameter IDs are part of saved state and host automation: never rename.
    // Front panel.
    inline constexpr auto machine   = "machine";
    inline constexpr auto mode      = "mode";
    inline constexpr auto time      = "time";
    inline constexpr auto sync      = "sync";
    inline constexpr auto note      = "note";
    inline constexpr auto hold      = "hold";
    inline constexpr auto feedback  = "feedback";
    inline constexpr auto drive     = "drive";
    inline constexpr auto wobble    = "wobble";
    inline constexpr auto diffuse   = "diffuse";
    inline constexpr auto lowCut    = "lowCut";
    inline constexpr auto highCut   = "highCut";
    inline constexpr auto paramA    = "paramA";
    inline constexpr auto paramB    = "paramB";
    inline constexpr auto timeRight = "timeRight";
    inline constexpr auto noteRight = "noteRight";
    inline constexpr auto width     = "width";
    inline constexpr auto steps     = "steps";
    inline constexpr auto mix       = "mix";
    inline constexpr auto output    = "output";
    inline constexpr auto duck      = "duck";
    inline constexpr auto wetOnly   = "wetOnly";
    inline constexpr auto bypass    = "bypass";

    // Advanced deck.
    inline constexpr auto groove        = "groove";
    inline constexpr auto feel          = "feel";
    inline constexpr auto crossFeed     = "crossFeed";
    inline constexpr auto panRule       = "panRule";
    inline constexpr auto wobbleRate    = "wobbleRate";
    inline constexpr auto wobbleShape   = "wobbleShape";
    inline constexpr auto noise         = "noise";
    inline constexpr auto trails        = "trails";
    inline constexpr auto duckAmount    = "duckAmount";
    inline constexpr auto duckThreshold = "duckThreshold";
    inline constexpr auto duckRelease   = "duckRelease";
    inline constexpr auto bpm           = "bpm";
    inline constexpr auto holdMode      = "holdMode";

    // Pattern step levels: step1 .. step16.
    inline constexpr int maxSteps = 16;
    juce::String stepId (int index);    // 0-based index -> "step1" ...

    inline constexpr int versionHint = 1;

    inline constexpr float driveMinDb       = -12.0f;
    inline constexpr float driveMaxDb       = 24.0f;
    inline constexpr float lowCutOffHz      = 20.0f;      // minimum position means "Off"
    inline constexpr float lowCutMaxHz      = 1000.0f;
    inline constexpr float highCutMinHz     = 1000.0f;
    inline constexpr float highCutOffHz     = 20000.0f;   // maximum position means "Off"
    inline constexpr float outputRangeDb    = 24.0f;
    inline constexpr float feelMaxMs        = 30.0f;
    inline constexpr float wobbleRateMin    = 0.25f;
    inline constexpr float wobbleRateMax    = 4.0f;
    inline constexpr float duckAmountMaxDb  = 30.0f;
    inline constexpr float duckThresholdMin = -60.0f;
    inline constexpr float duckReleaseMinMs = 50.0f;
    inline constexpr float duckReleaseMaxMs = 2000.0f;
    inline constexpr float bpmMin           = 40.0f;
    inline constexpr float bpmMax           = 240.0f;

    inline bool isLowCutOff  (float hz) noexcept { return hz <= lowCutOffHz + 0.01f; }
    inline bool isHighCutOff (float hz) noexcept { return hz >= highCutOffHz - 0.5f; }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::StringArray machineNames();
    juce::StringArray modeNames();
    juce::StringArray noteNames();
    juce::StringArray panRuleNames();

    /** Display text of Param A / Param B on a machine ("1+2+3", "12 bits", "+7 st", "15 ips"). */
    juce::String characterText (Machine m, bool isParamB, float percent);

    /** Reads a typed Param A / B value: a displayed value, its number, or a percentage. -1 if not understood. */
    float characterPercentFromText (Machine m, bool isParamB, const juce::String& text);

    juce::String formatMs (double ms);

    /** Parameters that make up the sound (stored in presets, A/B, recorded for undo). */
    const juce::StringArray& soundParameterIds();
}

/** Cached raw parameter pointers for lock-free reads on the audio thread. */
struct ParameterRefs
{
    explicit ParameterRefs (juce::AudioProcessorValueTreeState& apvts);

    std::atomic<float>* machine {};
    std::atomic<float>* mode {};
    std::atomic<float>* time {};
    std::atomic<float>* sync {};
    std::atomic<float>* note {};
    std::atomic<float>* hold {};
    std::atomic<float>* feedback {};
    std::atomic<float>* drive {};
    std::atomic<float>* wobble {};
    std::atomic<float>* diffuse {};
    std::atomic<float>* lowCut {};
    std::atomic<float>* highCut {};
    std::atomic<float>* paramA {};
    std::atomic<float>* paramB {};
    std::atomic<float>* timeRight {};
    std::atomic<float>* noteRight {};
    std::atomic<float>* width {};
    std::atomic<float>* steps {};
    std::atomic<float>* mix {};
    std::atomic<float>* output {};
    std::atomic<float>* duck {};
    std::atomic<float>* wetOnly {};

    std::atomic<float>* groove {};
    std::atomic<float>* feel {};
    std::atomic<float>* crossFeed {};
    std::atomic<float>* panRule {};
    std::atomic<float>* wobbleRate {};
    std::atomic<float>* wobbleShape {};
    std::atomic<float>* noise {};
    std::atomic<float>* trails {};
    std::atomic<float>* duckAmount {};
    std::atomic<float>* duckThreshold {};
    std::atomic<float>* duckRelease {};
    std::atomic<float>* bpm {};
    std::atomic<float>* holdMode {};

    std::array<std::atomic<float>*, params::maxSteps> stepLevels {};

    juce::AudioParameterBool* bypass {};
};

} // namespace mulberry
