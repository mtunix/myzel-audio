#include "plugin/PluginProcessor.h"

#include "common/plugin/SessionState.h"

namespace basalt
{

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

BasaltProcessor::BasaltProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Side Chain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, params::stateTag, params::createLayout()),
      params (apvts)
{
    setLatencySamples (dsp::BasaltChain::latencySamples (getLatencyMode(), reportingRate()));
}

double BasaltProcessor::reportingRate() const noexcept
{
    // The rate prepareToPlay() was given (a host may call it without setRateAndBufferSizeDetails()),
    // else the host's, else the likeliest: a host may ask for the latency before it prepares.
    if (const auto prepared = preparedRate.load(); prepared > 0.0)
        return prepared;
    return getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
}

void BasaltProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    const auto mode = getLatencyMode();
    preparedRate.store (sampleRate);
    chain.prepare (sampleRate, maximumBlockSize, getMainBusNumOutputChannels(), mode);
    setLatencySamples (dsp::BasaltChain::latencySamples (mode, sampleRate));
}

void BasaltProcessor::reset()
{
    chain.reset();
}

bool BasaltProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void BasaltProcessor::setLatencyMode (LatencyMode mode)
{
    latencyMode.store (static_cast<int> (mode));
    setLatencySamples (dsp::BasaltChain::latencySamples (mode, reportingRate()));
}

dsp::ChainSettings BasaltProcessor::readSettings() const noexcept
{
    const auto on = [] (const std::atomic<float>* p) { return p->load() >= 0.5f; };
    const auto index = [] (const std::atomic<float>* p, int count) { return juce::jlimit (0, count - 1, juce::roundToInt (p->load())); };

    dsp::ChainSettings s;
    s.model = static_cast<Model> (index (params.model, numModels));
    s.vcaDetector = static_cast<VcaDetector> (index (params.vcaDetector, numVcaDetectors));
    s.thresholdDb = params.threshold->load();
    s.inverseRatio = params::inverseRatio (params.ratio->load());
    s.kneeDb = params.knee->load();
    s.attackMs = params.attack->load();
    s.releaseMs = params.release->load();
    s.autoRelease = on (params.autoRelease);
    s.rangeDb = params.range->load();
    s.floorDb = params.floor->load();
    s.gainDb = params.gain->load();
    s.autoGain = on (params.autoGain);
    s.mix = params.mix->load() / 100.0;
    s.lookaheadMs = params.lookahead->load();
    s.stereo = static_cast<StereoMode> (index (params.stereo, numStereoModes));
    s.link = params.link->load() / 100.0;
    s.key = static_cast<KeySource> (index (params.key, numKeySources));

    const auto hpf = params.keyHpf->load(), lpf = params.keyLpf->load();
    s.keyEq.highPass = ! params::isKeyHpfOff (hpf);
    s.keyEq.highPassHz = hpf;
    s.keyEq.lowPass = ! params::isKeyLpfOff (lpf);
    s.keyEq.lowPassHz = lpf;
    s.keyEq.bellHz = params.keyBellFreq->load();
    s.keyEq.bellDb = params.keyBellGain->load();
    s.keyEq.bellQ = params.keyBellQ->load();

    s.listen = static_cast<Listen> (index (params.listen, numListenModes));
    s.outputDb = params.output->load();
    s.bypass = params.bypass->get();
    s.latency = getLatencyMode();
    return s;
}

void BasaltProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The main bus comes first in the buffer; the key's channels follow and are never output.
    const auto numChannels = std::min (buffer.getNumChannels(), getMainBusNumOutputChannels());
    const auto key = getBusCount (true) > 1 ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();

    chain.process (buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples(), readSettings(),
                   key.getNumChannels() > 0 ? key.getArrayOfReadPointers() : nullptr, key.getNumChannels());
}

juce::AudioProcessorEditor* BasaltProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

// The latency mode lives in an atomic, never in the live tree: setLatencyMode() could not take the
// APVTS lock that copyState() holds, so the mode joins only the saved copy. The format is
// sessionState::write's plus that property.
void BasaltProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // A restored tree carries the version already; re-added in one order, a reloaded session saves the same bytes.
    state.removeProperty (myzel::plugin::sessionState::versionId, nullptr);
    state.setProperty (sessionKeys::latencyMode, latencyMode.load(), nullptr);
    state.setProperty (myzel::plugin::sessionState::versionId, stateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BasaltProcessor::setStateInformation (const void* data, int sizeInBytes)
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
}

} // namespace basalt
