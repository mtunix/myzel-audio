#include "support/ProcessorHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace basalt;
using namespace basalt::testing;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// Everything here is frozen with the IDs (PLAN.md, Parameter IDs): hosts store automation as
// normalised values and sessions as plain ones, so a range, mapping, default or choice list that
// moves changes every saved session. The expected values are written out, never read from the
// layout, so that any change fails here.

namespace
{
    juce::RangedAudioParameter& parameter (BasaltProcessor& p, const char* id)
    {
        auto* rp = p.getState().getParameter (id);
        REQUIRE (rp != nullptr);
        return *rp;
    }

    float defaultOf (BasaltProcessor& p, const char* id)
    {
        auto& rp = parameter (p, id);
        return rp.convertFrom0to1 (rp.getDefaultValue());
    }

    juce::String textAt (BasaltProcessor& p, const char* id, float plain)
    {
        auto& rp = parameter (p, id);
        return rp.getText (rp.convertTo0to1 (plain), 32);
    }

    float valueForText (BasaltProcessor& p, const char* id, const juce::String& text)
    {
        auto& rp = parameter (p, id);
        return rp.convertFrom0to1 (rp.getValueForText (text));
    }

    juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }

    enum class Mapping
    {
        linear,     // linear in its unit, 0.01 steps
        log,        // equal travel per doubling
        ratio       // 1 … 20 log, then the step to ∞
    };

    struct Continuous
    {
        const char* id;
        float min, max, defaultValue;
        const char* label;
        Mapping mapping;
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
        const char* offText;
        const char* onText;
    };

    // PLAN.md, Controls and Frozen with the IDs (Ratio's ∞ stored as 21).
    const std::vector<Continuous> continuous {
        { params::threshold,   -60.0f,  0.0f,     -18.0f,   "dBFS", Mapping::linear },
        { params::ratio,       1.0f,    21.0f,    4.0f,     "",     Mapping::ratio  },
        { params::knee,        0.0f,    24.0f,    6.0f,     "dB",   Mapping::linear },
        { params::attack,      0.02f,   300.0f,   10.0f,    "ms",   Mapping::log    },
        { params::release,     5.0f,    5000.0f,  150.0f,   "ms",   Mapping::log    },
        { params::range,       0.0f,    40.0f,    40.0f,    "dB",   Mapping::linear },
        { params::floor,       -90.0f,  -30.0f,   -70.0f,   "dBFS", Mapping::linear },
        { params::gain,        -24.0f,  24.0f,    0.0f,     "dB",   Mapping::linear },
        { params::mix,         0.0f,    100.0f,   100.0f,   "%",    Mapping::linear },
        { params::drive,       0.0f,    24.0f,    6.0f,     "dB",   Mapping::linear },
        { params::target,      1.0f,    12.0f,    3.0f,     "dB",   Mapping::linear },
        { params::lookahead,   0.0f,    10.0f,    0.0f,     "ms",   Mapping::linear },
        { params::link,        0.0f,    100.0f,   100.0f,   "%",    Mapping::linear },
        { params::keyHpf,      20.0f,   2000.0f,  20.0f,    "Hz",   Mapping::log    },
        { params::keyLpf,      2000.0f, 20000.0f, 20000.0f, "Hz",   Mapping::log    },
        { params::keyBellFreq, 40.0f,   16000.0f, 1000.0f,  "Hz",   Mapping::log    },
        { params::keyBellGain, -18.0f,  18.0f,    0.0f,     "dB",   Mapping::linear },
        { params::keyBellQ,    0.3f,    6.0f,     1.0f,     "",     Mapping::log    },
        { params::output,      -24.0f,  24.0f,    0.0f,     "dB",   Mapping::linear },
    };

    const std::vector<Choice> choices {
        { params::model,       { "Clean", "VCA", "FET", "Opto", "Vari-mu", "Upward", "Diode bridge", "PWM", "Pumping" }, 0 },
        { params::vcaDetector, { "Peak", "RMS" }, 0 },
        { params::character,   { "Off", "Warm", "Iron", "Edge" }, 0 },
        { params::stereo,      { "Linked", "Unlinked", "Mid", "Side", "Mid/Side" }, 0 },
        { params::key,         { "Internal", "External" }, 0 },
        { params::listen,      { "Output", "Key", "Delta" }, 0 },
    };

    const std::vector<Switch> switches {
        { params::autoRelease, true,  "Off", "On"   },
        { params::autoGain,    false, "Off", "On"   },
        { params::charPost,    true,  "Pre", "Post" },
        { params::driveLink,   false, "Off", "On"   },
        { params::bypass,      false, "Off", "On"   },
    };

    /** Ratio's knob position for a finite ratio: 1 … 20 over the first 90 % of the travel. */
    double ratioPosition (double ratio) { return 0.9 * std::log (ratio) / std::log (20.0); }
}

TEST_CASE ("Parameters: the ID set is frozen, in the plan's order", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;

    // PLAN.md, Parameter IDs, in its order. Sessions and automation refer to these: never rename or remove one.
    const juce::StringArray expected {
        "model", "vcaDetector", "threshold", "ratio", "knee", "attack", "release", "autoRelease", "range",
        "floor", "gain", "autoGain", "mix", "character", "drive", "charPost", "driveLink", "target",
        "lookahead", "stereo", "link", "key", "keyHpf", "keyLpf", "keyBellFreq", "keyBellGain", "keyBellQ",
        "listen", "output", "bypass"
    };

    juce::StringArray actual;
    for (auto* param : p.getParameters())
        actual.add (idOf (param));

    CHECK (actual == expected);
    CHECK (params::allParameterIds() == expected);

    for (auto* param : p.getParameters())
        CHECK (dynamic_cast<juce::RangedAudioParameter*> (param)->getVersionHint() == 1);

    // Every parameter is covered by exactly one table above.
    CHECK (continuous.size() + choices.size() + switches.size() == (size_t) expected.size());

    // Saved in sessions and preset files.
    CHECK (juce::String (params::stateTag) == "BasaltState");
    CHECK (juce::String (params::presetTag) == "BasaltPreset");
    CHECK (p.getState().state.getType().toString() == "BasaltState");
}

TEST_CASE ("Parameters: no latency parameter, and Learn is a button", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    for (const auto* id : { "latencyMode", "latency", "learn" })
    {
        INFO (id);
        CHECK (p.getState().getParameter (id) == nullptr);
    }
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

TEST_CASE ("Parameters: ranges, defaults, units and choice lists", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;

    for (const auto& c : continuous)
    {
        INFO (c.id);
        auto* rp = dynamic_cast<juce::AudioParameterFloat*> (&parameter (p, c.id));
        REQUIRE (rp != nullptr);
        CHECK (juce::exactlyEqual (rp->range.start, c.min));
        CHECK (juce::exactlyEqual (rp->range.end, c.max));
        CHECK_THAT (defaultOf (p, c.id), WithinAbs (c.defaultValue, 1.0e-4 * std::max (1.0f, std::abs (c.defaultValue))));
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
        CHECK (parameter (p, s.id).getText (0.0f, 32) == s.offText);
        CHECK (parameter (p, s.id).getText (1.0f, 32) == s.onText);
    }

    // The enums the DSP reads are the choices' indices.
    CHECK (params::modelNames().size() == numModels);
    CHECK (params::vcaDetectorNames().size() == numVcaDetectors);
    CHECK (params::characterNames().size() == numCharacterModes);
    CHECK (params::stereoModeNames().size() == numStereoModes);
    CHECK (params::keySourceNames().size() == numKeySources);
    CHECK (params::listenNames().size() == numListenModes);
    CHECK (params::modelNames()[(int) Model::variMu] == "Vari-mu");
    CHECK (params::modelNames()[(int) Model::upward] == "Upward");
    CHECK (params::modelNames()[(int) Model::pumping] == "Pumping");
    CHECK (params::vcaDetectorNames()[(int) VcaDetector::rms] == "RMS");
    CHECK (params::characterNames()[(int) CharacterMode::edge] == "Edge");
    CHECK (params::stereoModeNames()[(int) StereoMode::midSide] == "Mid/Side");
    CHECK (params::keySourceNames()[(int) KeySource::external] == "External");
    CHECK (params::listenNames()[(int) Listen::delta] == "Delta");
}

TEST_CASE ("Parameters: the identity's values are exact", "[parameters]")
{
    // PLAN.md, Signal flow: the gain path short-circuits on exactly Ratio 1, Gain 0, Output 0 and Mix 100.
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;

    CHECK (juce::exactlyEqual (defaultOf (p, params::gain), 0.0f));
    CHECK (juce::exactlyEqual (defaultOf (p, params::output), 0.0f));
    CHECK (juce::exactlyEqual (defaultOf (p, params::keyBellGain), 0.0f));
    CHECK (juce::exactlyEqual (defaultOf (p, params::mix), 100.0f));
    CHECK (juce::exactlyEqual (defaultOf (p, params::lookahead), 0.0f));

    for (const auto* id : { params::gain, params::output, params::keyBellGain })
    {
        INFO (id);
        CHECK (juce::exactlyEqual (valueForText (p, id, "0"), 0.0f));
        CHECK (juce::exactlyEqual (parameter (p, id).convertFrom0to1 (0.5f), 0.0f));
    }

    CHECK (juce::exactlyEqual (valueForText (p, params::mix, "100"), 100.0f));
    CHECK (juce::exactlyEqual (valueForText (p, params::ratio, "1"), 1.0f));
}

TEST_CASE ("Parameters: everything linear moves in 0.01 steps of its unit", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;

    for (const auto& c : continuous)
    {
        if (c.mapping != Mapping::linear)
            continue;

        INFO (c.id);
        auto& rp = parameter (p, c.id);
        const auto range = rp.getNormalisableRange();
        CHECK (juce::exactlyEqual (range.interval, 0.01f));
        CHECK (juce::exactlyEqual (range.skew, 1.0f));

        for (const auto x : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f })
        {
            const auto expected = std::round ((c.min + x * (c.max - c.min)) * 100.0) / 100.0;
            CHECK_THAT (rp.convertFrom0to1 (x), WithinAbs (expected, 1.0e-4));
        }
    }
}

TEST_CASE ("Parameters: times, frequencies and Q travel equally per doubling", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;

    for (const auto& c : continuous)
    {
        if (c.mapping != Mapping::log)
            continue;

        INFO (c.id);
        auto& rp = parameter (p, c.id);
        const auto ratio = (double) c.max / c.min;

        for (const auto x : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f })
        {
            const auto expected = c.min * std::pow (ratio, (double) x);
            CHECK_THAT (rp.convertFrom0to1 (x), WithinRel (expected, 1.0e-5));
            CHECK_THAT (rp.convertTo0to1 ((float) expected), WithinAbs (x, 1.0e-5));
        }

        // Where the default sits on the travel.
        CHECK_THAT (rp.getDefaultValue(), WithinAbs (std::log (c.defaultValue / c.min) / std::log (ratio), 1.0e-5));

        CHECK (juce::exactlyEqual (rp.convertTo0to1 (c.min), 0.0f));
        CHECK_THAT (rp.convertTo0to1 (c.max), WithinAbs (1.0, 1.0e-6));
        // Out-of-range values clamp, never NaN.
        CHECK (juce::exactlyEqual (rp.convertTo0to1 (c.min * 0.5f), 0.0f));
        CHECK_THAT (rp.convertTo0to1 (c.max * 2.0f), WithinAbs (1.0, 1.0e-6));
    }
}

TEST_CASE ("Parameters: Ratio runs 1 to 20 logarithmically, then one step to infinity", "[parameters][ratio]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    auto& ratio = parameter (p, params::ratio);

    // The finite part: 20^(x / 0.9) over the first 90 % of the travel.
    for (const auto x : { 0.05f, 0.1f, 0.25f, 0.4f, 0.5f, 0.75f, 0.85f, 0.89f })
    {
        INFO ("x = " << x);
        CHECK_THAT (ratio.convertFrom0to1 (x), WithinRel (std::pow (20.0, x / 0.9), 1.0e-5));
    }

    for (const auto r : { 1.5, 2.0, 4.0, 8.0, 10.0, 12.0 })
    {
        INFO ("ratio " << r);
        CHECK_THAT (ratio.convertTo0to1 ((float) r), WithinAbs (ratioPosition (r), 1.0e-6));
    }

    CHECK (juce::exactlyEqual (ratio.convertFrom0to1 (0.0f), 1.0f));
    CHECK (juce::exactlyEqual (ratio.convertFrom0to1 (0.9f), 20.0f));
    CHECK (juce::exactlyEqual (ratio.convertFrom0to1 (1.0f), 21.0f));
    CHECK_THAT (ratio.getDefaultValue(), WithinAbs (ratioPosition (4.0), 1.0e-6));
    CHECK_THAT (ratio.convertTo0to1 (20.0f), WithinAbs (0.9, 1.0e-6));
    CHECK (juce::exactlyEqual (ratio.convertTo0to1 (21.0f), 1.0f));

    // The step snaps: its lower half is 20, its upper half ∞, so automation through it jumps once.
    for (const auto x : { 0.901f, 0.92f, 0.949f })
        CHECK (juce::exactlyEqual (ratio.convertFrom0to1 (x), 20.0f));
    for (const auto x : { 0.951f, 0.97f, 0.999f })
        CHECK (juce::exactlyEqual (ratio.convertFrom0to1 (x), 21.0f));
    // A stored value inside the step (an LV2 host's slider) snaps the same way.
    CHECK (juce::exactlyEqual (ratio.convertFrom0to1 (ratio.convertTo0to1 (20.3f)), 20.0f));
    CHECK (juce::exactlyEqual (ratio.convertFrom0to1 (ratio.convertTo0to1 (20.7f)), 21.0f));

    // What the DSP reads.
    CHECK (params::isRatioInfinite (21.0f));
    CHECK_FALSE (params::isRatioInfinite (20.0f));
    CHECK (juce::exactlyEqual (params::inverseRatio (21.0f), 0.0f));
    CHECK (juce::exactlyEqual (params::inverseRatio (1.0f), 1.0f));
    CHECK (juce::exactlyEqual (params::inverseRatio (4.0f), 0.25f));
    CHECK (juce::exactlyEqual (params::inverseRatio (20.0f), 0.05f));

    // Read-outs and typed values.
    CHECK (textAt (p, params::ratio, 1.0f) == "1.00:1");
    CHECK (textAt (p, params::ratio, 1.006f) == "1.01:1");
    CHECK (textAt (p, params::ratio, 1.003f) == "1.003:1");
    CHECK (textAt (p, params::ratio, 1.0004f) == "1.0004:1");
    CHECK (textAt (p, params::ratio, 4.0f) == "4.00:1");
    CHECK (textAt (p, params::ratio, 12.5f) == "12.5:1");
    CHECK (textAt (p, params::ratio, 20.0f) == "20.0:1");
    CHECK (textAt (p, params::ratio, 21.0f) == utf8 ("\xe2\x88\x9e:1"));
    CHECK_THAT (valueForText (p, params::ratio, "4:1"), WithinAbs (4.0, 1.0e-4));
    CHECK_THAT (valueForText (p, params::ratio, "2.5 : 1"), WithinAbs (2.5, 1.0e-4));
    CHECK_THAT (valueForText (p, params::ratio, "8"), WithinAbs (8.0, 1.0e-4));
    CHECK (juce::exactlyEqual (valueForText (p, params::ratio, "inf"), 21.0f));
    CHECK (juce::exactlyEqual (valueForText (p, params::ratio, utf8 ("\xe2\x88\x9e")), 21.0f));
    CHECK (juce::exactlyEqual (valueForText (p, params::ratio, utf8 ("\xe2\x88\x9e:1")), 21.0f));
    // Above 20, the nearer of 20 and ∞ by slope: 1/30 is nearer 1/20, 1/50 nearer 0.
    CHECK (juce::exactlyEqual (valueForText (p, params::ratio, "30"), 20.0f));
    CHECK (juce::exactlyEqual (valueForText (p, params::ratio, "50:1"), 21.0f));
    setParam (p, params::ratio, 6.0f);
    for (const auto* text : { "steep", "1:4", "4:2", ":1", "" })
    {
        INFO ("'" << text << "'");
        CHECK_THAT (valueForText (p, params::ratio, text), WithinAbs (6.0, 1.0e-4));
    }
}

TEST_CASE ("Parameters: read-outs and typed values", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    const auto micro = utf8 ("\xc2\xb5");

    CHECK (textAt (p, params::threshold, -18.0f) == "-18.0 dBFS");
    CHECK (textAt (p, params::threshold, 0.0f) == "0.0 dBFS");
    CHECK (textAt (p, params::knee, 6.0f) == "6.0 dB");
    CHECK (textAt (p, params::attack, 0.02f) == "20 " + micro + "s");
    CHECK (textAt (p, params::attack, 0.8f) == "800 " + micro + "s");
    CHECK (textAt (p, params::attack, 3.0f) == "3.0 ms");
    CHECK (textAt (p, params::attack, 10.0f) == "10 ms");
    CHECK (textAt (p, params::attack, 300.0f) == "300 ms");
    CHECK (textAt (p, params::release, 150.0f) == "150 ms");
    CHECK (textAt (p, params::release, 1200.0f) == "1.20 s");
    CHECK (textAt (p, params::release, 5000.0f) == "5.00 s");
    CHECK (textAt (p, params::autoRelease, 1.0f) == "On");
    CHECK (textAt (p, params::range, 40.0f) == "40.0 dB");
    CHECK (textAt (p, params::floor, -70.0f) == "-70.0 dBFS");
    CHECK (textAt (p, params::gain, 3.5f) == "+3.5 dB");
    CHECK (textAt (p, params::mix, 100.0f) == "100 %");
    CHECK (textAt (p, params::mix, 99.6f) == "99.6 %");
    CHECK (textAt (p, params::mix, 99.96f) == "99.96 %");
    CHECK (textAt (p, params::mix, 99.4f) == "99 %");
    CHECK (textAt (p, params::mix, 0.0f) == "0 %");
    CHECK (textAt (p, params::mix, 0.3f) == "0.3 %");
    CHECK (textAt (p, params::mix, 0.02f) == "0.02 %");
    CHECK (textAt (p, params::drive, 6.0f) == "6.0 dB");
    CHECK (textAt (p, params::charPost, 1.0f) == "Post");
    CHECK (textAt (p, params::target, 3.0f) == "3.0 dB");
    CHECK (textAt (p, params::lookahead, 5.0f) == "5.00 ms");
    CHECK (textAt (p, params::lookahead, 0.04f) == "0.04 ms");
    CHECK (textAt (p, params::link, 50.0f) == "50 %");
    CHECK (textAt (p, params::keyHpf, 20.0f) == "Off");
    CHECK (textAt (p, params::keyHpf, 120.0f) == "120 Hz");
    CHECK (textAt (p, params::keyLpf, 20000.0f) == "Off");
    CHECK (textAt (p, params::keyLpf, 8000.0f) == "8.00 kHz");
    CHECK (textAt (p, params::keyBellFreq, 1000.0f) == "1.00 kHz");
    CHECK (textAt (p, params::keyBellGain, -6.0f) == "-6.0 dB");
    CHECK (textAt (p, params::keyBellQ, 1.0f) == "1.00");
    CHECK (textAt (p, params::output, -3.5f) == "-3.5 dB");
    CHECK (textAt (p, params::model, 4.0f) == "Vari-mu");
    CHECK (textAt (p, params::stereo, 4.0f) == "Mid/Side");

    CHECK_THAT (valueForText (p, params::attack, "50 us"), WithinRel (0.05f, 1.0e-4f));
    CHECK_THAT (valueForText (p, params::attack, "50 " + micro + "s"), WithinRel (0.05f, 1.0e-4f));
    CHECK_THAT (valueForText (p, params::attack, utf8 ("50 \xce\xbcs")), WithinRel (0.05f, 1.0e-4f));   // Greek mu
    CHECK_THAT (valueForText (p, params::attack, "2.5 ms"), WithinRel (2.5f, 1.0e-4f));
    CHECK_THAT (valueForText (p, params::attack, "30"), WithinRel (30.0f, 1.0e-4f));
    CHECK_THAT (valueForText (p, params::release, "1.5 s"), WithinRel (1500.0f, 1.0e-4f));
    CHECK_THAT (valueForText (p, params::release, "1,5 s"), WithinRel (1500.0f, 1.0e-4f));
    CHECK_THAT (valueForText (p, params::release, "80 ms"), WithinRel (80.0f, 1.0e-4f));
    CHECK_THAT (valueForText (p, params::release, "9 s"), WithinRel (5000.0f, 1.0e-4f));    // clamped
    CHECK_THAT (valueForText (p, params::threshold, "-24"), WithinAbs (-24.0, 1.0e-4));
    CHECK_THAT (valueForText (p, params::keyLpf, "off"), WithinRel (20000.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::keyLpf, "6k"), WithinRel (6000.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::keyHpf, "Off"), WithinRel (20.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::keyBellFreq, "2,5 kHz"), WithinRel (2500.0f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::keyBellQ, "0.7"), WithinRel (0.7f, 1.0e-5f));
    CHECK_THAT (valueForText (p, params::mix, "35 %"), WithinAbs (35.0, 1.0e-4));
    CHECK_THAT (valueForText (p, params::charPost, "Pre"), WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (valueForText (p, params::charPost, "Post"), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (valueForText (p, params::charPost, " post "), WithinAbs (1.0, 1.0e-6));

    // Switches take their own words or on/off, 1/0, true/false; anything else leaves them alone.
    for (const auto& sw : switches)
        for (const auto value : { false, true })
        {
            INFO (sw.id << " at " << (value ? "on" : "off"));
            setParam (p, sw.id, value ? 1.0f : 0.0f);
            for (const auto* text : { "maybe", "", "2", "-1", "yes", "no" })
            {
                INFO ("'" << text << "'");
                CHECK (juce::exactlyEqual (valueForText (p, sw.id, text), value ? 1.0f : 0.0f));
            }
            for (const auto* text : { "on", "1", "TRUE" })
                CHECK (juce::exactlyEqual (valueForText (p, sw.id, text), 1.0f));
            for (const auto* text : { "off", "0", "false" })
                CHECK (juce::exactlyEqual (valueForText (p, sw.id, text), 0.0f));
            CHECK (juce::exactlyEqual (valueForText (p, sw.id, sw.onText), 1.0f));
            CHECK (juce::exactlyEqual (valueForText (p, sw.id, sw.offText), 0.0f));
        }

    // The minus sign the docs print (U+2212).
    const auto minus = juce::String::charToString ((juce::juce_wchar) 0x2212);
    CHECK_THAT (valueForText (p, params::output, minus + "6 dB"), WithinAbs (-6.0, 1.0e-4));

    // Text that does not parse, or could be misread, leaves the value where it is.
    setParam (p, params::keyBellFreq, 3000.0f);
    for (const auto* text : { "bright", "", "k", "4,500 Hz", "4.5.1k", "5-3k", "1e4", "-", "+" })
    {
        INFO ("'" << text << "'");
        CHECK_THAT (valueForText (p, params::keyBellFreq, text), WithinRel (3000.0f, 1.0e-4f));
    }
    setParam (p, params::attack, 2.0f);
    CHECK_THAT (valueForText (p, params::attack, "fast"), WithinRel (2.0f, 1.0e-4f));
}

TEST_CASE ("Parameters: every read-out parses back to itself", "[parameters]")
{
    // A dense sweep: rounding boundaries (999.6 µs, 9.95 ms, 999.5 ms, 9.996 kHz, the first Key
    // filter steps beside Off) are a few thousandths of the travel wide.
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
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

TEST_CASE ("Parameters: a read-out of an identity value means exactly that value", "[parameters]")
{
    // The chain leaves the samples untouched only at exactly 0 dB, Ratio 1 and Mix 100 (PLAN.md,
    // Signal flow); Mix 0 and Link 0 and 100 are exact too. A value near one must say so.
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;

    CHECK (textAt (p, params::output, 0.0f) == "0.0 dB");
    CHECK (textAt (p, params::output, -0.04f) == "-0.04 dB");
    CHECK (textAt (p, params::gain, 0.01f) == "+0.01 dB");
    CHECK (textAt (p, params::gain, 0.06f) == "+0.1 dB");
    CHECK (textAt (p, params::mix, 99.99f) == "99.99 %");
    CHECK (textAt (p, params::link, 0.01f) == "0.01 %");

    struct Exact
    {
        const char* id;
        const char* text;
        float value;
    };

    for (const auto& e : std::vector<Exact> { { params::output, "0.0 dB", 0.0f }, { params::gain, "0.0 dB", 0.0f },
                                              { params::keyBellGain, "0.0 dB", 0.0f }, { params::ratio, "1.00:1", 1.0f },
                                              { params::mix, "100 %", 100.0f }, { params::mix, "0 %", 0.0f },
                                              { params::link, "100 %", 100.0f }, { params::link, "0 %", 0.0f } })
    {
        auto& rp = parameter (p, e.id);
        auto seen = 0;
        for (int k = 0; k <= 20000; ++k)
        {
            const auto x = (float) k / 20000.0f;
            if (rp.getText (x, 32) == e.text)
            {
                ++seen;
                INFO (e.id << " at x = " << x);
                CHECK (juce::exactlyEqual (rp.convertFrom0to1 (x), e.value));
            }
        }
        INFO (e.id << " reads '" << e.text << "' somewhere");
        CHECK (seen > 0);
    }

    // Ratio has no step: the first positions above 1 still read as more than 1.
    auto& ratio = parameter (p, params::ratio);
    for (const auto x : { 1.0e-6f, 1.0e-5f, 1.0e-4f, 1.0e-3f })
    {
        INFO ("x = " << x << ", " << ratio.convertFrom0to1 (x));
        if (ratio.convertFrom0to1 (x) > 1.0f)
            CHECK (ratio.getText (x, 32) != "1.00:1");
    }
}

TEST_CASE ("Parameters: the key filters are Off exactly where they read Off", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    BasaltProcessor p;
    auto& hpf = parameter (p, params::keyHpf);
    auto& lpf = parameter (p, params::keyLpf);

    CHECK (textAt (p, params::keyHpf, 20.4f) == "Off");
    CHECK (textAt (p, params::keyHpf, 20.6f) == "21 Hz");
    CHECK (params::isKeyHpfOff (valueForText (p, params::keyHpf, "20 Hz")));
    CHECK_FALSE (params::isKeyHpfOff (valueForText (p, params::keyHpf, "21 Hz")));

    CHECK (textAt (p, params::keyLpf, 19960.0f) == "Off");
    CHECK (textAt (p, params::keyLpf, 19940.0f) == "19.9 kHz");
    CHECK (params::isKeyLpfOff (valueForText (p, params::keyLpf, "20 kHz")));
    CHECK_FALSE (params::isKeyLpfOff (valueForText (p, params::keyLpf, "19.9 kHz")));

    for (int k = 0; k <= 20000; ++k)
    {
        const auto x = (float) k / 20000.0f;
        INFO ("x = " << x);
        CHECK ((hpf.getText (x, 32) == "Off") == params::isKeyHpfOff (hpf.convertFrom0to1 (x)));
        CHECK ((lpf.getText (x, 32) == "Off") == params::isKeyLpfOff (lpf.convertFrom0to1 (x)));
    }
}

TEST_CASE ("Parameters: the 1.1 models hold their places and run as Clean in v1", "[parameters]")
{
    // PLAN.md, Frozen with the IDs: all nine in the list from M0, the last three built in 1.1.
    for (int i = 0; i < numModels; ++i)
    {
        const auto m = static_cast<Model> (i);
        INFO (params::modelNames()[i]);
        CHECK (isOffered (m) == (i < 6));
        CHECK (runningModel (m) == (i < 6 ? m : Model::clean));
    }
}
