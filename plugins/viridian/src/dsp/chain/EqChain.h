#pragma once

#include "dsp/EqParams.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace viridian::dsp
{

/** Plain-value snapshot of every parameter the chain needs, taken once per block. */
struct ChainSettings
{
    Model model = Model::inductor;
    bool alt = false;
    SlotPositions positions = defaultPositions();
    float driveDb = 0.0f;
    bool eqIn = true;
    float outputDb = 0.0f;
    StereoMode stereo = StereoMode::stereo;
    float tolerance = 0.0f;         // 0 .. 1
    float noise = 0.0f;             // 0 .. 1
    bool bypass = false;
};

/**
    The Viridian signal path (PLAN.md, Signal flow). The scaffold runs Output and Bypass only; the
    stages (input amp, EQ network, make-up amp, output transformer) join them in M1 and add their
    latency to getLatencySamples().
*/
class EqChain
{
public:
    /** Not real-time safe. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset() noexcept;

    /** Mono (one channel) or stereo (two), any block length. */
    void process (float* const* channels, int numChannels, int numSamples, const ChainSettings&) noexcept;

    int getLatencySamples() const noexcept { return latencySamples; }

    static constexpr double bypassSeconds = 0.02;
    static constexpr double outputSeconds = 0.05;

private:
    double sampleRate = 44100.0;
    int latencySamples = 0;
    bool primed = false;    // the first block after reset jumps to its settings
    juce::SmoothedValue<float> outputGain { 1.0f }, bypassAmount { 0.0f };
};

} // namespace viridian::dsp
