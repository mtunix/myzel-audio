#pragma once

#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <random>
#include <vector>

namespace halite::testing
{

inline void setParam (HaliteProcessor& p, const char* id, float plainValue)
{
    auto* rp = p.getState().getParameter (id);
    REQUIRE (rp != nullptr);
    rp->setValueNotifyingHost (rp->convertTo0to1 (plainValue));
}

inline float plainValue (HaliteProcessor& p, const char* id)
{
    return p.getState().getRawParameterValue (id)->load();
}

inline juce::String idOf (juce::AudioProcessorParameter* param)
{
    return dynamic_cast<juce::HostedAudioProcessorParameter*> (param)->getParameterID();
}

/** Main bus in and out, plus the side-chain key (disabled by default). */
inline juce::AudioProcessor::BusesLayout layout (juce::AudioChannelSet in, juce::AudioChannelSet out,
                                                 juce::AudioChannelSet key = juce::AudioChannelSet::disabled())
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (in);
    l.inputBuses.add (key);
    l.outputBuses.add (out);
    return l;
}

inline juce::AudioBuffer<float> noise (int channels, int samples, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    juce::AudioBuffer<float> b (channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            b.setSample (ch, i, dist (rng));
    return b;
}

/** Runs the buffer through in place, in blocks of the given sizes (cycled), and returns it. */
inline juce::AudioBuffer<float> run (HaliteProcessor& p, const juce::AudioBuffer<float>& in, const std::vector<int>& sizes)
{
    juce::AudioBuffer<float> out (in);
    juce::MidiBuffer midi;
    int pos = 0;
    size_t k = 0;

    while (pos < in.getNumSamples())
    {
        const auto n = std::min (sizes[k++ % sizes.size()], in.getNumSamples() - pos);
        juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
        p.processBlock (block, midi);
        pos += n;
    }
    return out;
}

/** The first `channels` channels of a and b, bit for bit. */
inline bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channels)
{
    if (a.getNumChannels() < channels || b.getNumChannels() < channels || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < channels; ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;
    return true;
}

/** The first `channels` channels of `out` are those of `in` delayed by `delay` samples (silence before), bit for bit. */
inline bool delayedIdentical (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int channels, int delay)
{
    if (out.getNumChannels() < channels || in.getNumChannels() < channels || out.getNumSamples() != in.getNumSamples())
        return false;
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const auto expected = i >= delay ? in.getSample (ch, i - delay) : 0.0f;
            const auto got = out.getSample (ch, i);
            if (std::memcmp (&expected, &got, sizeof (float)) != 0)
                return false;
        }
    return true;
}

inline bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    return a.getNumChannels() == b.getNumChannels() && identical (a, b, a.getNumChannels());
}

inline juce::MemoryBlock stateOf (HaliteProcessor& p)
{
    juce::MemoryBlock m;
    p.getStateInformation (m);
    return m;
}

inline void restore (HaliteProcessor& p, const juce::MemoryBlock& m)
{
    p.setStateInformation (m.getData(), (int) m.getSize());
}

inline juce::ValueTree treeOf (const juce::MemoryBlock& m)
{
    const auto xml = juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize());
    REQUIRE (xml != nullptr);
    return juce::ValueTree::fromXml (*xml);
}

inline juce::MemoryBlock save (const juce::ValueTree& t)
{
    juce::MemoryBlock m;
    juce::AudioProcessor::copyXmlToBinary (*t.createXml(), m);
    return m;
}

} // namespace halite::testing
