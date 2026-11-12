#include "ui/PanelController.h"

#include "dsp/NoteValues.h"
#include "plugin/PluginProcessor.h"

namespace mulberry::ui
{

PanelController::PanelController (MulberryProcessor& p)
    : EditorController (p, p.getPresets()),
      processor (p)
{
}

Meanings PanelController::readMeanings() const
{
    Meanings m;
    m.machine = static_cast<Machine> (juce::jlimit (0, numMachines - 1, choice (params::machine)));
    m.mode = static_cast<Mode> (juce::jlimit (0, numModes - 1, choice (params::mode)));
    m.synced = flag (params::sync);
    m.holdMode = choice (params::holdMode) == (int) HoldMode::momentary ? HoldMode::momentary : HoldMode::latch;
    return m;
}

const char* PanelController::timeParameterId (const Meanings& m) noexcept
{
    return m.synced ? params::note : params::time;
}

const char* PanelController::modeAmountParameterId (const Meanings& m) noexcept
{
    switch (m.mode)
    {
        case Mode::dual:    return m.synced ? params::noteRight : params::timeRight;
        case Mode::pattern: return params::steps;
        case Mode::single:
        case Mode::pingPong: break;
    }
    return params::width;
}

juce::String PanelController::modeAmountLegend (Mode m)
{
    switch (m)
    {
        case Mode::dual:    return "RIGHT TIME";
        case Mode::pattern: return "STEPS";
        case Mode::single:
        case Mode::pingPong: break;
    }
    return "WIDTH";
}

juce::String PanelController::characterLegend (Machine m, bool isParamB)
{
    return isParamB ? info (m).paramB : info (m).paramA;
}

int PanelController::characterSteps (Machine m, bool isParamB) noexcept
{
    return isParamB ? info (m).paramBSteps : info (m).paramASteps;
}

juce::String PanelController::characterText (const Meanings& m, bool isParamB, float value01) const
{
    return params::characterText (m.machine, isParamB, value01 * 100.0f);
}

juce::String PanelController::noteText (int noteIndex) const
{
    const auto note = juce::jlimit (0, notes::count - 1, noteIndex);
    const auto folded = notes::fold (notes::lengthMs (note, currentBpm()), (double) maps::maxTimeMs);
    auto text = noteName (note) + "  " + params::formatMs (folded.ms);
    if (folded.octaves > 0)
        text << juce::String::fromUTF8 (" \xc2\xb7 folded");
    return text;
}

juce::String PanelController::noteName (int noteIndex)
{
    return notes::names[(size_t) juce::jlimit (0, notes::count - 1, noteIndex)];
}

juce::String PanelController::noteLengthText (int noteIndex) const
{
    const auto bpm = juce::jlimit (20.0, 999.0, currentBpm());
    const auto folded = notes::fold (notes::lengthMs (juce::jlimit (0, notes::count - 1, noteIndex), bpm), (double) maps::maxTimeMs);
    const auto wholeBpm = std::abs (bpm - std::round (bpm)) < 0.05;
    auto text = params::formatMs (folded.ms) + " at " + juce::String (bpm, wholeBpm ? 0 : 1) + " BPM";
    if (folded.octaves > 0)
        text << ", folded " << folded.octaves << (folded.octaves == 1 ? " octave" : " octaves");
    return text;
}

int PanelController::noteIndexFromText (const juce::String& text)
{
    const auto token = text.trim().upToFirstOccurrenceOf (" ", false, false).trim();
    for (int i = 0; i < notes::count; ++i)
        if (token.equalsIgnoreCase (notes::names[(size_t) i]))
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

EchoShape PanelController::currentShape() const
{
    const auto cs = processor.readSettings (currentBpm());
    EchoShape s;
    s.machine = cs.machine;
    s.plan = cs.plan;
    s.plan.headMask = cs.machine == Machine::loop ? character::headMask (cs.paramA) : 1;
    s.timeMs = cs.timeMs;
    s.feedback = cs.feedback;
    s.synced = flag (params::sync);
    s.bpm = currentBpm();
    s.hold = cs.hold;
    s.bypassed = cs.bypass;
    s.foldedOctaves = processor.getMeters().foldedOctaves.load (std::memory_order_relaxed);
    return s;
}

bool PanelController::tap (double now)
{
    if (! taps.empty() && now - taps.back() > 2.0)
        taps.clear();
    taps.push_back (now);
    if (taps.size() > 5)
        taps.erase (taps.begin());
    if (taps.size() < 2)
        return false;

    const auto ms = (taps.back() - taps.front()) / (double) (taps.size() - 1) * 1000.0;
    auto& time = parameter (params::time);
    auto& sync = parameter (params::sync);
    time.beginChangeGesture();
    time.setValueNotifyingHost (time.convertTo0to1 ((float) juce::jlimit ((double) maps::minTimeMs, (double) maps::maxTimeMs, ms)));
    time.endChangeGesture();
    if (sync.getValue() > 0.5f)
    {
        sync.beginChangeGesture();
        sync.setValueNotifyingHost (0.0f);
        sync.endChangeGesture();
    }
    return true;
}

juce::String PanelController::machineDescription (Machine m)
{
    switch (m)
    {
        case Machine::clean:  return "Clean: ideal memory. A: Glide (how a Time change happens: crossfade to tape glide), B: Shift (each repeat climbs or falls by an interval).";
        case Machine::rack:   return "Rack: an early rack digital delay with a gain-ranging converter at its own clock. A: Bits, B: Rate (bandwidth). Time jumps with a crossfade.";
        case Machine::bucket: return "Bucket: a bucket-brigade chip, clocked. Longer times clock lower: darker and grittier. A: Chip (stages), B: Compander.";
        case Machine::oilCan: return "Oil Can: an electrostatic drum in oil. Warbling, smeared, softer with longer times. A: Brushes (one pickup to a cluster), B: Oil (fresh to old).";
        case Machine::loop:   return "Loop: a tape loop with three playback heads. Time is the motor: changing it bends every echo. A: Heads, B: Wear.";
        case Machine::reel:   return "Reel: a studio tape deck with a sliding head: the tone stays put across Time. A: Speed (7.5 / 15 / 30 ips), B: Bias.";
    }
    return {};
}

juce::String PanelController::modeDescription (Mode m)
{
    switch (m)
    {
        case Mode::single:   return "Single: each side echoes itself; Width sets the stereo image (0 = mono echo).";
        case Mode::dual:     return "Dual: two delays, left at Time and right at Right time, with Cross-feed between them (Advanced).";
        case Mode::pingPong: return "Ping-Pong: the input bounces left, right, left at Time; Width sets how far.";
        case Mode::pattern:  return "Pattern: up to 16 steps over Time, each with its level (drag them in the echo map); Pan rule and Groove in Advanced.";
    }
    return {};
}

} // namespace mulberry::ui
