#pragma once

#include "dsp/chain/EchoChain.h"
#include "common/plugin/ParameterUndo.h"
#include "common/plugin/PresetManager.h"
#include "dsp/NoteValues.h"
#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace mulberry
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
    std::atomic<float> hold { 0.0f };
    std::atomic<float> timeLeftMs { 0.0f }, timeRightMs { 0.0f };    // live (gliding) line times
    std::atomic<int> foldedOctaves { 0 };   // synced Time folded down to fit (read-out)
    std::atomic<double> bpm { 120.0 };      // tempo in use (host, or the BPM parameter)
    std::atomic<bool> hostHasTempo { false };

    static void storeMax (std::atomic<float>& target, float value) noexcept
    {
        auto current = target.load (std::memory_order_relaxed);
        while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed)) {}
    }
};

class MulberryProcessor final : public juce::AudioProcessor,
                                public myzel::plugin::PresetHost,
                                private juce::AsyncUpdater
{
public:
    MulberryProcessor();
    ~MulberryProcessor() override;

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
    const juce::String getName() const override { return "Mulberry"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /** Follows Time and Feedback; infinite above 100 % and while held. */
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

    juce::AudioProcessorValueTreeState& getState() noexcept override { return apvts; }
    MeterValues& getMeters() noexcept { return meters; }

    /** Factory and user presets (message thread only). Lives here so host program changes
        work while the editor is closed. */
    myzel::plugin::PresetManager& getPresets() noexcept { return *presets; }

    /** Called by the preset manager when the current preset changes: moves the host program. */
    void currentPresetChanged (const juce::String& name, bool isUser) override;

    /** Called by the preset manager when the mix lock changes (programs read it off-thread). */
    void mixLockChanged (bool isLocked) override { mixLocked.store (isLocked, std::memory_order_relaxed); }

    /** Carries out requests that arrived off the message thread (program changes) right now
        instead of on the next message loop turn. Tests. */
    void processPendingRequests() { handleUpdateNowIfNeeded(); }

    /** Undo history of user edits only (see ParameterUndoRecorder). Message thread only. */
    juce::UndoManager& getUndoManager() noexcept override { return undoManager; }

    /** Non-parameter UI state (e.g. editor scale) persisted with the session. */
    juce::ValueTree getUiState() override { return apvts.state.getOrCreateChildWithName ("UI", nullptr); }

    /**
        Call after a preset (or A/B slot) has been applied: the audio thread then moves the input
        to a fresh slot and lets the old echoes ring out (Trails), like a pedal.
    */
    void notifyPresetLoaded() noexcept { presetGeneration.fetch_add (1, std::memory_order_release); }

    /** Like a pedal: the new sound starts in a fresh slot and the old echoes ring out (Trails). */
    void presetApplied() override { notifyPresetLoaded(); }

    /** The current chain settings as the audio thread would read them (tests, tools). */
    dsp::ChainSettings readSettings (double hostBpm = 0.0) const noexcept;

    /** The Time of a line (0 = Time, 1 = Right time) in ms at a tempo, folded to fit. */
    notes::Folded resolveTime (int line, double bpm) const noexcept;

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
    myzel::plugin::ParameterUndoRecorder undoRecorder { *this, undoManager, { params::bypass, params::hold, params::bpm, params::holdMode } };
    dsp::EchoChain chain;
    std::vector<float> monoScratch;     // the second chain channel for a mono output
    MeterValues meters;
    std::atomic<uint32_t> presetGeneration { 0 };

    std::unique_ptr<myzel::plugin::PresetManager> presets;
    std::vector<Program> programs;              // fixed at construction (read from any thread)
    std::vector<juce::RangedAudioParameter*> soundParams;
    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> mixLocked { false };

    // Requests from other threads, carried out on the message thread (handleAsyncUpdate).
    std::atomic<bool> pendingUndoClear { false };
    std::atomic<int> pendingProgram { -1 };     // applied already; the browser still has to show it

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MulberryProcessor)
};

} // namespace mulberry
