#include "ui/MainPanel.h"

#include "dsp/NoteValues.h"
#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

namespace mulberry::ui
{

namespace
{
    float norm (juce::RangedAudioParameter& p, float value) { return p.convertTo0to1 (value); }
    juce::String minus() { return juce::String::fromUTF8 ("\xe2\x88\x92"); }

    KnobStyle smallKnob (const juce::String& title)
    {
        KnobStyle s;
        s.title = title;
        s.diameter = layout::smallKnobDiameter;
        return s;
    }

    /** Synced Time (and Right time): a 21-position note switch. */
    KnobStyle noteStyle (const juce::String& title, float diameter, bool hero, const juce::String& badge = {})
    {
        KnobStyle s;
        s.title = title;
        s.diameter = diameter;
        s.hero = hero;
        s.badge = badge;
        s.steps = notes::count;
        s.ticks = notes::count;
        s.dragPixelsForFullRange = 12.0f * (float) notes::count;
        const auto at = [] (int i) { return (float) i / (float) (notes::count - 1); };
        if (hero)
            s.marks = { { at (1), "1/64", true }, { at (7), "1/16", false }, { at (10), "1/8", false },
                        { at (13), "1/4", true }, { at (16), "1/2", false }, { at (19), "1/1", true } };
        else
            s.marks = { { at (1), "1/64", true }, { at (13), "1/4", false }, { at (19), "1/1", true } };
        return s;
    }

    KnobStyle timeStyle (juce::RangedAudioParameter& p)
    {
        KnobStyle s;
        s.title = "TIME";
        s.diameter = layout::timeDiameter;
        s.hero = true;
        s.dragPixelsForFullRange = 320.0f;
        const std::pair<float, const char*> labels[] = { { 1.0f, "1" }, { 10.0f, "10" }, { 50.0f, "50" }, { 100.0f, "100" },
                                                         { 250.0f, "250" }, { 500.0f, "500" }, { 1000.0f, "1s" }, { 4000.0f, "4s" } };
        for (const auto& [ms, text] : labels)
        {
            s.marks.push_back ({ norm (p, ms), text, true });
            s.tickPositions.push_back (norm (p, ms));
        }
        for (const auto ms : { 2.0f, 5.0f, 20.0f, 30.0f, 200.0f, 2000.0f })
            s.tickPositions.push_back (norm (p, ms));
        return s;
    }

    juce::String dot (const char* a, const char* b) { return juce::String (a) + juce::String::fromUTF8 ("\xc2\xb7") + b; }

    KnobStyle characterStyle (Machine m, bool isB)
    {
        auto s = smallKnob (PanelController::characterLegend (m, isB));
        s.badge = isB ? "B" : "A";
        auto ends = [&s] (const juce::String& lo, const juce::String& hi) { s.marks = { { 0.0f, lo, true }, { 1.0f, hi, true } }; };
        auto stepped = [&s] (std::initializer_list<juce::String> labels, bool snap = true)
        {
            const auto n = (int) labels.size();
            int i = 0;
            s.marks.clear();
            for (const auto& l : labels)
            {
                // A stepped control's positions are the centres of equal shares of the travel.
                const auto t = snap ? ((float) i + 0.5f) / (float) n : (float) i / (float) (n - 1);
                s.marks.push_back ({ t, l, i == 0 || i == n - 1 });
                ++i;
            }
            s.ticks = 0;
            for (int k = 0; k < n; ++k)
                s.tickPositions.push_back (snap ? ((float) k + 0.5f) / (float) n : (float) k / (float) (n - 1));
        };

        if (! isB)
        {
            // Wheel and arrows step through a switch's positions; dragging stays continuous.
            s.steps = PanelController::characterSteps (m, false);
            switch (m)
            {
                case Machine::clean:  ends ("FADE", "GLIDE"); break;
                case Machine::rack:   stepped ({ "8", "12", "16" }, false); s.marks[1].position = character::stepCentre (4, 9); break;
                case Machine::bucket: stepped ({ "1K", "2K", "4K", "8K" }); break;
                case Machine::oilCan: ends ("1", "5"); break;
                case Machine::loop:   stepped ({ "1", "2", "3", dot ("1", "2"), dot ("2", "3"), dot ("1", "3"), dot ("1", "2") + juce::String::fromUTF8 ("\xc2\xb7") + "3" }); break;
                case Machine::reel:   stepped ({ "7.5", "15", "30" }); break;
                case Machine::drum:
                    // Fifteen positions: head 1, head 4 (the last single), all four heads.
                    stepped ({ "1", "", "", "4", "", "", "", "", "", "", "", "", "", "", "ALL" });
                    std::erase_if (s.marks, [] (const auto& mark) { return mark.text.isEmpty(); });
                    break;
            }
        }
        else
        {
            switch (m)
            {
                case Machine::clean:  s.centreDetent = true; s.marks = { { 0.0f, minus() + "12", true }, { 0.5f, "0", false }, { 1.0f, "+12", true } }; break;
                case Machine::rack:   ends ("6K", "20K"); break;
                case Machine::bucket: ends ("OFF", "FULL"); break;
                case Machine::oilCan: ends ("NEW", "OLD"); break;
                case Machine::loop:   ends ("NEW", "WORN"); break;
                case Machine::reel:   s.centreDetent = true; s.marks = { { 0.0f, "UNDER", true }, { 0.5f, "NOM", false }, { 1.0f, "OVER", true } }; break;
                case Machine::drum:   ends ("OFF", "FULL"); break;
            }
        }
        return s;
    }

    KnobStyle modeAmountStyle (Mode mode, bool synced, juce::RangedAudioParameter& p)
    {
        if (mode == Mode::dual)
        {
            if (synced)
                return noteStyle ("RIGHT TIME", layout::smallKnobDiameter, false);
            auto s = smallKnob ("RIGHT TIME");
            s.marks = { { 0.0f, "1", true }, { norm (p, 100.0f), "100", false }, { 1.0f, "4s", true } };
            return s;
        }
        if (mode == Mode::pattern)
        {
            auto s = smallKnob ("STEPS");
            s.steps = params::maxSteps;
            s.ticks = params::maxSteps;
            s.dragPixelsForFullRange = 10.0f * (float) params::maxSteps;
            s.marks = { { 0.0f, "1", true }, { norm (p, 4.0f), "4", false }, { norm (p, 8.0f), "8", false }, { 1.0f, "16", true } };
            return s;
        }
        auto s = smallKnob ("WIDTH");
        s.marks = { { 0.0f, "MONO", true }, { 1.0f, "WIDE", true } };
        return s;
    }
}

MainPanel::MainPanel (PanelController& c)
    : controller (c),
      processor (c.getProcessor()),
      drawer (processor, bubble)
{
    setOpaque (true);
    setSize (metrics::baseWidth, metrics::baseHeight);
    setWantsKeyboardFocus (false);

    addAndMakeVisible (timeScale);
    addAndMakeVisible (paramAScale);
    addAndMakeVisible (paramBScale);
    addAndMakeVisible (modeAmountScale);

    buildControls();
    addAndMakeVisible (echoMap);
    addChildComponent (drawer);
    addAndMakeVisible (advancedTab);
    addChildComponent (bubble);

    if (auto* mix = findKnob (params::mix))
    {
        const auto legendFont = drawing::tracked (fonts->legend (metrics::legendSize), metrics::legendTracking);
        const auto anchor = mix->getLegendAnchor();
        const auto x = anchor.x + juce::GlyphArrangement::getStringWidth (legendFont, "MIX") * 0.5f + 8.0f;
        mixLockAnchor = juce::Point<int> (juce::roundToInt (x), juce::roundToInt (anchor.y) - 1);
    }

    updateMeanings (false);
    echoMap.setShape (controller.currentShape());
    applyUiState();

    for (auto& k : machineKeys)
        k->snapLamp();
    for (auto* k : { &syncKey, &tapKey, &holdKey, &duckKey, &wetKey })
        k->snapLamp();
}

void MainPanel::buildControls()
{
    auto* um = &processor.getUndoManager();

    struct Def
    {
        juce::RangedAudioParameter* param;
        juce::Point<float> centre;
        KnobStyle style;
        const char* tip;
        bool dynamicScale = false;
    };

    std::vector<Def> defs;
    auto& timeP = controller.parameter (params::time);

    defs.push_back ({ &timeP, layout::timeCentre(), timeStyle (timeP),
                      "Time: the echo time, 1 ms to 4 s, or a note value with Sync. Each machine changes time its own way.", true });
    {
        auto s = smallKnob ("FEEDBACK");
        auto& p = controller.parameter (params::feedback);
        // 100 - 120 % is the RUN zone, hatched in the panel artwork; its end carries the name.
        s.marks = { { 0.0f, "0", true }, { norm (p, 50.0f), "50", false }, { 1.0f, "RUN", true } };
        s.ticks = 0;
        for (int k = 0; k <= 12; ++k)
            s.tickPositions.push_back (norm (p, 10.0f * (float) k));
        defs.push_back ({ &p, layout::feedbackCentre(), s,
                          "Feedback: how much of each echo comes round again. Up to 100 % trails always fade; above, RUN: the machine runs away into its own saturation." });
    }
    {
        auto s = smallKnob ("DRIVE");
        s.marks = { { 0.0f, minus() + "12", true }, { norm (controller.parameter (params::drive), 0.0f), "0", false }, { 1.0f, "+24", true } };
        defs.push_back ({ &controller.parameter (params::drive), layout::driveCentre(), s,
                          "Drive: record level into the machine. The playback makes up for it: Drive changes colour, not the echo level." });
    }
    {
        auto s = smallKnob ("WOBBLE");
        s.centreDetent = true;
        s.marks = { { 0.0f, "OFF", true }, { 0.5f, "NOM", false }, { 1.0f, "WORN", true } };
        defs.push_back ({ &controller.parameter (params::wobble), layout::wobbleCentre(), s,
                          "Wobble: the machine's transport or clock instability. 0 is perfect, the middle its spec, the top worn out. Sticks at nominal." });
    }
    {
        auto s = smallKnob ("DIFFUSE");
        s.marks = { { 0.0f, "OFF", true }, { 1.0f, "WASH", true } };
        defs.push_back ({ &controller.parameter (params::diffuse), layout::diffuseCentre(), s,
                          "Diffuse: an allpass smear inside the loop: each repeat blurs more, until the trail is a wash." });
    }
    {
        auto s = smallKnob ("LOW CUT");
        auto& p = controller.parameter (params::lowCut);
        s.marks = { { 0.0f, "OFF", true }, { norm (p, 150.0f), "150", false }, { 1.0f, "1K", true } };
        defs.push_back ({ &p, layout::lowCutCentre(), s, "Low Cut: inside the loop: every repeat thins out more. Far left is off." });
    }
    {
        auto s = smallKnob ("HIGH CUT");
        auto& p = controller.parameter (params::highCut);
        s.marks = { { 0.0f, "1K", true }, { norm (p, 5000.0f), "5K", false }, { 1.0f, "OFF", true } };
        defs.push_back ({ &p, layout::highCutCentre(), s, "High Cut: inside the loop: every repeat darkens more. Far right is off." });
    }
    defs.push_back ({ &controller.parameter (params::paramA), layout::paramACentre(), characterStyle (Machine::reel, false),
                      "Param A: the machine's first character control. Its legend changes with the machine.", true });
    defs.push_back ({ &controller.parameter (params::paramB), layout::paramBCentre(), characterStyle (Machine::reel, true),
                      "Param B: the machine's second character control. Its legend changes with the machine.", true });
    {
        auto s = smallKnob ("MODE");
        s.steps = numModes;
        s.ticks = numModes;
        s.dragPixelsForFullRange = 16.0f * (float) numModes;
        s.marks = { { 0.0f, "SINGLE", true }, { 1.0f / 3.0f, "DUAL", false }, { 2.0f / 3.0f, "PING", false }, { 1.0f, "PATTERN", true } };
        defs.push_back ({ &controller.parameter (params::mode), layout::modeCentre(), s,
                          "Mode: Single, Dual, Ping-Pong or Pattern. The knob to its right follows it (Width, Right time, Steps)." });
    }
    defs.push_back ({ &controller.parameter (params::width), layout::modeAmountCentre(),
                      modeAmountStyle (Mode::single, true, controller.parameter (params::width)),
                      "Width (Single, Ping-Pong), Right time (Dual) or Steps (Pattern).", true });
    {
        auto s = smallKnob ("MIX");
        s.marks = { { 0.0f, "DRY", true }, { 0.5f, "50", false }, { 1.0f, "WET", true } };
        defs.push_back ({ &controller.parameter (params::mix), layout::mixCentre(), s,
                          "Mix: pedal law. Up to 50 % the dry stays at full level while the echoes come up; past 50 % the dry fades." });
    }
    {
        auto s = smallKnob ("OUTPUT");
        s.centreDetent = true;
        s.marks = { { 0.0f, minus() + "24", true }, { 0.5f, "0", false }, { 1.0f, "+24", true } };
        defs.push_back ({ &controller.parameter (params::output), layout::outputCentre(), s, "Output: level after the mix, -24 to +24 dB. Sticks at 0 dB." });
    }

    artwork.knobs.clear();
    for (auto& d : defs)
    {
        if (! d.dynamicScale)
            artwork.knobs.push_back ({ d.centre, d.style });

        auto knob = std::make_unique<Knob> (*d.param, um, d.style);
        knob->setValueBubble (&bubble);
        knob->setTooltip (d.tip);
        knob->setBounds (drawing::snapped (layout::knobBounds (d.centre, d.style.diameter)));
        addAndMakeVisible (*knob);

        if (d.param == &timeP)                                          timeKnob = knob.get();
        if (d.param == &controller.parameter (params::paramA))          paramAKnob = knob.get();
        if (d.param == &controller.parameter (params::paramB))          paramBKnob = knob.get();
        if (d.param == &controller.parameter (params::width))           modeAmountKnob = knob.get();
        knobs.push_back (std::move (knob));
    }

    timeScale.setKnobCentre (layout::timeCentre(), layout::timeDiameter);
    paramAScale.setKnobCentre (layout::paramACentre(), layout::smallKnobDiameter);
    paramBScale.setKnobCentre (layout::paramBCentre(), layout::smallKnobDiameter);
    modeAmountScale.setKnobCentre (layout::modeAmountCentre(), layout::smallKnobDiameter);

    // Machine keys in panel order (3 over 4); each stays indexed by its Machine value.
    for (int slot = 0; slot < numMachines; ++slot)
    {
        const auto m = machineOrder[(size_t) slot];
        const auto i = (int) m;
        auto key = std::make_unique<LampButton> (juce::String (info (m).name).toUpperCase(), LampButton::Shape::square);
        key->setKeyBounds (layout::machineKey (slot));
        key->setTooltip (PanelController::machineDescription (m));
        key->setTitle (juce::String (info (m).name) + " machine");
        key->setRadioGroupId (1, juce::dontSendNotification);
        key->setClickingTogglesState (false);
        key->onClick = [this, i] { machineAttachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (*key);
        machineKeys[(size_t) i] = std::move (key);
    }
    machineAttachment = std::make_unique<juce::ParameterAttachment> (controller.parameter (params::machine), [this] (float v)
    {
        const auto index = juce::roundToInt (v);
        for (int i = 0; i < numMachines; ++i)
            machineKeys[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);
    }, um);
    machineAttachment->sendInitialUpdate();

    auto bindToggle = [this, um] (LampButton& b, const char* id, juce::Rectangle<float> key, const char* tip)
    {
        b.setKeyBounds (key);
        b.setClickingTogglesState (true);
        b.setTooltip (tip);
        addAndMakeVisible (b);
        buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (controller.parameter (id), b, um));
    };

    bindToggle (syncKey, params::sync, layout::syncKey(), "Sync: Time as a note value at the host tempo (or the BPM in Advanced). Time then picks the note.");
    bindToggle (duckKey, params::duck, layout::duckKey(), "Duck: the echoes dip while you play and bloom back when you stop. Amount, threshold and release are in Advanced.");
    bindToggle (wetKey, params::wetOnly, layout::wetKey(), "Wet: kills the dry signal (100 % wet, whatever Mix says), for send/return use.");

    // Tap: a UI action. Two or more taps set Time (free) and turn Sync off.
    tapKey.setKeyBounds (layout::tapKey());
    tapKey.setClickingTogglesState (false);
    tapKey.setTitle ("Tap tempo");
    tapKey.setTooltip ("Tap: tap twice or more to set Time (free, in ms; Sync turns off). The average of the last four taps; a 2 s pause starts over.");
    tapKey.onClick = [this]
    {
        controller.tap (juce::Time::getMillisecondCounterHiRes() * 0.001);
        tapFlash = 0.12f;
        tapKey.setToggleState (true, juce::dontSendNotification);
    };
    addAndMakeVisible (tapKey);

    // Hold: latching, or held while pressed (Hold Mode). A performance switch: not undoable.
    holdKey.setKeyBounds (layout::holdKey());
    holdKey.setClickingTogglesState (false);
    holdKey.setTitle ("Hold");
    addAndMakeVisible (holdKey);
    holdAttachment = std::make_unique<juce::ParameterAttachment> (controller.parameter (params::hold), [this] (float v)
    {
        holdKey.setToggleState (v >= 0.5f, juce::dontSendNotification);
    }, nullptr);
    holdAttachment->sendInitialUpdate();
    holdKey.onClick = [this]
    {
        if (holdKey.takeMomentaryClick())
            return;
        holdAttachment->setValueAsCompleteGesture (holdKey.getToggleState() ? 0.0f : 1.0f);
    };
    holdKey.onPress = [this] (bool down)
    {
        if (down)
        {
            holdAttachment->beginGesture();
            holdAttachment->setValueAsPartOfGesture (1.0f);
        }
        else
        {
            holdAttachment->setValueAsPartOfGesture (0.0f);
            holdAttachment->endGesture();
        }
    };

    // The echo map edits the pattern's step levels.
    for (int k = 0; k < params::maxSteps; ++k)
        stepAttachments[(size_t) k] = std::make_unique<juce::ParameterAttachment> (
            controller.parameter (params::stepId (k).toRawUTF8()), [] (float) {}, um);
    echoMap.onStepGestureStart = [this] (int k) { stepAttachments[(size_t) k]->beginGesture(); };
    echoMap.onStepLevel = [this] (int k, float level) { stepAttachments[(size_t) k]->setValueAsPartOfGesture (level * 100.0f); };
    echoMap.onStepGestureEnd = [this] (int k) { stepAttachments[(size_t) k]->endGesture(); };
    echoMap.onStepReset = [this] (int k) { stepAttachments[(size_t) k]->setValueAsCompleteGesture (100.0f); };
    echoMap.setBounds (drawing::snapped (layout::echoMap()));

    advancedTab.setClickingTogglesState (false);
    advancedTab.onClick = [this] { setAdvancedOpen (! drawer.isOpen()); };

    artwork.grooves.clear();
    artwork.grooves.push_back ({ layout::leftGrooveX, layout::grooveTop, layout::leftGrooveX, layout::grooveBottom });
    artwork.grooves.push_back ({ layout::rightGrooveX, layout::grooveTop, layout::rightGrooveX, layout::grooveBottom });
    artwork.grooves.push_back ({ 36.0f, 296.0f, 432.0f, 296.0f });

    artwork.wells.clear();
    artwork.wells.push_back (layout::machineKey (0).getUnion (layout::machineKey (numMachines - 1)).expanded (8.0f));
    artwork.wells.push_back (layout::syncKey().getUnion (layout::holdKey()).expanded (7.0f));
    artwork.wells.push_back (layout::duckKey().expanded (8.0f));
    artwork.wells.push_back (layout::wetKey().expanded (8.0f));
    artwork.legends.clear();
    artwork.legends.push_back ({ "MACHINE", juce::Rectangle<float> (200.0f, 14.0f).withCentre ({ layout::centreX, layout::machineTopY - 20.0f }) });
}

Knob* MainPanel::findKnob (const juce::String& parameterId) const
{
    for (const auto& k : knobs)
        if (k->getParameter().getParameterID() == parameterId)
            return k.get();
    return drawer.findKnob (parameterId);
}

void MainPanel::updateMeanings (bool animate)
{
    const auto m = controller.readMeanings();
    const auto first = ! meaningsShown;
    if (! first && m == shown)
        return;

    const auto previous = shown;
    shown = m;
    meaningsShown = true;
    const auto fade = animate && ! first;

    if (first || m.synced != previous.synced)
    {
        auto& p = controller.parameter (PanelController::timeParameterId (m));
        const auto style = m.synced ? noteStyle ("TIME", layout::timeDiameter, true) : timeStyle (p);
        timeKnob->rebind (p, style);
        if (m.synced)
            timeKnob->setValueToText ([this] (float v01) { return controller.noteText (juce::roundToInt (v01 * (float) (notes::count - 1))); });
        else
            timeKnob->setValueToText (nullptr);
        timeScale.setStyle (style, fade);
    }

    if (first || m.machine != previous.machine)
    {
        const auto a = characterStyle (m.machine, false), b = characterStyle (m.machine, true);
        paramAKnob->setStyle (a);
        paramBKnob->setStyle (b);
        paramAKnob->setValueToText ([this] (float v01) { return controller.characterText (shown, false, v01); });
        paramBKnob->setValueToText ([this] (float v01) { return controller.characterText (shown, true, v01); });
        paramAScale.setStyle (a, fade);
        paramBScale.setStyle (b, fade);
    }

    // Loop's and Drum's own heads step back in Pattern mode (the pattern replaces them).
    paramAKnob->setAlpha ((m.machine == Machine::loop || m.machine == Machine::drum) && m.mode == Mode::pattern ? 0.45f : 1.0f);

    if (first || m.mode != previous.mode || m.synced != previous.synced)
    {
        auto& p = controller.parameter (PanelController::modeAmountParameterId (m));
        const auto style = modeAmountStyle (m.mode, m.synced, p);
        modeAmountKnob->rebind (p, style);
        if (m.mode == Mode::dual && m.synced)
            modeAmountKnob->setValueToText ([this] (float v01) { return controller.noteText (juce::roundToInt (v01 * (float) (notes::count - 1))); });
        else
            modeAmountKnob->setValueToText (nullptr);
        modeAmountScale.setStyle (style, fade);
    }

    if (first || m.holdMode != previous.holdMode)
    {
        const auto momentary = m.holdMode == HoldMode::momentary;
        holdKey.setMomentary (momentary);
        holdKey.setTooltip (momentary ? "Hold (momentary): the current loop plays on while you hold the key; play over it."
                                      : "Hold: the current loop plays on, losslessly, while you play over it. Momentary mode is in Advanced.");
    }
}

int MainPanel::getBaseHeight() const noexcept
{
    return metrics::baseHeight + (drawer.isOpen() ? juce::roundToInt (layout::deckHeight) : 0);
}

void MainPanel::setAdvancedOpen (bool shouldBeOpen)
{
    processor.getUiState().setProperty (uistate::advancedOpen, shouldBeOpen, nullptr);
    drawer.setOpen (shouldBeOpen);
    advancedTab.setOpenState (shouldBeOpen);
    setSize (metrics::baseWidth, getBaseHeight());
    if (onBaseSizeChanged != nullptr)
        onBaseSizeChanged();
    if (refreshHeader != nullptr)
        refreshHeader();
}

void MainPanel::applyUiState()
{
    const auto open = (bool) processor.getUiState().getProperty (uistate::advancedOpen, false);
    if (open != drawer.isOpen())
        setAdvancedOpen (open);
    advancedTab.setOpenState (open);
}

void MainPanel::animateFrame (float dt)
{
    updateMeanings (true);

    for (auto& key : machineKeys)
        key->tick (dt);
    for (auto* k : { &syncKey, &tapKey, &holdKey, &duckKey, &wetKey })
        k->tick (dt);

    if (tapFlash > 0.0f && (tapFlash -= dt) <= 0.0f)
        tapKey.setToggleState (false, juce::dontSendNotification);

    timeScale.tick (dt);
    paramAScale.tick (dt);
    paramBScale.tick (dt);
    modeAmountScale.tick (dt);

    echoMap.setShape (controller.currentShape());
    echoMap.tick (dt, processor.getMeters().wetRms.load (std::memory_order_relaxed));
    drawer.tick (dt);
}

void MainPanel::resized()
{
    drawer.setBounds (layout::deck().toNearestInt());
    advancedTab.setBounds (layout::advancedTab (drawer.isOpen()).toNearestInt());
}

void MainPanel::paint (juce::Graphics& g)
{
    const auto panelArea = juce::Rectangle<float> ((float) metrics::baseWidth, (float) metrics::baseHeight);

    panelLayer.draw (g, panelArea, [this] (juce::Graphics& lg, juce::Rectangle<float> area, float scale)
    {
        const auto panel = area.withTrimmedTop (metrics::headerHeight);
        renderPanelArtwork (lg, panel, artwork, *fonts, scale);

        // Feedback's RUN zone, 100 - 120 %: engraved hatching on the scale (never the caution colour).
        {
            const auto& pal = Theme::palette();
            const auto& ext = Theme::extras();
            const auto c = layout::feedbackCentre();
            const auto r0 = layout::smallKnobDiameter * 0.5f + 8.0f, r1 = r0 + 7.0f;
            const auto start = metrics::knobStartAngle + (metrics::knobEndAngle - metrics::knobStartAngle) * (100.0f / 120.0f);
            const auto end = metrics::knobEndAngle;
            juce::Path band;
            band.addCentredArc (c.x, c.y, r1, r1, 0.0f, start, end, true);
            band.addCentredArc (c.x, c.y, r0, r0, 0.0f, end, start, false);
            band.closeSubPath();
            lg.setColour (pal.grooveDark);
            lg.strokePath (band, juce::PathStrokeType (1.0f));
            {
                juce::Graphics::ScopedSaveState clip (lg);
                lg.reduceClipRegion (band);
                lg.setColour (ext.runHatch.withMultipliedAlpha (0.8f));
                for (float x = c.x - r1 - 20.0f; x < c.x + r1 + 20.0f; x += 3.0f)
                    lg.drawLine (x, c.y - r1 - 2.0f, x + 10.0f, c.y + r1 + 2.0f, 0.8f);
            }
        }

        lg.setGradientFill (juce::ColourGradient (juce::Colour (0x70000000), 0.0f, panel.getY(),
                                                  juce::Colour (0x00000000), 0.0f, panel.getY() + 9.0f, false));
        lg.fillRect (panel.withHeight (9.0f));
    });
}

} // namespace mulberry::ui
