#include "support/ProcessorHelpers.h"

#include "common/plugin/SessionState.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <numbers>

using namespace basalt;
using namespace basalt::testing;
using Catch::Matchers::WithinAbs;
using Set = juce::AudioChannelSet;

namespace
{
    // The identity conditions of PLAN.md's Identity gate (Character off, Mix 100, Gain 0, Output 0,
    // Ratio 1), so these tests keep their meaning once the dynamics run (M1).
    void makeIdle (BasaltProcessor& p)
    {
        setParam (p, params::ratio, 1.0f);
    }

    /** Blocks of 1 … 4096 samples in an order no host would pick. */
    std::vector<int> variableBlocks()
    {
        std::mt19937 rng (4096u);
        std::uniform_int_distribution<int> size (1, 4096);
        std::vector<int> sizes { 1, 4096, 2, 4095 };
        for (int i = 0; i < 60; ++i)
            sizes.push_back (size (rng));
        return sizes;
    }
}

TEST_CASE ("Processor: identity and metadata", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;

    CHECK (p.getName() == "Basalt");
    CHECK_FALSE (p.acceptsMidi());
    CHECK_FALSE (p.producesMidi());
    CHECK_FALSE (p.isMidiEffect());
    CHECK (juce::exactlyEqual (p.getTailLengthSeconds(), 0.0));
    CHECK (p.hasEditor());
    CHECK (p.getBypassParameter() == p.getState().getParameter (params::bypass));
}

TEST_CASE ("Processor: buses are a main pair and an optional side-chain key", "[processor][buses]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;

    REQUIRE (p.getBusCount (true) == 2);
    REQUIRE (p.getBusCount (false) == 1);
    CHECK (p.getBus (true, 0)->isMain());
    CHECK (p.getBus (true, 0)->getName() == "Input");
    CHECK (p.getBus (false, 0)->getName() == "Output");
    CHECK (p.getBus (true, 0)->getDefaultLayout() == Set::stereo());
    CHECK (p.getBus (false, 0)->getDefaultLayout() == Set::stereo());
    CHECK (p.getBus (true, 1)->getName() == "Side Chain");
    CHECK (p.getBus (true, 1)->getDefaultLayout() == Set::stereo());
    CHECK_FALSE (p.getBus (true, 1)->isEnabledByDefault());
    CHECK_FALSE (p.getBus (true, 1)->isEnabled());
    CHECK (p.getMainBusNumInputChannels() == 2);
    CHECK (p.getTotalNumInputChannels() == 2);
}

TEST_CASE ("Processor: mono -> mono, stereo -> stereo, key off, mono or stereo", "[processor][buses]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;

    for (const auto& main : { Set::mono(), Set::stereo() })
        for (const auto& key : { Set::disabled(), Set::mono(), Set::stereo() })
        {
            INFO (main.getDescription() << ", key " << key.getDescription());
            CHECK (p.checkBusesLayoutSupported (layout (main, main, key)));
        }

    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::create5point1(), Set::create5point1())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::createLCR(), Set::createLCR())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo(), Set::create5point1())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo(), Set::createLCR())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::disabled(), Set::disabled(), Set::stereo())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::disabled(), Set::stereo())));

    // The host can switch the key on and off, and the processor follows.
    REQUIRE (p.setBusesLayout (layout (Set::stereo(), Set::stereo(), Set::mono())));
    CHECK (p.getTotalNumInputChannels() == 3);
    CHECK (p.getChannelCountOfBus (true, 1) == 1);
    REQUIRE (p.setBusesLayout (layout (Set::mono(), Set::mono(), Set::stereo())));
    CHECK (p.getTotalNumInputChannels() == 3);
    CHECK (p.getTotalNumOutputChannels() == 1);
    REQUIRE (p.setBusesLayout (layout (Set::stereo(), Set::stereo())));
    CHECK (p.getTotalNumInputChannels() == 2);
}

TEST_CASE ("Processor: at Output 0 dB the output is the input, bit for bit", "[processor][identity]")
{
    // Every layout, rate and block size from 1 to 4096, fixed and variable. The key carries its
    // own, different signal; it must never reach the output.
    const juce::ScopedJuceInitialiser_GUI juce;
    const auto variable = variableBlocks();

    for (const auto& main : { Set::mono(), Set::stereo() })
        for (const auto& key : { Set::disabled(), Set::mono(), Set::stereo() })
            for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
                for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 2 }, { 3 }, { 64 }, { 333 }, { 4096 },
                                                                         { 1, 4096, 17, 512, 2, 999 }, variable })
                {
                    INFO (main.getDescription() << ", key " << key.getDescription() << ", " << rate
                                                << " Hz, first block " << sizes.front() << " of " << sizes.size());
                    BasaltProcessor p;
                    REQUIRE (p.setBusesLayout (layout (main, main, key)));
                    makeIdle (p);
                    p.prepareToPlay (rate, 4096);

                    const auto channels = main.size();
                    const auto in = noise (channels + key.size(), 20000, 7u);
                    const auto out = run (p, in, sizes);
                    CHECK (delayedIdentical (out, in, channels, p.getLatencySamples()));
                }
}

TEST_CASE ("Processor: idle is the identity in every model, VCA detector and latency", "[processor][identity]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    const auto variable = variableBlocks();

    for (int model = 0; model < numModels; ++model)
        for (int detector = 0; detector < numVcaDetectors; ++detector)
            for (const auto mode : { LatencyMode::normal, LatencyMode::live })
            {
                INFO (params::modelNames()[model] << ", " << params::vcaDetectorNames()[detector]
                                                  << (mode == LatencyMode::live ? ", Live" : ", Normal"));
                BasaltProcessor p;
                makeIdle (p);
                setParam (p, params::model, (float) model);
                setParam (p, params::vcaDetector, (float) detector);
                p.setLatencyMode (mode);
                p.prepareToPlay (48000.0, 4096);

                const auto in = noise (2, 20000, 31u);
                CHECK (delayedIdentical (run (p, in, variable), in, 2, p.getLatencySamples()));
            }
}

TEST_CASE ("Processor: idle leaves denormals and extreme values untouched", "[processor][identity]")
{
    // A multiply by 1 under flush-to-zero would zero a denormal input: idle must not touch the samples.
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto mode : { LatencyMode::normal, LatencyMode::live })
    {
        BasaltProcessor p;
        makeIdle (p);
        p.setLatencyMode (mode);
        p.prepareToPlay (48000.0, 256);

        juce::AudioBuffer<float> in (2, 2048);
        const float specials[] { std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min(),
                                 1.0e-40f, -0.0f, 0.0f, std::numeric_limits<float>::max(), 8.0f, -8.0f };
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (ch, i, specials[(size_t) (i + ch) % std::size (specials)]);

        CHECK (delayedIdentical (run (p, in, { 64 }), in, 2, p.getLatencySamples()));
    }
}

TEST_CASE ("Processor: Output gain is applied, Bypass returns the input", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    constexpr double rate = 48000.0;
    makeIdle (p);
    p.prepareToPlay (rate, 512);
    const auto latency = p.getLatencySamples();

    // The first block after prepare takes its settings at once; later changes glide.
    setParam (p, params::output, -6.0f);
    const auto in = noise (2, 2048, 3u);
    const auto out = run (p, in, { 512 });
    const auto g = dsp::BasaltChain::gainFor (plainValue (p, params::output));
    REQUIRE_THAT (g, WithinAbs (0.501187, 1.0e-6));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = latency; i < in.getNumSamples(); ++i)
            REQUIRE (juce::exactlyEqual (out.getSample (ch, i), in.getSample (ch, i - latency) * g));

    setParam (p, params::bypass, 1.0f);
    run (p, noise (2, (int) (rate * 0.05), 4u), { 256 });
    const auto in2 = noise (2, 2048, 5u);
    const auto out2 = run (p, in2, { 256 });
    CHECK (alignedFrom (out2, in2, 2, latency));

    // And back: Output 0 dB is the identity again once the glides end.
    setParam (p, params::bypass, 0.0f);
    setParam (p, params::output, 0.0f);
    run (p, noise (2, (int) (rate * 0.2), 6u), { 256 });
    const auto in3 = noise (2, 2048, 8u);
    CHECK (alignedFrom (run (p, in3, { 256 }), in3, 2, latency));
}

TEST_CASE ("Processor: Bypass and Output glide without steps", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    constexpr double rate = 48000.0;
    makeIdle (p);
    p.prepareToPlay (rate, 512);

    juce::AudioBuffer<float> dc (2, (int) (rate * 0.3));
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, dc.getNumSamples());

    setParam (p, params::output, -12.0f);
    run (p, dc, { 512 });

    const auto largestStep = [] (const juce::AudioBuffer<float>& out)
    {
        auto largest = 0.0f;
        for (int i = 1; i < out.getNumSamples(); ++i)
            largest = std::max (largest, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        return largest;
    };

    SECTION ("Bypass on: a 20 ms raised-cosine crossfade")
    {
        setParam (p, params::bypass, 1.0f);
        const auto out = run (p, dc, { 512 });
        const auto g = dsp::BasaltChain::gainFor (-12.0f);
        // The steepest step of a raised cosine over N samples is π / 2 times a linear fade's.
        const auto linear = 0.5f * (1.0f - g) / (float) (rate * dsp::BasaltChain::bypassSeconds);
        CHECK (largestStep (out) <= linear * 1.5708f * 1.01f);
        CHECK (largestStep (out) >= linear);
        CHECK (juce::exactlyEqual (out.getSample (0, out.getNumSamples() - 1), 0.5f));
    }

    SECTION ("Output to +12 dB: two one-poles, landing on the target")
    {
        setParam (p, params::output, 12.0f);
        const auto out = run (p, dc, { 512 });
        const auto from = dsp::BasaltChain::gainFor (-12.0f), to = dsp::BasaltChain::gainFor (plainValue (p, params::output));
        // A critically damped pair rises fastest at t = τ, by Δ / (e τ) per second.
        const auto tau = dsp::BasaltChain::outputMs * 0.001 * rate;
        CHECK (largestStep (out) <= 0.5f * (to - from) / (float) (std::exp (1.0) * tau) * 1.01f);
        for (int i = 1; i < out.getNumSamples(); ++i)
            REQUIRE (out.getSample (0, i) >= out.getSample (0, i - 1));
        CHECK (juce::exactlyEqual (out.getSample (0, out.getNumSamples() - 1), 0.5f * to));
    }

    SECTION ("Bypass during an Output glide: both move, and the end is the input")
    {
        setParam (p, params::output, 6.0f);
        setParam (p, params::bypass, 1.0f);
        const auto out = run (p, dc, { 512 });
        CHECK (largestStep (out) < 0.01f);
        CHECK (juce::exactlyEqual (out.getSample (1, out.getNumSamples() - 1), 0.5f));
    }
}

TEST_CASE ("Processor: Normal reports the 10 ms look-ahead budget, Live none, at any rate", "[processor][latency]")
{
    // PLAN.md, Latency: Normal is 10 ms of look-ahead plus Character's delay (0 until M3); Live 0.
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    CHECK (p.getLatencyMode() == LatencyMode::normal);
    CHECK (p.getLatencySamples() == 480);

    const std::pair<double, int> normal[] { { 44100.0, 441 }, { 48000.0, 480 }, { 88200.0, 882 },
                                            { 96000.0, 960 }, { 176400.0, 1764 }, { 192000.0, 1920 } };

    for (const auto mode : { LatencyMode::normal, LatencyMode::live, LatencyMode::normal })
        for (const auto& [rate, samples] : normal)
        {
            INFO (rate << " Hz, " << (mode == LatencyMode::live ? "Live" : "Normal"));
            p.setLatencyMode (mode);
            p.prepareToPlay (rate, 512);
            CHECK (p.getLatencyMode() == mode);
            CHECK (p.getLatencySamples() == dsp::BasaltChain::latencySamples (mode, rate));
            CHECK (p.getLatencySamples() == (mode == LatencyMode::live ? 0 : samples));
            CHECK (dsp::BasaltChain::characterDelaySamples (mode, rate) == 0);
        }
}

TEST_CASE ("Processor: switching the latency mode reports at once, without a new prepare", "[processor][latency]")
{
    // Each switch that changes the figure notifies the host, and no other does.
    struct LatencyCounter final : juce::AudioProcessorListener
    {
        int changes = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        {
            changes += details.latencyChanged ? 1 : 0;
        }
    };

    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        INFO (rate << " Hz");
        BasaltProcessor p;
        p.prepareToPlay (rate, 512);
        LatencyCounter counter;
        p.addListener (&counter);

        auto expectedChanges = 0;
        auto reported = p.getLatencySamples();

        for (const auto mode : { LatencyMode::live, LatencyMode::normal, LatencyMode::live, LatencyMode::live })
        {
            p.setLatencyMode (mode);
            const auto expected = dsp::BasaltChain::latencySamples (mode, rate);
            expectedChanges += expected != reported ? 1 : 0;
            reported = expected;

            CHECK (p.getLatencyMode() == mode);
            CHECK (p.getLatencySamples() == expected);
            CHECK (counter.changes == expectedChanges);
        }

        // Restoring a session switches the mode the same way.
        BasaltProcessor normal;
        restore (p, stateOf (normal));
        expectedChanges += dsp::BasaltChain::latencySamples (LatencyMode::normal, rate) != reported ? 1 : 0;
        CHECK (p.getLatencyMode() == LatencyMode::normal);
        CHECK (p.getLatencySamples() == dsp::BasaltChain::latencySamples (LatencyMode::normal, rate));
        CHECK (counter.changes == expectedChanges);

        p.removeListener (&counter);
    }
}

TEST_CASE ("Processor: a session restored before prepareToPlay keeps the latency a host may already ask for", "[processor][latency]")
{
    // A fresh instance reports Normal at the likeliest rate; restoring a Normal or Live session
    // before any prepare reports at that rate too, never 0 for Normal.
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor normal, live;
    live.setLatencyMode (LatencyMode::live);
    const auto normalState = stateOf (normal), liveState = stateOf (live);

    BasaltProcessor p;
    CHECK (p.getLatencySamples() == 480);
    restore (p, liveState);
    CHECK (p.getLatencySamples() == 0);
    restore (p, normalState);
    CHECK (p.getLatencySamples() == 480);

    // Once prepared, the prepared rate decides.
    p.prepareToPlay (96000.0, 512);
    restore (p, liveState);
    restore (p, normalState);
    CHECK (p.getLatencySamples() == 960);
}

TEST_CASE ("Processor: a NaN or an infinity reaches the detectors as silence", "[processor][realtime]")
{
    // dynamics/NOTES.md, Key path: the key is taken sane, so no detector, crest or key EQ state
    // ever holds a non-finite value. The audio path passes the bad sample on untouched; one
    // latency later the output is finite again and back on the render without it.
    const juce::ScopedJuceInitialiser_GUI juce;
    constexpr double rate = 48000.0;
    const auto n = (int) (2.0 * rate), bad = 1000;

    const auto sine = [&]
    {
        juce::AudioBuffer<float> b (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, (float) (0.5 * std::sin (2.0 * std::numbers::pi * 440.0 * i / rate)));
        return b;
    }();

    struct Setting
    {
        const char* name;
        void (*apply) (BasaltProcessor&);
    };
    const Setting settings[] {
        { "Auto release on (the default)", [] (BasaltProcessor&) {} },
        { "Auto release off", [] (BasaltProcessor& p) { setParam (p, params::autoRelease, 0.0f); } },
        { "Ratio 1, Auto release off", [] (BasaltProcessor& p) { setParam (p, params::autoRelease, 0.0f); setParam (p, params::ratio, 1.0f); } },
        { "key EQ on, Attack 0.5 ms", [] (BasaltProcessor& p)
          {
              setParam (p, params::keyHpf, 200.0f);
              setParam (p, params::keyLpf, 5000.0f);
              setParam (p, params::keyBellGain, 9.0f);
              setParam (p, params::attack, 0.5f);
          } },
    };

    for (const auto value : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
        for (const auto& setting : settings)
            for (const auto key : { KeySource::internal, KeySource::external })
            {
                INFO (value << ", " << setting.name << (key == KeySource::external ? ", external key" : ", internal key"));
                const auto renderWith = [&] (bool damaged)
                {
                    BasaltProcessor p;
                    REQUIRE (p.setBusesLayout (layout (Set::stereo(), Set::stereo(), Set::stereo())));
                    setParam (p, params::threshold, -30.0f);
                    setParam (p, params::release, 50.0f);
                    setParam (p, params::key, (float) static_cast<int> (key));
                    setting.apply (p);
                    p.prepareToPlay (rate, 512);

                    juce::AudioBuffer<float> in (4, n);
                    for (int ch = 0; ch < 4; ++ch)
                        in.copyFrom (ch, 0, sine, ch % 2, 0, n);
                    // The internal key is the main input; an external one alone carries the bad sample.
                    if (damaged)
                        for (const auto ch : key == KeySource::external ? std::array { 2, 3 } : std::array { 0, 1 })
                            in.setSample (ch, bad, value);
                    return std::pair { run (p, in, { 512 }), p.getLatencySamples() };
                };

                const auto [out, latency] = renderWith (true);
                const auto [clean, cleanLatency] = renderWith (false);
                REQUIRE (latency == cleanLatency);

                auto nonFinite = 0, lastNonFinite = -1;
                auto worstLateDiff = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        const auto v = out.getSample (ch, i);
                        if (! std::isfinite (v))
                        {
                            ++nonFinite;
                            lastNonFinite = std::max (lastNonFinite, i);
                        }
                        else if (i >= n - (int) rate / 2)
                        {
                            worstLateDiff = std::max (worstLateDiff, std::abs (v - clean.getSample (ch, i)));
                        }
                    }

                // The bad sample itself, on the audio path: once per channel, where the latency puts it.
                CHECK (nonFinite == (key == KeySource::external ? 0 : 2));
                CHECK (lastNonFinite <= bad + latency);
                CHECK (worstLateDiff < 1.0e-5f);
            }
}

TEST_CASE ("Processor: state round-trips, including the extremes", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    auto roundTrip = [] (auto&& setUp)
    {
        BasaltProcessor a;
        setUp (a);
        const auto saved = stateOf (a);

        BasaltProcessor b;
        restore (b, saved);

        // Compared as plain values: a switch a host left at a raw 0.37 is saved as what it means, off.
        for (auto* param : a.getParameters())
        {
            auto* source = dynamic_cast<juce::RangedAudioParameter*> (param);
            auto* restored = b.getState().getParameter (idOf (param));
            INFO (idOf (param));
            CHECK (juce::exactlyEqual (restored->convertFrom0to1 (restored->getValue()),
                                       source->convertFrom0to1 (source->getValue())));
        }

        CHECK (b.getLatencyMode() == a.getLatencyMode());
        CHECK (treeOf (saved)[myzel::plugin::sessionState::versionId].equals (BasaltProcessor::stateVersion));
    };

    SECTION ("defaults")
    {
        roundTrip ([] (BasaltProcessor&) {});
    }

    SECTION ("everything at its minimum")
    {
        roundTrip ([] (BasaltProcessor& p) { for (auto* param : p.getParameters()) param->setValueNotifyingHost (0.0f); });
    }

    SECTION ("everything at its maximum, Live latency")
    {
        roundTrip ([] (BasaltProcessor& p)
        {
            for (auto* param : p.getParameters())
                param->setValueNotifyingHost (1.0f);
            p.setLatencyMode (LatencyMode::live);
        });
    }

    SECTION ("random positions")
    {
        roundTrip ([] (BasaltProcessor& p)
        {
            std::mt19937 rng (2026u);
            std::uniform_real_distribution<float> dist (0.0f, 1.0f);
            for (auto* param : p.getParameters())
                param->setValueNotifyingHost (dist (rng));
        });
    }

    SECTION ("a typical setting, Ratio at infinity, a 1.1 model")
    {
        roundTrip ([] (BasaltProcessor& p)
        {
            setParam (p, params::model, (float) Model::pumping);
            setParam (p, params::vcaDetector, (float) VcaDetector::rms);
            setParam (p, params::threshold, -27.5f);
            setParam (p, params::ratio, params::ratioInfinity);
            setParam (p, params::attack, 0.35f);
            setParam (p, params::release, 1800.0f);
            setParam (p, params::character, (float) CharacterMode::iron);
            setParam (p, params::stereo, (float) StereoMode::midSide);
            setParam (p, params::keyLpf, 9000.0f);
            setParam (p, params::output, -1.25f);
            p.setLatencyMode (LatencyMode::live);
        });
    }
}

TEST_CASE ("Processor: the stored values are the plain ones, Ratio's infinity as 21", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    setParam (p, params::ratio, params::ratioInfinity);
    setParam (p, params::model, (float) Model::pwm);
    setParam (p, params::release, 2000.0f);

    const auto tree = treeOf (stateOf (p));
    const auto stored = [&tree] (const char* id) { return (float) tree.getChildWithProperty ("id", id)["value"]; };
    CHECK (juce::exactlyEqual (stored (params::ratio), 21.0f));
    CHECK (juce::exactlyEqual (stored (params::model), 7.0f));
    CHECK_THAT (stored (params::release), WithinAbs (2000.0, 1.0e-2));
    CHECK (juce::exactlyEqual (stored (params::threshold), -18.0f));
}

TEST_CASE ("Processor: a reloaded state saves the same bytes", "[processor][state]")
{
    // clap-validator's state-reproducibility-binary: hosts compare saved chunks to detect a change.
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto mode : { LatencyMode::normal, LatencyMode::live })
    {
        BasaltProcessor a;
        setParam (a, params::threshold, -30.5f);
        setParam (a, params::model, (float) Model::opto);
        a.setLatencyMode (mode);
        const auto saved = stateOf (a);
        const auto savedXml = treeOf (saved).toXmlString();

        BasaltProcessor b;
        restore (b, saved);
        const auto fromB = stateOf (b);
        INFO ("saved:    " << savedXml << "\nreloaded: " << treeOf (fromB).toXmlString());
        CHECK (fromB == saved);

        restore (a, saved);
        CHECK (stateOf (a) == saved);
    }
}

TEST_CASE ("Processor: the latency mode is a session setting", "[processor][state][latency]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    SECTION ("Live survives a session round trip, and Normal replaces Live")
    {
        BasaltProcessor a;
        a.setLatencyMode (LatencyMode::live);
        BasaltProcessor b;
        restore (b, stateOf (a));
        CHECK (b.getLatencyMode() == LatencyMode::live);

        BasaltProcessor normal;
        normal.setLatencyMode (LatencyMode::normal);
        restore (b, stateOf (normal));
        CHECK (b.getLatencyMode() == LatencyMode::normal);
    }

    SECTION ("a session without the setting, or with one this version does not know, loads as Normal")
    {
        BasaltProcessor source;
        source.setLatencyMode (LatencyMode::live);
        auto tree = treeOf (stateOf (source));
        REQUIRE ((int) tree[sessionKeys::latencyMode] == 1);

        for (const auto& value : { juce::var(), juce::var (7), juce::var (-1), juce::var ("fast") })
        {
            INFO (value.toString());
            auto edited = tree.createCopy();
            if (value.isVoid())
                edited.removeProperty (sessionKeys::latencyMode, nullptr);
            else
                edited.setProperty (sessionKeys::latencyMode, value, nullptr);

            BasaltProcessor p;
            p.setLatencyMode (LatencyMode::live);
            restore (p, save (edited));
            CHECK (p.getLatencyMode() == LatencyMode::normal);
        }
    }

    SECTION ("it is stored in the session tree, under its frozen key, not as a parameter")
    {
        CHECK (sessionKeys::latencyMode.toString() == "latencyMode");
        BasaltProcessor p;
        p.setLatencyMode (LatencyMode::live);
        const auto tree = treeOf (stateOf (p));
        CHECK ((int) tree[sessionKeys::latencyMode] == 1);
        CHECK_FALSE (tree.getChildWithProperty ("id", "latencyMode").isValid());
        CHECK_FALSE (params::soundParameterIds().contains ("latencyMode"));

        // Only the saved copy carries it: the live tree, which copyState() reads under the APVTS
        // lock, is never written outside that lock for it.
        CHECK_FALSE (p.getState().state.hasProperty (sessionKeys::latencyMode));
        restore (p, stateOf (p));
        CHECK_FALSE (p.getState().state.hasProperty (sessionKeys::latencyMode));
        CHECK (p.getLatencyMode() == LatencyMode::live);
    }
}

TEST_CASE ("Processor: restoring a state undoes raw host values on switches", "[processor][state]")
{
    // Hosts (and pluginval) may set a bool to any value, e.g. 0.76: the session's 1 must give 1.
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    setParam (p, params::autoGain, 1.0f);
    const auto saved = stateOf (p);

    for (const auto* id : { params::autoRelease, params::autoGain, params::charPost, params::driveLink, params::bypass })
    {
        auto* param = p.getState().getParameter (id);
        const auto value = param->getValue();
        param->setValueNotifyingHost (value > 0.5f ? 0.76f : 0.24f);
        restore (p, saved);
        INFO (id);
        CHECK (juce::exactlyEqual (param->getValue(), value));
    }
}

TEST_CASE ("Processor: sessions from other versions load", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    BasaltProcessor source;
    setParam (source, params::range, 15.0f);
    const auto tree = treeOf (stateOf (source));

    SECTION ("a newer session's parameters are ignored")
    {
        auto newer = tree.createCopy();
        newer.appendChild (juce::ValueTree ("PARAM", { { "id", "sideHold" }, { "value", -20.0f } }), nullptr);

        BasaltProcessor p;
        restore (p, save (newer));
        CHECK_THAT (plainValue (p, params::range), WithinAbs (15.0, 1.0e-4));
    }

    SECTION ("a parameter missing from an older session takes its default")
    {
        auto older = tree.createCopy();
        older.removeChild (older.getChildWithProperty ("id", params::output), nullptr);

        BasaltProcessor p;
        setParam (p, params::output, 12.0f);
        restore (p, save (older));
        CHECK (juce::exactlyEqual (plainValue (p, params::output), 0.0f));
        CHECK_THAT (plainValue (p, params::range), WithinAbs (15.0, 1.0e-4));
    }

}

TEST_CASE ("Processor: foreign or broken state is ignored", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    setParam (p, params::range, 22.0f);
    p.setLatencyMode (LatencyMode::live);

    const auto unchanged = [&p]
    {
        CHECK_THAT (plainValue (p, params::range), WithinAbs (22.0, 1.0e-4));
        CHECK (p.getLatencyMode() == LatencyMode::live);
    };

    SECTION ("bytes that are not a state at all")
    {
        const char junk[] = "definitely not a plugin state";
        p.setStateInformation (junk, (int) sizeof (junk));
        unchanged();
        p.setStateInformation (nullptr, 0);
        unchanged();
    }

    SECTION ("another plugin's state, latency mode included")
    {
        juce::ValueTree foreign ("HaliteState");
        foreign.appendChild (juce::ValueTree ("PARAM", { { "id", params::range }, { "value", 3.0f } }), nullptr);
        foreign.setProperty (sessionKeys::latencyMode, 0, nullptr);
        restore (p, save (foreign));
        unchanged();
    }
}

TEST_CASE ("Processor: a value that is not finite in a session restores as the default", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto* bad : { "nan", "inf", "-inf" })
    {
        INFO (bad);
        BasaltProcessor source;
        auto tree = source.getState().copyState();
        for (auto child : tree)
            if (child.hasType ("PARAM"))
                child.setProperty ("value", bad, nullptr);

        BasaltProcessor p;
        for (auto* param : p.getParameters())
            param->setValueNotifyingHost (0.7f);
        restore (p, save (tree));

        for (auto* param : p.getParameters())
        {
            INFO (idOf (param));
            CHECK (juce::exactlyEqual (param->getValue(), param->getDefaultValue()));
        }
    }
}
