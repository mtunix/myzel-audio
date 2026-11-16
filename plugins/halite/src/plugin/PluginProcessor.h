#pragma once

#include "common/plugin/ParameterUndo.h"
#include "common/plugin/PresetManager.h"
#include "dsp/chain/HaliteChain.h"
#include "plugin/LearnControl.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace halite
{

class HaliteProcessor final : public juce::AudioProcessor,
                              public myzel::plugin::PresetHost,
                              private juce::AsyncUpdater
{
public:
    HaliteProcessor();
    ~HaliteProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void reset() override;

    /** Main mono -> mono or stereo -> stereo; the side-chain key off, mono or stereo (PLAN.md, Decisions). */
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "Halite"; }
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

    juce::AudioProcessorValueTreeState& getState() noexcept override { return apvts; }

    /** Undo history of user edits only (ParameterUndoRecorder: gestures, never automation; not Bypass or Listen). Message thread only. */
    juce::UndoManager& getUndoManager() noexcept override { return undoManager; }

    /** Non-parameter UI state (common/plugin/UiState.h, src/ui/UiState.h), saved with the session. */
    juce::ValueTree getUiState() override { return apvts.state.getOrCreateChildWithName ("UI", nullptr); }

    /** Factory and user presets, A/B's store (message thread only). */
    myzel::plugin::PresetManager& getPresets() noexcept { return *presets; }

    /** The session's latency setting (PLAN.md, Latency): saved with the session, never in presets. */
    LatencyMode getLatencyMode() const noexcept { return static_cast<LatencyMode> (latencyMode.load()); }

    /** Reports the new latency to the host, which applies it on its next transport stop. */
    void setLatencyMode (LatencyMode);

    /** The current chain settings as the audio thread reads them (tests, tools). */
    dsp::ChainSettings readSettings() const noexcept;

    /** The signal path, for what it publishes (detector meters, events, Learn's histograms) to the UI and the tests. */
    const dsp::HaliteChain& getChain() const noexcept { return chain; }

    /** Learn (PLAN.md, Learn): a button, not a parameter; message thread. */
    LearnControl& getLearn() noexcept { return learnControl; }

    /** Bumped whenever the saved-state format changes, so old sessions can be migrated. */
    static constexpr int stateVersion = 1;

private:
    void handleAsyncUpdate() override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;   // no undo manager of its own: see ParameterUndoRecorder
    ParameterRefs params;
    myzel::plugin::ParameterUndoRecorder undoRecorder { *this, undoManager, { params::bypass, params::listen } };
    std::unique_ptr<myzel::plugin::PresetManager> presets;
    dsp::HaliteChain chain;
    LearnControl learnControl { apvts, chain.learn() };
    std::atomic<bool> pendingUndoClear { false };
    std::atomic<int> latencyMode { static_cast<int> (LatencyMode::normal) };
    std::atomic<double> preparedRate { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HaliteProcessor)
};

} // namespace halite
