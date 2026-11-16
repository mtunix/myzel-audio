#include "plugin/LearnControl.h"

#include "plugin/Parameters.h"

#include <array>

namespace halite
{

LearnControl::LearnControl (juce::AudioProcessorValueTreeState& state, dsp::Learn& l)
    : apvts (state), learn (l)
{
    const auto& parameters = apvts.processor.getParameters();
    edits.assign ((size_t) parameters.size(), false);
    for (int i = 0; i < parameters.size(); ++i)
    {
        const auto* p = dynamic_cast<const juce::RangedAudioParameter*> (parameters[i]);
        edits[(size_t) i] = p != nullptr && params::soundParameterIds().contains (p->getParameterID());
        parameters[i]->addListener (this);
    }
}

LearnControl::~LearnControl()
{
    stopTimer();
    for (auto* p : apvts.processor.getParameters())
        p->removeListener (this);
}

void LearnControl::parameterGestureChanged (int index, bool starting)
{
    // Gestures come from the plugin's own side (controls, presets, undo), on the message thread.
    if (starting && ! applying && juce::isPositiveAndBelow (index, (int) edits.size()) && edits[(size_t) index]
        && juce::MessageManager::existsAndIsCurrentThread())
        dismiss();
}

void LearnControl::start()
{
    pending = learn.request();
    current = LearnState::listening;
    startTimerHz (pollHz);
}

void LearnControl::service()
{
    if (pending == 0)
    {
        stopTimer();
        return;
    }
    // The id first: once it is ours, the phase is this capture's.
    if (learn.captureId() != pending || learn.phase() != dsp::Learn::Phase::captured)
        return;

    // The audio thread leaves the captured histograms alone until the next request, which only start() makes.
    dsp::LevelHistogram::Counts esses {}, voiced {};
    learn.capturedEsses().read (esses);
    learn.capturedVoiced().read (voiced);
    last = dsp::learnThresholds (esses, learn.capturedEsses().total(), voiced, learn.capturedVoiced().total(), learn.capturedHarshOn(),
                                 learn.capturedHopSeconds(), learn.getTuning());
    pending = 0;
    stopTimer();

    current = last.essSet ? LearnState::learned : LearnState::tooFew;
    apply (last, learn.capturedDetection());
}

void LearnControl::dismiss() noexcept
{
    if (current == LearnState::learned || current == LearnState::tooFew)
        current = LearnState::idle;
}

float LearnControl::progress() const noexcept
{
    if (current != LearnState::listening)
        return current == LearnState::idle ? 0.0f : 1.0f;
    return learn.captureId() == pending ? learn.progress() : 0.0f;
}

void LearnControl::apply (dsp::LearnResult& r, Detection detection)
{
    const auto voice = detection == Detection::voice;
    std::array<std::pair<juce::RangedAudioParameter*, float>, 2> changes {};
    auto n = 0;
    if (r.essSet)
        changes[(size_t) n++] = { apvts.getParameter (voice ? params::threshold : params::thresholdAbs), r.essDb };
    if (r.harshSet)
        changes[(size_t) n++] = { apvts.getParameter (voice ? params::harshThreshold : params::harshThresholdAbs), r.harshDb };
    if (n == 0)
        return;

    if (beforeApplying)
        beforeApplying();
    const juce::ScopedValueSetter<bool> guard (applying, true);
    for (int i = 0; i < n; ++i)
        changes[(size_t) i].first->beginChangeGesture();
    // The range clamps and snaps to its 0.01 dB steps.
    for (int i = 0; i < n; ++i)
        changes[(size_t) i].first->setValueNotifyingHost (changes[(size_t) i].first->convertTo0to1 (changes[(size_t) i].second));
    for (int i = 0; i < n; ++i)
        changes[(size_t) i].first->endChangeGesture();

    // What the parameters now hold, for the display.
    const auto held = [] (const juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); };
    if (r.essSet)
        r.essDb = held (changes[0].first);
    if (r.harshSet)
        r.harshDb = held (changes[(size_t) n - 1].first);
}

} // namespace halite
