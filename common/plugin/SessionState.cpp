#include "common/plugin/SessionState.h"

#include <cmath>

namespace myzel::plugin::sessionState
{

namespace
{
    /** The saved tree, with a parameter value that is not finite replaced by its default. */
    juce::ValueTree withFiniteValues (const juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& saved)
    {
        auto state = saved;

        for (auto* p : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (const auto child = saved.getChildWithProperty ("id", ranged->getParameterID());
                    child.isValid() && ! std::isfinite ((float) child.getProperty ("value")))
                {
                    if (state == saved)
                        state = saved.createCopy();

                    state.getChildWithProperty ("id", ranged->getParameterID())
                        .setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
                }

        return state;
    }
}

void write (juce::AudioProcessorValueTreeState& apvts, int version, juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (versionId, version, nullptr);

    if (const auto xml = state.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, destData);
}

juce::ValueTree read (const juce::AudioProcessorValueTreeState& apvts, const void* data, int sizeInBytes)
{
    const auto xml = juce::AudioProcessor::getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return {};

    return juce::ValueTree::fromXml (*xml);
}

void restore (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& saved)
{
    const auto state = withFiniteValues (apvts, saved);
    apvts.replaceState (state);

    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (const auto child = state.getChildWithProperty ("id", ranged->getParameterID()); child.isValid())
                if (const auto target = ranged->convertTo0to1 ((float) child.getProperty ("value"));
                    std::abs (ranged->getValue() - target) > 1.0e-6f)
                    ranged->setValueNotifyingHost (target);
}

} // namespace myzel::plugin::sessionState
