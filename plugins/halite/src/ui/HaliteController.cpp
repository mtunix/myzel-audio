#include "ui/HaliteController.h"

#include "dsp/chain/HaliteChain.h"
#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

namespace halite::ui
{

HaliteController::HaliteController (HaliteProcessor& p)
    : EditorController (p, p.getPresets()),
      processor (p)
{
}

juce::ValueTree HaliteController::ui() const
{
    return processor.getUiState();
}

float HaliteController::plain (const char* id) const
{
    auto& p = parameter (id);
    return p.convertFrom0to1 (p.getValue());
}

float HaliteController::normalised (const char* id) const
{
    return parameter (id).getValue();
}

juce::String HaliteController::text (const char* id) const
{
    auto& p = parameter (id);
    return withMinus (p.getText (p.getValue(), 64));
}

void HaliteController::setNormalised (const char* id, float value01)
{
    auto& p = parameter (id);
    value01 = juce::jlimit (0.0f, 1.0f, value01);

    getUndoManager().beginNewTransaction();
    p.beginChangeGesture();
    p.setValueNotifyingHost (value01);
    p.endChangeGesture();
}

void HaliteController::setPlain (const char* id, float value)
{
    setNormalised (id, parameter (id).convertTo0to1 (value));
}

void HaliteController::toggle (const char* id)
{
    setNormalised (id, normalised (id) >= 0.5f ? 0.0f : 1.0f);
}

void HaliteController::resetToDefault (const char* id)
{
    setNormalised (id, parameter (id).getDefaultValue());
}

void HaliteController::setFromText (const char* id, const juce::String& typed)
{
    auto& p = parameter (id);
    const auto value = p.getValueForText (typed.trim());

    if (! juce::approximatelyEqual (value, p.getValue()))
        setNormalised (id, value);
}

void HaliteController::beginEdit (const char* id, bool newTransaction)
{
    if (newTransaction)
        getUndoManager().beginNewTransaction();
    parameter (id).beginChangeGesture();
}

void HaliteController::endEdit (const char* id)
{
    parameter (id).endChangeGesture();
}

Detection HaliteController::detection() const
{
    return (Detection) juce::jlimit (0, numDetections - 1, choice (params::detection));
}

Shape HaliteController::shape() const
{
    return (Shape) juce::jlimit (0, numShapes - 1, choice (params::shape));
}

bool HaliteController::harshOn() const
{
    return flag (params::harshOn);
}

LatencyMode HaliteController::latency() const
{
    return processor.getLatencyMode();
}

const char* HaliteController::thresholdId (Band band) const
{
    const auto relative = detection() == Detection::voice;

    if (band == Band::harsh)
        return relative ? params::harshThreshold : params::harshThresholdAbs;

    return relative ? params::threshold : params::thresholdAbs;
}

juce::String HaliteController::latencyText() const
{
    if (latency() == LatencyMode::live)
        return "Live";

    // Before the host prepares, the likeliest rate: the read-out is the frame at 48 kHz.
    const auto rate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const auto ms = 1000.0 * dsp::HaliteChain::latencySamples (LatencyMode::normal, rate) / rate;
    return juce::String (ms, 1) + " ms";
}

juce::String HaliteController::modeSummary() const
{
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    const auto live = latency() == LatencyMode::live;

    auto shapeText = params::shapeNames()[(int) shape()];
    if (live && shape() == Shape::spectral)
        shapeText = "Split (Spectral needs Normal latency)";

    auto s = params::detectionNames()[(int) detection()] + dot + shapeText + dot
           + params::stereoModeNames()[juce::jlimit (0, numStereoModes - 1, choice (params::stereo))];

    s << dot << (live ? juce::String ("Live, no look-ahead")
                      : "look-ahead " + juce::String (plain (params::lookahead), 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " ms");

    if (choice (params::key) == (int) KeySource::external)
        s << dot << "side-chain key";

    return s;
}

void HaliteController::setLatency (LatencyMode mode)
{
    processor.setLatencyMode (mode);
    notifyUiChanged();
}

DisplayView HaliteController::getDisplayView() const
{
    return ui().getProperty (uistate::displayView).toString() == "cloud" ? DisplayView::cloud : DisplayView::timeline;
}

void HaliteController::setDisplayView (DisplayView v)
{
    ui().setProperty (uistate::displayView, v == DisplayView::cloud ? "cloud" : "timeline", nullptr);
    notifyUiChanged();
}

Band HaliteController::getBand() const
{
    return ui().getProperty (uistate::selectedBand).toString() == "harsh" ? Band::harsh : Band::ess;
}

void HaliteController::setBand (Band b)
{
    ui().setProperty (uistate::selectedBand, b == Band::harsh ? "harsh" : "ess", nullptr);
    notifyUiChanged();
}

bool HaliteController::isEventsShown() const
{
    return (bool) ui().getProperty (uistate::eventsShown, true);
}

void HaliteController::setEventsShown (bool shown)
{
    ui().setProperty (uistate::eventsShown, shown, nullptr);
    notifyUiChanged();
}

bool HaliteController::isAdvancedOpen() const
{
    return (bool) ui().getProperty (uistate::advancedOpen, false);
}

void HaliteController::setAdvancedOpen (bool open)
{
    ui().setProperty (uistate::advancedOpen, open, nullptr);
    notifyUiChanged();
}

float HaliteController::getZoom (DisplayView v) const
{
    if (v == DisplayView::cloud)
        return juce::jlimit (10.0f, 60.0f, (float) ui().getProperty (uistate::cloudZoom, 30.0f));

    return juce::jlimit (2.0f, 10.0f, (float) ui().getProperty (uistate::timelineZoom, 6.0f));
}

void HaliteController::setZoom (DisplayView v, float seconds)
{
    if (v == DisplayView::cloud)
        ui().setProperty (uistate::cloudZoom, juce::jlimit (10.0f, 60.0f, seconds), nullptr);
    else
        ui().setProperty (uistate::timelineZoom, juce::jlimit (2.0f, 10.0f, seconds), nullptr);

    notifyUiChanged();
}

void HaliteController::setHover (const juce::Component* source, const HelpText& help)
{
    hoverSource = source;
    hoverHelp = help;
}

void HaliteController::clearHover (const juce::Component* source)
{
    if (hoverSource == source)
    {
        hoverSource = nullptr;
        hoverHelp = {};
    }
}

void HaliteController::setFocusHelp (const juce::Component* source, const HelpText& help)
{
    focusSource = source;
    focusHelp = help;
}

void HaliteController::clearFocusHelp (const juce::Component* source)
{
    if (focusSource == source)
    {
        focusSource = nullptr;
        focusHelp = {};
    }
}

HelpText HaliteController::currentHelp() const
{
    if (flashLeft > 0.0f)
        return flashHelp;
    if (hoverSource != nullptr)
        return hoverHelp;
    if (focusSource != nullptr)
        return focusHelp;
    return {};
}

void HaliteController::flash (const juce::String& title, const juce::String& message)
{
    flashHelp = { title, message };
    flashLeft = flashSeconds;
}

void HaliteController::tick (float dt)
{
    if (flashLeft > 0.0f)
        flashLeft = std::max (0.0f, flashLeft - dt);
}

} // namespace halite::ui
