#include "dsp/detector/DetectorProbe.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace halite::dsp::probe
{

namespace
{
    struct Collector final : DetectorObserver
    {
        std::vector<DetectorFrame>* frames = nullptr;
        std::vector<EssEvent>* events = nullptr;

        void hop (const DetectorFrame& f) noexcept override { frames->push_back (f); }
        void event (const EssEvent& e) noexcept override { events->push_back (e); }
    };
}

Result run (const float* const* input, int numChannels, int numSamples, double sampleRate,
            const DetectorSettings& settings, const float* const* key, int numKeyChannels,
            const std::vector<int>& blockSizes, const DetectorTuning& tuning)
{
    const juce::ScopedNoDenormals noDenormals;

    Result result;
    result.sampleRate = sampleRate;

    if (numChannels <= 0 || numSamples <= 0)
        return result;

    Detector detector;
    detector.prepare (sampleRate, tuning);

    std::vector<DetectorFrame> frames;
    frames.reserve ((size_t) numSamples / Detector::hopSize + 64);
    Collector collector;
    collector.frames = &frames;
    collector.events = &result.events;

    const auto numKey = key != nullptr ? std::clamp (numKeyChannels, 0, 2) : 0;
    numChannels = std::min (numChannels, 2);

    std::array<const float*, 2> in {}, side {};
    auto pos = 0;
    size_t k = 0;

    while (pos < numSamples)
    {
        const auto wanted = blockSizes.empty() ? 512 : std::max (1, blockSizes[k++ % blockSizes.size()]);
        const auto n = std::min (wanted, numSamples - pos);
        for (int c = 0; c < numChannels; ++c)
            in[(size_t) c] = input[c] + pos;
        for (int c = 0; c < numKey; ++c)
            side[(size_t) c] = key[c] + pos;

        detector.process (in.data(), numChannels, numKey > 0 ? side.data() : nullptr, numKey, n, settings, &collector);
        pos += n;
    }

    // Silence after the input: completes the last hop, brings out the look-ahead's reductions and
    // lets an open event end (at most 2 s more).
    const auto lookahead = detector.reductionDelaySamples();
    result.lookaheadSamples = lookahead;
    const auto numHops = (numSamples + Detector::hopSize - 1) / Detector::hopSize;
    const auto lookaheadHops = lookahead / Detector::hopSize;

    std::array<float, Detector::hopSize> zeros {};
    std::array<const float*, 2> silent { zeros.data(), zeros.data() };
    const auto tail = numHops * Detector::hopSize - numSamples;
    const auto maxFlushHops = numHops + lookaheadHops + (int) std::ceil (2.0 * sampleRate / Detector::hopSize);

    if (tail > 0)
        detector.process (silent.data(), numChannels, numKey > 0 ? silent.data() : nullptr, numKey, tail, settings, &collector);

    while ((int) frames.size() < numHops + lookaheadHops || (detector.eventActive() && (int) frames.size() < maxFlushHops))
        detector.process (silent.data(), numChannels, numKey > 0 ? silent.data() : nullptr, numKey, Detector::hopSize, settings, &collector);

    result.hops.assign (frames.begin(), frames.begin() + numHops);
    for (int i = 0; i < numHops; ++i)
    {
        const auto& late = frames[(size_t) (i + lookaheadHops)];
        auto& h = result.hops[(size_t) i];
        h.reductions = late.reductions;
        h.eventActive = late.eventActive;
        h.reductionPosition = h.position;
    }

    return result;
}

} // namespace halite::dsp::probe
