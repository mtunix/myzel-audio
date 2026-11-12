#pragma once

#include "dsp/chain/EqChain.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace viridian
{

class ViridianProcessor final : public juce::AudioProcessor
{
public:
    ViridianProcessor();
    ~ViridianProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "Viridian"; }
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

    /** Non-parameter UI state (see ui/UiState.h), saved with the session. */
    juce::ValueTree getUiState() { return apvts.state.getOrCreateChildWithName ("UI", nullptr); }

    /** The current chain settings as the audio thread reads them (tests, tools). */
    dsp::ChainSettings readSettings() const noexcept;

    /** Bumped whenever the saved-state format changes, so old sessions can be migrated. */
    static constexpr int stateVersion = 1;

private:
    juce::AudioProcessorValueTreeState apvts;
    ParameterRefs params;
    dsp::EqChain chain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViridianProcessor)
};

} // namespace viridian
