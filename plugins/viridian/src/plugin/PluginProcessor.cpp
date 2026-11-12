#include "plugin/PluginProcessor.h"

#include "common/plugin/SessionState.h"

namespace viridian
{

namespace
{
    int choice (const std::atomic<float>* p) { return juce::roundToInt (p->load()); }
    bool flag (const std::atomic<float>* p) { return p->load() >= 0.5f; }

    /**
        A session saved before a parameter existed (the later dynamics section) gives it its
        default, not whatever this instance had: older sessions load unchanged.
    */
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

ViridianProcessor::ViridianProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, params::stateTag, params::createLayout()),
      params (apvts)
{
}

void ViridianProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    chain.prepare (sampleRate, maximumBlockSize, getTotalNumOutputChannels());
    setLatencySamples (chain.getLatencySamples());
}

void ViridianProcessor::reset()
{
    chain.reset();
}

bool ViridianProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    // Mono -> mono, stereo -> stereo.
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

dsp::ChainSettings ViridianProcessor::readSettings() const noexcept
{
    dsp::ChainSettings s;
    s.model = static_cast<Model> (juce::jlimit (0, numModels - 1, choice (params.model)));
    s.alt = flag (params.alt);
    for (size_t i = 0; i < (size_t) numControls; ++i)
        s.positions[i] = params.controls[i]->load();
    s.driveDb = params.drive->load();
    s.eqIn = flag (params.eqIn);
    s.outputDb = params.output->load();
    s.stereo = static_cast<StereoMode> (juce::jlimit (0, numStereoModes - 1, choice (params.stereo)));
    s.tolerance = params.tolerance->load() / 100.0f;
    s.noise = params.noise->load() / 100.0f;
    s.bypass = params.bypass->get();
    return s;
}

void ViridianProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto numChannels = std::min (buffer.getNumChannels(), getTotalNumOutputChannels());
    chain.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, readSettings());
}

juce::AudioProcessorEditor* ViridianProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void ViridianProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    myzel::plugin::sessionState::write (apvts, stateVersion, destData);
}

void ViridianProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = myzel::plugin::sessionState::read (apvts, data, sizeInBytes);

    if (! state.isValid())
        return;

    // Future: migrate older state versions here, based on state[sessionState::versionId].
    addMissingDefaults (state, *this);
    myzel::plugin::sessionState::restore (apvts, state);
}

} // namespace viridian
