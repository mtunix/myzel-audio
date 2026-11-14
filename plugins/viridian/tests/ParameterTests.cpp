#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace viridian;
using Catch::Matchers::WithinAbs;

namespace
{
    void setParam (ViridianProcessor& p, const char* id, float plainValue)
    {
        auto* rp = p.getState().getParameter (id);
        REQUIRE (rp != nullptr);
        rp->setValueNotifyingHost (rp->convertTo0to1 (plainValue));
    }

    juce::String hostText (ViridianProcessor& p, const char* id)
    {
        auto* rp = p.getState().getParameter (id);
        return rp->getText (rp->getValue(), 32);
    }
}

TEST_CASE ("Parameters: the ID set is frozen", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;

    // Saved sessions and automation refer to these: never rename or remove one.
    const juce::StringArray expected {
        "model",
        "hpfFreq", "lowFreq", "lowGain", "lowQ", "lowShape", "lmFreq", "lmGain", "lmQ",
        "hmFreq", "hmGain", "hmQ", "highFreq", "highGain", "highQ", "highShape", "lpfFreq",
        "alt", "drive", "eqIn", "output", "stereo", "tolerance", "noise", "bypass"
    };

    juce::StringArray actual;
    for (auto* param : p.getParameters())
        actual.add (dynamic_cast<juce::HostedAudioProcessorParameter*> (param)->getParameterID());

    CHECK (actual == expected);

    for (int i = 0; i < numControls; ++i)
        CHECK (juce::String (params::controlId (static_cast<Control> (i))) == expected[i + 1]);

    // Every v1 parameter carries version hint 1; later sections take a higher one.
    for (auto* param : p.getParameters())
        CHECK (dynamic_cast<juce::RangedAudioParameter*> (param)->getVersionHint() == 1);
}

TEST_CASE ("Parameters: there is no Mix", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;
    CHECK (p.getState().getParameter ("mix") == nullptr);
}

TEST_CASE ("Parameters: presets store the sound, not Bypass", "[parameters]")
{
    const auto& ids = params::soundParameterIds();
    CHECK_FALSE (ids.contains (params::bypass));
    CHECK (ids.size() == 24);

    for (const auto* id : params::controlIds)
        CHECK (ids.contains (id));
    for (const auto* id : { params::model, params::alt, params::drive, params::eqIn, params::output,
                            params::stereo, params::tolerance, params::noise })
        CHECK (ids.contains (id));
}

TEST_CASE ("Parameters: defaults are the Inductor, flat, matched and silent", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;
    const auto s = p.readSettings();

    CHECK (s.model == Model::inductor);
    CHECK_FALSE (s.alt);
    CHECK (s.positions == defaultPositions());
    CHECK (juce::exactlyEqual (s.driveDb, 0.0f));
    CHECK (s.eqIn);
    CHECK (juce::exactlyEqual (s.outputDb, 0.0f));
    CHECK (s.stereo == StereoMode::stereo);
    CHECK (juce::exactlyEqual (s.tolerance, 0.0f));
    CHECK (juce::exactlyEqual (s.noise, 0.0f));
    CHECK_FALSE (s.bypass);

    // Exactly the values the slot mapping calls flat on this model.
    CHECK (s.positions == flatPositions (Model::inductor));
}

TEST_CASE ("Parameters: ranges", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;
    auto& state = p.getState();

    auto range = [&] (const char* id) { return state.getParameterRange (id); };

    CHECK (juce::exactlyEqual (range (params::drive).start, -12.0f));
    CHECK (juce::exactlyEqual (range (params::drive).end, 24.0f));
    CHECK (juce::exactlyEqual (range (params::output).start, -24.0f));
    CHECK (juce::exactlyEqual (range (params::output).end, 24.0f));
    CHECK (juce::exactlyEqual (range (params::tolerance).end, 100.0f));
    CHECK (juce::exactlyEqual (range (params::noise).end, 100.0f));

    // The slot controls are shared, normalised knob positions.
    for (const auto* id : params::controlIds)
    {
        INFO (id);
        CHECK (juce::exactlyEqual (range (id).start, 0.0f));
        CHECK (juce::exactlyEqual (range (id).end, 1.0f));
    }

    CHECK (params::modelNames() == juce::StringArray { "Passive", "Inductor", "Proportional", "Console", "Mastering" });
    CHECK (params::stereoModeNames() == juce::StringArray { "Stereo", "Mid", "Side" });
}

TEST_CASE ("Parameters: slot read-outs per model", "[parameters]")
{
    using params::controlText;

    CHECK (controlText (Model::inductor, false, Control::hpfFreq, 0.0f) == "Off");
    CHECK (controlText (Model::inductor, false, Control::hpfFreq, 0.25f) == "50 Hz");
    CHECK (controlText (Model::inductor, false, Control::hpfFreq, 1.0f) == "300 Hz");
    CHECK (controlText (Model::inductor, false, Control::lowFreq, 0.0f) == "35 Hz");
    CHECK (controlText (Model::inductor, false, Control::lowFreq, 1.0f) == "220 Hz");
    CHECK (controlText (Model::inductor, false, Control::lmFreq, 0.4f) == "1.6 kHz");
    CHECK (controlText (Model::inductor, false, Control::lmGain, 0.5f) == "0.0 dB");
    CHECK (controlText (Model::inductor, false, Control::lmGain, 1.0f) == "+18.0 dB");
    CHECK (controlText (Model::inductor, false, Control::highFreq, 0.3f) == "12 kHz");
    CHECK (controlText (Model::inductor, false, Control::hmGain, 0.3f) == "Unused");
    CHECK (controlText (Model::inductor, false, Control::lpfFreq, 0.0f) == "6 kHz");
    CHECK (controlText (Model::inductor, false, Control::lpfFreq, 1.0f) == "Off");

    CHECK (controlText (Model::proportional, false, Control::lowGain, 0.5f) == "0 dB");
    CHECK (controlText (Model::proportional, false, Control::lowGain, 1.0f) == "+12 dB");
    CHECK (controlText (Model::proportional, false, Control::lowGain, 0.0f) == "-12 dB");
    CHECK (controlText (Model::proportional, false, Control::hmFreq, 1.0f) == "12.5 kHz");
    CHECK (controlText (Model::proportional, false, Control::highShape, 1.0f) == "Peak");
    CHECK (controlText (Model::proportional, false, Control::hpfFreq, 0.5f) == "Unused");
    CHECK (controlText (Model::proportional, true, Control::hpfFreq, 0.5f) == "50 Hz");
    CHECK (controlText (Model::proportional, true, Control::lpfFreq, 0.5f) == "15 kHz");

    CHECK (controlText (Model::passive, false, Control::lowGain, 0.75f) == "7.5");
    CHECK (controlText (Model::passive, false, Control::lowFreq, 1.0f) == "100 Hz");
    CHECK (controlText (Model::passive, false, Control::hpfFreq, 0.0f) == "Unused");

    CHECK (controlText (Model::console, false, Control::lowShape, 0.0f) == "Shelf");
    CHECK (controlText (Model::console, false, Control::lowShape, 1.0f) == "Bell");
    CHECK (controlText (Model::console, false, Control::lowQ, 0.5f) == "0.91");
    CHECK (controlText (Model::console, true, Control::lowQ, 0.5f) == "2.12");
    CHECK (controlText (Model::console, false, Control::lowGain, 1.0f) == "+15.2 dB");
    CHECK (controlText (Model::console, true, Control::lowGain, 1.0f) == "+18.0 dB");
    CHECK (controlText (Model::console, false, Control::hpfFreq, 0.0f) == "Off");
    CHECK (controlText (Model::console, false, Control::hpfFreq, 0.01f) == "11 Hz");

    CHECK (controlText (Model::mastering, false, Control::highFreq, 1.0f) == "26.0 kHz");
    CHECK (controlText (Model::mastering, false, Control::lowQ, 0.0f) == "0.40");
}

TEST_CASE ("Parameters: typed values find their position", "[parameters]")
{
    using params::controlPositionFromText;

    auto value = [] (Model m, bool alt, Control c, const char* text)
    {
        const auto p = controlPositionFromText (m, alt, c, text);
        REQUIRE (p >= 0.0f);
        return map (controlInfo (m, alt, c), p);
    };

    CHECK (juce::exactlyEqual (value (Model::inductor, false, Control::lowFreq, "110").value, 110.0f));
    CHECK (juce::exactlyEqual (value (Model::inductor, false, Control::lowFreq, "100 Hz").value, 110.0f));   // nearest detent
    CHECK (juce::exactlyEqual (value (Model::inductor, false, Control::lmFreq, "1.6k").value, 1600.0f));
    CHECK (value (Model::inductor, false, Control::hpfFreq, "off").off);
    CHECK (value (Model::inductor, false, Control::lpfFreq, "Off").off);
    CHECK (juce::exactlyEqual (value (Model::proportional, false, Control::lmGain, "-4 dB").value, -4.0f));
    CHECK (juce::exactlyEqual (value (Model::proportional, false, Control::lmGain, "+5").value, 4.0f));       // between 4 and 6: 4
    CHECK_THAT (value (Model::console, false, Control::lmFreq, "1 kHz").value, WithinAbs (1000.0, 0.05));
    CHECK_THAT (value (Model::mastering, false, Control::lmGain, "-3.5 dB").value, WithinAbs (-3.5, 1.0e-4));
    CHECK_THAT (value (Model::passive, false, Control::lowGain, "6").value, WithinAbs (6.0, 1.0e-5));
    CHECK (juce::exactlyEqual (value (Model::console, false, Control::highShape, "bell").value, 1.0f));

    CHECK (controlPositionFromText (Model::console, false, Control::lmFreq, "loud") < 0.0f);
    CHECK (controlPositionFromText (Model::passive, false, Control::hpfFreq, "100") < 0.0f);   // unused here
    CHECK (controlPositionFromText (Model::inductor, false, Control::highFreq, "10k") < 0.0f); // fixed
}

TEST_CASE ("Parameters: host read-outs follow the model and Alt", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;

    setParam (p, params::lowGain, 1.0f);
    CHECK (hostText (p, params::lowGain) == "+16.0 dB");
    CHECK (hostText (p, params::hpfFreq) == "Off");

    setParam (p, params::model, (float) Model::proportional);
    CHECK (hostText (p, params::lowGain) == "+12 dB");
    CHECK (hostText (p, params::lowShape) == "Shelf");
    CHECK (hostText (p, params::hpfFreq) == "Unused");
    setParam (p, params::alt, 1.0f);
    CHECK (hostText (p, params::hpfFreq) == "50 Hz");

    setParam (p, params::model, (float) Model::passive);
    CHECK (hostText (p, params::lowGain) == "10.0");

    // Typed text goes through the same mapping.
    setParam (p, params::model, (float) Model::inductor);
    auto* lowFreq = p.getState().getParameter (params::lowFreq);
    CHECK (juce::exactlyEqual (lowFreq->getValueForText ("60 Hz"), detentPosition (controlInfo (Model::inductor, false, Control::lowFreq), 1)));
    CHECK (juce::exactlyEqual (lowFreq->getValueForText ("25 %"), 0.25f));
    setParam (p, params::lowFreq, 0.6f);
    CHECK (juce::exactlyEqual (lowFreq->getValueForText ("%"), lowFreq->getValue()));
    CHECK (juce::exactlyEqual (lowFreq->getValueForText ("abc%"), lowFreq->getValue()));

    auto* shape = p.getState().getParameter (params::lowShape);
    CHECK (juce::exactlyEqual (shape->getValueForText ("Bell"), 1.0f));
}

TEST_CASE ("Parameters: a layout reads out through its own Model and Alt in any processor", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    juce::AudioProcessorGraph dummy;   // any processor to own the parameters
    juce::AudioProcessorValueTreeState state (dummy, nullptr, "T", params::createLayout());

    auto* gain = state.getParameter (params::lmGain);
    auto* hpf = state.getParameter (params::hpfFreq);
    CHECK (gain->getText (0.5f, 32) == "0.0 dB");

    auto set = [&] (const char* id, float plainValue)
    {
        auto* p = state.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    };

    set (params::model, (float) Model::proportional);
    CHECK (gain->getText (0.5f, 32) == "0 dB");
    CHECK (hpf->getText (0.5f, 32) == "Unused");
    set (params::alt, 1.0f);
    CHECK (hpf->getText (0.5f, 32) == "50 Hz");
}

TEST_CASE ("Parameters: every read-out parses back and text that does not parse moves nothing", "[parameters]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ViridianProcessor p;

    for (int m = 0; m < numModels; ++m)
        for (const auto altOn : { false, true })
        {
            setParam (p, params::model, (float) m);
            setParam (p, params::alt, altOn ? 1.0f : 0.0f);

            for (int c = 0; c < numControls; ++c)
            {
                const auto control = static_cast<Control> (c);
                const auto& info = controlInfo (static_cast<Model> (m), altOn, control);
                auto* param = p.getState().getParameter (params::controlId (control));
                const auto isSwitch = control == Control::lowShape || control == Control::highShape;

                for (int k = 0; k <= 40; ++k)
                {
                    const auto position = isSwitch ? (k % 2 == 0 ? 0.0f : 1.0f) : (float) k / 40.0f;
                    param->setValueNotifyingHost (position);
                    const auto text = param->getText (param->getValue(), 32);
                    INFO (params::modelNames()[m] << (altOn ? " Alt " : " ") << params::controlId (control)
                                                  << " at " << position << ": \"" << text << "\"");

                    // A dimmed or fixed slot's read-out names no position: the knob stays put.
                    if (info.kind == Kind::dim || info.kind == Kind::fixed)
                        CHECK (juce::exactlyEqual (param->getValueForText (text), param->getValue()));
                    else
                        CHECK (param->getText (param->getValueForText (text), 32) == text);

                    CHECK (juce::exactlyEqual (param->getValueForText ("loud"), param->getValue()));
                }
            }
        }
}
