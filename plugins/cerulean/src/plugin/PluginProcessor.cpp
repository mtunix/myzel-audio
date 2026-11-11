#include "plugin/PluginProcessor.h"

#include "dsp/Mappings.h"
#include "ui/PresetManager.h"

#if __has_include("ui/CeruleanEditor.h")
 #include "ui/CeruleanEditor.h"
 #define CERULEAN_HAS_EDITOR 1
#endif

namespace cerulean
{

namespace
{
    const juce::Identifier stateTreeType { "CeruleanState" };
    const juce::Identifier stateVersionId { "stateVersion" };

    int choice (const std::atomic<float>* p) { return juce::roundToInt (p->load()); }
    bool flag (const std::atomic<float>* p) { return p->load() >= 0.5f; }

    double syncedMs (int note, double bpm)
    {
        const auto beats = noteValueInBeats (static_cast<NoteValue> (juce::jlimit (0, numNoteValues - 1, note)));
        return 60000.0 / juce::jlimit (20.0, 999.0, bpm) * beats;
    }
}

CeruleanProcessor::~CeruleanProcessor()
{
    cancelPendingUpdate();
}

CeruleanProcessor::CeruleanProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, stateTreeType, params::createLayout()), // undo: see ParameterUndoRecorder
      params (apvts),
      presets (std::make_unique<ui::PresetManager> (*this))
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

void CeruleanProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    chain.prepare (sampleRate, maximumBlockSize);
    setLatencySamples (0);
}

void CeruleanProcessor::reset()
{
    chain.reset();
}

bool CeruleanProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    // Mono -> stereo and stereo -> stereo only: the tail is always stereo.
    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

dsp::ChainSettings CeruleanProcessor::readSettings (double hostBpm) const noexcept
{
    const auto bpm = hostBpm > 0.0 ? hostBpm : (double) params.bpm->load();
    const auto algorithm = static_cast<Algorithm> (juce::jlimit (0, numAlgorithms - 1, choice (params.algorithm)));

    dsp::ChainSettings s;
    s.algorithm = algorithm;

    auto& e = s.engine;
    e.decaySeconds = juce::jlimit (params::decayMinS, params::decayMaxS, params.decay->load());
    e.sizeScale    = dsp::maps::sizeToScale (params.size->load() / 100.0f);
    e.mod          = params.mod->load() / 100.0f;
    e.modRate      = params.modRate->load();
    e.tone         = params.tone->load() / params::toneRange;
    e.lowEnd       = params.lowEnd->load();
    e.diffusion    = params.diffusion->load() / 100.0f;
    e.paramA       = params.paramA->load() / 100.0f;
    e.paramB       = params.paramB->load() / 100.0f;
    e.echoTimeMs   = flag (params.echoSync) ? (float) syncedMs (choice (params.echoNote), bpm)
                                            : dsp::maps::echoTimeMs (e.paramA);
    e.echoTimeMs   = std::min (e.echoTimeMs, dsp::maps::echoMaxMs);

    s.freeze          = flag (params.freeze);
    s.freezeLayer     = flag (params.freezeLayer);
    s.preDelayMs      = flag (params.preDelaySync) ? (float) syncedMs (choice (params.preDelayNote), bpm)
                                                   : params.preDelay->load();
    s.preDelayMs      = std::min (s.preDelayMs, dsp::ReverbChain::maxPreDelayMs);
    s.mix             = params.mix->load() / 100.0f;
    s.wetOnly         = flag (params.wetOnly);
    s.outputDb        = params.output->load();
    s.duck            = flag (params.duck);
    s.duckAmountDb    = params.duckAmount->load();
    s.duckThresholdDb = params.duckThreshold->load();
    s.duckReleaseMs   = params.duckRelease->load();
    s.width           = params.width->load() / 100.0f;
    s.inLowCutHz      = params.inLowCut->load();
    s.inHighCutHz     = params.inHighCut->load();
    s.bypass          = params.bypass->get();
    s.trails          = flag (params.trails);
    s.presetGeneration = presetGeneration.load (std::memory_order_acquire);
    return s;
}

double CeruleanProcessor::getTailLengthSeconds() const
{
    if (flag (params.freeze))
        return std::numeric_limits<double>::infinity();

    const auto algorithm = static_cast<Algorithm> (juce::jlimit (0, numAlgorithms - 1, choice (params.algorithm)));
    auto tail = 1.5 * (double) params.decay->load() + 2.0;   // RT60 is -60 dB; go a little further

    if (algorithm == Algorithm::echo || algorithm == Algorithm::billow)
        tail += 6.0;
    return tail;
}

void CeruleanProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    // Mono in: feed the same signal to both sides of the engine.
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    double hostBpm = 0.0;
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
            if (const auto b = position->getBpm())
                hostBpm = *b;

    meters.bpm.store (hostBpm > 0.0 ? hostBpm : (double) params.bpm->load(), std::memory_order_relaxed);
    meters.hostHasTempo.store (hostBpm > 0.0, std::memory_order_relaxed);

    dsp::ChainMeters m;
    chain.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples, readSettings (hostBpm), m);

    meters.inputRms.store (m.inputRms, std::memory_order_relaxed);
    meters.wetRms.store (m.wetRms, std::memory_order_relaxed);
    meters.outputRms.store (m.outputRms, std::memory_order_relaxed);
    meters.duckGain.store (m.duckGain, std::memory_order_relaxed);
    meters.freeze.store (m.freeze, std::memory_order_relaxed);
    MeterValues::storeMax (meters.inputPeak, m.inputPeak);
    MeterValues::storeMax (meters.wetPeak, m.wetPeak);
    MeterValues::storeMax (meters.outputPeak, m.outputPeak);
}

//==============================================================================
int CeruleanProcessor::getNumPrograms()
{
    return (int) programs.size();
}

int CeruleanProcessor::getCurrentProgram()
{
    return juce::jlimit (0, getNumPrograms() - 1, currentProgram.load (std::memory_order_relaxed));
}

const juce::String CeruleanProcessor::getProgramName (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) programs.size()))
        return {};

    const auto& program = programs[(size_t) index];
    return program.isUser ? "User: " + program.name : program.name;
}

void CeruleanProcessor::setCurrentProgram (int index)
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

void CeruleanProcessor::applyProgramValues (int index)
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

void CeruleanProcessor::loadProgram (int index)
{
    const auto& program = programs[(size_t) index];
    const auto& entries = presets->getEntries();

    // By name: the browser's list may have changed since this instance was created.
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == program.name && entries[i].isFactory != program.isUser)
            return presets->load ((int) i);
}

int CeruleanProcessor::programFor (const juce::String& name, bool isUser) const noexcept
{
    for (size_t i = 0; i < programs.size(); ++i)
        if (programs[i].isUser == isUser && programs[i].name == name)
            return (int) i;

    return 0;   // an unsaved sound, or a user preset newer than this instance
}

void CeruleanProcessor::currentPresetChanged (const juce::String& name, bool isUser)
{
    const auto program = programFor (name, isUser);

    if (currentProgram.exchange (program, std::memory_order_relaxed) != program)
        updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

juce::AudioProcessorEditor* CeruleanProcessor::createEditor()
{
   #if CERULEAN_HAS_EDITOR
    return new CeruleanEditor (*this);
   #else
    return new juce::GenericAudioProcessorEditor (*this);
   #endif
}

void CeruleanProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (stateVersionId, stateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CeruleanProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    // Future: migrate older state versions here, based on state[stateVersionId].

    // The restored session wins over a program change the browser has not shown yet, and the
    // host program follows its preset (read from the new tree: the live UI state belongs to
    // the message thread).
    pendingProgram.store (-1);
    const auto ui = state.getChildWithName ("UI");
    mixLocked.store ((bool) ui.getProperty ("mixLock", false), std::memory_order_relaxed);
    currentProgram.store (programFor (ui.getProperty ("presetName").toString(), (bool) ui.getProperty ("presetIsUser", false)),
                          std::memory_order_relaxed);

    apvts.replaceState (state);

    // replaceState() skips a parameter whose restored value equals its cached *snapped* value,
    // so a bool a host set to a raw 0.76 would stay 0.76 when the session says 1. Write any
    // parameter whose raw value is not what the session says.
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (const auto child = state.getChildWithProperty ("id", ranged->getParameterID()); child.isValid())
                if (const auto target = ranged->convertTo0to1 ((float) child.getProperty ("value"));
                    std::abs (ranged->getValue() - target) > 1.0e-6f)
                    ranged->setValueNotifyingHost (target);

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

void CeruleanProcessor::handleAsyncUpdate()
{
    if (pendingUndoClear.exchange (false))
        undoManager.clearUndoHistory();

    // A program set off the message thread: its values are in place, now the browser shows it.
    if (const auto program = pendingProgram.exchange (-1); program >= 0)
        presets->setCurrent (programs[(size_t) program].name, programs[(size_t) program].isUser);
}

} // namespace cerulean
