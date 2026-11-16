#include "ui/Bindings.h"

#include "plugin/Parameters.h"

namespace halite::ui
{

namespace
{
    /** One wheel notch or arrow key in travel: a round step in the parameter's unit (Shift takes a fifth). */
    float stepFor (const juce::String& id)
    {
        using namespace params;

        if (id == threshold)                         return 0.5f / (thresholdMaxDb - thresholdMinDb);     // 0.5 dB
        if (id == harshThreshold)                    return 0.5f / (harshThresholdMaxDb - harshThresholdMinDb);
        if (id == thresholdAbs || id == harshThresholdAbs) return 0.5f / -absThresholdMinDb;
        if (id == range)                             return 0.5f / rangeMaxDb;
        if (id == harshRange)                        return 0.25f / harshRangeMaxDb;
        if (id == focus)                             return 0.01f;                                          // 1 %
        if (id == detectLow || id == detectHigh)     return 1.0f / (12.0f * std::log2 (detectMaxHz / detectMinHz));   // 1/12 octave
        if (id == harshFreq)                         return 1.0f / (12.0f * std::log2 (harshFreqMaxHz / harshFreqMinHz));
        if (id == harshWidth)                        return 0.05f / (harshWidthMaxOct - harshWidthMinOct);
        if (id == lookahead)                         return 0.1f / lookaheadMaxMs;
        if (id == output)                            return 0.1f / (2.0f * outputRangeDb);
        return 1.0f / 40.0f;
    }

    juce::String withoutUnit (const juce::String& text)
    {
        for (const auto* unit : { " dBFS", " dB" })
            if (text.endsWith (unit))
                return text.dropLastCharacters ((int) strlen (unit));

        return text.replace (" %", "%");
    }
}

ParameterBinding::ParameterBinding (HaliteController& c, IdSource source)
    : controller (c), idSource (std::move (source))
{
}

ParameterBinding::ParameterBinding (HaliteController& c, const char* id)
    : ParameterBinding (c, [id] { return id; })
{
}

HelpText ParameterBinding::help() const
{
    return helpFor (currentId());
}

float ParameterBinding::position() const
{
    return controller.normalised (editing != nullptr ? editing : currentId());
}

float ParameterBinding::defaultPosition() const
{
    return controller.parameter (currentId()).getDefaultValue();
}

void ParameterBinding::begin()
{
    if (editing != nullptr)
        return;

    editing = currentId();
    controller.beginEdit (editing);
}

void ParameterBinding::setPosition (float travel)
{
    if (editing != nullptr)
        controller.parameter (editing).setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, travel));
}

void ParameterBinding::end()
{
    if (editing != nullptr)
    {
        controller.endEdit (editing);
        editing = nullptr;
    }
}

juce::String ParameterBinding::text() const
{
    return controller.text (currentId());
}

juce::String ParameterBinding::shortText() const
{
    return withoutUnit (text());
}

void ParameterBinding::setFromText (const juce::String& typed)
{
    controller.setFromText (currentId(), typed);
}

float ParameterBinding::step() const
{
    return stepFor (currentId());
}

juce::StringArray ParameterBinding::parameterIds() const
{
    return { currentId() };
}

//==============================================================================
SpeedBinding::SpeedBinding (HaliteController& c, const char* autoParameter, const char* releaseParameter)
    : controller (c), autoId (autoParameter), releaseId (releaseParameter)
{
}

HelpText SpeedBinding::help() const
{
    return helpFor (autoId);
}

bool SpeedBinding::isAuto() const
{
    return controller.normalised (autoId) >= 0.5f;
}

float SpeedBinding::position() const
{
    return isAuto() ? 0.0f : autoSpan + (1.0f - autoSpan) * controller.normalised (releaseId);
}

float SpeedBinding::stepFrom (float position, float travel) const
{
    // Up from Auto: the fixed release as it was left; down past the first release: Auto.
    if (isAuto())
        return travel > 0.0f ? autoSpan + (1.0f - autoSpan) * controller.normalised (releaseId) : 0.0f;

    return position + travel < autoSpan ? 0.0f : position + travel;
}

void SpeedBinding::begin()
{
    if (editing)
        return;

    editing = true;
    controller.beginEdit (autoId);
    controller.beginEdit (releaseId, false);
}

void SpeedBinding::setPosition (float travel)
{
    if (! editing)
        return;

    auto& automatic = controller.parameter (autoId);

    if (travel < autoSpan)
    {
        if (automatic.getValue() < 0.5f)
            automatic.setValueNotifyingHost (1.0f);
        return;
    }

    if (automatic.getValue() >= 0.5f)
        automatic.setValueNotifyingHost (0.0f);

    controller.parameter (releaseId).setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (travel - autoSpan) / (1.0f - autoSpan)));
}

void SpeedBinding::end()
{
    if (editing)
    {
        controller.endEdit (releaseId);
        controller.endEdit (autoId);
        editing = false;
    }
}

juce::String SpeedBinding::text() const
{
    return isAuto() ? juce::String ("Auto") : controller.text (releaseId);
}

void SpeedBinding::setFromText (const juce::String& typed)
{
    const auto t = typed.trim().toLowerCase();

    if (t.startsWith ("a"))
    {
        if (! isAuto())
            controller.setNormalised (autoId, 1.0f);
        return;
    }

    // A time: Auto goes off and the release takes it, as one step. Text without a number changes nothing.
    if (! t.containsAnyOf ("0123456789"))
        return;

    auto& release = controller.parameter (releaseId);
    const auto value = release.getValueForText (t);
    begin();
    controller.parameter (autoId).setValueNotifyingHost (0.0f);
    release.setValueNotifyingHost (value);
    end();
}

} // namespace halite::ui
