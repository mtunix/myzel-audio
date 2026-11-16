#include "support/ProcessorHelpers.h"

#include "common/plugin/SessionState.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <iterator>
#include <limits>

using namespace halite;
using namespace halite::testing;
using Catch::Matchers::WithinAbs;
using Set = juce::AudioChannelSet;

TEST_CASE ("Processor: identity and metadata", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;

    CHECK (p.getName() == "Halite");
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
    HaliteProcessor p;

    REQUIRE (p.getBusCount (true) == 2);
    REQUIRE (p.getBusCount (false) == 1);
    CHECK (p.getBus (true, 0)->isMain());
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
    HaliteProcessor p;

    for (const auto& main : { Set::mono(), Set::stereo() })
        for (const auto& key : { Set::disabled(), Set::mono(), Set::stereo() })
        {
            INFO (main.getDescription() << ", key " << key.getDescription());
            CHECK (p.checkBusesLayoutSupported (layout (main, main, key)));
        }

    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::create5point1(), Set::create5point1())));
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

TEST_CASE ("Processor: with nothing to remove the output is the input, delayed by the latency, bit for bit", "[processor][identity]")
{
    // White noise is all ess to the detector, so both Ranges go to 0: this checks the path itself
    // (every layout, rate and block size); src/dsp tests check idle signals at the defaults.
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto& main : { Set::mono(), Set::stereo() })
        for (const auto& key : { Set::disabled(), Set::mono(), Set::stereo() })
            for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
                for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 3 }, { 64 }, { 333 }, { 4096 },
                                                                         { 1, 4096, 17, 512, 2, 999 } })
                {
                    INFO (main.getDescription() << ", key " << key.getDescription() << ", " << rate
                                                << " Hz, first block " << sizes.front());
                    HaliteProcessor p;
                    REQUIRE (p.setBusesLayout (layout (main, main, key)));
                    setParam (p, params::range, 0.0f);
                    setParam (p, params::harshRange, 0.0f);
                    setParam (p, params::harshOn, 1.0f);
                    p.prepareToPlay (rate, 4096);
                    CHECK (p.getLatencySamples() == dsp::HaliteChain::frameSize (rate));

                    // The key carries its own, different signal; it must never reach the output.
                    const auto channels = main.size();
                    const auto in = noise (channels + key.size(), 9000, 7u);
                    const auto out = run (p, in, sizes);
                    CHECK (delayedIdentical (out, in, channels, p.getLatencySamples()));
                }
}

TEST_CASE ("Processor: idle leaves denormals and extreme values untouched", "[processor][identity]")
{
    // A multiply by 1 or a subtraction of 0 under flush-to-zero would zero a denormal input: idle
    // must not touch the samples. Range 0, since the pattern is loud and bright.
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto mode : { LatencyMode::normal, LatencyMode::live })
    {
        HaliteProcessor p;
        setParam (p, params::range, 0.0f);
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

TEST_CASE ("Processor: at its defaults the detector runs DetectorSettings{}", "[processor][parameters]")
{
    // tools/measure, the corpus tests and the probe measure DetectorSettings{} as "the defaults":
    // a parameter default that moves without it would leave the measured gates describing
    // another plugin.
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    const auto d = p.readSettings().detector;
    const dsp::DetectorSettings expected;

    const auto same = [] (float a, float b) { return std::abs (a - b) <= 1.0e-6f * std::max (1.0f, std::abs (b)); };
    CHECK (d.detection == expected.detection);
    CHECK (same (d.thresholdDb, expected.thresholdDb));
    CHECK (same (d.thresholdAbsDb, expected.thresholdAbsDb));
    CHECK (same (d.rangeDb, expected.rangeDb));
    CHECK (same (d.detectLowHz, expected.detectLowHz));
    CHECK (same (d.detectHighHz, expected.detectHighHz));
    CHECK (same (d.focus, expected.focus));
    CHECK (d.speedAuto == expected.speedAuto);
    CHECK (same (d.releaseMs, expected.releaseMs));
    CHECK (same (d.lookaheadMs, expected.lookaheadMs));
    CHECK (d.latency == expected.latency);
    CHECK (d.stereo == expected.stereo);
    CHECK (d.key == expected.key);
    CHECK (same (d.keyHpfHz, expected.keyHpfHz));
    CHECK (d.harshOn == expected.harshOn);
    CHECK (same (d.harshThresholdDb, expected.harshThresholdDb));
    CHECK (same (d.harshThresholdAbsDb, expected.harshThresholdAbsDb));
    CHECK (same (d.harshRangeDb, expected.harshRangeDb));
    CHECK (same (d.harshFreqHz, expected.harshFreqHz));
    CHECK (same (d.harshWidthOct, expected.harshWidthOct));
    CHECK (d.harshSpeedAuto == expected.harshSpeedAuto);
    CHECK (same (d.harshReleaseMs, expected.harshReleaseMs));
    CHECK (d.noiseCue == expected.noiseCue);
}

TEST_CASE ("Processor: NaN and Inf in the input leave the detector's meters finite", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    setParam (p, params::harshOn, 1.0f);
    p.prepareToPlay (48000.0, 256);

    auto in = noise (2, 4096, 11u);
    in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
    in.setSample (1, 900, std::numeric_limits<float>::infinity());
    in.setSample (0, 2000, -std::numeric_limits<float>::infinity());
    in.setSample (1, 3000, std::numeric_limits<float>::max());
    run (p, in, { 256 });
    run (p, noise (2, 4096, 12u), { 256 });

    const auto& m = p.getChain().meters();
    for (const auto v : { m.bandDb.load(), m.bodyDb.load(), m.sibilanceDb.load(), m.noise.load(), m.harshDb.load(), m.harshBodyDb.load(),
                          m.essReductionDb[0].load(), m.essReductionDb[1].load(), m.harshReductionDb[0].load(), m.harshReductionDb[1].load() })
        CHECK (std::isfinite (v));
}

TEST_CASE ("Processor: Output gain is applied, Bypass returns the aligned input", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    constexpr double rate = 48000.0;
    setParam (p, params::range, 0.0f);
    p.prepareToPlay (rate, 512);
    const auto latency = p.getLatencySamples();

    // The first block after prepare takes its settings at once; later changes glide.
    setParam (p, params::output, -6.0f);
    const auto in = noise (2, 2048, 3u);
    const auto out = run (p, in, { 512 });
    const auto g = juce::Decibels::decibelsToGain (-6.0f);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = latency; i < in.getNumSamples(); ++i)
            REQUIRE (juce::exactlyEqual (out.getSample (ch, i), in.getSample (ch, i - latency) * g));

    setParam (p, params::bypass, 1.0f);
    run (p, noise (2, (int) (rate * 0.05), 4u), { 256 });
    const auto in2 = noise (2, 2048, 5u);
    const auto out2 = run (p, in2, { 256 });
    for (int ch = 0; ch < 2; ++ch)
        for (int i = latency; i < in2.getNumSamples(); ++i)
            REQUIRE (juce::exactlyEqual (out2.getSample (ch, i), in2.getSample (ch, i - latency)));

    // And back: Output 0 dB is the identity again once the glides end.
    setParam (p, params::bypass, 0.0f);
    setParam (p, params::output, 0.0f);
    run (p, noise (2, (int) (rate * 0.2), 6u), { 256 });
    const auto in3 = noise (2, 2048, 8u);
    const auto out3 = run (p, in3, { 256 });
    for (int ch = 0; ch < 2; ++ch)
        for (int i = latency; i < in3.getNumSamples(); ++i)
            REQUIRE (juce::exactlyEqual (out3.getSample (ch, i), in3.getSample (ch, i - latency)));
}

TEST_CASE ("Processor: Bypass and Output glide without steps", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    constexpr double rate = 48000.0;
    p.prepareToPlay (rate, 512);

    // DC has nothing in the Detect band: nothing is removed, so only the glides move the output.
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
        const auto g = juce::Decibels::decibelsToGain (-12.0f);
        // The steepest step of a raised cosine over N samples is π / 2 times a linear fade's.
        const auto linear = 0.5f * (1.0f - g) / (float) (rate * dsp::HaliteChain::bypassSeconds);
        CHECK (largestStep (out) <= linear * 1.5708f * 1.01f);
        CHECK (largestStep (out) >= linear);
        CHECK (juce::exactlyEqual (out.getSample (0, out.getNumSamples() - 1), 0.5f));
    }

    SECTION ("Output to +12 dB: two one-poles, landing on the target")
    {
        setParam (p, params::output, 12.0f);
        const auto out = run (p, dc, { 512 });
        const auto from = juce::Decibels::decibelsToGain (-12.0f), to = juce::Decibels::decibelsToGain (12.0f);
        // A critically damped pair rises fastest at t = τ, by Δ / (e τ) per second.
        const auto tau = dsp::HaliteChain::outputMs * 0.001 * rate;
        CHECK (largestStep (out) <= 0.5f * (to - from) / (float) (std::exp (1.0) * tau) * 1.01f);
        for (int i = 1; i < out.getNumSamples(); ++i)
            REQUIRE (out.getSample (0, i) >= out.getSample (0, i - 1));
        CHECK (juce::exactlyEqual (out.getSample (0, out.getNumSamples() - 1), 0.5f * to));
    }
}

TEST_CASE ("Processor: with both Ranges at 0 no other parameter changes the sound", "[processor][identity]")
{
    // Every parameter at its maximum but those that are meant to change what is heard.
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    REQUIRE (p.setBusesLayout (layout (Set::stereo(), Set::stereo(), Set::stereo())));
    for (auto* param : p.getParameters())
        if (! juce::StringArray { params::output, params::bypass, params::listen, params::range, params::harshRange }.contains (idOf (param)))
            param->setValueNotifyingHost (1.0f);
    setParam (p, params::range, 0.0f);
    setParam (p, params::harshRange, 0.0f);
    p.prepareToPlay (48000.0, 512);

    const auto in = noise (4, 4096, 21u);
    CHECK (delayedIdentical (run (p, in, { 512 }), in, 2, p.getLatencySamples()));
}

TEST_CASE ("Processor: latency is one Spectral frame in Normal and 0 in Live", "[processor][latency]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    CHECK (p.getLatencyMode() == LatencyMode::normal);
    CHECK (p.getLatencySamples() == 512);    // before any prepare: the frame at 44.1 and 48 kHz

    for (const auto mode : { LatencyMode::normal, LatencyMode::live, LatencyMode::normal })
        for (const auto& [rate, frame] : std::vector<std::pair<double, int>> { { 44100.0, 512 }, { 48000.0, 512 }, { 88200.0, 1024 },
                                                                                { 96000.0, 1024 }, { 176400.0, 2048 }, { 192000.0, 2048 } })
        {
            p.setLatencyMode (mode);
            p.prepareToPlay (rate, 512);
            CHECK (p.getLatencyMode() == mode);
            CHECK (p.getLatencySamples() == dsp::HaliteChain::latencySamples (mode, rate));
            CHECK (p.getLatencySamples() == (mode == LatencyMode::live ? 0 : frame));
        }
}

TEST_CASE ("Processor: switching the latency mode reports at once, without a new prepare", "[processor][latency]")
{
    // Every switch between Normal and Live changes the latency, so each one notifies the host.
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
        HaliteProcessor p;
        p.prepareToPlay (rate, 512);
        LatencyCounter counter;
        p.addListener (&counter);

        auto expectedChanges = 0;
        auto reported = p.getLatencySamples();

        for (const auto mode : { LatencyMode::live, LatencyMode::normal, LatencyMode::live, LatencyMode::live })
        {
            p.setLatencyMode (mode);
            const auto expected = dsp::HaliteChain::latencySamples (mode, rate);
            expectedChanges += expected != reported ? 1 : 0;
            reported = expected;

            CHECK (p.getLatencyMode() == mode);
            CHECK (p.getLatencySamples() == expected);
            CHECK (counter.changes == expectedChanges);
        }

        // Restoring a session switches the mode the same way.
        HaliteProcessor normal;
        restore (p, stateOf (normal));
        expectedChanges += dsp::HaliteChain::latencySamples (LatencyMode::normal, rate) != reported ? 1 : 0;
        CHECK (p.getLatencyMode() == LatencyMode::normal);
        CHECK (p.getLatencySamples() == dsp::HaliteChain::latencySamples (LatencyMode::normal, rate));
        CHECK (counter.changes == expectedChanges);

        p.removeListener (&counter);
    }
}

TEST_CASE ("Processor: state round-trips, including the extremes", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    auto roundTrip = [] (auto&& setUp)
    {
        HaliteProcessor a;
        setUp (a);
        const auto saved = stateOf (a);

        HaliteProcessor b;
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
        CHECK (treeOf (saved)[myzel::plugin::sessionState::versionId].equals (HaliteProcessor::stateVersion));
    };

    SECTION ("defaults")
    {
        roundTrip ([] (HaliteProcessor&) {});
    }

    SECTION ("everything at its minimum")
    {
        roundTrip ([] (HaliteProcessor& p) { for (auto* param : p.getParameters()) param->setValueNotifyingHost (0.0f); });
    }

    SECTION ("everything at its maximum, Live latency")
    {
        roundTrip ([] (HaliteProcessor& p)
        {
            for (auto* param : p.getParameters())
                param->setValueNotifyingHost (1.0f);
            p.setLatencyMode (LatencyMode::live);
        });
    }

    SECTION ("random positions")
    {
        roundTrip ([] (HaliteProcessor& p)
        {
            std::mt19937 rng (2026u);
            std::uniform_real_distribution<float> dist (0.0f, 1.0f);
            for (auto* param : p.getParameters())
                param->setValueNotifyingHost (dist (rng));
        });
    }

    SECTION ("a typical setting")
    {
        roundTrip ([] (HaliteProcessor& p)
        {
            setParam (p, params::threshold, 9.5f);
            setParam (p, params::range, 12.0f);
            setParam (p, params::detectLow, 5200.0f);
            setParam (p, params::detectHigh, 14500.0f);
            setParam (p, params::shape, (float) Shape::spectral);
            setParam (p, params::stereo, (float) StereoMode::mid);
            setParam (p, params::speedAuto, 0.0f);
            setParam (p, params::release, 65.0f);
            setParam (p, params::harshOn, 1.0f);
            setParam (p, params::harshFreq, 2800.0f);
            setParam (p, params::output, -1.25f);
            p.setLatencyMode (LatencyMode::live);
        });
    }
}

TEST_CASE ("Processor: a reloaded state saves the same bytes", "[processor][state]")
{
    // clap-validator's state-reproducibility-binary: hosts compare saved chunks to detect a change.
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto mode : { LatencyMode::normal, LatencyMode::live })
    {
        HaliteProcessor a;
        setParam (a, params::threshold, 3.5f);
        setParam (a, params::harshOn, 1.0f);
        a.setLatencyMode (mode);
        const auto saved = stateOf (a);
        const auto savedXml = treeOf (saved).toXmlString();

        HaliteProcessor b;
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
        HaliteProcessor a;
        a.setLatencyMode (LatencyMode::live);
        HaliteProcessor b;
        restore (b, stateOf (a));
        CHECK (b.getLatencyMode() == LatencyMode::live);

        HaliteProcessor normal;
        normal.setLatencyMode (LatencyMode::normal);
        restore (b, stateOf (normal));
        CHECK (b.getLatencyMode() == LatencyMode::normal);
    }

    SECTION ("a session without the setting, or with one this version does not know, loads as Normal")
    {
        HaliteProcessor source;
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

            HaliteProcessor p;
            p.setLatencyMode (LatencyMode::live);
            restore (p, save (edited));
            CHECK (p.getLatencyMode() == LatencyMode::normal);
        }
    }

    SECTION ("it is stored in the session tree, under its frozen key, not as a parameter")
    {
        CHECK (sessionKeys::latencyMode.toString() == "latencyMode");
        HaliteProcessor p;
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
    HaliteProcessor p;
    setParam (p, params::harshOn, 1.0f);
    const auto saved = stateOf (p);

    for (const auto* id : { params::speedAuto, params::harshOn, params::harshSpeedAuto, params::bypass })
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

    HaliteProcessor source;
    setParam (source, params::range, 15.0f);
    const auto tree = treeOf (stateOf (source));

    SECTION ("a newer session's parameters are ignored")
    {
        auto newer = tree.createCopy();
        newer.appendChild (juce::ValueTree ("PARAM", { { "id", "breathRange" }, { "value", -20.0f } }), nullptr);

        HaliteProcessor p;
        restore (p, save (newer));
        CHECK_THAT (plainValue (p, params::range), WithinAbs (15.0, 1.0e-4));
    }

    SECTION ("a parameter missing from an older session takes its default")
    {
        auto older = tree.createCopy();
        older.removeChild (older.getChildWithProperty ("id", params::output), nullptr);

        HaliteProcessor p;
        setParam (p, params::output, 12.0f);
        restore (p, save (older));
        CHECK (juce::exactlyEqual (plainValue (p, params::output), 0.0f));
        CHECK_THAT (plainValue (p, params::range), WithinAbs (15.0, 1.0e-4));
    }
}

TEST_CASE ("Processor: a value that is not finite in a session restores as the default", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto* bad : { "nan", "inf", "-inf" })
    {
        INFO (bad);
        HaliteProcessor source;
        auto tree = source.getState().copyState();
        for (auto child : tree)
            if (child.hasType ("PARAM"))
                child.setProperty ("value", bad, nullptr);

        HaliteProcessor p;
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

TEST_CASE ("Processor: the UI state is saved with the session", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor a;
    a.getUiState().setProperty ("eventsShown", false, nullptr);

    HaliteProcessor b;
    restore (b, stateOf (a));
    CHECK_FALSE ((bool) b.getUiState().getProperty ("eventsShown", true));
}

TEST_CASE ("Processor: foreign or broken state is ignored", "[processor][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    setParam (p, params::threshold, 10.0f);
    p.setLatencyMode (LatencyMode::live);

    const char junk[] = "definitely not a plugin state";
    p.setStateInformation (junk, (int) sizeof (junk));
    CHECK_THAT (plainValue (p, params::threshold), WithinAbs (10.0, 1.0e-4));
    CHECK (p.getLatencyMode() == LatencyMode::live);

    juce::ValueTree other ("ViridianState");
    other.appendChild (juce::ValueTree ("PARAM", { { "id", params::threshold }, { "value", 2.0f } }), nullptr);
    restore (p, save (other));
    CHECK_THAT (plainValue (p, params::threshold), WithinAbs (10.0, 1.0e-4));
    CHECK (p.getLatencyMode() == LatencyMode::live);
}
