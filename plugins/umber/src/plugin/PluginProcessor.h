#pragma once

#include "dsp/chain/SaturatorChain.h"
#include "plugin/ParameterUndo.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace umber
{

/**
    Levels published by the audio thread for the UI (lock-free).

    RMS values are per processed block (linear). Peaks are "max since last read": the UI reads
    them with exchange (0.0f). The UI applies its own meter ballistics.
*/
struct MeterValues
{
    std::atomic<float> inputRms { 0.0f }, inputPeak { 0.0f };
    std::atomic<float> driveRms { 0.0f };   // level hitting the circuit, after Drive/Slam
    std::atomic<float> outputRms { 0.0f }, outputPeak { 0.0f };
    std::atomic<int> oversamplingFactor { 4 };

    static void storeMax (std::atomic<float>& target, float value) noexcept
    {
        auto current = target.load (std::memory_order_relaxed);
        while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed)) {}
    }
};

class UmberProcessor final : public juce::AudioProcessor,
                                private juce::AsyncUpdater
{
public:
    UmberProcessor();
    ~UmberProcessor() override;

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
    const juce::String getName() const override { return "Umber"; }
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
    MeterValues& getMeters() noexcept { return meters; }
    /** Undo history of user edits only (see ParameterUndoRecorder). Message thread only. */
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    /** Non-parameter UI state (e.g. editor scale) persisted with the session. */
    juce::ValueTree getUiState() { return apvts.state.getOrCreateChildWithName ("UI", nullptr); }

    /** Bumped whenever the saved-state format changes, so old sessions can be migrated. */
    static constexpr int stateVersion = 1;

private:
    dsp::ChainSettings readSettings() const noexcept;
    void handleAsyncUpdate() override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    ParameterRefs params;
    ParameterUndoRecorder undoRecorder { *this, undoManager, { params::bypass, params::delta } };
    dsp::SaturatorChain chain;
    MeterValues meters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UmberProcessor)
};

} // namespace umber
