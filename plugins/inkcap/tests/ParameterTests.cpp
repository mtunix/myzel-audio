/*
    The frozen parameter layout: IDs and their order, ranges, defaults, units and read-outs, and
    which parameters are sound (presets, A/B, undo) and which are session settings.
*/

#include "plugin/PluginProcessor.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace inkcap;

namespace
{
    juce::RangedAudioParameter& param (InkcapProcessor& p, const char* id)
    {
        auto* rp = p.getState().getParameter (id);
        REQUIRE (rp != nullptr);
        return *rp;
    }

    float plainDefault (InkcapProcessor& p, const char* id)
    {
        auto& rp = param (p, id);
        return rp.convertFrom0to1 (rp.getDefaultValue());
    }

    juce::String textAt (InkcapProcessor& p, const char* id, float plain)
    {
        auto& rp = param (p, id);
        return rp.getText (rp.convertTo0to1 (plain), 64);
    }

    float plainFromText (InkcapProcessor& p, const char* id, const juce::String& text)
    {
        auto& rp = param (p, id);
        return rp.convertFrom0to1 (rp.getValueForText (text));
    }
}

TEST_CASE ("Parameters: the ID set is frozen", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;

    // Saved sessions, presets and automation refer to these (PLAN.md, Parameter IDs): never rename or remove one.
    const juce::StringArray expected {
        "damage", "chaos", "mix", "output", "bypass", "match", "bpm",
        "glitchOn", "glitchAmount", "glitchType", "glitchRate", "glitchLength", "glitchGroove",
        "warbleOn", "warbleAmount", "warbleType", "warbleRate", "warbleSync", "warbleSpread",
        "crushOn", "crushAmount", "crushType", "crushDrive", "crushTone",
        "bitsOn", "bitsAmount", "bitsType", "bitsRate", "bitsDepth", "bitsJitter",
        "hissOn", "hissAmount", "hissType", "hissTone", "hissFollow",
        "roomOn", "roomAmount", "roomType", "roomSize", "roomDuck"
    };

    juce::StringArray actual;
    for (auto* parameter : p.getParameters())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
        REQUIRE (withId != nullptr);
        actual.add (withId->getParameterID());
        CHECK (withId->getVersionHint() == 1);
    }

    CHECK (actual == expected);
    CHECK (params::allParameterIds() == expected);

    // Hosts list each module's parameters as a group.
    const auto groups = p.getParameterTree().getSubgroups (false);
    REQUIRE (groups.size() == numModules);
    for (int m = 0; m < numModules; ++m)
    {
        CHECK (groups[m]->getID() == moduleInfo (static_cast<Module> (m)).key);
        CHECK (groups[m]->getName() == moduleInfo (static_cast<Module> (m)).name);
    }
}

TEST_CASE ("Parameters: ranges and defaults", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;

    struct Expected
    {
        const char* id;
        float min, max, def;
    };

    for (const auto& e : { Expected { params::damage, 0, 100, 50 }, Expected { params::chaos, 0, 100, 25 },
                           Expected { params::mix, 0, 100, 100 }, Expected { params::output, -24, 24, 0 },
                           Expected { params::bpm, 40, 240, 120 },
                           Expected { params::glitchLength, 0, 100, 50 }, Expected { params::glitchGroove, 50, 75, 50 },
                           Expected { params::warbleRate, 0, 100, 50 }, Expected { params::warbleSpread, 0, 100, 0 },
                           Expected { params::crushDrive, 0, 100, 50 }, Expected { params::crushTone, -100, 100, 0 },
                           Expected { params::bitsRate, 0, 100, 50 }, Expected { params::bitsDepth, 0, 100, 50 },
                           Expected { params::bitsJitter, 0, 100, 0 },
                           Expected { params::hissTone, -100, 100, 0 }, Expected { params::hissFollow, -100, 100, 0 },
                           Expected { params::roomSize, 0, 100, 50 }, Expected { params::roomDuck, 0, 100, 0 } })
    {
        INFO (e.id);
        const auto& range = param (p, e.id).getNormalisableRange();
        CHECK (juce::exactlyEqual (range.start, e.min));
        CHECK (juce::exactlyEqual (range.end, e.max));
        CHECK (std::abs (plainDefault (p, e.id) - e.def) < 1.0e-4f);
    }

    for (int m = 0; m < numModules; ++m)
    {
        const auto module = static_cast<Module> (m);
        const auto& info = moduleInfo (module);
        INFO (info.name);

        CHECK ((plainDefault (p, params::onIds[(size_t) m]) > 0.5f) == info.defaultOn);
        CHECK (std::abs (plainDefault (p, params::amountIds[(size_t) m]) - 50.0f) < 1.0e-4f);

        auto* type = dynamic_cast<juce::AudioParameterChoice*> (&param (p, params::typeIds[(size_t) m]));
        REQUIRE (type != nullptr);
        CHECK (type->choices == params::typeNames (module));
        CHECK (type->getIndex() == info.defaultType);
    }

    // The fresh instance: worn tape (Warble, Crush, Hiss), Match on, nothing bypassed.
    const auto s = p.readSettings();
    CHECK_FALSE (s.glitch.on);
    CHECK (s.warble.on);
    CHECK (s.crush.on);
    CHECK_FALSE (s.bits.on);
    CHECK (s.hiss.on);
    CHECK_FALSE (s.room.on);
    CHECK (s.match);
    CHECK_FALSE (s.bypass);
    CHECK (s.glitch.rate == defaultGlitchRate);
    CHECK (std::string (glitchRates[(size_t) s.glitch.rate].name) == "1/16");
    CHECK (std::abs (s.glitch.groove - 0.5f) < 1.0e-6f);
    CHECK (std::abs (s.mix - 1.0f) < 1.0e-6f);
}

TEST_CASE ("Parameters: the module types and Glitch's grid", "[parameters]")
{
    CHECK (params::typeNames (Module::glitch) == juce::StringArray { "Skip", "Chew", "Stall", "Bend", "Any" });
    CHECK (params::typeNames (Module::warble) == juce::StringArray { "Wow", "Flutter", "Drift" });
    CHECK (params::typeNames (Module::crush) == juce::StringArray { "Tape", "Cassette", "Valve", "Fuzz" });
    CHECK (params::typeNames (Module::bits) == juce::StringArray { "Sampler 12", "Sampler 8", "Phone" });
    CHECK (params::typeNames (Module::hiss) == juce::StringArray { "Vinyl", "Tape", "Hum" });
    CHECK (params::typeNames (Module::room) == juce::StringArray { "Closet", "Room", "Spring", "Hall" });

    // Strictly slow to fast: every step a shorter slot.
    CHECK (params::glitchRateNames() == juce::StringArray { "1/4D", "1/4", "1/8D", "1/4T", "1/8", "1/16D",
                                                            "1/8T", "1/16", "1/32D", "1/16T", "1/32", "1/32T" });
    for (size_t i = 1; i < glitchRates.size(); ++i)
        CHECK (glitchRates[i].beats < glitchRates[i - 1].beats);
    CHECK (juce::exactlyEqual (glitchRates.front().beats, 1.5));
    CHECK (std::abs (glitchRates.back().beats * 12.0 - 1.0) < 1.0e-12);
}

TEST_CASE ("Parameters: read-outs and typed entry", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    InkcapProcessor p;

    CHECK (textAt (p, params::damage, 42.0f) == "42 %");
    CHECK (textAt (p, params::output, 1.5f) == "+1.5 dB");
    CHECK (textAt (p, params::output, 0.0f) == "0.0 dB");
    CHECK (textAt (p, params::output, -6.0f) == "-6.0 dB");
    CHECK (textAt (p, params::bpm, 92.5f) == "92.5 BPM");
    CHECK (textAt (p, params::glitchGroove, 50.0f) == "Straight");
    CHECK (textAt (p, params::glitchGroove, 66.0f) == "66 %");
    CHECK (textAt (p, params::crushTone, 0.0f) == "Neutral");
    CHECK (textAt (p, params::crushTone, -40.0f) == "Dark 40");
    CHECK (textAt (p, params::hissTone, 25.0f) == "Bright 25");
    CHECK (textAt (p, params::hissFollow, 0.0f) == "Steady");
    CHECK (textAt (p, params::hissFollow, -30.0f) == "Duck 30");
    CHECK (textAt (p, params::hissFollow, 70.0f) == "Follow 70");
    CHECK (textAt (p, params::warbleRate, 50.0f) == "0.55 Hz");      // Wow, free: the type's own rate
    CHECK (textAt (p, params::glitchLength, 50.0f) == "49 ms");       // Skip: the repeated piece
    CHECK (textAt (p, params::crushDrive, 50.0f) == "+13 dB");        // Tape: dB into the coating
    CHECK (textAt (p, params::bitsRate, 50.0f) == "26.0 kHz");        // Sampler 12: its own clock
    CHECK (textAt (p, params::bitsDepth, 50.0f) == "12 bits");

    CHECK (std::abs (plainFromText (p, params::damage, "73 %") - 73.0f) < 0.01f);
    CHECK (std::abs (plainFromText (p, params::output, "-3.5 dB") + 3.5f) < 0.01f);
    CHECK (std::abs (plainFromText (p, params::crushTone, "dark 40") + 40.0f) < 0.01f);
    CHECK (std::abs (plainFromText (p, params::crushTone, "Neutral")) < 0.01f);
    CHECK (std::abs (plainFromText (p, params::hissFollow, "duck 20") + 20.0f) < 0.01f);
    CHECK (std::abs (plainFromText (p, params::hissFollow, "follow 20") - 20.0f) < 0.01f);
    CHECK (std::abs (plainFromText (p, params::glitchGroove, "straight") - 50.0f) < 0.01f);
    CHECK (textAt (p, params::roomSize, 50.0f) == "5.0 m");           // Room: the longest dimension
    CHECK (std::abs (plainFromText (p, params::roomSize, "5 m") - 50.0f) < 0.01f);

    // Text that does not parse leaves a control where it is: numbers, choices and switches.
    for (auto* parameter : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        REQUIRE (ranged != nullptr);
        juce::AudioProcessorParameter& base = *ranged;
        base.setValueNotifyingHost (ranged->convertTo0to1 (ranged->convertFrom0to1 (0.37f)));
        INFO (ranged->getParameterID());
        for (const auto* junk : { "loud", "", "  ", "fast" })
            CHECK (std::abs (base.getValueForText (junk) - base.getValue()) < 1.0e-6f);
        if (dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr)
            CHECK (std::abs (base.getValueForText ("1/8x") - base.getValue()) < 1.0e-6f);
    }

    // Names match in any case, around spaces.
    auto* rate = p.getState().getParameter (params::glitchRate);
    CHECK (rate->getText (rate->getValueForText (" 1/8d "), 64) == "1/8D");
    auto* room = p.getState().getParameter (params::roomType);
    CHECK (room->getText (room->getValueForText ("hall"), 64) == "Hall");
    auto* on = p.getState().getParameter (params::warbleOn);
    CHECK (on->getValueForText ("OFF") < 0.5f);
    CHECK (on->getValueForText ("On") > 0.5f);

    // Every read-out reads back to the value it shows (Groove rounds to 1 % of a 25 % range).
    for (auto* parameter : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        REQUIRE (ranged != nullptr);

        for (const auto x : { 0.0f, 0.13f, 0.5f, 0.77f, 1.0f })
        {
            const auto text = ranged->getText (x, 64);
            INFO (ranged->getParameterID() << " at " << x << ": " << text);
            CHECK (std::abs (ranged->getValueForText (text) - ranged->convertTo0to1 (ranged->convertFrom0to1 (x))) < 0.021f);
        }
    }
}

TEST_CASE ("Parameters: presets store the sound, not the session settings", "[parameters]")
{
    const auto& sound = params::soundParameterIds();
    for (const auto* setting : { params::bypass, params::match, params::bpm })
    {
        INFO (setting);
        CHECK_FALSE (sound.contains (setting));
        CHECK (params::settingParameterIds().contains (setting));
    }

    CHECK (sound.size() + params::settingParameterIds().size() == params::allParameterIds().size());
    CHECK (sound.contains (params::damage));
    CHECK (sound.contains (params::mix));
    CHECK (sound.contains (params::roomDuck));
}
