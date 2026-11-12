#include "dsp/chain/EqChain.h"

namespace viridian::dsp
{

void EqChain::prepare (double newSampleRate, int /*maximumBlockSize*/, int /*numChannels*/)
{
    sampleRate = newSampleRate;
    outputGain.reset (sampleRate, outputSeconds);
    bypassAmount.reset (sampleRate, bypassSeconds);
    latencySamples = 0;
    reset();
}

void EqChain::reset() noexcept
{
    primed = false;
}

void EqChain::process (float* const* channels, int numChannels, int numSamples, const ChainSettings& s) noexcept
{
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const auto gain = juce::Decibels::decibelsToGain (s.outputDb);
    const auto bypassTarget = s.bypass ? 1.0f : 0.0f;

    if (! primed)
    {
        outputGain.setCurrentAndTargetValue (gain);
        bypassAmount.setCurrentAndTargetValue (bypassTarget);
        primed = true;
    }
    else
    {
        outputGain.setTargetValue (gain);
        bypassAmount.setTargetValue (bypassTarget);
    }

    if (! outputGain.isSmoothing() && ! bypassAmount.isSmoothing())
    {
        if (bypassAmount.getCurrentValue() >= 1.0f)
            return;

        // At 0 dB this multiplies by exactly 1: the scaffold passes audio through bit for bit.
        for (int c = 0; c < numChannels; ++c)
            juce::FloatVectorOperations::multiply (channels[c], outputGain.getCurrentValue(), numSamples);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto og = outputGain.getNextValue();
        const auto bp = bypassAmount.getNextValue();

        for (int c = 0; c < numChannels; ++c)
        {
            const auto dry = channels[c][i];
            channels[c][i] = dry * og * (1.0f - bp) + dry * bp;
        }
    }
}

} // namespace viridian::dsp
