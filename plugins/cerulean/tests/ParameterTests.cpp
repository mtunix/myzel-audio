#include "dsp/Mappings.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace cerulean;

TEST_CASE ("Parameters: the ID set is frozen", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;

    // Saved sessions and automation refer to these: never rename or remove one.
    const juce::StringArray expected {
        "algorithm", "decay", "freeze", "preDelay", "size", "mod", "tone", "paramA", "paramB", "mix",
        "output", "duck", "wetOnly", "bypass", "lowEnd", "diffusion", "modRate", "width", "inLowCut",
        "inHighCut", "duckAmount", "duckThreshold", "duckRelease", "preDelaySync", "preDelayNote",
        "echoSync", "echoNote", "bpm", "freezeMode", "trails",
        // Added later: new IDs go on the end.
        "freezeLayer"
    };

    juce::StringArray actual;
    for (auto* param : p.getParameters())
        actual.add (dynamic_cast<juce::HostedAudioProcessorParameter*> (param)->getParameterID());

    CHECK (actual == expected);
}

TEST_CASE ("Parameters: presets store the sound, not the performance switches", "[parameters]")
{
    const auto& ids = params::soundParameterIds();
    for (const auto* excluded : { params::bypass, params::freeze, params::bpm, params::freezeMode })
    {
        INFO (excluded);
        CHECK_FALSE (ids.contains (excluded));
    }
    CHECK (ids.contains (params::algorithm));
    CHECK (ids.contains (params::trails));
}

TEST_CASE ("Parameters: defaults and mappings", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CeruleanProcessor p;
    const auto s = p.readSettings();

    CHECK (s.algorithm == Algorithm::hall);
    CHECK (std::abs (s.engine.decaySeconds - 3.0f) < 1.0e-4f);
    CHECK (std::abs (s.engine.sizeScale - 1.0f) < 1.0e-6f);
    CHECK (std::abs (s.mix - 0.35f) < 1.0e-6f);
    CHECK (s.trails);
    CHECK_FALSE (s.duck);

    CHECK (std::abs (dsp::maps::sizeToScale (0.0f) - 0.3f) < 1.0e-5f);
    CHECK (std::abs (dsp::maps::sizeToScale (1.0f) - 2.0f) < 1.0e-5f);

    // Decay is exponential: the middle of the knob is the geometric mean.
    auto* decay = p.getState().getParameter (params::decay);
    CHECK (std::abs (decay->convertFrom0to1 (0.5f) - std::sqrt (0.1f * 60.0f)) < 0.01f);
}

TEST_CASE ("Parameters: Param A/B read out per algorithm", "[parameters]")
{
    CHECK (params::characterText (Algorithm::halo, false, 100.0f) == "+12/+19");
    CHECK (params::characterText (Algorithm::halo, false, 0.0f) == "-12");
    CHECK (params::characterText (Algorithm::spring, false, 0.0f) == "1 spring");
    CHECK (params::characterText (Algorithm::spring, false, 100.0f) == "3 springs");
    CHECK (params::characterText (Algorithm::choir, false, 0.0f) == "A");
    CHECK (params::characterText (Algorithm::choir, false, 100.0f) == "U");
    CHECK (params::characterText (Algorithm::rise, true, 0.0f) == "Wet only");
    CHECK (params::characterText (Algorithm::echo, false, 0.0f) == "20 ms");
    CHECK (params::characterText (Algorithm::hall, true, 42.0f) == "42 %");

    for (int a = 0; a < numAlgorithms; ++a)
    {
        const auto [legendA, legendB] = params::characterLegends (static_cast<Algorithm> (a));
        CHECK (legendA.isNotEmpty());
        CHECK (legendB.isNotEmpty());
    }
}
