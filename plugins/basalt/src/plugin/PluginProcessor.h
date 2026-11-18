#pragma once

#include "dsp/chain/BasaltChain.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace basalt
{

class BasaltProcessor final : public juce::AudioProcessor
{
public:
    BasaltProcessor();
    ~BasaltProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void reset() override;

    /** Main mono -> mono or stereo -> stereo; the side-chain key off, mono or stereo (PLAN.md, Plugin metadata). */
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    /** JUCE's generic editor until M4 (PLAN.md, Milestones). */
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "Basalt"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return params.bypass; }

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }

    /** The session's latency setting (PLAN.md, Latency): saved with the session, never in presets. */
    LatencyMode getLatencyMode() const noexcept { return static_cast<LatencyMode> (latencyMode.load()); }

    /** Reports the new latency to the host, which applies it on its next transport stop. */
    void setLatencyMode (LatencyMode);

    /** The current chain settings as the audio thread reads them (tests, tools). */
    dsp::ChainSettings readSettings() const noexcept;

    /** The signal path, for what it publishes (meters, status) to the UI and the tests. */
    const dsp::BasaltChain& getChain() const noexcept { return chain; }
    dsp::BasaltChain& getChain() noexcept { return chain; }

    /** Bumped whenever the saved-state format changes, so old sessions can be migrated. */
    static constexpr int stateVersion = 1;

private:
    /** The rate the reported latency is computed at, before prepareToPlay() too. */
    double reportingRate() const noexcept;

    juce::AudioProcessorValueTreeState apvts;
    ParameterRefs params;
    dsp::BasaltChain chain;
    std::atomic<int> latencyMode { static_cast<int> (LatencyMode::normal) };
    std::atomic<double> preparedRate { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BasaltProcessor)
};

} // namespace basalt
