// HaliteChain: the detectors run on the key every block and publish what they find, and the audio is
// the latency-aligned input less what the shapes remove (PLAN.md, Signal flow). The reduction's own
// gates are in tests/reduction.

#include "dsp/detector/DetectorProbe.h"
#include "support/ProcessorHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <numbers>

using namespace halite;
using namespace halite::testing;

namespace
{
    /** A vowel-like tone with three noise bursts in the Detect band, and a second of silence after. */
    juce::AudioBuffer<float> voiceWithEsses (int channels, double rate, unsigned seed)
    {
        const auto n = (int) (3.0 * rate);
        juce::AudioBuffer<float> b (channels, n);
        b.clear();
        std::mt19937 rng (seed);
        std::normal_distribution<float> gauss;
        myzel::dsp::TptSvf hp, lp;
        hp.setCoefficients (5000.0, 0.7071, rate);
        lp.setCoefficients (11000.0, 0.7071, rate);

        for (int i = 0; i < (int) (2.0 * rate); ++i)
        {
            const auto t = i / rate;
            auto x = 0.0;
            for (int k = 1; k <= 12; ++k)
                x += 0.1 / (k * k) * std::sin (2.0 * std::numbers::pi * 180.0 * k * t);
            const auto burst = std::fmod (t, 0.6) > 0.4 && std::fmod (t, 0.6) < 0.55;
            const auto noise = lp.processLowpass (hp.processHighpass (gauss (rng)));
            x += burst ? 0.05 * noise : 0.0;
            for (int c = 0; c < channels; ++c)
                b.setSample (c, i, (float) (x * (c == 0 ? 1.0 : 0.8)));
        }
        return b;
    }

    /** Runs the chain over the buffer in place, in blocks of the given sizes (cycled). */
    void runChain (dsp::HaliteChain& chain, juce::AudioBuffer<float>& buffer, const dsp::ChainSettings& s,
                   const std::vector<int>& sizes, const juce::AudioBuffer<float>* key = nullptr)
    {
        size_t k = 0;
        for (int pos = 0; pos < buffer.getNumSamples();)
        {
            const auto n = std::min (sizes[k++ % sizes.size()], buffer.getNumSamples() - pos);
            std::array<float*, 2> channels {};
            std::array<const float*, 2> side {};
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                channels[(size_t) c] = buffer.getWritePointer (c, pos);
            if (key != nullptr)
                for (int c = 0; c < key->getNumChannels(); ++c)
                    side[(size_t) c] = key->getReadPointer (c, pos);

            chain.process (channels.data(), buffer.getNumChannels(), n, s,
                           key != nullptr ? side.data() : nullptr, key != nullptr ? key->getNumChannels() : 0);
            pos += n;
        }
    }

    std::vector<dsp::EssEvent> eventsOf (const dsp::HaliteChain& chain)
    {
        std::vector<dsp::EssEvent> events ((size_t) dsp::EventRing::size);
        std::uint64_t cursor = 0;
        events.resize ((size_t) chain.events().read (cursor, events.data(), (int) events.size()));
        return events;
    }
}

TEST_CASE ("Chain: the output is the aligned input less what Listen Removed plays", "[chain]")
{
    const auto rate = 48000.0;
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);

    for (const auto cue : { dsp::NoiseCueKind::periodicity, dsp::NoiseCueKind::flatness, dsp::NoiseCueKind::blend })
        for (const auto stereo : { StereoMode::linked, StereoMode::unlinked, StereoMode::mid, StereoMode::side })
            for (const auto key : { KeySource::internal, KeySource::external })
            {
                dsp::ChainSettings s;
                s.detector.noiseCue = cue;
                s.detector.stereo = stereo;
                s.detector.key = key;
                s.detector.thresholdDb = -14.0f;
                s.detector.harshOn = true;

                const auto input = voiceWithEsses (2, rate, 7u);
                const auto side = voiceWithEsses (2, rate, 8u);
                const auto render = [&] (Listen listen)
                {
                    dsp::HaliteChain chain;
                    chain.prepare (rate, 512, 2, LatencyMode::normal);
                    auto settings = s;
                    settings.listen = listen;
                    auto buffer = input;
                    runChain (chain, buffer, settings, { 512, 17, 4096, 1, 300 }, &side);
                    INFO ("cue " << (int) cue << ", stereo " << (int) stereo << ", key " << (int) key);
                    CHECK (chain.events().count() > 0);
                    // The input ends in a second of silence, where B has decayed to nothing; V holds.
                    CHECK (chain.meters().bodyDb.load() > dsp::silentDb);
                    CHECK (chain.meters().harshBodyDb.load() > dsp::silentDb);
                    return buffer;
                };
                const auto output = render (Listen::output), removed = render (Listen::removed);

                INFO ("cue " << (int) cue << ", stereo " << (int) stereo << ", key " << (int) key);
                auto worst = 0.0f, largest = 0.0f;
                for (int c = 0; c < 2; ++c)
                    for (int i = delay; i < input.getNumSamples(); ++i)
                    {
                        const auto aligned = input.getSample (c, i - delay);
                        worst = std::max (worst, std::abs (output.getSample (c, i) + removed.getSample (c, i) - aligned));
                        largest = std::max (largest, std::abs (removed.getSample (c, i)));
                    }
                CHECK (largest > 1.0e-3f);
                CHECK (worst <= 1.0e-6f);
            }
}

TEST_CASE ("Chain: the events it publishes are the detector's", "[chain]")
{
    // The same Detector the probe runs: the chain's event ring holds exactly the probe's events.
    const auto rate = 44100.0;
    const auto input = voiceWithEsses (1, rate, 3u);

    dsp::ChainSettings s;
    s.detector.thresholdDb = -14.0f;
    dsp::HaliteChain chain;
    chain.prepare (rate, 1024, 1, LatencyMode::normal);
    auto buffer = input;
    runChain (chain, buffer, s, { 1024 });

    const float* channel = input.getReadPointer (0);
    const auto probe = dsp::probe::run (&channel, 1, input.getNumSamples(), rate, s.detector);
    const auto events = eventsOf (chain);

    REQUIRE (events.size() == probe.events.size());
    REQUIRE (! events.empty());
    for (size_t e = 0; e < events.size(); ++e)
    {
        CHECK (events[e].start == probe.events[e].start);
        CHECK (events[e].end == probe.events[e].end);
        CHECK (juce::exactlyEqual (events[e].peakReductionDb, probe.events[e].peakReductionDb));
        CHECK (juce::exactlyEqual (events[e].peakSibilanceDb, probe.events[e].peakSibilanceDb));
    }

    // Silence afterwards: the meters fall back, the last event is over.
    CHECK_FALSE (chain.meters().eventActive.load());
    CHECK (chain.meters().essReductionDb[0].load() < 0.5f);
}

TEST_CASE ("Processor: the detectors read the side chain when Key is External", "[processor][key]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    const auto rate = 48000.0;

    // Silence on the main bus, esses on the side chain: only the key can produce events.
    juce::AudioBuffer<float> io (4, (int) (2.0 * rate));
    io.clear();
    const auto esses = voiceWithEsses (2, rate, 5u);
    for (int c = 0; c < 2; ++c)
        io.copyFrom (2 + c, 0, esses, c, 0, io.getNumSamples());

    const auto eventsWith = [&] (KeySource key, bool keyBus)
    {
        HaliteProcessor p;
        REQUIRE (p.setBusesLayout (layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo(),
                                           keyBus ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled())));
        // Allround with the lowest threshold: the relative range is re-centred later in M1.
        setParam (p, params::key, (float) static_cast<int> (key));
        setParam (p, params::detection, (float) static_cast<int> (Detection::allround));
        setParam (p, params::thresholdAbs, params::absThresholdMinDb);
        p.prepareToPlay (rate, 512);

        juce::AudioBuffer<float> buffer (io);
        if (! keyBus)
            buffer.setSize (2, buffer.getNumSamples(), true);
        const auto out = run (p, buffer, { 512 });
        CHECK (identical (out, buffer, 2));     // the main bus is silent, and so is what it loses
        return p.getChain().events().count();
    };

    CHECK (eventsWith (KeySource::external, true) > 0);
    CHECK (eventsWith (KeySource::internal, true) == 0);
    CHECK (eventsWith (KeySource::external, false) == 0);     // no side chain: the silent input is the key
}

TEST_CASE ("Processor: the host's play head anchors the analysis and restarts it on a jump", "[processor][timeline]")
{
    // PLAN.md, Reduction (Warm start): positions are the timeline's, so events carry song
    // positions, hops end on its multiples of 32, and a jump while playing restarts there.
    const juce::ScopedJuceInitialiser_GUI juce;
    const auto rate = 48000.0;
    const auto input = voiceWithEsses (2, rate, 7u);

    struct Head final : juce::AudioPlayHead
    {
        std::int64_t position = 0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setTimeInSamples (position);
            info.setIsPlaying (playing);
            return info;
        }
    };

    const auto eventsFrom = [&] (Head* head, std::int64_t start, std::int64_t jumpAt, std::int64_t jumpTo)
    {
        HaliteProcessor p;
        if (head != nullptr)
            p.setPlayHead (head);
        p.prepareToPlay (rate, 512);
        juce::AudioBuffer<float> io (input);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < io.getNumSamples(); pos += 512)
        {
            if (head != nullptr)
                head->position = pos < jumpAt ? start + pos : jumpTo + (pos - jumpAt);
            juce::AudioBuffer<float> block (io.getArrayOfWritePointers(), 2, pos, std::min (512, io.getNumSamples() - pos));
            p.processBlock (block, midi);
        }
        std::vector<dsp::EssEvent> events (64);
        std::uint64_t cursor = 0;
        events.resize ((size_t) p.getChain().events().read (cursor, events.data(), 64));
        return events;
    };

    const auto plain = eventsFrom (nullptr, 0, 1 << 30, 0);
    Head head;
    const std::int64_t start = 3'000'037;
    const auto anchored = eventsFrom (&head, start, 1 << 30, 0);
    REQUIRE (plain.size() >= 2);
    REQUIRE (anchored.size() == plain.size());
    for (size_t i = 0; i < plain.size(); ++i)
    {
        INFO ("event " << i << ": " << plain[i].start << " without a play head, " << anchored[i].start - start << " after " << start);
        CHECK (anchored[i].start % 32 == 0);
        CHECK (std::abs ((anchored[i].start - start) - plain[i].start) < 2 * 32);
    }

    // A jump at 1 s to 20 s: the events after it are at song positions after 20 s.
    const std::int64_t jumpAt = 48000 / 512 * 512, jumpTo = 960'000;
    const auto jumped = eventsFrom (&head, 0, jumpAt, jumpTo);
    auto after = 0;
    for (const auto& e : jumped)
        if (e.start >= jumpTo)
        {
            ++after;
            CHECK (e.start % 32 == 0);
        }
        else
        {
            CHECK (e.end <= jumpAt + 4096);
        }
    CHECK (after >= 1);
}
