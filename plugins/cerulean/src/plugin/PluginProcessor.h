#pragma once

#include "dsp/chain/ReverbChain.h"
#include "plugin/ParameterUndo.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace cerulean::ui
{
class PresetManager;
}

namespace cerulean
{

/**
    Levels published by the audio thread for the UI (lock-free).

    RMS values are per processed block (linear). Peaks are "max since last read": the UI reads
    them with exchange (0.0f) and applies its own ballistics.
*/
struct MeterValues
{
    std::atomic<float> inputRms { 0.0f }, inputPeak { 0.0f };
    std::atomic<float> wetRms { 0.0f }, wetPeak { 0.0f };
    std::atomic<float> outputRms { 0.0f }, outputPeak { 0.0f };
    std::atomic<float> duckGain { 1.0f };
    std::atomic<float> freeze { 0.0f };
    std::atomic<double> bpm { 120.0 };      // tempo in use (host, or the BPM parameter)
    std::atomic<bool> hostHasTempo { false };

    static void storeMax (std::atomic<float>& target, float value) noexcept
    {
        auto current = target.load (std::memory_order_relaxed);
        while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed)) {}
    }
};

class CeruleanProcessor final : public juce::AudioProcessor,
                                private juce::AsyncUpdater
{
public:
    CeruleanProcessor();
    ~CeruleanProcessor() override;

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
    const juce::String getName() const override { return "Cerulean"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /** Follows Decay (and Echo); infinite while frozen. */
    double getTailLengthSeconds() const override;

    /**
        Host programs, so a MIDI program change (or the host's own preset list) switches sounds
        hands-free: the factory presets in browser order, then the user presets that existed
        when this instance was created (alphabetical; hosts cache the list, so it never changes
        while an instance lives). The LV2 build lists factory presets only: their names are
        written into the bundle at build time.

        Selecting a program takes effect at once, like any host call: on the message thread it
        loads the preset like the browser does (trails, undo, mix lock); on any other thread
        (audio, loader) it sets the parameters from a snapshot taken at construction (trails and
        mix lock, no undo step) and the browser catches up on the message thread.
    */
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return params.bypass; }

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    MeterValues& getMeters() noexcept { return meters; }

    /** Factory and user presets (message thread only). Lives here so host program changes
        work while the editor is closed. */
    ui::PresetManager& getPresets() noexcept { return *presets; }

    /** Called by the preset manager when the current preset changes: moves the host program. */
    void currentPresetChanged (const juce::String& name, bool isUser);

    /** Called by the preset manager when the mix lock changes (programs read it off-thread). */
    void mixLockChanged (bool isLocked) noexcept { mixLocked.store (isLocked, std::memory_order_relaxed); }

    /** Carries out requests that arrived off the message thread (program changes) right now
        instead of on the next message loop turn. Tests. */
    void processPendingRequests() { handleUpdateNowIfNeeded(); }

    /** Undo history of user edits only (see ParameterUndoRecorder). Message thread only. */
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    /** Non-parameter UI state (e.g. editor scale) persisted with the session. */
    juce::ValueTree getUiState() { return apvts.state.getOrCreateChildWithName ("UI", nullptr); }

    /**
        Call after a preset (or A/B slot) has been applied: the audio thread then moves the input
        to a fresh engine instance and lets the old sound ring out (Trails), like a pedal.
    */
    void notifyPresetLoaded() noexcept { presetGeneration.fetch_add (1, std::memory_order_release); }

    /** The current chain settings as the audio thread would read them (tests, tools). */
    dsp::ChainSettings readSettings (double hostBpm = 0.0) const noexcept;

    /** Bumped whenever the saved-state format changes, so old sessions can be migrated. */
    static constexpr int stateVersion = 1;

private:
    struct Program
    {
        juce::String name;
        bool isUser = false;
        std::vector<float> values;      // normalised, one per soundParams entry
    };

    void handleAsyncUpdate() override;
    void loadProgram (int index);
    void applyProgramValues (int index);
    int programFor (const juce::String& name, bool isUser) const noexcept;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    ParameterRefs params;
    ParameterUndoRecorder undoRecorder { *this, undoManager, { params::bypass, params::freeze, params::bpm } };
    dsp::ReverbChain chain;
    MeterValues meters;
    std::atomic<uint32_t> presetGeneration { 0 };

    std::unique_ptr<ui::PresetManager> presets;
    std::vector<Program> programs;              // fixed at construction (read from any thread)
    std::vector<juce::RangedAudioParameter*> soundParams;
    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> mixLocked { false };

    // Requests from other threads, carried out on the message thread (handleAsyncUpdate).
    std::atomic<bool> pendingUndoClear { false };
    std::atomic<int> pendingProgram { -1 };     // applied already; the browser still has to show it

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeruleanProcessor)
};

} // namespace cerulean
