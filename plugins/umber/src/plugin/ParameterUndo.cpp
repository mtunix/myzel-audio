#include "plugin/ParameterUndo.h"

namespace umber
{

class ParameterUndoRecorder::ParameterChange final : public juce::UndoableAction
{
public:
    ParameterChange (ParameterUndoRecorder& r, int i, float before, float after)
        : recorder (r), index (i), oldValue (before), newValue (after) {}

    bool perform() override
    {
        // The value is already in place when the action is recorded; only re-apply on redo.
        if (! firstPerform)
            recorder.apply (index, newValue);

        firstPerform = false;
        return true;
    }

    bool undo() override
    {
        recorder.apply (index, oldValue);
        return true;
    }

    int getSizeInUnits() override { return (int) sizeof (*this); }

private:
    ParameterUndoRecorder& recorder;
    const int index;
    const float oldValue, newValue;
    bool firstPerform = true;
};

ParameterUndoRecorder::ParameterUndoRecorder (juce::AudioProcessor& processor, juce::UndoManager& um,
                                              const juce::StringArray& excludedIds)
    : undoManager (um), parameters (processor.getParameters())
{
    gestureStartValue.assign ((size_t) parameters.size(), 0.0f);
    gestureDepth.assign ((size_t) parameters.size(), 0);
    excluded.assign ((size_t) parameters.size(), false);

    for (int i = 0; i < parameters.size(); ++i)
        if (auto* withId = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameters[i]))
            excluded[(size_t) i] = excludedIds.contains (withId->getParameterID());

    for (auto* p : parameters)
        p->addListener (this);
}

ParameterUndoRecorder::~ParameterUndoRecorder()
{
    for (auto* p : parameters)
        p->removeListener (this);
}

void ParameterUndoRecorder::parameterGestureChanged (int index, bool gestureIsStarting)
{
    if (applying || ! juce::isPositiveAndBelow (index, parameters.size()) || excluded[(size_t) index])
        return;

    if (! juce::MessageManager::existsAndIsCurrentThread())
        return;

    auto& depth = gestureDepth[(size_t) index];
    auto* parameter = parameters[index];

    if (gestureIsStarting)
    {
        if (depth++ == 0)
            gestureStartValue[(size_t) index] = parameter->getValue();
        return;
    }

    if (depth == 0 || --depth > 0)
        return;

    const auto before = gestureStartValue[(size_t) index];
    const auto after = parameter->getValue();

    if (! juce::approximatelyEqual (before, after))
        undoManager.perform (new ParameterChange (*this, index, before, after));
}

void ParameterUndoRecorder::apply (int index, float normalisedValue)
{
    auto* parameter = parameters[index];
    const juce::ScopedValueSetter<bool> guard (applying, true);

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (normalisedValue);
    parameter->endChangeGesture();
}

} // namespace umber
