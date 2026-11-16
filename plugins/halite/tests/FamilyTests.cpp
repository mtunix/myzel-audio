// The family's engineering gates through the processor, with the reduction acting (PLAN.md,
// Measurement and quality gates): every channel layout, 44.1–192 kHz, block sizes 1–4096 and
// variable, state round-trip, bit-identical offline renders, latency reported equals measured.
// The identity of the idle path is in ProcessorTests.cpp; the shapes' own gates in tests/reduction.

#include "detector/Signals.h"
#include "support/ProcessorHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <tuple>

using namespace halite;
using namespace halite::testing;
using Set = juce::AudioChannelSet;

namespace
{
    constexpr std::array shapes { Shape::split, Shape::spectral, Shape::wide };

    const char* nameOf (Shape s) { return s == Shape::split ? "Split" : (s == Shape::spectral ? "Spectral" : "Wide"); }

    /** A vowel with a resonance in the Harsh band and, if asked, an ess every 0.5 s from 0.4 s on. Made once per set of arguments. */
    Samples voice (double rate, double seconds, bool esses, unsigned seed)
    {
        static std::map<std::tuple<double, double, bool, unsigned>, Samples> made;
        if (const auto it = made.find ({ rate, seconds, esses, seed }); it != made.end())
            return it->second;

        const auto n = samplesFor (seconds, rate);
        auto x = harmonic ({ .f0 = 170.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -9.0, .peakHz = 3200.0, .peakDb = 18.0, .rms = 0.1,
                             .seed = seed }, n, rate);
        if (esses)
            for (auto t = 0.4; t + 0.14 < seconds; t += 0.5)
                x = sum (x, gated (bandNoise (4500.0, 12000.0, n, rate, seed + (unsigned) (t * 10.0), 0.05), samplesFor (t, rate),
                                   samplesFor (t + 0.14, rate), rate));
        return made[{ rate, seconds, esses, seed }] = x;
    }

    /** Main channels first, then the key's, as processBlock receives them. */
    juce::AudioBuffer<float> buffer (const std::vector<Samples>& channels)
    {
        juce::AudioBuffer<float> b ((int) channels.size(), (int) channels[0].size());
        for (size_t c = 0; c < channels.size(); ++c)
            b.copyFrom ((int) c, 0, channels[c].data(), (int) channels[c].size());
        return b;
    }

    /** A voice with esses on the main bus (the right channel quieter, its esses its own) and `keyChannels` of the given key. */
    juce::AudioBuffer<float> voiceInput (int mainChannels, double rate, double seconds, const std::vector<Samples>& key = {})
    {
        std::vector<Samples> channels { voice (rate, seconds, true, 3) };
        if (mainChannels == 2)
            channels.push_back (scaled (voice (rate, seconds, true, 4), 0.8));
        channels.insert (channels.end(), key.begin(), key.end());
        return buffer (channels);
    }

    void configure (HaliteProcessor& p, Shape shape, bool harsh)
    {
        setParam (p, params::shape, (float) shape);
        setParam (p, params::harshOn, harsh ? 1.0f : 0.0f);
        setParam (p, params::threshold, -24.0f);
        setParam (p, params::harshThreshold, -20.0f);
    }

    /** Whether channel c of out differs from in delayed by `delay` anywhere in [from, to). */
    bool reducedIn (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int c, int delay, int from, int to)
    {
        for (int i = std::max (from, delay); i < to; ++i)
        {
            const auto expected = in.getSample (c, i - delay), got = out.getSample (c, i);
            if (std::memcmp (&expected, &got, sizeof (float)) != 0)
                return true;
        }
        return false;
    }

    bool finite (const juce::AudioBuffer<float>& b, int channels)
    {
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i)))
                    return false;
        return true;
    }

    /** The lag of the largest correlation between out and in over [0, maxLag], from `from` on. */
    int measuredLag (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int maxLag, int from)
    {
        auto best = -1.0e300;
        auto lag = -1;
        for (int l = 0; l <= maxLag; ++l)
        {
            auto corr = 0.0;
            for (int i = from + l; i < out.getNumSamples(); ++i)
                corr += (double) out.getSample (0, i) * in.getSample (0, i - l);
            if (corr > best)
            {
                best = corr;
                lag = l;
            }
        }
        return lag;
    }
}

TEST_CASE ("Family: every channel layout reduces, and the key is heard only when it is asked for", "[processor][family][buses]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    constexpr auto rate = 48000.0;
    constexpr auto seconds = 1.2;
    const auto firstEss = samplesFor (0.4, rate), n = samplesFor (seconds, rate);

    for (const auto shape : shapes)
        for (const auto& main : { Set::mono(), Set::stereo() })
            for (const auto& key : { Set::disabled(), Set::mono(), Set::stereo() })
            {
                INFO (nameOf (shape) << ", " << main.getDescription() << ", key " << key.getDescription());
                const auto channels = main.size();

                const auto render = [&] (const juce::AudioBuffer<float>& in, KeySource source)
                {
                    HaliteProcessor p;
                    REQUIRE (p.setBusesLayout (layout (main, main, key)));
                    configure (p, shape, false);
                    setParam (p, params::key, (float) source);
                    p.prepareToPlay (rate, 512);
                    return std::pair { run (p, in, { 512 }), p.getLatencySamples() };
                };

                // Internal: the voice is reduced in its esses and untouched before them; the key's
                // own signal (white noise, every hop an ess) changes nothing.
                std::vector<Samples> noiseKey;
                for (int c = 0; c < key.size(); ++c)
                    noiseKey.push_back (bandNoise (100.0, 20000.0, n, rate, 40u + (unsigned) c, 0.3));
                const auto in = voiceInput (channels, rate, seconds, noiseKey);
                const auto [out, latency] = render (in, KeySource::internal);
                CHECK (finite (out, channels));
                for (int c = 0; c < channels; ++c)
                {
                    CHECK_FALSE (reducedIn (out, in, c, latency, 0, firstEss - samplesFor (0.02, rate)));
                    CHECK (reducedIn (out, in, c, latency, firstEss + latency, firstEss + latency + samplesFor (0.14, rate)));
                }
                if (key.size() > 0)
                {
                    const auto [unkeyed, unused] = render (voiceInput (channels, rate, seconds, std::vector<Samples> ((size_t) key.size(), Samples ((size_t) n, 0.0f))),
                                                           KeySource::internal);
                    CHECK (identical (out, unkeyed, channels));
                }

                // External: an esses-free main bus is cut where the key has its esses; with no key
                // bus it falls back to the main bus and has nothing to remove.
                std::vector<Samples> mainVowel { voice (rate, seconds, false, 3) };
                if (channels == 2)
                    mainVowel.push_back (scaled (voice (rate, seconds, false, 4), 0.8));
                auto withKey = mainVowel;
                for (int c = 0; c < key.size(); ++c)
                    withKey.push_back (voice (rate, seconds, true, 5u + (unsigned) c));
                const auto keyed = buffer (withKey);
                const auto [externalOut, externalLatency] = render (keyed, KeySource::external);
                CHECK (finite (externalOut, channels));
                for (int c = 0; c < channels; ++c)
                {
                    const auto during = reducedIn (externalOut, keyed, c, externalLatency, firstEss + externalLatency,
                                                   firstEss + externalLatency + samplesFor (0.14, rate));
                    CHECK (during == (key.size() > 0));
                    if (key.size() == 0)
                        CHECK_FALSE (reducedIn (externalOut, keyed, c, externalLatency, 0, n));
                }
            }
}

TEST_CASE ("Family: a stereo bus carrying one signal twice gives the mono bus's output on both channels, bit for bit", "[processor][family][buses]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    constexpr auto rate = 48000.0;

    for (const auto shape : shapes)
        for (const auto stereo : { StereoMode::linked, StereoMode::unlinked })
        {
            INFO (nameOf (shape) << ", stereo mode " << (int) stereo);
            const auto x = voice (rate, 1.5, true, 3);

            const auto render = [&] (const Set& main)
            {
                HaliteProcessor p;
                REQUIRE (p.setBusesLayout (layout (main, main)));
                configure (p, shape, true);
                setParam (p, params::stereo, (float) stereo);
                p.prepareToPlay (rate, 512);
                return run (p, buffer (std::vector<Samples> ((size_t) main.size(), x)), { 512 });
            };

            const auto mono = render (Set::mono());
            const auto both = render (Set::stereo());
            for (int c = 0; c < 2; ++c)
                CHECK (std::memcmp (both.getReadPointer (c), mono.getReadPointer (0), sizeof (float) * (size_t) mono.getNumSamples()) == 0);
        }
}

TEST_CASE ("Family: block sizes 1 to 4096 and variable give the same output, bit for bit", "[processor][family][blocks]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    constexpr auto rate = 44100.0;

    for (const auto shape : shapes)
        for (const auto latency : { LatencyMode::normal, LatencyMode::live })
        {
            const auto render = [&] (const std::vector<int>& sizes)
            {
                HaliteProcessor p;
                REQUIRE (p.setBusesLayout (layout (Set::stereo(), Set::stereo())));
                configure (p, shape, true);
                setParam (p, params::stereo, (float) StereoMode::unlinked);
                p.setLatencyMode (latency);
                p.prepareToPlay (rate, 4096);
                return run (p, voiceInput (2, rate, 1.3), sizes);
            };

            const auto reference = render ({ 512 });
            for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 2 }, { 3 }, { 64 }, { 333 }, { 4096 }, { 1, 4096, 17, 512, 2, 999 } })
            {
                INFO (nameOf (shape) << ", " << (latency == LatencyMode::live ? "Live" : "Normal") << ", first block " << sizes.front()
                                     << " of " << sizes.size());
                CHECK (identical (render (sizes), reference));
            }
        }
}

TEST_CASE ("Family: a restored state sounds the same, bit for bit", "[processor][family][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    constexpr auto rate = 48000.0;

    for (const auto latency : { LatencyMode::normal, LatencyMode::live })
        for (const auto shape : shapes)
        {
            INFO (nameOf (shape) << ", " << (latency == LatencyMode::live ? "Live" : "Normal"));
            HaliteProcessor a;
            configure (a, shape, true);
            setParam (a, params::range, 12.0f);
            setParam (a, params::detectLow, 5200.0f);
            setParam (a, params::detectHigh, 14500.0f);
            setParam (a, params::focus, 55.0f);
            setParam (a, params::stereo, (float) StereoMode::unlinked);
            setParam (a, params::speedAuto, 0.0f);
            setParam (a, params::release, 65.0f);
            setParam (a, params::lookahead, 8.0f);
            setParam (a, params::harshFreq, 2800.0f);
            setParam (a, params::harshWidth, 1.4f);
            setParam (a, params::harshRange, 6.0f);
            setParam (a, params::output, -1.25f);
            a.setLatencyMode (latency);

            HaliteProcessor b;
            restore (b, stateOf (a));
            REQUIRE (b.getLatencyMode() == latency);

            a.prepareToPlay (rate, 512);
            b.prepareToPlay (rate, 512);
            CHECK (b.getLatencySamples() == a.getLatencySamples());
            const auto in = voiceInput (2, rate, 1.5);
            const auto original = run (a, in, { 512 });
            CHECK_FALSE (delayedIdentical (original, in, 2, a.getLatencySamples()));
            CHECK (identical (run (b, in, { 512 }), original));
        }
}

TEST_CASE ("Family: offline renders are bit-identical, from a new instance, a new prepare or a reset", "[processor][family][render]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto rate : { 44100.0, 96000.0 })
        for (const auto latency : { LatencyMode::normal, LatencyMode::live })
            for (const auto shape : shapes)
            {
                INFO (nameOf (shape) << ", " << (latency == LatencyMode::live ? "Live" : "Normal") << ", " << rate << " Hz");
                const auto in = voiceInput (2, rate, 2.5);
                const auto make = [&] (HaliteProcessor& p)
                {
                    configure (p, shape, true);
                    p.setLatencyMode (latency);
                    p.prepareToPlay (rate, 512);
                };

                HaliteProcessor first;
                make (first);
                const auto reference = run (first, in, { 512 });
                CHECK_FALSE (delayedIdentical (reference, in, 2, first.getLatencySamples()));

                HaliteProcessor second;
                make (second);
                CHECK (identical (run (second, in, { 512 }), reference));

                first.prepareToPlay (rate, 512);
                CHECK (identical (run (first, in, { 512 }), reference));

                first.reset();
                CHECK (identical (run (first, in, { 512 }), reference));
            }
}

TEST_CASE ("Family: latency reported equals measured, every shape and rate, Normal and Live", "[processor][family][latency]")
{
    // White noise is sibilant from start to end, so the reduction acts while the delay is measured.
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (const auto latency : { LatencyMode::normal, LatencyMode::live })
            for (const auto shape : shapes)
            {
                INFO (nameOf (shape) << ", " << (latency == LatencyMode::live ? "Live" : "Normal") << ", " << rate << " Hz");
                HaliteProcessor p;
                REQUIRE (p.setBusesLayout (layout (Set::mono(), Set::mono())));
                configure (p, shape, false);
                p.setLatencyMode (latency);
                p.prepareToPlay (rate, 512);

                std::mt19937 rng (5u);
                std::normal_distribution<float> gauss (0.0f, 0.1f);
                Samples x ((size_t) samplesFor (0.15, rate));
                for (auto& v : x)
                    v = gauss (rng);
                const auto in = buffer ({ x });
                const auto out = run (p, in, { 512 });
                CHECK_FALSE (delayedIdentical (out, in, 1, p.getLatencySamples()));
                CHECK (measuredLag (out, in, 2 * dsp::HaliteChain::frameSize (rate), samplesFor (0.05, rate)) == p.getLatencySamples());
            }
}

TEST_CASE ("Family: every parameter reaches the sound where its mode is on", "[processor][family][parameters]")
{
    // The other half of "with both Ranges at 0 no other parameter changes the sound": a parameter the
    // processor never passes on would survive every round trip and render test.
    const juce::ScopedJuceInitialiser_GUI juce;
    constexpr auto rate = 48000.0;

    struct Case
    {
        const char* id;
        float value;
        std::vector<std::pair<const char*, float>> mode;     // set on both renders
    };
    const std::vector<Case> cases {
        { params::threshold, -10.0f, {} },
        { params::thresholdAbs, -50.0f, { { params::detection, (float) Detection::allround } } },
        { params::range, 3.0f, {} },
        { params::detectLow, 6000.0f, {} },
        { params::detectHigh, 9000.0f, {} },
        { params::shape, (float) Shape::spectral, {} },
        { params::detection, (float) Detection::allround, {} },
        { params::focus, 20.0f, {} },
        { params::speedAuto, 0.0f, {} },
        { params::release, 150.0f, { { params::speedAuto, 0.0f } } },
        { params::lookahead, 0.0f, {} },
        { params::stereo, (float) StereoMode::unlinked, {} },
        { params::key, (float) KeySource::external, {} },
        { params::keyHpf, 1500.0f, {} },
        { params::output, -3.0f, {} },
        { params::bypass, 1.0f, {} },
        { params::listen, (float) Listen::removed, {} },
        { params::harshOn, 1.0f, {} },
        { params::harshThreshold, -14.0f, { { params::harshOn, 1.0f }, { params::harshRange, 12.0f } } },
        { params::harshThresholdAbs, -50.0f, { { params::harshOn, 1.0f }, { params::detection, (float) Detection::allround } } },
        { params::harshRange, 10.0f, { { params::harshOn, 1.0f } } },
        { params::harshFreq, 2000.0f, { { params::harshOn, 1.0f } } },
        { params::harshWidth, 0.5f, { { params::harshOn, 1.0f } } },
        { params::harshSpeedAuto, 0.0f, { { params::harshOn, 1.0f }, { params::harshRange, 12.0f } } },
        { params::harshRelease, 250.0f, { { params::harshOn, 1.0f }, { params::harshRange, 12.0f }, { params::harshSpeedAuto, 0.0f } } },
    };

    // The key carries a voice of its own, with esses elsewhere; 0.3 s of silence at the end, so every reduction lets go.
    auto in = voiceInput (2, rate, 1.5, { voice (rate, 1.5, true, 7), scaled (voice (rate, 1.5, true, 8), 0.9) });
    for (int c = 0; c < in.getNumChannels(); ++c)
        in.clear (c, samplesFor (1.2, rate), in.getNumSamples() - samplesFor (1.2, rate));
    const auto render = [&] (const std::vector<std::pair<const char*, float>>& values)
    {
        HaliteProcessor p;
        REQUIRE (p.setBusesLayout (layout (Set::stereo(), Set::stereo(), Set::stereo())));
        configure (p, Shape::split, false);
        for (const auto& [id, value] : values)
            setParam (p, id, value);
        p.prepareToPlay (rate, 512);
        return run (p, in, { 512 });
    };

    std::set<juce::String> covered;
    for (const auto& c : cases)
    {
        INFO (c.id << " → " << c.value);
        covered.insert (c.id);
        auto changed = c.mode;
        changed.emplace_back (c.id, c.value);
        CHECK_FALSE (identical (render (changed), render (c.mode), 2));
    }

    HaliteProcessor p;
    for (auto* param : p.getParameters())
    {
        INFO (idOf (param));
        CHECK (covered.contains (idOf (param)));
    }
}
