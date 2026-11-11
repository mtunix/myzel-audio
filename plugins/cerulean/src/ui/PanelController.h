#pragma once

#include "plugin/Parameters.h"
#include "ui/AbCompare.h"
#include "ui/PresetManager.h"
#include "ui/TailModel.h"
#include "ui/ViewMode.h"

namespace cerulean
{
class CeruleanProcessor;
}

namespace cerulean::ui
{

/**
    What the meaning-changing parameters currently say. Several controls change what they do
    with these: Param A/B relabel with the algorithm, Pre-delay becomes a note value with Sync,
    Param A picks the echo note with Echo + Echo Sync, and Freeze Mode makes Freeze momentary.
*/
struct Meanings
{
    Algorithm algorithm = Algorithm::hall;
    bool preDelaySynced = false;
    bool echoNoteOnParamA = false;      // Echo is selected and Echo Sync is on
    FreezeMode freezeMode = FreezeMode::latch;

    bool operator== (const Meanings&) const = default;
};

/**
    The editor's model, shared by both views (panel and studio): presets, A/B compare, the
    parameter meanings, value texts and the tail shape. It outlives the view, so switching views
    keeps the preset browser and A/B state, and the views never duplicate this logic.

    Message thread only.
*/
class PanelController
{
public:
    explicit PanelController (CeruleanProcessor& processor);

    CeruleanProcessor& getProcessor() noexcept { return processor; }
    PresetManager& getPresets() noexcept     { return presets; }
    AbCompare& getAb() noexcept              { return ab; }
    juce::UndoManager& getUndoManager() noexcept;

    juce::RangedAudioParameter& parameter (const char* id) const;
    bool flag (const char* id) const;
    int choice (const char* id) const;

    //==============================================================================
    Meanings readMeanings() const;

    /** The parameter the Pre-delay control edits: preDelay, or preDelayNote with Sync on. */
    static const char* preDelayParameterId (const Meanings& m) noexcept;

    /** The parameter the Param A control edits: paramA, or echoNote with Echo + Echo Sync. */
    static const char* paramAParameterId (const Meanings& m) noexcept;

    /** Param A / B legend for the algorithm (front-panel caps, e.g. "INTERVAL"). */
    static juce::String characterLegend (Algorithm a, bool isParamB);

    /** Positions of a stepped Param A / B (e.g. Halo's five intervals), or 0 if continuous. */
    static int characterSteps (Algorithm a, bool isParamB) noexcept;

    /** Value text of the Pre-delay control for a normalised value of the parameter it edits. */
    juce::String preDelayText (const Meanings& m, float value01) const;

    /** Value text of Param A / B for a normalised value of the parameter it edits. */
    juce::String characterText (const Meanings& m, bool isParamB, float value01) const;

    /** A note value with its length at the current tempo, e.g. "1/8D  375 ms". */
    juce::String noteText (int noteIndex) const;

    /** A note value's name alone, e.g. "1/8D". */
    static juce::String noteName (int noteIndex);

    /** A note value's length at the current tempo, e.g. "375 ms at 120 BPM". */
    juce::String noteLengthText (int noteIndex) const;

    /** Parses typed text into a note value index ("1/8D", "1/8d  375 ms"); -1 if none matches. */
    static int noteIndexFromText (const juce::String& text);

    double currentBpm() const;
    bool hostHasTempo() const;

    //==============================================================================
    /** The decay envelope the tail displays draw, from the current settings. */
    TailShape currentTailShape() const;

    /** One-line description of an algorithm (tooltips). */
    static juce::String algorithmDescription (Algorithm a);

    /** A few words on an algorithm's character (the studio view's algorithm list). */
    static juce::String algorithmTagline (Algorithm a);

    //==============================================================================
    /** The view stored in the session, or the user's default view if the session has none. */
    ViewMode getView() const;

    /** Stores the view in the session and makes it the default for new instances. */
    void setView (ViewMode mode);

private:
    CeruleanProcessor& processor;
    PresetManager& presets;   // the processor's (it outlives every editor)
    AbCompare ab;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelController)
};

} // namespace cerulean::ui
