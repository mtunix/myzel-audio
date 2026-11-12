#include "plugin/PluginProcessor.h"

#include "common/plugin/SessionState.h"

#include "dsp/MachineInfo.h"
#include "ui/FactoryPresets.h"

#if __has_include("ui/MulberryEditor.h")
 #include "ui/MulberryEditor.h"
 #define MULBERRY_HAS_EDITOR 1
#endif

namespace mulberry
{

namespace
{
    const juce::Identifier stateTreeType { "MulberryState" };

    int choice (const std::atomic<float>* p) { return juce::roundToInt (p->load()); }
    bool flag (const std::atomic<float>* p) { return p->load() >= 0.5f; }

}

MulberryProcessor::~MulberryProcessor()
{
    cancelPendingUpdate();
}

MulberryProcessor::MulberryProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, stateTreeType, params::createLayout()), // undo: see ParameterUndoRecorder
      params (apvts),
      presets (std::make_unique<myzel::plugin::PresetManager> (*this, ui::presetFormat()))
{
    // LV2 writes the program names into the bundle at build time: the builder's own user
    // presets must not end up there.
    const auto withUserPresets = wrapperType != wrapperType_LV2;

    for (const auto& id : params::soundParameterIds())
        soundParams.push_back (apvts.getParameter (id));

    for (const auto& e : presets->getEntries())
    {
        if (! e.isFactory && ! withUserPresets)
            continue;

        Program program { e.name, ! e.isFactory, {} };

        for (auto* p : soundParams)
        {
            const juce::Identifier key (p->getParameterID());
            program.values.push_back (e.values.hasProperty (key) ? p->convertTo0to1 ((float) e.values.getProperty (key))
                                                                 : p->getDefaultValue());
        }

        programs.push_back (std::move (program));
    }
}

void MulberryProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    chain.prepare (sampleRate, maximumBlockSize);
    monoScratch.assign ((size_t) std::max (maximumBlockSize, 1) * 2, 0.0f);
    setLatencySamples (0);
}

void MulberryProcessor::reset()
{
    chain.reset();
}

bool MulberryProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    // Mono -> mono, mono -> stereo, stereo -> stereo.
    if (in == juce::AudioChannelSet::mono())
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    return in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo();
}

notes::Folded MulberryProcessor::resolveTime (int line, double bpm) const noexcept
{
    const auto synced = flag (params.sync);
    if (! synced)
        return { (double) (line == 0 ? params.time->load() : params.timeRight->load()), 0 };

    const auto note = choice (line == 0 ? params.note : params.noteRight);
    return notes::fold (notes::lengthMs (note, bpm), (double) maps::maxTimeMs);
}

dsp::ChainSettings MulberryProcessor::readSettings (double hostBpm) const noexcept
{
    const auto bpm = hostBpm > 0.0 ? hostBpm : (double) params.bpm->load();

    dsp::ChainSettings s;
    s.machine     = static_cast<Machine> (juce::jlimit (0, numMachines - 1, choice (params.machine)));
    s.timeMs      = { (float) resolveTime (0, bpm).ms, (float) resolveTime (1, bpm).ms };
    s.feedback    = params.feedback->load() / 100.0f;
    s.driveDb     = params.drive->load();
    s.wobble      = params.wobble->load() / 100.0f;
    s.wobbleRate  = params.wobbleRate->load();
    s.wobbleShape = params.wobbleShape->load() / 100.0f;
    s.noise       = params.noise->load() / 100.0f;
    s.diffuse     = params.diffuse->load() / 100.0f;
    s.lowCutHz    = params.lowCut->load();
    s.highCutHz   = params.highCut->load();
    s.paramA      = params.paramA->load() / 100.0f;
    s.paramB      = params.paramB->load() / 100.0f;

    auto& p = s.plan;
    p.mode = static_cast<Mode> (juce::jlimit (0, numModes - 1, choice (params.mode)));
    p.groove = maps::grooveOffset (params.groove->load() / 100.0f);
    // Synced, Feel moves by up to a 1/64 note instead of 30 ms.
    const auto feel = params.feel->load() / params::feelMaxMs;
    p.feelMs = flag (params.sync) ? feel * (float) notes::lengthMs (1, bpm) : feel * params::feelMaxMs;
    p.width = params.width->load() / 100.0f;
    p.steps = juce::jlimit (1, params::maxSteps, juce::roundToInt (params.steps->load()));
    for (int i = 0; i < params::maxSteps; ++i)
        p.stepLevels[(size_t) i] = params.stepLevels[(size_t) i]->load() / 100.0f;
    p.panRule = static_cast<PanRule> (juce::jlimit (0, numPanRules - 1, choice (params.panRule)));
    p.crossFeed = params.crossFeed->load() / 100.0f;

    s.mix             = params.mix->load() / 100.0f;
    s.wetOnly         = flag (params.wetOnly);
    s.outputDb        = params.output->load();
    s.duck            = flag (params.duck);
    s.duckAmountDb    = params.duckAmount->load();
    s.duckThresholdDb = params.duckThreshold->load();
    s.duckReleaseMs   = params.duckRelease->load();
    s.hold            = flag (params.hold);
    s.bypass          = params.bypass->get();
    s.trails          = flag (params.trails);
    s.presetGeneration = presetGeneration.load (std::memory_order_acquire);
    return s;
}

double MulberryProcessor::getTailLengthSeconds() const
{
    const auto fb = params.feedback->load() / 100.0f;
    if (flag (params.hold) || fb >= 0.999f)
        return std::numeric_limits<double>::infinity();

    const auto bpm = (double) params.bpm->load();
    const auto longest = std::max (resolveTime (0, bpm).ms, resolveTime (1, bpm).ms) * 0.001 * 3.0;
    // Repeats until -60 dB, plus the last echo's way out.
    const auto repeats = fb > 0.001f ? std::ceil (-3.0 / std::log10 ((double) fb)) : 1.0;
    return std::min (600.0, longest * (repeats + 1.0) + 1.0);
}

void MulberryProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto inputs = getTotalNumInputChannels();
    const auto outputs = getTotalNumOutputChannels();
    if (numSamples == 0 || buffer.getNumChannels() < 1)
        return;

    double hostBpm = 0.0;
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
            if (const auto b = position->getBpm())
                hostBpm = *b;

    const auto bpm = hostBpm > 0.0 ? hostBpm : (double) params.bpm->load();
    meters.bpm.store (bpm, std::memory_order_relaxed);
    meters.hostHasTempo.store (hostBpm > 0.0, std::memory_order_relaxed);
    meters.foldedOctaves.store (resolveTime (0, bpm).octaves, std::memory_order_relaxed);

    // The chain is stereo: a mono input feeds both sides, a mono output takes the sum.
    const auto monoInput = inputs < 2;
    const auto settings = readSettings (hostBpm);
    dsp::ChainMeters m;

    if (outputs >= 2 && buffer.getNumChannels() >= 2)
    {
        if (monoInput)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
        chain.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples, settings, m, monoInput);
    }
    else
    {
        auto* mono = buffer.getWritePointer (0);
        const auto chunk = std::max (1, (int) monoScratch.size() / 2);

        for (int offset = 0; offset < numSamples; offset += chunk)
        {
            const auto n = std::min (chunk, numSamples - offset);
            auto* l = monoScratch.data();
            auto* r = monoScratch.data() + chunk;
            std::copy (mono + offset, mono + offset + n, l);
            std::copy (mono + offset, mono + offset + n, r);
            chain.process (l, r, n, settings, m, true);
            for (int i = 0; i < n; ++i)
                mono[offset + i] = 0.5f * (l[i] + r[i]);
        }
    }

    meters.inputRms.store (m.inputRms, std::memory_order_relaxed);
    meters.wetRms.store (m.wetRms, std::memory_order_relaxed);
    meters.outputRms.store (m.outputRms, std::memory_order_relaxed);
    meters.duckGain.store (m.duckGain, std::memory_order_relaxed);
    meters.hold.store (m.hold, std::memory_order_relaxed);
    meters.timeLeftMs.store (m.timeMs[0], std::memory_order_relaxed);
    meters.timeRightMs.store (m.timeMs[1], std::memory_order_relaxed);
    MeterValues::storeMax (meters.inputPeak, m.inputPeak);
    MeterValues::storeMax (meters.wetPeak, m.wetPeak);
    MeterValues::storeMax (meters.outputPeak, m.outputPeak);
}

//==============================================================================
int MulberryProcessor::getNumPrograms()
{
    return (int) programs.size();
}

int MulberryProcessor::getCurrentProgram()
{
    return juce::jlimit (0, getNumPrograms() - 1, currentProgram.load (std::memory_order_relaxed));
}

const juce::String MulberryProcessor::getProgramName (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) programs.size()))
        return {};

    const auto& program = programs[(size_t) index];
    return program.isUser ? "User: " + program.name : program.name;
}

void MulberryProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    // Hosts call this from the message thread, the audio thread or a loader thread, and expect
    // the program to be in place when it returns. Off the message thread, set the parameters now
    // (that is thread-safe) and leave the browser's state (name, undo) to the message thread.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        loadProgram (index);
        return;
    }

    applyProgramValues (index);
    currentProgram.store (index, std::memory_order_relaxed);
    pendingProgram.store (index, std::memory_order_release);
    triggerAsyncUpdate();
}

void MulberryProcessor::applyProgramValues (int index)
{
    const auto& values = programs[(size_t) index].values;
    const auto locked = mixLocked.load (std::memory_order_relaxed);

    for (size_t i = 0; i < soundParams.size(); ++i)
    {
        auto* p = soundParams[i];
        const auto& id = p->getParameterID();

        if (locked && (id == params::mix || id == params::wetOnly))
            continue;

        if (std::abs (values[i] - p->getValue()) > 1.0e-6f)
            p->setValueNotifyingHost (values[i]);
    }

    notifyPresetLoaded();
}

void MulberryProcessor::loadProgram (int index)
{
    const auto& program = programs[(size_t) index];
    const auto& entries = presets->getEntries();

    // By name: the browser's list may have changed since this instance was created.
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == program.name && entries[i].isFactory != program.isUser)
            return presets->load ((int) i);
}

int MulberryProcessor::programFor (const juce::String& name, bool isUser) const noexcept
{
    for (size_t i = 0; i < programs.size(); ++i)
        if (programs[i].isUser == isUser && programs[i].name == name)
            return (int) i;

    return 0;   // an unsaved sound, or a user preset newer than this instance
}

void MulberryProcessor::currentPresetChanged (const juce::String& name, bool isUser)
{
    const auto program = programFor (name, isUser);

    if (currentProgram.exchange (program, std::memory_order_relaxed) != program)
        updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

juce::AudioProcessorEditor* MulberryProcessor::createEditor()
{
   #if MULBERRY_HAS_EDITOR
    return new MulberryEditor (*this);
   #else
    return new juce::GenericAudioProcessorEditor (*this);
   #endif
}

void MulberryProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    myzel::plugin::sessionState::write (apvts, stateVersion, destData);
}

void MulberryProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = myzel::plugin::sessionState::read (apvts, data, sizeInBytes);

    if (! state.isValid())
        return;

    // Future: migrate older state versions here, based on state[sessionState::versionId].

    // The restored session wins over a program change the browser has not shown yet, and the
    // host program follows its preset (read from the new tree: the live UI state belongs to
    // the message thread).
    pendingProgram.store (-1);
    const auto ui = state.getChildWithName ("UI");
    mixLocked.store ((bool) ui.getProperty ("mixLock", false), std::memory_order_relaxed);
    currentProgram.store (programFor (ui.getProperty ("presetName").toString(), (bool) ui.getProperty ("presetIsUser", false)),
                          std::memory_order_relaxed);

    myzel::plugin::sessionState::restore (apvts, state);

    // A restored session starts with a fresh history. Hosts may call this off the message
    // thread, and UndoManager is message-thread only.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        undoManager.clearUndoHistory();
    }
    else
    {
        pendingUndoClear.store (true);
        triggerAsyncUpdate();
    }
}

void MulberryProcessor::handleAsyncUpdate()
{
    if (pendingUndoClear.exchange (false))
        undoManager.clearUndoHistory();

    // A program set off the message thread: its values are in place, now the browser shows it.
    if (const auto program = pendingProgram.exchange (-1); program >= 0)
        presets->setCurrent (programs[(size_t) program].name, programs[(size_t) program].isUser);
}

} // namespace mulberry
