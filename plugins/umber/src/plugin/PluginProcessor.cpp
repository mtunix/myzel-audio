#include "plugin/PluginProcessor.h"

#include "ui/UmberEditor.h"

namespace umber
{

namespace
{
    const juce::Identifier stateTreeType { "UmberState" };
    const juce::Identifier stateVersionId { "stateVersion" };

    constexpr int realtimeFactors[] = { 2, 4, 8 };   // Quality: Eco, Standard, High
    constexpr int renderFactors[]   = { 0, 8, 16 };  // Render: same, High, Ultra
}

UmberProcessor::~UmberProcessor()
{
    cancelPendingUpdate();
}

UmberProcessor::UmberProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, stateTreeType, params::createLayout()), // undo: see ParameterUndoRecorder
      params (apvts)
{
}

void UmberProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    const auto numChannels = std::max (1, getTotalNumOutputChannels());
    chain.prepare (sampleRate, maximumBlockSize, numChannels);
    setLatencySamples (chain.getLatencySamples());
}

void UmberProcessor::reset()
{
    chain.reset();
}

bool UmberProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

dsp::ChainSettings UmberProcessor::readSettings() const noexcept
{
    auto choice = [] (const std::atomic<float>* p) { return juce::roundToInt (p->load()); };
    auto flag   = [] (const std::atomic<float>* p) { return p->load() >= 0.5f; };

    dsp::ChainSettings s;
    s.driveKnob      = params.drive->load();
    s.slam           = flag (params.slam);
    s.model          = static_cast<Model> (juce::jlimit (0, numModels - 1, choice (params.model)));
    s.tone           = params.tone->load() / params::toneRange;
    s.lowCutHz       = params.lowCut->load();
    s.weight         = flag (params.weight);
    s.highCutHz      = params.highCut->load();
    s.autoGain       = flag (params.autoGain);
    s.mix            = params.mix->load() / 100.0f;
    s.outputDb       = params.output->load();
    s.realtimeFactor = realtimeFactors[juce::jlimit (0, 2, choice (params.quality))];
    s.renderFactor   = renderFactors[juce::jlimit (0, 2, choice (params.renderQuality))];
    s.bypass         = params.bypass->get();
    s.delta          = flag (params.delta);
    s.stereoMode     = static_cast<StereoMode> (juce::jlimit (0, 2, choice (params.stereoMode)));
    s.emphasis       = params.emphasis->load() / params::emphasisRange;
    s.bias           = params.bias->load() / params::biasRange;
    return s;
}

void UmberProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    dsp::ChainMeters m;
    chain.process (buffer, readSettings(), isNonRealtime(), m);

    meters.inputRms.store (m.inputRms, std::memory_order_relaxed);
    meters.driveRms.store (m.driveRms, std::memory_order_relaxed);
    meters.outputRms.store (m.outputRms, std::memory_order_relaxed);
    MeterValues::storeMax (meters.inputPeak, m.inputPeak);
    MeterValues::storeMax (meters.outputPeak, m.outputPeak);
    meters.oversamplingFactor.store (m.oversamplingFactor, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* UmberProcessor::createEditor()
{
    return new UmberEditor (*this);
}

void UmberProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (stateVersionId, stateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void UmberProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    // Future: migrate older state versions here, based on state[stateVersionId].
    apvts.replaceState (state);

    // A restored session starts with a fresh history. Hosts may call this off the message
    // thread, and UndoManager is message-thread only.
    if (juce::MessageManager::existsAndIsCurrentThread())
        undoManager.clearUndoHistory();
    else
        triggerAsyncUpdate();
}

void UmberProcessor::handleAsyncUpdate()
{
    undoManager.clearUndoHistory();
}

} // namespace umber
