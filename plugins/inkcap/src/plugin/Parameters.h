#pragma once

#include "dsp/ModuleInfo.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace inkcap
{

namespace params
{
    // Parameter IDs are part of saved state and host automation: never rename (PLAN.md, Parameter IDs).
    inline constexpr auto damage = "damage";
    inline constexpr auto chaos  = "chaos";
    inline constexpr auto mix    = "mix";
    inline constexpr auto output = "output";
    inline constexpr auto bypass = "bypass";
    inline constexpr auto match  = "match";
    inline constexpr auto bpm    = "bpm";

    inline constexpr auto glitchOn     = "glitchOn";
    inline constexpr auto glitchAmount = "glitchAmount";
    inline constexpr auto glitchType   = "glitchType";
    inline constexpr auto glitchRate   = "glitchRate";
    inline constexpr auto glitchLength = "glitchLength";
    inline constexpr auto glitchGroove = "glitchGroove";

    inline constexpr auto warbleOn     = "warbleOn";
    inline constexpr auto warbleAmount = "warbleAmount";
    inline constexpr auto warbleType   = "warbleType";
    inline constexpr auto warbleRate   = "warbleRate";
    inline constexpr auto warbleSync   = "warbleSync";
    inline constexpr auto warbleSpread = "warbleSpread";

    inline constexpr auto crushOn     = "crushOn";
    inline constexpr auto crushAmount = "crushAmount";
    inline constexpr auto crushType   = "crushType";
    inline constexpr auto crushDrive  = "crushDrive";
    inline constexpr auto crushTone   = "crushTone";

    inline constexpr auto bitsOn     = "bitsOn";
    inline constexpr auto bitsAmount = "bitsAmount";
    inline constexpr auto bitsType   = "bitsType";
    inline constexpr auto bitsRate   = "bitsRate";
    inline constexpr auto bitsDepth  = "bitsDepth";
    inline constexpr auto bitsJitter = "bitsJitter";

    inline constexpr auto hissOn     = "hissOn";
    inline constexpr auto hissAmount = "hissAmount";
    inline constexpr auto hissType   = "hissType";
    inline constexpr auto hissTone   = "hissTone";
    inline constexpr auto hissFollow = "hissFollow";

    inline constexpr auto roomOn     = "roomOn";
    inline constexpr auto roomAmount = "roomAmount";
    inline constexpr auto roomType   = "roomType";
    inline constexpr auto roomSize   = "roomSize";
    inline constexpr auto roomDuck   = "roomDuck";

    /** Each module's On, Amount and Type, indexed by Module. */
    inline constexpr std::array<const char*, numModules> onIds     { glitchOn, warbleOn, crushOn, bitsOn, hissOn, roomOn };
    inline constexpr std::array<const char*, numModules> amountIds { glitchAmount, warbleAmount, crushAmount, bitsAmount, hissAmount, roomAmount };
    inline constexpr std::array<const char*, numModules> typeIds   { glitchType, warbleType, crushType, bitsType, hissType, roomType };

    // Saved in sessions and preset files: never rename.
    inline constexpr auto stateTag  = "InkcapState";
    inline constexpr auto presetTag = "InkcapPreset";

    /** Version hint of every v1 parameter, see juce::ParameterID. A parameter added later takes 2. */
    inline constexpr int versionHint = 1;

    inline constexpr float outputRangeDb = 24.0f;
    inline constexpr float grooveMin     = 50.0f;     // percent: the off-beat slot starts halfway (straight)
    inline constexpr float grooveMax     = 75.0f;     // a dotted-eighth feel
    inline constexpr float bpmMin        = 40.0f;
    inline constexpr float bpmMax        = 240.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::StringArray typeNames (Module);
    juce::StringArray glitchRateNames();

    /** Every parameter, in the layout's (and the host's) order. */
    const juce::StringArray& allParameterIds();

    /** Parameters that make up the sound: stored in presets and A/B, recorded for undo. */
    const juce::StringArray& soundParameterIds();

    /** Session settings and listening switches, never in presets or the undo history. */
    const juce::StringArray& settingParameterIds();
}

/** Cached raw parameter pointers for lock-free reads on the audio thread. */
struct ParameterRefs
{
    explicit ParameterRefs (juce::AudioProcessorValueTreeState& apvts);

    struct ModuleRefs
    {
        std::atomic<float>* on {};
        std::atomic<float>* amount {};
        std::atomic<float>* type {};
    };

    std::atomic<float>* damage {};
    std::atomic<float>* chaos {};
    std::atomic<float>* mix {};
    std::atomic<float>* output {};
    std::atomic<float>* match {};
    std::atomic<float>* bpm {};

    std::array<ModuleRefs, numModules> modules {};     // indexed by Module

    std::atomic<float>* glitchRate {};
    std::atomic<float>* glitchLength {};
    std::atomic<float>* glitchGroove {};
    std::atomic<float>* warbleRate {};
    std::atomic<float>* warbleSync {};
    std::atomic<float>* warbleSpread {};
    std::atomic<float>* crushDrive {};
    std::atomic<float>* crushTone {};
    std::atomic<float>* bitsRate {};
    std::atomic<float>* bitsDepth {};
    std::atomic<float>* bitsJitter {};
    std::atomic<float>* hissTone {};
    std::atomic<float>* hissFollow {};
    std::atomic<float>* roomSize {};
    std::atomic<float>* roomDuck {};

    juce::AudioParameterBool* bypass {};
};

} // namespace inkcap
