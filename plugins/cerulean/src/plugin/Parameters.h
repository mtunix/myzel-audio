#pragma once

#include "dsp/Algorithms.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace cerulean
{

/** Note values for tempo-synced times. Never reorder: saved in state. */
enum class NoteValue
{
    n1_32 = 0, n1_16T, n1_16, n1_16D, n1_8T, n1_8, n1_8D, n1_4T, n1_4, n1_4D, n1_2T, n1_2, n1_2D, n1_1
};

inline constexpr int numNoteValues = 14;

/** Length of a note value in quarter notes (beats). */
double noteValueInBeats (NoteValue v) noexcept;

enum class FreezeMode
{
    latch = 0,
    momentary
};

namespace params
{
    // Parameter IDs are part of saved state and host automation: never rename.
    // Front panel.
    inline constexpr auto algorithm   = "algorithm";
    inline constexpr auto decay       = "decay";
    inline constexpr auto freeze      = "freeze";
    inline constexpr auto preDelay    = "preDelay";
    inline constexpr auto size        = "size";
    inline constexpr auto mod         = "mod";
    inline constexpr auto tone        = "tone";
    inline constexpr auto paramA      = "paramA";
    inline constexpr auto paramB      = "paramB";
    inline constexpr auto mix         = "mix";
    inline constexpr auto output      = "output";
    inline constexpr auto duck        = "duck";
    inline constexpr auto wetOnly     = "wetOnly";
    inline constexpr auto bypass      = "bypass";

    // Advanced drawer.
    inline constexpr auto lowEnd        = "lowEnd";
    inline constexpr auto diffusion     = "diffusion";
    inline constexpr auto modRate       = "modRate";
    inline constexpr auto width         = "width";
    inline constexpr auto inLowCut      = "inLowCut";
    inline constexpr auto inHighCut     = "inHighCut";
    inline constexpr auto duckAmount    = "duckAmount";
    inline constexpr auto duckThreshold = "duckThreshold";
    inline constexpr auto duckRelease   = "duckRelease";
    inline constexpr auto preDelaySync  = "preDelaySync";
    inline constexpr auto preDelayNote  = "preDelayNote";
    inline constexpr auto echoSync      = "echoSync";
    inline constexpr auto echoNote      = "echoNote";
    inline constexpr auto bpm           = "bpm";
    inline constexpr auto freezeMode    = "freezeMode";
    inline constexpr auto trails        = "trails";

    // Added after v0.1 (new IDs only: existing ones never change).
    inline constexpr auto freezeLayer   = "freezeLayer";    // playing adds into a frozen tail

    inline constexpr int versionHint = 1;

    // Ranges shared between the layout, the DSP and the tests.
    inline constexpr float decayMinS        = 0.1f;
    inline constexpr float decayMaxS        = 60.0f;
    inline constexpr float preDelayMaxMs    = 500.0f;
    inline constexpr float toneRange        = 10.0f;    // -10 (dark) .. +10 (bright)
    inline constexpr float outputRangeDb    = 24.0f;
    inline constexpr float lowEndMin        = 0.5f;     // x low-band RT60
    inline constexpr float lowEndMax        = 2.0f;
    inline constexpr float modRateMin       = 0.25f;    // x each algorithm's nominal rate
    inline constexpr float modRateMax       = 4.0f;
    inline constexpr float widthMax         = 150.0f;   // percent
    inline constexpr float inLowCutMinHz    = 20.0f;    // minimum position means "Off"
    inline constexpr float inLowCutMaxHz    = 500.0f;
    inline constexpr float inHighCutMinHz   = 1000.0f;
    inline constexpr float inHighCutMaxHz   = 20000.0f; // maximum position means "Off"
    inline constexpr float duckAmountMaxDb  = 30.0f;
    inline constexpr float duckThresholdMin = -60.0f;
    inline constexpr float duckReleaseMinMs = 50.0f;
    inline constexpr float duckReleaseMaxMs = 2000.0f;
    inline constexpr float bpmMin           = 40.0f;
    inline constexpr float bpmMax           = 240.0f;

    inline bool isInLowCutOff  (float hz) noexcept { return hz <= inLowCutMinHz + 0.01f; }
    inline bool isInHighCutOff (float hz) noexcept { return hz >= inHighCutMaxHz - 0.5f; }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::StringArray algorithmNames();
    juce::StringArray noteValueNames();

    /** Front-panel legends of Param A / Param B for an algorithm (e.g. "INTERVAL", "AMOUNT"). */
    std::pair<juce::String, juce::String> characterLegends (Algorithm a);

    /** Display text of Param A / Param B for an algorithm (e.g. "+12", "1 spring", "Wet only"). */
    juce::String characterText (Algorithm a, bool isParamB, float percent);

    /**
        Reads typed text for Param A / Param B of an algorithm: a displayed value ("+7", "O",
        "2 springs", "Early 40", "Wet only"), a time for the time-valued controls ("350 ms",
        "1.2 s"), or a plain percentage. Returns the percent (0..100), or -1 if not understood.
    */
    float characterPercentFromText (Algorithm a, bool isParamB, const juce::String& text);

    /** Parameters that make up the sound (stored in presets, A/B, recorded for undo). */
    const juce::StringArray& soundParameterIds();
}

/** Cached raw parameter pointers for lock-free reads on the audio thread. */
struct ParameterRefs
{
    explicit ParameterRefs (juce::AudioProcessorValueTreeState& apvts);

    std::atomic<float>* algorithm {};
    std::atomic<float>* decay {};
    std::atomic<float>* freeze {};
    std::atomic<float>* preDelay {};
    std::atomic<float>* size {};
    std::atomic<float>* mod {};
    std::atomic<float>* tone {};
    std::atomic<float>* paramA {};
    std::atomic<float>* paramB {};
    std::atomic<float>* mix {};
    std::atomic<float>* output {};
    std::atomic<float>* duck {};
    std::atomic<float>* wetOnly {};

    std::atomic<float>* lowEnd {};
    std::atomic<float>* diffusion {};
    std::atomic<float>* modRate {};
    std::atomic<float>* width {};
    std::atomic<float>* inLowCut {};
    std::atomic<float>* inHighCut {};
    std::atomic<float>* duckAmount {};
    std::atomic<float>* duckThreshold {};
    std::atomic<float>* duckRelease {};
    std::atomic<float>* preDelaySync {};
    std::atomic<float>* preDelayNote {};
    std::atomic<float>* echoSync {};
    std::atomic<float>* echoNote {};
    std::atomic<float>* bpm {};
    std::atomic<float>* trails {};
    std::atomic<float>* freezeLayer {};

    juce::AudioParameterBool* bypass {};
};

} // namespace cerulean
