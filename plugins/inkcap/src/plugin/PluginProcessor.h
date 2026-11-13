#pragma once

#include "common/plugin/ParameterUndo.h"
#include "common/plugin/PresetManager.h"
#include "dsp/chain/InkcapChain.h"
#include "plugin/Parameters.h"
#include "plugin/SeedState.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace inkcap
{

class InkcapProcessor final : public juce::AudioProcessor,
                              public myzel::plugin::PresetHost,
                              private juce::AsyncUpdater
{
public:
    InkcapProcessor();
    ~InkcapProcessor() override;

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
    const juce::String getName() const override { return "Inkcap"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /** Glitch's longest event and the other modules' tails (Room's from M2). */
    double getTailLengthSeconds() const override { return dsp::InkcapChain::maxTailSeconds; }

    /**
        Host programs, so a MIDI program change (or the host's own preset list) switches sounds
        hands-free: the factory presets in browser order, then the user presets that existed when
        this instance was created (hosts cache the list, so it never changes while an instance
        lives). The LV2 build lists factory presets only: their names are written into the bundle
        at build time.

        On the message thread a program loads like the browser loads it (undo, both locks). On any
        other thread (audio, a loader) it sets the parameters and the audio thread's seed from a
        snapshot taken at construction, keeping Mix and the seed while their locks are on, and the
        state tree and the browser catch up on the message thread (no undo step).
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

    /** Undo history of user edits only: parameter gestures (ParameterUndoRecorder) and seed actions. Message thread only. */
    juce::UndoManager& getUndoManager() noexcept override { return undoManager; }

    /** Non-parameter UI state (see common/plugin/UiState.h and seedstate), saved with the session. */
    juce::ValueTree getUiState() override { return apvts.state.getOrCreateChildWithName ("UI", nullptr); }

    /** Factory and user presets (message thread only). Lives here so the seed Lock works without an editor. */
    myzel::plugin::PresetManager& getPresets() noexcept { return *presets; }

    /** The seed: current() on any thread, the seed actions on the message thread. */
    SeedState& getSeeds() noexcept { return seeds; }

    /** The seed Lock: loading a preset or switching A/B keeps the seed (PLAN.md, Seeds). */
    bool isSeedLocked() const { return presets->isStateLocked (seedstate::seedLock); }
    void setSeedLocked (bool shouldLock) { presets->setStateLocked (seedstate::seedLock, shouldLock); }

    /** Called by the preset manager after a preset or A/B: repairs a seed that is not a code. */
    void presetApplied() override { seeds.repairAfterPreset(); }

    /** Called by the preset manager when the current preset changes: moves the host program. */
    void currentPresetChanged (const juce::String& name, bool isUser) override;

    /** Called by the preset manager when the mix lock changes (programs read it off the message thread). */
    void mixLockChanged (bool isLocked) override { mixLocked.store (isLocked, std::memory_order_relaxed); }

    /** Carries out requests that arrived off the message thread (an undo-history clear, a program) now. Tests. */
    void processPendingRequests() { handleUpdateNowIfNeeded(); }

    /** The current chain settings as the audio thread reads them (tests, tools). */
    dsp::ChainSettings readSettings() const noexcept;

    /** What the audio thread publishes for the display (atomics; see src/dsp/chain/Telemetry.h). */
    const dsp::Telemetry& getTelemetry() const noexcept { return chain.telemetry(); }

    /** Bumped whenever the saved-state format changes, so old sessions can be migrated. */
    static constexpr int stateVersion = 1;

private:
    struct Program
    {
        juce::String name;
        bool isUser = false;
        std::vector<float> values;      // normalised, one per soundParams entry
        uint32_t seed = 0;
        juce::String seedLabel;
    };

    void handleAsyncUpdate() override;
    void loadProgram (int index);
    void applyProgramValues (int index);
    int programFor (const juce::String& name, bool isUser) const noexcept;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    ParameterRefs params;
    myzel::plugin::ParameterUndoRecorder undoRecorder { *this, undoManager, params::settingParameterIds() };
    SeedState seeds { *this };
    std::unique_ptr<myzel::plugin::PresetManager> presets;
    dsp::InkcapChain chain;

    std::vector<Program> programs;              // fixed at construction (read from any thread)
    std::vector<juce::RangedAudioParameter*> soundParams;
    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> mixLocked { false };

    // Requests from other threads, carried out on the message thread (handleAsyncUpdate).
    std::atomic<bool> pendingUndoClear { false };
    std::atomic<int> pendingProgram { -1 };     // applied already; the state tree and the browser still have to show it

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InkcapProcessor)
};

} // namespace inkcap
