#include "support/ProcessorHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace halite;
using namespace halite::testing;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
    juce::RangedAudioParameter& parameter (HaliteProcessor& p, const char* id)
    {
        auto* rp = p.getState().getParameter (id);
        REQUIRE (rp != nullptr);
        return *rp;
    }

    float defaultOf (HaliteProcessor& p, const char* id)
    {
        auto& rp = parameter (p, id);
        return rp.convertFrom0to1 (rp.getDefaultValue());
    }

    juce::String textAt (HaliteProcessor& p, const char* id, float plain)
    {
        auto& rp = parameter (p, id);
        return rp.getText (rp.convertTo0to1 (plain), 32);
    }

    float valueForText (HaliteProcessor& p, const char* id, const juce::String& text)
    {
        auto& rp = parameter (p, id);
        return rp.convertFrom0to1 (rp.getValueForText (text));
    }

    struct Continuous
    {
        const char* id;
        float min, max, defaultValue;
        const char* label;
    };

    struct Choice
    {
        const char* id;
        juce::StringArray names;
        int defaultIndex;
    };

    struct Switch
    {
        const char* id;
        bool defaultValue;
    };

    // PLAN.md, Controls. The relative thresholds' ranges are the corpus's (M1, M1b); every threshold
    // default is what Learn sets on the corpus and the fixed releases' defaults were measured (M3,
    // Settled). The Key HPF's range is a first choice (the plan names the control, not its range).
    const std::vector<Continuous> continuous {
        { params::threshold,         -36.0f,   24.0f,    -17.0f,   "dB"   },
        { params::thresholdAbs,      -60.0f,   0.0f,     -16.0f,   "dBFS" },
        { params::range,             0.0f,     24.0f,    8.0f,     "dB"   },
        { params::detectLow,         1500.0f,  20000.0f, 4500.0f,  "Hz"   },
        { params::detectHigh,        1500.0f,  20000.0f, 12000.0f, "Hz"   },
        { params::focus,             0.0f,     100.0f,   70.0f,    "%"    },
        { params::release,           5.0f,     200.0f,   20.0f,    "ms"   },
        { params::lookahead,         0.0f,     10.0f,    5.0f,     "ms"   },
        { params::keyHpf,            20.0f,    2000.0f,  20.0f,    "Hz"   },
        { params::output,            -12.0f,   12.0f,    0.0f,     "dB"   },
        { params::harshThreshold,    -36.0f,   12.0f,    -9.9f,    "dB"   },
        { params::harshThresholdAbs, -60.0f,   0.0f,     -22.4f,   "dBFS" },
        { params::harshRange,        0.0f,     12.0f,    4.0f,     "dB"   },
        { params::harshFreq,         1500.0f,  6000.0f,  3200.0f,  "Hz"   },
        { params::harshWidth,        0.3f,     2.0f,     1.0f,     "oct"  },
        { params::harshRelease,      10.0f,    300.0f,   80.0f,    "ms"   },
    };

    const std::vector<Choice> choices {
        { params::shape,     { "Split", "Spectral", "Wide" }, 0 },
        { params::detection, { "Voice", "Allround" }, 0 },
        { params::stereo,    { "Linked", "Unlinked", "Mid", "Side" }, 0 },
        { params::key,       { "Internal", "External" }, 0 },
        { params::listen,    { "Output", "Removed", "Ess removed", "Harsh removed", "Ess trigger", "Harsh trigger" }, 0 },
    };

    const std::vector<Switch> switches {
        { params::speedAuto,      true  },
        { params::bypass,         false },
        { params::harshOn,        false },
        { params::harshSpeedAuto, true  },
    };
}

TEST_CASE ("Parameters: the ID set is frozen", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;

    // PLAN.md, Parameter IDs, in its order. Sessions and automation refer to these: never rename or remove one.
    const juce::StringArray expected {
        "threshold", "thresholdAbs", "range", "detectLow", "detectHigh", "shape",
        "detection", "focus", "speedAuto", "release", "lookahead", "stereo", "key", "keyHpf", "output",
        "bypass", "listen", "harshOn", "harshThreshold", "harshThresholdAbs", "harshRange", "harshFreq",
        "harshWidth", "harshSpeedAuto", "harshRelease"
    };

    juce::StringArray actual;
    for (auto* param : p.getParameters())
        actual.add (idOf (param));

    CHECK (actual == expected);
    CHECK (params::allParameterIds() == expected);

    for (auto* param : p.getParameters())
        CHECK (dynamic_cast<juce::RangedAudioParameter*> (param)->getVersionHint() == 1);

    // Every parameter is covered by exactly one table below.
    CHECK (continuous.size() + choices.size() + switches.size() == (size_t) expected.size());
}

TEST_CASE ("Parameters: there is no Mix and no latency parameter", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    CHECK (p.getState().getParameter ("mix") == nullptr);
    CHECK (p.getState().getParameter ("latencyMode") == nullptr);
    CHECK (p.getState().getParameter ("latency") == nullptr);
}

TEST_CASE ("Parameters: presets store the sound, not Bypass or Listen", "[parameters]")
{
    const auto& ids = params::soundParameterIds();
    CHECK_FALSE (ids.contains (params::bypass));
    CHECK_FALSE (ids.contains (params::listen));
    CHECK (ids.size() == params::allParameterIds().size() - 2);

    for (const auto& id : params::allParameterIds())
        if (id != params::bypass && id != params::listen)
            CHECK (ids.contains (id));
}

TEST_CASE ("Parameters: ranges, defaults and units", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;

    for (const auto& c : continuous)
    {
        INFO (c.id);
        auto* rp = dynamic_cast<juce::AudioParameterFloat*> (&parameter (p, c.id));
        REQUIRE (rp != nullptr);
        CHECK (juce::exactlyEqual (rp->range.start, c.min));
        CHECK (juce::exactlyEqual (rp->range.end, c.max));
        CHECK_THAT (defaultOf (p, c.id), WithinAbs (c.defaultValue, 1.0e-3));
        CHECK (rp->getLabel() == c.label);
        CHECK (juce::exactlyEqual (rp->convertFrom0to1 (0.0f), c.min));
        CHECK (juce::exactlyEqual (rp->convertFrom0to1 (1.0f), c.max));
    }

    for (const auto& c : choices)
    {
        INFO (c.id);
        auto* rp = dynamic_cast<juce::AudioParameterChoice*> (&parameter (p, c.id));
        REQUIRE (rp != nullptr);
        CHECK (rp->choices == c.names);
        CHECK (rp->getIndex() == c.defaultIndex);
    }

    for (const auto& s : switches)
    {
        INFO (s.id);
        auto* rp = dynamic_cast<juce::AudioParameterBool*> (&parameter (p, s.id));
        REQUIRE (rp != nullptr);
        CHECK (rp->get() == s.defaultValue);
    }

    // The enums the DSP reads are the choices' indices.
    CHECK (params::shapeNames().size() == numShapes);
    CHECK (params::detectionNames().size() == numDetections);
    CHECK (params::stereoModeNames().size() == numStereoModes);
    CHECK (params::keySourceNames().size() == numKeySources);
    CHECK (params::listenNames().size() == numListenModes);
    CHECK (params::shapeNames()[(int) Shape::spectral] == "Spectral");
    CHECK (params::detectionNames()[(int) Detection::allround] == "Allround");
    CHECK (params::stereoModeNames()[(int) StereoMode::side] == "Side");
    CHECK (params::keySourceNames()[(int) KeySource::external] == "External");
    CHECK (params::listenNames()[(int) Listen::harshTrigger] == "Harsh trigger");
}

TEST_CASE ("Parameters: frequencies and releases travel equally per octave", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;

    for (const auto* id : { params::detectLow, params::detectHigh, params::keyHpf, params::harshFreq,
                            params::release, params::harshRelease })
    {
        INFO (id);
        auto& rp = parameter (p, id);
        const auto range = rp.getNormalisableRange();
        const auto ratio = range.end / range.start;

        for (const auto x : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f })
        {
            const auto expected = range.start * std::pow (ratio, x);
            CHECK_THAT (rp.convertFrom0to1 (x), WithinRel (expected, 1.0e-5f));
            CHECK_THAT (rp.convertTo0to1 (expected), WithinAbs (x, 1.0e-5));
        }

        CHECK (juce::exactlyEqual (rp.convertTo0to1 (range.start), 0.0f));
        CHECK_THAT (rp.convertTo0to1 (range.end), WithinAbs (1.0, 1.0e-6));
        // Out-of-range values clamp, never NaN.
        CHECK (juce::exactlyEqual (rp.convertTo0to1 (range.start * 0.5f), 0.0f));
        CHECK_THAT (rp.convertTo0to1 (range.end * 2.0f), WithinAbs (1.0, 1.0e-6));
    }

    // The default Detect band sits where the plan puts it on the knob's travel.
    CHECK_THAT (parameter (p, params::detectLow).getDefaultValue(),
                WithinAbs (std::log (4500.0 / 1500.0) / std::log (20000.0 / 1500.0), 1.0e-5));
}

TEST_CASE ("Parameters: read-outs and typed values", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;

    CHECK (textAt (p, params::threshold, 6.0f) == "+6.0 dB");
    CHECK (textAt (p, params::threshold, 0.0f) == "0.0 dB");
    CHECK (textAt (p, params::threshold, -17.6f) == "-17.6 dB");
    CHECK (textAt (p, params::threshold, 24.0f) == "+24.0 dB");
    CHECK (textAt (p, params::thresholdAbs, -30.0f) == "-30.0 dBFS");
    CHECK (textAt (p, params::range, 8.0f) == "8.0 dB");
    CHECK (textAt (p, params::harshRange, 4.0f) == "4.0 dB");
    CHECK (textAt (p, params::detectLow, 4500.0f) == "4.50 kHz");
    CHECK (textAt (p, params::detectHigh, 12000.0f) == "12.0 kHz");
    CHECK (textAt (p, params::focus, 70.0f) == "70 %");
    CHECK (textAt (p, params::release, 40.0f) == "40 ms");
    CHECK (textAt (p, params::release, 5.0f) == "5.0 ms");
    CHECK (textAt (p, params::lookahead, 5.0f) == "5.0 ms");
    CHECK (textAt (p, params::keyHpf, 20.0f) == "Off");
    CHECK (textAt (p, params::keyHpf, 120.0f) == "120 Hz");
    CHECK (textAt (p, params::output, -3.5f) == "-3.5 dB");
    CHECK (textAt (p, params::harshFreq, 3200.0f) == "3.20 kHz");
    CHECK (textAt (p, params::harshWidth, 1.0f) == "1.00 oct");
    CHECK (textAt (p, params::speedAuto, 1.0f) == "Auto");
    CHECK (textAt (p, params::speedAuto, 0.0f) == "Manual");
    CHECK (textAt (p, params::harshOn, 0.0f) == "Off");
    CHECK (textAt (p, params::shape, 1.0f) == "Spectral");

    CHECK_THAT (valueForText (p, params::detectLow, "5k"), WithinRel (5000.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::detectLow, "5.5 kHz"), WithinRel (5500.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::detectHigh, "9000"), WithinRel (9000.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::threshold, "+9 dB"), WithinAbs (9.0, 1.0e-4));
    CHECK_THAT (valueForText (p, params::threshold, "-20"), WithinAbs (-20.0, 1.0e-4));
    CHECK_THAT (valueForText (p, params::thresholdAbs, "-42"), WithinAbs (-42.0, 1.0e-4));
    CHECK_THAT (valueForText (p, params::keyHpf, "off"), WithinAbs (20.0, 1.0e-3));
    CHECK_THAT (valueForText (p, params::keyHpf, "Off"), WithinAbs (20.0, 1.0e-3));
    CHECK_THAT (valueForText (p, params::release, "120 ms"), WithinRel (120.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::focus, "35 %"), WithinAbs (35.0, 1.0e-4));
    CHECK_THAT (valueForText (p, params::detectHigh, "99k"), WithinAbs (20000.0, 1.0e-2));   // clamped
    CHECK_THAT (valueForText (p, params::speedAuto, "Manual"), WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (valueForText (p, params::speedAuto, "Auto"), WithinAbs (1.0, 1.0e-6));

    // The minus sign the docs print (U+2212), and a decimal comma.
    const auto minus = juce::String::charToString ((juce::juce_wchar) 0x2212);
    CHECK_THAT (valueForText (p, params::output, minus + "6 dB"), WithinAbs (-6.0, 1.0e-4));
    CHECK_THAT (valueForText (p, params::thresholdAbs, minus + "40"), WithinAbs (-40.0, 1.0e-4));
    CHECK_THAT (valueForText (p, params::detectLow, "4,5 kHz"), WithinRel (4500.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::detectLow, "4,5k"), WithinRel (4500.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::harshWidth, "0,5"), WithinAbs (0.5, 1.0e-4));
    CHECK_THAT (valueForText (p, params::output, "-0,25 dB"), WithinAbs (-0.25, 1.0e-4));

    // Text that does not parse, or could be misread, leaves the value where it is.
    setParam (p, params::detectLow, 6000.0f);
    for (const auto* text : { "bright", "", "k", "4,500 Hz", "4.5.1k", "5-3k", "1e4", "-", "+" })
    {
        INFO ("'" << text << "'");
        CHECK_THAT (valueForText (p, params::detectLow, text), WithinRel (6000.0f, 1.0e-4f));
    }
    setParam (p, params::range, 11.0f);
    CHECK_THAT (valueForText (p, params::range, "lots"), WithinAbs (11.0, 1.0e-3));
    setParam (p, params::output, 3.0f);
    for (const auto& text : { juce::String ("-"), minus, juce::String ("+ dB"), juce::String ("6 - 2") })
    {
        INFO ("'" << text << "'");
        CHECK_THAT (valueForText (p, params::output, text), WithinAbs (3.0, 1.0e-4));
    }
}

TEST_CASE ("Parameters: every read-out parses back to itself", "[parameters]")
{
    // A dense sweep: rounding boundaries (9.996 kHz, 9.95 ms, the first Key HPF step above Off) are
    // a few thousandths of the travel wide.
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    constexpr int steps = 20000;

    for (const auto& c : continuous)
    {
        auto& rp = parameter (p, c.id);
        auto mismatches = 0;

        for (int k = 0; k <= steps; ++k)
        {
            const auto x = (float) k / (float) steps;
            const auto text = rp.getText (x, 32);
            const auto back = rp.getText (rp.getValueForText (text), 32);

            if (back != text && ++mismatches <= 5)
                FAIL_CHECK (c.id << " at " << rp.convertFrom0to1 (x) << ": '" << text << "' reads back as '" << back << "'");
        }

        INFO (c.id);
        CHECK (mismatches == 0);
    }
}

TEST_CASE ("Parameters: a read-out of 0 dB means exactly 0 dB", "[parameters]")
{
    // The chain leaves the samples untouched only at Output 0 dB exactly; a near-zero gain must say so.
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    auto& output = parameter (p, params::output);

    CHECK (textAt (p, params::output, 0.0f) == "0.0 dB");
    CHECK (textAt (p, params::output, -0.04f) == "-0.04 dB");
    CHECK (textAt (p, params::output, 0.01f) == "+0.01 dB");
    CHECK (textAt (p, params::output, 0.05f) == "+0.1 dB");
    CHECK (textAt (p, params::range, 0.02f) == "0.02 dB");

    for (int k = 0; k <= 20000; ++k)
    {
        const auto x = (float) k / 20000.0f;
        if (output.getText (x, 32) == "0.0 dB")
        {
            INFO ("x = " << x);
            CHECK (juce::exactlyEqual (output.convertFrom0to1 (x), 0.0f));
        }
    }
}

TEST_CASE ("Parameters: Key HPF is Off exactly where it reads Off", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    auto& keyHpf = parameter (p, params::keyHpf);

    CHECK (textAt (p, params::keyHpf, 20.4f) == "Off");
    CHECK (textAt (p, params::keyHpf, 20.6f) == "21 Hz");
    CHECK_THAT (valueForText (p, params::keyHpf, "20 Hz"), WithinAbs (20.0, 1.0e-3));
    CHECK (params::isKeyHpfOff (valueForText (p, params::keyHpf, "20 Hz")));
    CHECK_FALSE (params::isKeyHpfOff (valueForText (p, params::keyHpf, "21 Hz")));

    for (int k = 0; k <= 2000; ++k)
    {
        const auto x = (float) k / 20000.0f;
        INFO ("x = " << x);
        CHECK ((keyHpf.getText (x, 32) == "Off") == params::isKeyHpfOff (keyHpf.convertFrom0to1 (x)));
    }
}
