#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace umber
{

/** Saturation circuit models, in panel order. */
enum class Model
{
    reel = 0,   // tape record amp + magnetic tape
    desk,       // transformer console line amp
    classA,     // single-ended discrete transistor stage
    valve,      // triode preamp stage
    fuzz        // overdriven pentode / two-stage clipper
};

inline constexpr int numModels = 5;

/** Which part of a stereo signal goes through the circuit (mono input: always everything). */
enum class StereoMode
{
    stereo = 0, // left and right, each through its own circuit
    mid,        // only the mid (L+R); the sides pass untouched
    side        // only the sides (L-R); the mid passes untouched
};

/** Realtime oversampling quality. */
enum class Quality
{
    eco = 0,    // 2x
    standard,   // 4x
    high        // 8x
};

/** Oversampling used for offline (non-realtime) rendering. */
enum class RenderQuality
{
    sameAsRealtime = 0,
    high,       // 8x
    ultra       // 16x
};

namespace params
{
    // Parameter IDs are part of saved state and host automation: never rename.
    inline constexpr auto drive         = "drive";
    inline constexpr auto model         = "model";
    inline constexpr auto slam          = "slam";
    inline constexpr auto tone          = "tone";
    inline constexpr auto lowCut        = "lowCut";
    inline constexpr auto weight        = "weight";
    inline constexpr auto highCut       = "highCut";
    inline constexpr auto autoGain      = "autoGain";
    inline constexpr auto mix           = "mix";
    inline constexpr auto output        = "output";
    inline constexpr auto quality       = "quality";
    inline constexpr auto renderQuality = "renderQuality";
    inline constexpr auto bypass        = "bypass";
    inline constexpr auto delta         = "delta";
    inline constexpr auto stereoMode    = "stereoMode";
    inline constexpr auto emphasis      = "emphasis";
    inline constexpr auto bias          = "bias";

    /** Version hints, see juce::ParameterID: 1 for the original set, 2 for parameters added later. */
    inline constexpr int versionHint = 1;
    inline constexpr int versionHintV2 = 2;

    // Ranges shared between the layout, the DSP and the tests.
    inline constexpr float driveMax       = 10.0f;
    inline constexpr float toneRange      = 10.0f;  // -10 (dark) .. +10 (bright)
    inline constexpr float lowCutMinHz    = 20.0f;  // minimum position means "Off"
    inline constexpr float lowCutMaxHz    = 500.0f;
    inline constexpr float highCutMinHz   = 2000.0f;
    inline constexpr float highCutMaxHz   = 20000.0f; // maximum position means "Off"
    inline constexpr float outputRangeDb  = 24.0f;
    inline constexpr float emphasisRange  = 10.0f;  // -10 (lows into the circuit) .. +10 (highs)
    inline constexpr float biasRange      = 10.0f;  // -10 (cold) .. +10 (hot)

    inline bool isLowCutOff  (float hz) noexcept { return hz <= lowCutMinHz + 0.01f; }
    inline bool isHighCutOff (float hz) noexcept { return hz >= highCutMaxHz - 0.5f; }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::StringArray modelNames();
    juce::StringArray stereoModeNames();
}

/** Cached raw parameter pointers for lock-free reads on the audio thread. */
struct ParameterRefs
{
    explicit ParameterRefs (juce::AudioProcessorValueTreeState& apvts);

    std::atomic<float>* drive {};
    std::atomic<float>* model {};
    std::atomic<float>* slam {};
    std::atomic<float>* tone {};
    std::atomic<float>* lowCut {};
    std::atomic<float>* weight {};
    std::atomic<float>* highCut {};
    std::atomic<float>* autoGain {};
    std::atomic<float>* mix {};
    std::atomic<float>* output {};
    std::atomic<float>* quality {};
    std::atomic<float>* renderQuality {};
    std::atomic<float>* delta {};
    std::atomic<float>* stereoMode {};
    std::atomic<float>* emphasis {};
    std::atomic<float>* bias {};

    juce::AudioParameterBool* bypass {};
};

} // namespace umber
