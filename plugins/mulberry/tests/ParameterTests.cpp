#include "dsp/NoteValues.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace mulberry;

TEST_CASE ("Parameters: the ID set is frozen", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    MulberryProcessor p;

    // Saved sessions and automation refer to these: never rename or remove one.
    juce::StringArray expected {
        "machine", "mode", "time", "sync", "note", "hold", "feedback", "drive", "wobble", "diffuse",
        "lowCut", "highCut", "paramA", "paramB", "timeRight", "noteRight", "width", "steps", "mix",
        "output", "duck", "wetOnly", "bypass", "groove", "feel", "crossFeed", "panRule", "wobbleRate",
        "wobbleShape", "noise", "trails", "duckAmount", "duckThreshold", "duckRelease", "bpm", "holdMode"
    };
    for (int i = 1; i <= 16; ++i)
        expected.add ("step" + juce::String (i));

    juce::StringArray actual;
    for (auto* param : p.getParameters())
        actual.add (dynamic_cast<juce::HostedAudioProcessorParameter*> (param)->getParameterID());

    CHECK (actual == expected);
}

TEST_CASE ("Parameters: presets store the sound, not the performance switches", "[parameters]")
{
    const auto& ids = params::soundParameterIds();
    for (const auto* excluded : { params::bypass, params::hold, params::bpm, params::holdMode })
    {
        INFO (excluded);
        CHECK_FALSE (ids.contains (excluded));
    }
    CHECK (ids.contains (params::machine));
    CHECK (ids.contains ("step16"));
}

TEST_CASE ("Parameters: defaults are Reel, Single, 1/8D synced", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    MulberryProcessor p;
    const auto s = p.readSettings (120.0);

    CHECK (s.machine == Machine::reel);
    CHECK (s.plan.mode == Mode::single);
    CHECK (std::abs (s.timeMs[0] - 375.0f) < 1.0e-3f);
    CHECK (std::abs (s.feedback - 0.35f) < 1.0e-6f);
    CHECK (s.noise == 0.0f);
    CHECK (s.trails);
    CHECK (std::abs (maps::wobbleScale (s.wobble) - 1.0f) < 1.0e-6f);

    // Time is exponential: the middle of the knob is the geometric mean.
    auto* time = p.getState().getParameter (params::time);
    CHECK (std::abs (time->convertFrom0to1 (0.5f) - std::sqrt (1.0f * 4000.0f)) < 0.01f);
}

TEST_CASE ("Parameters: note values and folding", "[parameters]")
{
    CHECK (notes::count == 21);
    CHECK (std::string (notes::names[(size_t) notes::index1_8D]) == "1/8D");
    CHECK (notes::beats (notes::index1_4) == 1.0);
    CHECK (std::abs (notes::beats (0) - 1.0 / 24.0) < 1.0e-12);     // 1/64T
    CHECK (notes::beats (20) == 6.0);                               // 1/1D
    CHECK (std::abs (notes::lengthMs (notes::index1_8D, 120.0) - 375.0) < 1.0e-9);

    // 1/1D at 60 BPM is 6 s: folded once to 3 s.
    const auto folded = notes::fold (notes::lengthMs (20, 60.0), 4000.0);
    CHECK (folded.octaves == 1);
    CHECK (std::abs (folded.ms - 3000.0) < 1.0e-9);
}

TEST_CASE ("Parameters: Param A/B read out per machine", "[parameters]")
{
    CHECK (params::characterText (Machine::reel, false, 50.0f) == "15 ips");
    CHECK (params::characterText (Machine::reel, false, 0.0f) == "7.5 ips");
    CHECK (params::characterText (Machine::reel, true, 50.0f) == "Nominal");
    CHECK (params::characterText (Machine::loop, false, 100.0f) == "1+2+3");
    CHECK (params::characterText (Machine::loop, false, 0.0f) == "1");
    CHECK (params::characterText (Machine::rack, false, 0.0f) == "8 bits");
    CHECK (params::characterText (Machine::rack, false, 100.0f) == "16 bits");
    CHECK (params::characterText (Machine::bucket, false, 0.0f) == "1024 stages");
    CHECK (params::characterText (Machine::bucket, false, 100.0f) == "8192 stages");
    CHECK (params::characterText (Machine::clean, true, 50.0f) == "Off");
    CHECK (params::characterText (Machine::clean, true, 100.0f) == "+12 st");
    CHECK (params::characterText (Machine::clean, true, 0.0f) == "-12 st");

    // Typed values find their position.
    CHECK (character::shiftSemitones (params::characterPercentFromText (Machine::clean, true, "+7") / 100.0f) == 7);
    CHECK (character::bits (params::characterPercentFromText (Machine::rack, false, "12") / 100.0f) == 12);
    CHECK (character::speedIps (params::characterPercentFromText (Machine::reel, false, "30 ips") / 100.0f) == 30.0f);
    CHECK (character::headMask (params::characterPercentFromText (Machine::loop, false, "2+3") / 100.0f) == 0b110);
    CHECK (params::characterPercentFromText (Machine::oilCan, true, "42 %") == 42.0f);
    CHECK (params::characterPercentFromText (Machine::oilCan, true, "nonsense") < 0.0f);
}
