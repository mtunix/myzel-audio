#include "ui/MainPanel.h"

#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

namespace umber::ui
{

namespace
{
    // Meter calibration: the level that reads 0 VU in each mode.
    constexpr float outputReferenceDb = -18.0f;
    constexpr float driveReferenceDb = -12.0f;

    const char* const modelTips[] = {
        "Reel: tube record amp into magnetic tape. Soft compression, head bump, rounded top.",
        "Desk: console line amp with transformers. Weighty low end that saturates first.",
        "Class A: single-ended discrete transistor stage. Forward, bright, rich in even harmonics.",
        "Valve: triode preamp stage. Warm and dynamic, with a grid-conduction bias shift.",
        "Fuzz: overdriven pentode / two-stage clipper. Dense, aggressive, hard-edged.",
    };

    juce::RangedAudioParameter& parameter (UmberProcessor& p, const char* id)
    {
        auto* param = p.getState().getParameter (id);
        jassert (param != nullptr);
        return *param;
    }

    float norm (juce::RangedAudioParameter& p, float value)
    {
        return p.convertTo0to1 (value);
    }

    juce::Rectangle<int> snapped (juce::Rectangle<float> r)
    {
        // Positions on a 4-unit grid map to whole device pixels at every menu scale (75-200 %).
        auto snap = [] (float v) { return (int) (std::round (v / 4.0f) * 4.0f); };
        const auto x = snap (r.getX()), y = snap (r.getY());
        return { x, y, snap (r.getRight()) - x, snap (r.getBottom()) - y };
    }
}

MainPanel::MainPanel (UmberProcessor& p)
    : processor (p),
      presets (p),
      ab (p, presets),
      header (p, presets, ab, dialog),
      vblank (this, [this] (double t) { animateFrame (t); })
{
    setOpaque (true);
    setSize (metrics::baseWidth, metrics::baseHeight);
    setWantsKeyboardFocus (false);

    buildControls();
    addAndMakeVisible (meter);
    addAndMakeVisible (header);
    addChildComponent (bubble);
    addChildComponent (dialog);

    header.getScale = [this] { return getScale != nullptr ? getScale() : 1.0f; };
    header.onScaleChosen = [this] (float s)
    {
        if (onScaleChosen != nullptr)
            onScaleChosen (s);
    };

    meter.onModeChanged = [this] (NeedleMeter::Mode m)
    {
        processor.getUiState().setProperty (uistate::meterMode, (int) m, nullptr);
    };

    presets.onChange = [this] { header.refresh(); };

    processor.getState().state.addListener (this);
    applyUiState();

    // Lamps start in their final state (no fade-in when the editor opens).
    for (auto& k : modelKeys)
        k->snapLamp();

    slamKey.snapLamp();
    weightKey.snapLamp();
    autoKey.snapLamp();
    deltaKey.snapLamp();
    updateWeightDimming();
}

MainPanel::~MainPanel()
{
    processor.getState().state.removeListener (this);
}

void MainPanel::buildControls()
{
    auto* um = &processor.getUndoManager();

    auto& driveP   = parameter (processor, params::drive);
    auto& toneP    = parameter (processor, params::tone);
    auto& lowCutP  = parameter (processor, params::lowCut);
    auto& highCutP = parameter (processor, params::highCut);
    auto& mixP     = parameter (processor, params::mix);
    auto& outputP  = parameter (processor, params::output);
    auto& stereoP  = parameter (processor, params::stereoMode);
    auto& emphP    = parameter (processor, params::emphasis);
    auto& biasP    = parameter (processor, params::bias);

    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");

    struct Def
    {
        juce::RangedAudioParameter* param;
        juce::Point<float> centre;
        KnobStyle style;
        const char* tip;
    };

    std::vector<Def> defs;

    {
        KnobStyle s;
        s.title = "DRIVE";
        s.diameter = layout::driveDiameter;
        s.hero = true;
        s.ticks = 21;
        s.dragPixelsForFullRange = 320.0f;

        for (int i = 0; i <= 10; ++i)
            s.marks.push_back ({ norm (driveP, (float) i), juce::String (i), true });

        defs.push_back ({ &driveP, layout::driveCentre(), s,
                          "Drive: how hard the circuit is pushed (-6 dB to +36 dB). Drag, Shift-drag for fine, double-click to reset." });
    }
    {
        KnobStyle s;
        s.title = "TONE";
        s.diameter = layout::smallKnobDiameter;
        s.centreDetent = true;
        s.marks = { { 0.0f, "DARK", true }, { 0.5f, "FLAT", false }, { 1.0f, "BRIGHT", true } };
        defs.push_back ({ &toneP, layout::toneCentre(), s, "Tone: tilt EQ around 800 Hz, darker or brighter. Sticks at Flat." });
    }
    {
        KnobStyle s;
        s.title = "LOW CUT";
        s.diameter = layout::smallKnobDiameter;
        s.marks = { { 0.0f, "OFF", true }, { norm (lowCutP, 100.0f), "100", false }, { 1.0f, "500", true } };
        defs.push_back ({ &lowCutP, layout::lowCutCentre(), s, "Low Cut: 12 dB/oct high-pass before the circuit." });
    }
    {
        KnobStyle s;
        s.title = "HIGH CUT";
        s.diameter = layout::smallKnobDiameter;
        s.marks = { { 0.0f, "2k", true }, { norm (highCutP, 6000.0f), "6k", false }, { 1.0f, "OFF", true } };
        defs.push_back ({ &highCutP, layout::highCutCentre(), s, "High Cut: 12 dB/oct low-pass after the circuit." });
    }
    {
        KnobStyle s;
        s.title = "MIX";
        s.diameter = layout::smallKnobDiameter;
        s.marks = { { 0.0f, "DRY", true }, { 0.5f, "50", false }, { 1.0f, "WET", true } };
        defs.push_back ({ &mixP, layout::mixCentre(), s, "Mix: blend of the dry (latency-aligned) and saturated signal." });
    }
    {
        KnobStyle s;
        s.title = "OUTPUT";
        s.diameter = layout::smallKnobDiameter;
        s.marks = { { 0.0f, minus + "24", true }, { 0.5f, "0", false }, { 1.0f, "+24", true } };
        defs.push_back ({ &outputP, layout::outputCentre(), s, "Output: level trim after the circuit." });
    }
    {
        KnobStyle s;
        s.title = "STEREO";
        s.diameter = layout::smallKnobDiameter;
        s.steps = 3;
        s.ticks = 3;
        s.dragPixelsForFullRange = 120.0f;
        s.marks = { { 0.0f, "L/R", true }, { 0.5f, "MID", true }, { 1.0f, "SIDE", true } };
        defs.push_back ({ &stereoP, layout::stereoCentre(), s,
                          "Stereo: L/R saturates left and right. Mid saturates only the centre, Side only the sides; the other half stays clean." });
    }
    {
        KnobStyle s;
        s.title = "EMPHASIS";
        s.diameter = layout::smallKnobDiameter;
        s.centreDetent = true;
        s.marks = { { 0.0f, "LOWS", true }, { 0.5f, "FLAT", false }, { 1.0f, "HIGHS", true } };
        defs.push_back ({ &emphP, layout::emphasisCentre(), s,
                          "Emphasis: tilts the signal into the circuit and back out again (up to 6 dB per side around 1 kHz). "
                          "The tone stays the same; it only changes which frequencies saturate first." });
    }
    {
        KnobStyle s;
        s.title = "BIAS";
        s.diameter = layout::smallKnobDiameter;
        s.centreDetent = true;
        s.marks = { { 0.0f, "COLD", true }, { 0.5f, "0", false }, { 1.0f, "HOT", true } };
        defs.push_back ({ &biasP, layout::biasCentre(), s,
                          "Bias: moves the circuit's operating point, like turning its bias trim. Each circuit answers in its own way: "
                          "the balance of even and odd harmonics changes, and so does how early and how hard it clips." });
    }

    artwork.knobs.clear();

    for (auto& d : defs)
    {
        artwork.knobs.push_back ({ d.centre, d.style });

        auto knob = std::make_unique<Knob> (*d.param, um, d.style);
        knob->setValueBubble (&bubble);
        knob->setTooltip (d.tip);
        knob->setBounds (snapped (layout::knobBounds (d.centre, d.style.diameter)));
        addAndMakeVisible (*knob);

        knobs.push_back (std::move (knob));
    }

    // Model selector.
    auto& modelP = parameter (processor, params::model);
    const auto names = params::modelNames();

    for (int i = 0; i < (int) modelKeys.size(); ++i)
    {
        auto key = std::make_unique<LampButton> (names[i].toUpperCase(), LampButton::Shape::square);
        key->setKeyBounds (layout::modelKey (i));
        key->setTooltip (modelTips[i]);
        key->setTitle (names[i] + " model");
        key->setRadioGroupId (1, juce::dontSendNotification);
        key->setClickingTogglesState (false);
        key->onClick = [this, i] { modelAttachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (*key);
        modelKeys[(size_t) i] = std::move (key);
    }

    modelAttachment = std::make_unique<juce::ParameterAttachment> (modelP, [this] (float v)
    {
        const auto index = juce::roundToInt (v);

        for (int i = 0; i < (int) modelKeys.size(); ++i)
            modelKeys[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);
    }, um);
    modelAttachment->sendInitialUpdate();

    // Toggles.
    auto bindToggle = [this, um] (LampButton& b, const char* id, juce::Rectangle<float> key, const char* tip)
    {
        b.setKeyBounds (key);
        b.setClickingTogglesState (true);
        b.setTooltip (tip);
        addAndMakeVisible (b);
        buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (parameter (processor, id), b, um));
    };

    bindToggle (slamKey, params::slam, layout::slam(), "Slam: +20 dB extra drive into the circuit.");
    bindToggle (weightKey, params::weight, layout::weightKey(), "Weight: resonant bump at the Low Cut frequency (needs Low Cut on).");
    bindToggle (autoKey, params::autoGain, layout::autoKey(), "Auto: output compensation that keeps the loudness steady as you drive.");
    bindToggle (deltaKey, params::delta, layout::deltaKey(), "Delta: listen to only what the circuit adds (the processed signal minus the dry).");

    meter.setBounds (snapped (layout::meter()));

    // Recesses the keys sit in.
    artwork.wells.clear();
    artwork.wells.push_back (layout::modelKey (0).getUnion (layout::modelKey (4)).expanded (8.0f));
    artwork.wells.push_back (layout::weightKey().expanded (8.0f));
    artwork.wells.push_back (layout::autoKey().expanded (8.0f));
    artwork.wells.push_back (layout::deltaKey().expanded (8.0f));
    artwork.wells.push_back (layout::slam().expanded (7.0f));
    artwork.legends.clear();
    artwork.legends.push_back ({ "CIRCUIT", juce::Rectangle<float> (200.0f, 14.0f).withCentre ({ layout::centreX, layout::modelKey (0).getY() - 22.0f }) });
}

Knob* MainPanel::findKnob (const juce::String& parameterId) const
{
    for (const auto& k : knobs)
        if (k->getParameter().getParameterID() == parameterId)
            return k.get();

    return nullptr;
}

void MainPanel::valueTreeRedirected (juce::ValueTree&)
{
    // The host restored a session while the editor is open.
    applyUiState();
    header.refresh();
}

void MainPanel::applyUiState()
{
    const auto ui = processor.getUiState();
    const auto mode = juce::jlimit (0, 1, (int) ui.getProperty (uistate::meterMode, 0));

    // Avoid writing back while restoring.
    auto callback = std::move (meter.onModeChanged);
    meter.setMode ((NeedleMeter::Mode) mode);
    meter.onModeChanged = std::move (callback);
}

void MainPanel::updateWeightDimming()
{
    const auto lowCut = processor.getState().getRawParameterValue (params::lowCut)->load (std::memory_order_relaxed);
    weightKey.setDimmed (params::isLowCutOff (lowCut));
}

void MainPanel::animateFrame (double timeSeconds)
{
    const auto dt = lastFrameTime < 0.0 ? (1.0f / 60.0f)
                                        : (float) juce::jlimit (0.0, 0.1, timeSeconds - lastFrameTime);
    lastFrameTime = timeSeconds;

    auto& meters = processor.getMeters();
    const auto driveRms = meters.driveRms.load (std::memory_order_relaxed);
    const auto outputRms = meters.outputRms.load (std::memory_order_relaxed);
    const auto outputPeak = meters.outputPeak.exchange (0.0f, std::memory_order_relaxed);

    for (auto& key : modelKeys)
        key->tick (dt);

    slamKey.tick (dt);
    weightKey.tick (dt);
    autoKey.tick (dt);
    deltaKey.tick (dt);

    const auto isOutput = meter.getMode() == NeedleMeter::Mode::output;
    const auto level = isOutput ? outputRms / juce::Decibels::decibelsToGain (outputReferenceDb)
                                : driveRms / juce::Decibels::decibelsToGain (driveReferenceDb);
    meter.tick (dt, level, outputPeak >= 0.989f);

    if (++framesSinceRefresh >= 6)
    {
        framesSinceRefresh = 0;
        updateWeightDimming();
        header.refresh();
    }
}

//==============================================================================
void MainPanel::resized()
{
    header.setBounds (layout::header().toNearestInt());
    dialog.setBounds (getLocalBounds());
    panelLayer.invalidate();
}

void MainPanel::paint (juce::Graphics& g)
{
    panelLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> area, float scale)
    {
        const auto panel = area.withTrimmedTop (metrics::headerHeight);
        renderPanelArtwork (lg, panel, artwork, *fonts, scale);

        // The header rail casts a short shadow onto the panel.
        lg.setGradientFill (juce::ColourGradient (juce::Colour (0x90000000), 0.0f, panel.getY(),
                                                  juce::Colour (0x00000000), 0.0f, panel.getY() + 9.0f, false));
        lg.fillRect (panel.withHeight (9.0f));
    });
}

} // namespace umber::ui
