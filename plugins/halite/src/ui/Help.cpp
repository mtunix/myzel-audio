#include "ui/Help.h"

#include "plugin/Parameters.h"

#include <map>

namespace halite::ui
{

namespace
{
    const std::map<juce::String, HelpText>& texts()
    {
        using namespace params;

        static const std::map<juce::String, HelpText> all {
            { threshold,         { "Threshold", "How far an ess may rise above the vowel around it before Halite reduces it." } },
            { thresholdAbs,      { "Threshold", "The level in dBFS the sibilance band may reach before Halite reduces it (Allround)." } },
            { range,             { "Range", "The most an ess is reduced: it comes down to the threshold, never further than this." } },
            { detectLow,         { "Detect Low", "The bottom of the sibilance band; the voice's body is measured below it." } },
            { detectHigh,        { "Detect High", "The top of the sibilance band." } },
            { focus,             { "Focus", "How much the noise and shape cues count, so bright vowels are left alone (Voice only)." } },
            { shape,             { "Shape", "How an ess is reduced: a cut over the band, only the bins that stick out, or the whole signal." } },
            { detection,         { "Detection", "Voice measures each ess against the voice around it; Allround uses the band's level alone." } },
            { speedAuto,         { "Speed", "How fast the Ess band lets go: Auto follows each ess's length, or a fixed release." } },
            { release,           { "Speed", "How fast the Ess band lets go: Auto follows each ess's length, or a fixed release." } },
            { lookahead,         { "Look-ahead", "How early the detector hears an ess, so its start is caught (Normal latency only)." } },
            { stereo,            { "Stereo", "Linked reduces both channels together; Unlinked each on its own; Mid or Side only that part." } },
            { key,               { "Key", "What the detectors listen to: the input itself, or the side-chain input." } },
            { keyHpf,            { "Key HPF", "A high-pass on what the detectors hear, to keep low rumble out of the key." } },
            { output,            { "Output", "The level after Halite, in dB." } },
            { bypass,            { "Bypass", "Passes the input through, latency-aligned, so you can compare." } },
            { listen,            { "Listen", "What you hear: the output, what Halite removes, or what each detector hears." } },
            { harshOn,           { "Harsh", "Switches the second band on: a dynamic cut for a voice that pushes in its upper mids." } },
            { harshThreshold,    { "Harsh threshold", "How far the upper mids may rise above the voice's body before the Harsh band reduces them." } },
            { harshThresholdAbs, { "Harsh threshold", "The level in dBFS the Harsh band may reach before it is reduced (Allround)." } },
            { harshRange,        { "Harsh range", "The most the Harsh band is reduced." } },
            { harshFreq,         { "Harsh freq", "The centre of the Harsh band." } },
            { harshWidth,        { "Harsh width", "How wide the Harsh band is, in octaves." } },
            { harshSpeedAuto,    { "Harsh speed", "How fast the Harsh band lets go: Auto follows how long it stays over, or a fixed release." } },
            { harshRelease,      { "Harsh speed", "How fast the Harsh band lets go: Auto follows how long it stays over, or a fixed release." } },

            { "learn",           { "Learn", "Listens to the next 8 seconds and sets Threshold from the esses it heard (and Harsh threshold with Harsh on)." } },
            { "bandEss",         { "Ess band", "Shows the Ess band's controls; its cut and handles are in the accent." } },
            { "bandHarsh",       { "Harsh band", "Shows the Harsh band's controls; the switch turns the band on or off." } },
            { "more",            { "More", "Opens the Advanced row: Ess speed, look-ahead, stereo, key, key high-pass and output." } },
            { "preset",          { "Preset", "Click to choose, save or rename a preset; the dot means it has unsaved changes." } },
            { "presetPrev",      { "Previous preset", "Steps to the preset before this one." } },
            { "presetNext",      { "Next preset", "Steps to the next preset." } },
            { "ab",              { "A/B", "Two sounds to compare; right-click to copy the one you hear to the other." } },
            { "undo",            { "Undo", "Takes back the last edit (Ctrl or Cmd + Z)." } },
            { "redo",            { "Redo", "Does the undone edit again (Ctrl or Cmd + Shift + Z)." } },
            { "latency",         { "Latency", "What Halite tells the host: one Spectral frame in Normal, none in Live (Settings, Latency)." } },
            { "settings",        { "Settings", "Latency, theme, window size and help." } },
            { "view",            { "View", "Timeline shows the last seconds as they pass; Cloud every ess of its zoom window as a dot." } },
            { "zoom",            { "Zoom", "How many seconds the view spans." } },
            { "freeze",          { "Freeze", "Holds the display still while the audio runs on; click again to return to live." } },
            { "meterIn",         { "In", "The input's peak, its hold and its RMS in dBFS; click to reset the hold." } },
            { "meterOut",        { "Out", "The output's peak, its hold and its RMS in dBFS; click to reset the hold." } },
            { "meterCut",        { "Cut", "Each band's reduction now, Ess beside Harsh, with the deepest held; click to reset it." } },
            { "events",          { "Events", "Shows or hides the events sidebar: totals, every ess of the last minute, and the Learn histogram." } },
            { "totals",          { "Totals", "The esses Halite cut in the last minute, their average cut and the largest." } },
            { "eventList",       { "Events", "Every ess of the last minute with its time, peak and cut; click one to mark it in the display." } },
            { "learnHistogram",  { "Learn histogram", "The peaks of the last minute's esses against the threshold line; Learn shows what it hears instead." } },
            { "learnHistogramDone", { "Learn histogram", "What Learn heard and the threshold it set; click to return to the last minute's esses." } },
        };
        return all;
    }
}

HelpText helpFor (const juce::String& id)
{
    if (const auto it = texts().find (id); it != texts().end())
        return it->second;

    return {};
}

HelpText withHint (HelpText h, const juce::String& hint)
{
    auto text = h.text.trimEnd();
    if (text.endsWithChar ('.'))
        text = text.dropLastCharacters (1);
    h.text = text + "; " + hint + ".";
    return h;
}

juce::StringArray helpIds()
{
    juce::StringArray ids;
    for (const auto& [id, help] : texts())
        ids.add (id);
    return ids;
}

} // namespace halite::ui
