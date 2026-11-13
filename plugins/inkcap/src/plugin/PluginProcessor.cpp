#include "plugin/PluginProcessor.h"

#include "common/plugin/SessionState.h"
#include "common/plugin/UiState.h"
#include "plugin/FactoryPresets.h"
#include "ui/InkcapEditor.h"

namespace inkcap
{

namespace
{
    int choice (const std::atomic<float>* p) { return juce::roundToInt (p->load()); }
    bool flag (const std::atomic<float>* p) { return p->load() >= 0.5f; }
    float unit (const std::atomic<float>* p) { return p->load() / 100.0f; }

    template <typename Enum>
    Enum typeOf (const ParameterRefs::ModuleRefs& m, Module module)
    {
        const auto count = (int) moduleInfo (module).types.size();
        return static_cast<Enum> (juce::jlimit (0, count - 1, choice (m.type)));
    }

    /** The host's song position at the block's start, as the chain's clock reads it. */
    dsp::HostPosition hostPosition (juce::AudioPlayHead* playHead)
    {
        dsp::HostPosition host;
        if (playHead == nullptr)
            return host;

        const auto position = playHead->getPosition();
        if (! position.hasValue())
            return host;

        host.playing = position->getIsPlaying();
        if (const auto ppq = position->getPpqPosition())
        {
            host.hasPpq = true;
            host.ppq = *ppq;
        }
        if (const auto seconds = position->getTimeInSeconds())
        {
            host.hasSeconds = true;
            host.seconds = *seconds;
        }
        if (const auto bpm = position->getBpm())
            host.bpm = *bpm;
        if (const auto signature = position->getTimeSignature())
        {
            host.timeSigNumerator = signature->numerator;
            host.timeSigDenominator = signature->denominator;
        }
        if (const auto loop = position->getLoopPoints(); loop.hasValue() && position->getIsLooping())
        {
            host.looping = true;
            host.loopStartPpq = loop->ppqStart;
            host.loopEndPpq = loop->ppqEnd;
        }
        if (const auto barStart = position->getPpqPositionOfLastBarStart())
        {
            host.hasBarStart = true;
            host.barStartPpq = *barStart;
        }
        return host;
    }

    /** A parameter a session does not mention (one added after it was saved) gets its default, not this instance's value. */
    void addMissingDefaults (juce::ValueTree& state, juce::AudioProcessor& processor)
    {
        for (auto* p : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! state.getChildWithProperty ("id", ranged->getParameterID()).isValid())
                    state.appendChild (juce::ValueTree ("PARAM", { { "id", ranged->getParameterID() },
                                                                   { "value", ranged->convertFrom0to1 (ranged->getDefaultValue()) } }),
                                       nullptr);
    }
}

InkcapProcessor::InkcapProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, params::stateTag, params::createLayout()),     // undo: see ParameterUndoRecorder
      params (apvts),
      presets (std::make_unique<myzel::plugin::PresetManager> (*this, presetFormat()))
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

        Program program { e.name, ! e.isFactory, {}, SeedState::initial().seed, {} };
        for (auto* p : soundParams)
        {
            const juce::Identifier key (p->getParameterID());
            program.values.push_back (e.values.hasProperty (key) ? p->convertTo0to1 ((float) e.values.getProperty (key))
                                                                 : p->getDefaultValue());
        }
        program.seed = SeedState::parseProperty (e.values.getProperty (seedstate::seed)).value_or (SeedState::initial().seed);
        program.seedLabel = e.values.getProperty (seedstate::seedLabel).toString();
        programs.push_back (std::move (program));
    }
    mixLocked.store (presets->isMixLocked(), std::memory_order_relaxed);
}

InkcapProcessor::~InkcapProcessor()
{
    cancelPendingUpdate();
}

void InkcapProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    chain.prepare (sampleRate, maximumBlockSize, getTotalNumOutputChannels());
    setLatencySamples (chain.getLatencySamples());
}

void InkcapProcessor::reset()
{
    chain.reset();
}

bool InkcapProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    // Mono -> mono, mono -> stereo, stereo -> stereo.
    if (in == juce::AudioChannelSet::mono())
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    return in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo();
}

dsp::ChainSettings InkcapProcessor::readSettings() const noexcept
{
    using M = Module;
    auto mod = [this] (M m) -> const ParameterRefs::ModuleRefs& { return params.modules[(size_t) m]; };

    dsp::ChainSettings s;
    s.damage = unit (params.damage);
    s.chaos = unit (params.chaos);

    s.glitch = { flag (mod (M::glitch).on), unit (mod (M::glitch).amount), typeOf<GlitchType> (mod (M::glitch), M::glitch),
                 juce::jlimit (0, numGlitchRates - 1, choice (params.glitchRate)), unit (params.glitchLength),
                 unit (params.glitchGroove) };
    s.warble = { flag (mod (M::warble).on), unit (mod (M::warble).amount), typeOf<WarbleType> (mod (M::warble), M::warble),
                 unit (params.warbleRate), flag (params.warbleSync), unit (params.warbleSpread) };
    s.crush = { flag (mod (M::crush).on), unit (mod (M::crush).amount), typeOf<CrushType> (mod (M::crush), M::crush),
                unit (params.crushDrive), unit (params.crushTone) };
    s.bits = { flag (mod (M::bits).on), unit (mod (M::bits).amount), typeOf<BitsType> (mod (M::bits), M::bits),
               unit (params.bitsRate), unit (params.bitsDepth), unit (params.bitsJitter) };
    s.hiss = { flag (mod (M::hiss).on), unit (mod (M::hiss).amount), typeOf<HissType> (mod (M::hiss), M::hiss),
               unit (params.hissTone), unit (params.hissFollow) };
    s.room = { flag (mod (M::room).on), unit (mod (M::room).amount), typeOf<RoomType> (mod (M::room), M::room),
               unit (params.roomSize), unit (params.roomDuck) };

    s.mix = unit (params.mix);
    s.outputDb = params.output->load();
    s.match = flag (params.match);
    s.bypass = params.bypass->get();
    s.fallbackBpm = (double) params.bpm->load();
    s.seed = seeds.current();
    return s;
}

void InkcapProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto inputs = getTotalNumInputChannels();
    const auto outputs = std::min (getTotalNumOutputChannels(), buffer.getNumChannels());

    if (numSamples == 0 || outputs < 1)
        return;

    // Mono in, stereo out: both sides start as the input.
    if (inputs == 1 && outputs >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    chain.process (buffer.getArrayOfWritePointers(), std::min (outputs, dsp::InkcapChain::maxChannels), numSamples, readSettings(),
                   hostPosition (getPlayHead()));
}

int InkcapProcessor::getNumPrograms()
{
    return std::max (1, (int) programs.size());
}

int InkcapProcessor::getCurrentProgram()
{
    return juce::jlimit (0, getNumPrograms() - 1, currentProgram.load (std::memory_order_relaxed));
}

const juce::String InkcapProcessor::getProgramName (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) programs.size()))
        return {};

    const auto& program = programs[(size_t) index];
    return program.isUser ? "User: " + program.name : program.name;
}

void InkcapProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) programs.size()))
        return;

    // Hosts call this from the message thread, the audio thread or a loader thread, and expect
    // the program to be in place when it returns. Off the message thread, set the parameters and
    // the audio thread's seed now (both thread-safe) and leave the state tree, the browser and
    // the undo history to the message thread.
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

void InkcapProcessor::applyProgramValues (int index)
{
    const auto& program = programs[(size_t) index];
    const auto locked = mixLocked.load (std::memory_order_relaxed);

    for (size_t i = 0; i < soundParams.size(); ++i)
    {
        auto* p = soundParams[i];
        if (locked && p->getParameterID() == params::mix)
            continue;

        if (std::abs (program.values[i] - p->getValue()) > 1.0e-6f)
            p->setValueNotifyingHost (program.values[i]);
    }

    if (! seeds.lockedNow())
        seeds.preview (program.seed);
}

void InkcapProcessor::loadProgram (int index)
{
    const auto& program = programs[(size_t) index];
    const auto& entries = presets->getEntries();

    // By name: the browser's list may have changed since this instance was created.
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == program.name && entries[i].isFactory != program.isUser)
            return presets->load ((int) i);
}

int InkcapProcessor::programFor (const juce::String& name, bool isUser) const noexcept
{
    for (size_t i = 0; i < programs.size(); ++i)
        if (programs[i].isUser == isUser && programs[i].name == name)
            return (int) i;

    return 0;   // an unsaved sound, or a user preset newer than this instance
}

void InkcapProcessor::currentPresetChanged (const juce::String& name, bool isUser)
{
    const auto program = programFor (name, isUser);

    if (currentProgram.exchange (program, std::memory_order_relaxed) != program)
        updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

juce::AudioProcessorEditor* InkcapProcessor::createEditor()
{
    return new InkcapEditor (*this);
}

void InkcapProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    myzel::plugin::sessionState::write (apvts, stateVersion, destData);
}

void InkcapProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = myzel::plugin::sessionState::read (apvts, data, sizeInBytes);

    if (! state.isValid())
        return;

    // Future: migrate older state versions here, based on state[sessionState::versionId].
    addMissingDefaults (state, *this);
    SeedState::repair (state);

    // The restored session wins over a program change the browser has not shown yet, and the
    // host program follows its preset (read from the new tree: the live UI state belongs to the
    // message thread).
    pendingProgram.store (-1);
    const auto ui = state.getChildWithName ("UI");
    mixLocked.store ((bool) ui.getProperty (myzel::plugin::uistate::mixLock, false), std::memory_order_relaxed);
    currentProgram.store (programFor (ui.getProperty (myzel::plugin::uistate::presetName).toString(),
                                      (bool) ui.getProperty (myzel::plugin::uistate::presetUser, false)),
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

void InkcapProcessor::handleAsyncUpdate()
{
    if (pendingUndoClear.exchange (false))
        undoManager.clearUndoHistory();

    // A program set off the message thread: its values and seed are in place, now the state
    // tree takes the seed (unless the Lock kept it) and the browser shows the program.
    if (const auto index = pendingProgram.exchange (-1, std::memory_order_acquire); index >= 0)
    {
        const auto& program = programs[(size_t) index];
        if (! isSeedLocked())
            seeds.adopt (program.seed, program.seedLabel);
        presets->setCurrent (program.name, program.isUser);
    }
}

} // namespace inkcap
