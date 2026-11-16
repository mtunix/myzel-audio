// Presets (PLAN.md, Presets; Shared infrastructure): the factory set loads, each preset sets every stored
// parameter and nothing else (not Bypass, Listen or the latency), one undo step; a user preset round-trips
// through its file; every preset sounds sane on a voice with esses.

#include "detector/Signals.h"
#include "support/ProcessorHelpers.h"

#include "plugin/FactoryPresets.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace halite;
using namespace halite::testing;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    /** The plain value a factory preset asks for: its own, else the parameter's default. */
    float wanted (const myzel::plugin::FactorySnapshot& preset, juce::RangedAudioParameter& p)
    {
        const auto id = juce::Identifier (p.getParameterID());
        const auto v = preset.values.contains (id) ? (float) preset.values[id] : p.convertFrom0to1 (p.getDefaultValue());
        return p.convertFrom0to1 (p.convertTo0to1 (v));     // as the range snaps it
    }

    /** A voice with esses spread over 18 dB, at about −18 dBFS RMS, stereo. */
    juce::AudioBuffer<float> voice (double seconds)
    {
        const auto n = samplesFor (seconds, rate);
        auto x = harmonic ({ .f0 = 170.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -9.0, .peakHz = 3000.0, .peakDb = 10.0, .rms = 0.1, .seed = 9 }, n, rate);
        for (int k = 0; 0.3 + 0.5 * k + 0.12 < seconds; ++k)
        {
            const auto from = samplesFor (0.3 + 0.5 * k, rate);
            const auto level = 0.03 * std::pow (10.0, (double) ((k * 7) % 18 - 9) / 20.0);
            x = sum (x, gated (bandNoise (4500.0, 12000.0, n, rate, 40 + (unsigned) k, level), from, from + samplesFor (0.12, rate), rate));
        }
        juce::AudioBuffer<float> b (2, n);
        for (int ch = 0; ch < 2; ++ch)
            b.copyFrom (ch, 0, x.data(), n);
        return b;
    }

    double rmsDb (const juce::AudioBuffer<float>& b, int from)
    {
        auto sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = from; i < b.getNumSamples(); ++i)
                sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return 10.0 * std::log10 (sum / (double) (b.getNumChannels() * (b.getNumSamples() - from)) + 1.0e-30);
    }

    /** A scratch user preset folder for the test's lifetime. */
    struct ScratchPresets
    {
        ScratchPresets()
        {
            dir.createDirectory();
            myzel::plugin::PresetManager::setUserPresetDirectoryOverride (dir);
        }
        ~ScratchPresets()
        {
            myzel::plugin::PresetManager::setUserPresetDirectoryOverride ({});
            dir.deleteRecursively();
        }
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("HalitePresetTests", "");
    };
}

TEST_CASE ("Presets: about 25 factory presets in the plan's categories, Init first, every name once", "[presets]")
{
    const auto format = presetFormat();
    const juce::StringArray categories { "Utility", "Vocals", "Speech", "Instruments", "Mix" };

    CHECK (format.factory.size() >= 22);
    CHECK (format.factory.size() <= 28);
    REQUIRE_FALSE (format.factory.empty());
    CHECK (format.factory.front().name == "Init");
    CHECK (format.factory.front().values.isEmpty());

    std::set<juce::String> names;
    auto category = 0;
    for (const auto& preset : format.factory)
    {
        INFO (preset.name);
        CHECK (names.insert (preset.name).second);
        const auto at = categories.indexOf (preset.category);
        CHECK (at >= category);     // grouped, in the plan's order
        category = std::max (category, at);

        // Only stored parameters, never Bypass, Listen or anything else.
        for (const auto& v : preset.values)
            CHECK (format.parameterIds.contains (v.name.toString()));
    }

    // Every plan name is there (PLAN.md, Presets).
    for (const auto* name : { "Init", "Gentle", "Firm", "Live tracking", "Lead female", "Lead male", "Rap", "Choir", "Backing stack", "Whisper",
                              "Harsh lead", "Shouty rock", "Podcast", "Voice-over", "Audiobook", "Harsh room mic", "Acoustic guitar",
                              "Hi-hat tamer", "Cymbal bus", "Brass", "Mastering air", "Bus" })
    {
        INFO (name);
        CHECK (names.count (name) == 1);
    }
}

TEST_CASE ("Presets: every factory preset sets each stored parameter as written, one undo step, and leaves Bypass, Listen and latency alone",
           "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresets scratch;
    HaliteProcessor p;
    p.prepareToPlay (rate, 512);
    auto& pm = p.getPresets();
    const auto format = presetFormat();

    std::set<std::vector<float>> sounds;
    for (int i = 0; i < (int) format.factory.size(); ++i)
    {
        const auto& preset = format.factory[(size_t) i];
        INFO (preset.name);

        // A state far from every preset, monitoring on, Live latency.
        for (const auto& id : params::soundParameterIds())
            setParam (p, id.toRawUTF8(), p.getState().getParameter (id)->convertFrom0to1 (0.37f));
        setParam (p, params::bypass, 1.0f);
        setParam (p, params::listen, 4.0f);
        p.setLatencyMode (LatencyMode::live);
        p.getUndoManager().clearUndoHistory();
        const auto before = pm.capture();

        pm.load (i);

        std::vector<float> sound;
        for (const auto& id : params::soundParameterIds())
        {
            INFO (id);
            auto* param = p.getState().getParameter (id);
            CHECK (plainValue (p, id.toRawUTF8()) == Approx (wanted (preset, *param)).margin (1.0e-3));
            sound.push_back (plainValue (p, id.toRawUTF8()));
        }
        CHECK (sounds.insert (sound).second);       // no two presets are the same sound
        CHECK (plainValue (p, params::bypass) > 0.5f);
        CHECK (plainValue (p, params::listen) == Approx (4.0f));
        CHECK (p.getLatencyMode() == LatencyMode::live);
        CHECK (pm.getCurrentName() == preset.name);
        CHECK (pm.getCurrentCategory() == preset.category);
        CHECK_FALSE (pm.isModified());

        // One undo step brings back everything it changed.
        REQUIRE (p.getUndoManager().canUndo());
        p.getUndoManager().undo();
        CHECK_FALSE (p.getUndoManager().canUndo());
        for (const auto& id : params::soundParameterIds())
        {
            INFO (id);
            CHECK (plainValue (p, id.toRawUTF8()) == Approx ((float) before[juce::Identifier (id)]).margin (1.0e-3));
        }
    }
}

TEST_CASE ("Presets: a user preset round-trips through its file, the sound only", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresets scratch;
    HaliteProcessor p;
    auto& pm = p.getPresets();

    setParam (p, params::threshold, -21.5f);
    setParam (p, params::thresholdAbs, -33.0f);
    setParam (p, params::detectLow, 5200.0f);
    setParam (p, params::shape, 2.0f);
    setParam (p, params::harshOn, 1.0f);
    setParam (p, params::harshWidth, 1.35f);
    setParam (p, params::speedAuto, 0.0f);
    setParam (p, params::release, 37.0f);
    setParam (p, params::output, -2.5f);
    setParam (p, params::listen, 1.0f);
    setParam (p, params::bypass, 1.0f);
    const auto saved = pm.capture();

    REQUIRE (pm.saveUserPreset ("Round trip").wasOk());
    const auto file = scratch.dir.getChildFile ("Round trip.xml");
    REQUIRE (file.existsAsFile());
    const auto xml = juce::XmlDocument::parse (file);
    REQUIRE (xml != nullptr);
    CHECK (xml->hasTagName (params::presetTag));
    CHECK_FALSE (xml->hasAttribute (params::bypass));
    CHECK_FALSE (xml->hasAttribute (params::listen));
    CHECK_FALSE (xml->hasAttribute ("latencyMode"));

    pm.load (0);    // Init
    CHECK (plainValue (p, params::threshold) == Approx (params::thresholdDefaultDb));
    CHECK (plainValue (p, params::listen) == Approx (1.0f));

    // A fresh manager reads the folder again, as a new instance would.
    pm.scanUserPresets();
    int index = -1;
    for (int i = 0; i < (int) pm.getEntries().size(); ++i)
        if (pm.getEntries()[(size_t) i].name == "Round trip" && ! pm.getEntries()[(size_t) i].isFactory)
            index = i;
    REQUIRE (index >= 0);
    pm.load (index);

    for (const auto& id : params::soundParameterIds())
    {
        INFO (id);
        CHECK (plainValue (p, id.toRawUTF8()) == Approx ((float) saved[juce::Identifier (id)]).margin (1.0e-3));
    }
    CHECK (pm.isCurrentUserPreset());
    CHECK_FALSE (pm.isModified());
}

TEST_CASE ("Presets: every factory preset sounds sane on a voice with esses", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ScratchPresets scratch;
    const auto in = voice (3.0);
    const auto skip = samplesFor (0.5, rate);
    const auto format = presetFormat();
    auto cutting = 0;

    for (int i = 0; i < (int) format.factory.size(); ++i)
    {
        const auto& preset = format.factory[(size_t) i];
        INFO (preset.name);
        HaliteProcessor p;
        p.setPlayConfigDetails (2, 2, rate, 512);
        p.prepareToPlay (rate, 512);
        p.getPresets().load (i);

        const auto out = run (p, in, { 512 });
        auto finite = true;
        auto peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < out.getNumSamples(); ++k)
            {
                finite = finite && std::isfinite (out.getSample (ch, k));
                peak = std::max (peak, std::abs (out.getSample (ch, k)));
            }

        // A de-esser takes out a little and adds nothing.
        const auto change = rmsDb (out, skip) - rmsDb (in, skip);
        INFO ("level change " << change << " dB, peak " << peak);
        CHECK (finite);
        CHECK (peak < 1.0f);
        CHECK (change < 0.05);
        CHECK (change > -3.0);
        cutting += change < -0.05 ? 1 : 0;
    }

    // Most presets find this voice's esses (the instrument and mix presets listen for other material).
    CHECK (cutting >= (int) format.factory.size() / 2);
}
