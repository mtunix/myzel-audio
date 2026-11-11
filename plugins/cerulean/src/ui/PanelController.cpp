#include "ui/PanelController.h"

#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

namespace cerulean::ui
{

PanelController::PanelController (CeruleanProcessor& p)
    : processor (p),
      presets (p.getPresets()),
      ab (p, presets)
{
}

juce::UndoManager& PanelController::getUndoManager() noexcept
{
    return processor.getUndoManager();
}

juce::RangedAudioParameter& PanelController::parameter (const char* id) const
{
    auto* param = processor.getState().getParameter (id);
    jassert (param != nullptr);
    return *param;
}

bool PanelController::flag (const char* id) const
{
    return processor.getState().getRawParameterValue (id)->load (std::memory_order_relaxed) >= 0.5f;
}

int PanelController::choice (const char* id) const
{
    return juce::roundToInt (processor.getState().getRawParameterValue (id)->load (std::memory_order_relaxed));
}

//==============================================================================
Meanings PanelController::readMeanings() const
{
    Meanings m;
    m.algorithm = static_cast<Algorithm> (juce::jlimit (0, numAlgorithms - 1, choice (params::algorithm)));
    m.preDelaySynced = flag (params::preDelaySync);
    m.echoNoteOnParamA = m.algorithm == Algorithm::echo && flag (params::echoSync);
    m.freezeMode = choice (params::freezeMode) == (int) FreezeMode::momentary ? FreezeMode::momentary : FreezeMode::latch;
    return m;
}

const char* PanelController::preDelayParameterId (const Meanings& m) noexcept
{
    return m.preDelaySynced ? params::preDelayNote : params::preDelay;
}

const char* PanelController::paramAParameterId (const Meanings& m) noexcept
{
    return m.echoNoteOnParamA ? params::echoNote : params::paramA;
}

juce::String PanelController::characterLegend (Algorithm a, bool isParamB)
{
    const auto legends = params::characterLegends (a);
    return isParamB ? legends.second : legends.first;
}

int PanelController::characterSteps (Algorithm a, bool isParamB) noexcept
{
    if (! isParamB)
    {
        switch (a)
        {
            case Algorithm::spring: return 3;
            case Algorithm::halo:   return 5;
            case Algorithm::choir:  return 5;
            case Algorithm::room:
            case Algorithm::hall:
            case Algorithm::plate:
            case Algorithm::nimbus:
            case Algorithm::rise:
            case Algorithm::billow:
            case Algorithm::echo:
                break;
        }

        return 0;
    }

    return a == Algorithm::choir ? 4 : 0;
}

juce::String PanelController::preDelayText (const Meanings& m, float value01) const
{
    if (m.preDelaySynced)
        return noteText (juce::roundToInt (value01 * (float) (numNoteValues - 1)));

    return parameter (params::preDelay).getText (value01, 32);
}

juce::String PanelController::characterText (const Meanings& m, bool isParamB, float value01) const
{
    if (! isParamB && m.echoNoteOnParamA)
        return noteText (juce::roundToInt (value01 * (float) (numNoteValues - 1)));

    return params::characterText (m.algorithm, isParamB, value01 * 100.0f);
}

juce::String PanelController::noteText (int noteIndex) const
{
    const auto note = juce::jlimit (0, numNoteValues - 1, noteIndex);
    const auto bpm = juce::jlimit (20.0, 999.0, currentBpm());
    const auto ms = 60000.0 / bpm * noteValueInBeats (static_cast<NoteValue> (note));
    return params::noteValueNames()[note] + "  " + juce::String (juce::roundToInt (ms)) + " ms";
}

juce::String PanelController::noteName (int noteIndex)
{
    return params::noteValueNames()[juce::jlimit (0, numNoteValues - 1, noteIndex)];
}

juce::String PanelController::noteLengthText (int noteIndex) const
{
    const auto note = juce::jlimit (0, numNoteValues - 1, noteIndex);
    const auto bpm = juce::jlimit (20.0, 999.0, currentBpm());
    const auto ms = 60000.0 / bpm * noteValueInBeats (static_cast<NoteValue> (note));
    const auto wholeBpm = std::abs (bpm - std::round (bpm)) < 0.05;
    return juce::String (juce::roundToInt (ms)) + " ms at " + juce::String (bpm, wholeBpm ? 0 : 1) + " BPM";
}

int PanelController::noteIndexFromText (const juce::String& text)
{
    const auto token = text.trim().upToFirstOccurrenceOf (" ", false, false).trim();
    const auto names = params::noteValueNames();

    for (int i = 0; i < names.size(); ++i)
        if (names[i].equalsIgnoreCase (token))
            return i;

    return -1;
}

double PanelController::currentBpm() const
{
    return processor.getMeters().bpm.load (std::memory_order_relaxed);
}

bool PanelController::hostHasTempo() const
{
    return processor.getMeters().hostHasTempo.load (std::memory_order_relaxed);
}

//==============================================================================
TailShape PanelController::currentTailShape() const
{
    const auto cs = processor.readSettings (currentBpm());

    TailShape s;
    s.algorithm = cs.algorithm;
    s.decaySeconds = cs.engine.decaySeconds;
    s.preDelayMs = cs.preDelayMs;
    s.sizeScale = cs.engine.sizeScale;
    s.lowEnd = cs.engine.lowEnd;
    s.tone = cs.engine.tone;
    s.paramA = cs.engine.paramA;
    s.paramB = cs.engine.paramB;
    s.echoTimeMs = cs.engine.echoTimeMs;
    s.diffusion = cs.engine.diffusion;
    s.freeze = cs.freeze;
    s.layering = cs.freeze && cs.freezeLayer;
    s.bypassed = flag (params::bypass);
    s.trailsRinging = s.bypassed && flag (params::trails);
    return s;
}

juce::String PanelController::algorithmDescription (Algorithm a)
{
    switch (a)
    {
        case Algorithm::room:   return "Room: small to medium rooms, early-reflection heavy and natural. A: Early/Late balance, B: Walls (soft to hard).";
        case Algorithm::hall:   return "Hall: big, smooth, slightly modulated concert hall. A: Early reflections, B: Build.";
        case Algorithm::plate:  return "Plate: bright, dense, instantly diffuse plate. A: Density, B: Metal.";
        case Algorithm::spring: return "Spring: guitar-amp spring tank with drip and chirp. A: number of Springs, B: Drip.";
        case Algorithm::nimbus: return "Nimbus: huge, dark, endlessly diffuse ambient cloud. A: Density, B: Distance.";
        case Algorithm::halo:   return "Halo: shimmer, octaves rising in the tail. A: Interval, B: Amount of shifted signal.";
        case Algorithm::rise:   return "Rise: volume swell, notes fade in with no attack. A: Rise time, B: Blend (wet only or both).";
        case Algorithm::billow: return "Billow: a reverb that keeps building after the note. A: Bloom length, B: Feedback.";
        case Algorithm::choir:  return "Choir: vowel-like voices emerging from the tail. A: Vowel, B: number of Voices.";
        case Algorithm::echo:   return "Echo: tape echo into a plate. A: Echo time (or note value with Echo Sync), B: Repeats.";
    }

    return {};
}

juce::String PanelController::algorithmTagline (Algorithm a)
{
    switch (a)
    {
        case Algorithm::room:   return "Natural space";
        case Algorithm::hall:   return "Concert hall";
        case Algorithm::plate:  return "Dense, bright";
        case Algorithm::spring: return "Amp tank";
        case Algorithm::nimbus: return "Ambient cloud";
        case Algorithm::halo:   return "Shimmer";
        case Algorithm::rise:   return "Volume swell";
        case Algorithm::billow: return "Blooming";
        case Algorithm::choir:  return "Vowel voices";
        case Algorithm::echo:   return "Tape into plate";
    }

    return {};
}

//==============================================================================
ViewMode PanelController::getView() const
{
    const auto stored = processor.getUiState().getProperty (uistate::view).toString();
    return viewModeFromString (stored, viewPreference::getDefault());
}

void PanelController::setView (ViewMode mode)
{
    processor.getUiState().setProperty (uistate::view, toString (mode), nullptr);
    viewPreference::setDefault (mode);
}

} // namespace cerulean::ui
