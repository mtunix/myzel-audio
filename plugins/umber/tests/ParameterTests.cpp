#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace umber;
using Catch::Matchers::WithinAbs;

namespace
{
    juce::RangedAudioParameter& param (UmberProcessor& p, const char* id)
    {
        auto* rp = p.getState().getParameter (id);
        REQUIRE (rp != nullptr);
        return *rp;
    }

    float defaultValue (UmberProcessor& p, const char* id)
    {
        auto& rp = param (p, id);
        return rp.convertFrom0to1 (rp.getDefaultValue());
    }
}

TEST_CASE ("All parameters exist with stable IDs", "[params]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    for (auto* id : { params::drive, params::model, params::slam, params::tone, params::lowCut,
                      params::weight, params::highCut, params::autoGain, params::mix, params::output,
                      params::quality, params::renderQuality, params::bypass, params::delta,
                      params::stereoMode, params::emphasis, params::bias })
    {
        INFO (id);
        CHECK (p.getState().getParameter (id) != nullptr);
    }

    CHECK (p.getParameters().size() == 17);
}

TEST_CASE ("Defaults are neutral and sensible", "[params]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    CHECK_THAT (defaultValue (p, params::drive), WithinAbs (3.0, 1e-4));
    CHECK_THAT (defaultValue (p, params::tone), WithinAbs (0.0, 1e-4));
    CHECK_THAT (defaultValue (p, params::output), WithinAbs (0.0, 1e-4));
    CHECK_THAT (defaultValue (p, params::mix), WithinAbs (100.0, 1e-4));
    CHECK (params::isLowCutOff (defaultValue (p, params::lowCut)));
    CHECK (params::isHighCutOff (defaultValue (p, params::highCut)));
    CHECK (defaultValue (p, params::autoGain) > 0.5f);
    CHECK (defaultValue (p, params::slam) < 0.5f);
    CHECK (defaultValue (p, params::bypass) < 0.5f);
    CHECK (defaultValue (p, params::delta) < 0.5f);
    CHECK_THAT (defaultValue (p, params::stereoMode), WithinAbs ((float) StereoMode::stereo, 1e-4));
    CHECK_THAT (defaultValue (p, params::emphasis), WithinAbs (0.0, 1e-4));
    CHECK_THAT (defaultValue (p, params::bias), WithinAbs (0.0, 1e-4));
}

TEST_CASE ("Emphasis and Bias text round-trips", "[params]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    auto& emphasis = param (p, params::emphasis);
    CHECK (emphasis.getText (0.5f, 32) == "Flat");
    CHECK (emphasis.getText (1.0f, 32) == "Highs 10.0");
    CHECK (emphasis.getText (0.0f, 32) == "Lows 10.0");
    CHECK_THAT (emphasis.convertFrom0to1 (emphasis.getValueForText ("Lows 4")), WithinAbs (-4.0, 0.01));
    CHECK_THAT (emphasis.convertFrom0to1 (emphasis.getValueForText ("flat")), WithinAbs (0.0, 0.01));

    auto& bias = param (p, params::bias);
    CHECK (bias.getText (0.5f, 32) == "Nominal");
    CHECK (bias.getText (0.75f, 32) == "Hot 5.0");
    CHECK_THAT (bias.convertFrom0to1 (bias.getValueForText ("Cold 2.5")), WithinAbs (-2.5, 0.01));

    CHECK (params::stereoModeNames() == juce::StringArray { "Stereo", "Mid", "Side" });
}

TEST_CASE ("Filter extremes display as Off and parse back", "[params]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    auto& lowCut = param (p, params::lowCut);
    CHECK (lowCut.getText (0.0f, 32) == "Off");
    CHECK (lowCut.getText (1.0f, 32) == "500 Hz");
    CHECK_THAT (lowCut.getValueForText ("off"), WithinAbs (0.0, 1e-4));
    CHECK_THAT (lowCut.convertFrom0to1 (lowCut.getValueForText ("120 Hz")), WithinAbs (120.0, 0.5));

    auto& highCut = param (p, params::highCut);
    CHECK (highCut.getText (1.0f, 32) == "Off");
    CHECK (highCut.getText (0.0f, 32) == "2.00 kHz");
    CHECK_THAT (highCut.getValueForText ("Off"), WithinAbs (1.0, 1e-4));
    CHECK_THAT (highCut.convertFrom0to1 (highCut.getValueForText ("8k")), WithinAbs (8000.0, 1.0));
}

TEST_CASE ("Tone text round-trips", "[params]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    UmberProcessor p;

    auto& tone = param (p, params::tone);
    CHECK (tone.getText (0.5f, 32) == "Flat");

    for (auto text : { "Dark 4.0", "Bright 7.5", "-2.5", "3" })
    {
        INFO (text);
        const auto normalised = tone.getValueForText (text);
        const auto value = tone.convertFrom0to1 (normalised);
        const auto expected = juce::String (text).startsWith ("Dark") || juce::String (text).startsWith ("-")
                                  ? -std::abs (juce::String (text).retainCharacters ("0123456789.").getFloatValue())
                                  : juce::String (text).retainCharacters ("0123456789.").getFloatValue();
        CHECK_THAT (value, WithinAbs (expected, 0.01));
    }
}

TEST_CASE ("Model names match the enum order", "[params]")
{
    const auto names = params::modelNames();
    REQUIRE (names.size() == numModels);
    CHECK (names[static_cast<int> (Model::reel)] == "Reel");
    CHECK (names[static_cast<int> (Model::valve)] == "Valve");
    CHECK (names[static_cast<int> (Model::fuzz)] == "Fuzz");
}
