#include "plugin/PluginProcessor.h"

#include "common/plugin/SessionState.h"
#include "plugin/FactoryPresets.h"
#include "ui/HaliteEditor.h"

namespace halite
{

static_assert (myzel::dsp::isExactly (dsp::essDeepestCutDb, params::rangeMaxDb)
                   && myzel::dsp::isExactly (dsp::harshDeepestCutDb, params::harshRangeMaxDb),
               "Split's fits must hold every depth the Ranges allow");

namespace
{
    /** A session saved before a parameter existed gives it its default, not whatever this instance had. */
    void addMissingDefaults (juce::ValueTree& state, juce::AudioProcessor& processor)
    {
        for (auto* p : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! state.getChildWithProperty ("id", ranged->getParameterID()).isValid())
                    state.appendChild (juce::ValueTree ("PARAM", { { "id", ranged->getParameterID() },
                                                                   { "value", ranged->convertFrom0to1 (ranged->getDefaultValue()) } }),
                                       nullptr);
    }

    bool isMonoOrStereo (const juce::AudioChannelSet& set)
    {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    }
}

HaliteProcessor::HaliteProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Side Chain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, params::stateTag, params::createLayout()),
      params (apvts),
      presets (std::make_unique<myzel::plugin::PresetManager> (*this, presetFormat()))
{
    // Learn sets the thresholds as one undo step, editor open or not (PLAN.md, Shared infrastructure).
    learnControl.beforeApplying = [this] { undoManager.beginNewTransaction ("Learn"); };

    // A host may ask before prepareToPlay(): the frame at 44.1 and 48 kHz, the likeliest rates.
    setLatencySamples (dsp::HaliteChain::latencySamples (getLatencyMode(), 48000.0));
}

HaliteProcessor::~HaliteProcessor()
{
    cancelPendingUpdate();
}

void HaliteProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    const auto mode = getLatencyMode();
    preparedRate.store (sampleRate);
    chain.prepare (sampleRate, maximumBlockSize, getMainBusNumOutputChannels(), mode);
    setLatencySamples (dsp::HaliteChain::latencySamples (mode, sampleRate));
}

void HaliteProcessor::reset()
{
    chain.reset();
}

bool HaliteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out || ! isMonoOrStereo (out) || layouts.outputBuses.size() != 1 || layouts.inputBuses.size() > 2)
        return false;

    if (layouts.inputBuses.size() < 2)
        return true;

    const auto key = layouts.getChannelSet (true, 1);
    return key.isDisabled() || isMonoOrStereo (key);
}

void HaliteProcessor::setLatencyMode (LatencyMode mode)
{
    latencyMode.store (static_cast<int> (mode));
    // The rate prepareToPlay() was given: a host may call it without setRateAndBufferSizeDetails().
    const auto rate = preparedRate.load();
    setLatencySamples (dsp::HaliteChain::latencySamples (mode, rate > 0.0 ? rate : getSampleRate()));
}

dsp::ChainSettings HaliteProcessor::readSettings() const noexcept
{
    const auto on = [] (const std::atomic<float>* p) { return p->load() >= 0.5f; };
    const auto index = [] (const std::atomic<float>* p) { return juce::roundToInt (p->load()); };

    dsp::ChainSettings s;
    auto& d = s.detector;
    d.detection = static_cast<Detection> (juce::jlimit (0, numDetections - 1, index (params.detection)));
    d.thresholdDb = params.threshold->load();
    d.thresholdAbsDb = params.thresholdAbs->load();
    d.rangeDb = params.range->load();
    d.detectLowHz = params.detectLow->load();
    d.detectHighHz = params.detectHigh->load();
    d.focus = params.focus->load() / 100.0f;
    d.speedAuto = on (params.speedAuto);
    d.releaseMs = params.release->load();
    d.lookaheadMs = params.lookahead->load();
    d.latency = getLatencyMode();
    d.stereo = static_cast<StereoMode> (juce::jlimit (0, numStereoModes - 1, index (params.stereo)));
    d.key = static_cast<KeySource> (juce::jlimit (0, numKeySources - 1, index (params.key)));
    const auto keyHpf = params.keyHpf->load();
    d.keyHpfHz = params::isKeyHpfOff (keyHpf) ? 0.0f : keyHpf;
    d.harshOn = on (params.harshOn);
    d.harshThresholdDb = params.harshThreshold->load();
    d.harshThresholdAbsDb = params.harshThresholdAbs->load();
    d.harshRangeDb = params.harshRange->load();
    d.harshFreqHz = params.harshFreq->load();
    d.harshWidthOct = params.harshWidth->load();
    d.harshSpeedAuto = on (params.harshSpeedAuto);
    d.harshReleaseMs = params.harshRelease->load();

    s.shape = static_cast<Shape> (juce::jlimit (0, numShapes - 1, index (params.shape)));
    s.listen = static_cast<Listen> (juce::jlimit (0, numListenModes - 1, index (params.listen)));
    s.outputDb = params.output->load();
    s.bypass = params.bypass->get();
    return s;
}

void HaliteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The main bus comes first in the buffer; the key's channels follow and are not output.
    const auto numChannels = std::min (buffer.getNumChannels(), getMainBusNumOutputChannels());
    const auto key = getBusCount (true) > 1 ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();

    // Spectral's grids sit on the timeline, and its target restarts when the transport starts or jumps.
    dsp::Timeline timeline;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto samples = position->getTimeInSamples())
                timeline = { *samples, true, position->getIsPlaying() };

    chain.process (buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples(), readSettings(),
                   key.getNumChannels() > 0 ? key.getArrayOfReadPointers() : nullptr, key.getNumChannels(), timeline);
}

juce::AudioProcessorEditor* HaliteProcessor::createEditor()
{
    return new HaliteEditor (*this);
}

// The latency mode lives in an atomic, never in the live tree: setLatencyMode() could not take the
// APVTS lock that copyState() holds, so the mode joins only the saved copy. The format is
// sessionState::write's plus that property.
void HaliteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // A restored tree carries the version already; re-added in one order, a reloaded session saves the same bytes.
    state.removeProperty (myzel::plugin::sessionState::versionId, nullptr);
    state.setProperty (sessionKeys::latencyMode, latencyMode.load(), nullptr);
    state.setProperty (myzel::plugin::sessionState::versionId, stateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HaliteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = myzel::plugin::sessionState::read (apvts, data, sizeInBytes);

    if (! state.isValid())
        return;

    // Absent (a session from before the setting) or not understood (a newer one): Normal.
    const auto stored = (int) state.getProperty (sessionKeys::latencyMode, static_cast<int> (LatencyMode::normal));
    const auto mode = stored >= 0 && stored < numLatencyModes ? static_cast<LatencyMode> (stored) : LatencyMode::normal;
    state.removeProperty (sessionKeys::latencyMode, nullptr);

    // Future: migrate older state versions here, based on state[sessionState::versionId].
    addMissingDefaults (state, *this);
    myzel::plugin::sessionState::restore (apvts, state);
    setLatencyMode (mode);

    // A restored session starts with a fresh history. Hosts may call this off the message thread,
    // and UndoManager is message-thread only.
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

void HaliteProcessor::handleAsyncUpdate()
{
    if (pendingUndoClear.exchange (false))
        undoManager.clearUndoHistory();
}

} // namespace halite
