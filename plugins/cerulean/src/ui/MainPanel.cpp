#include "ui/MainPanel.h"

#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

namespace cerulean::ui
{

namespace
{
    juce::RangedAudioParameter& parameter (CeruleanProcessor& p, const char* id)
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

    juce::String minus() { return juce::String::fromUTF8 ("\xe2\x88\x92"); }
    juce::String times() { return juce::String::fromUTF8 ("\xc3\x97"); }

    KnobStyle smallKnob (const juce::String& title)
    {
        KnobStyle s;
        s.title = title;
        s.diameter = layout::smallKnobDiameter;
        return s;
    }

    /** A note-value scale (Pre-delay or Echo time with Sync on): a 14-position switch. */
    KnobStyle noteStyle (const juce::String& title, const juce::String& badge = {})
    {
        auto s = smallKnob (title);
        s.badge = badge;
        s.steps = numNoteValues;
        s.ticks = numNoteValues;
        s.dragPixelsForFullRange = 16.0f * (float) numNoteValues;

        const auto names = params::noteValueNames();
        const auto at = [] (int i) { return (float) i / (float) (numNoteValues - 1); };
        s.marks = { { at (0), names[0], true }, { at ((int) NoteValue::n1_4), names[(int) NoteValue::n1_4], false },
                    { at (numNoteValues - 1), names[numNoteValues - 1], true } };
        return s;
    }

    /** Printed scale of Param A / B for an algorithm. */
    KnobStyle characterStyle (Algorithm a, bool isB)
    {
        const auto legends = params::characterLegends (a);
        auto s = smallKnob (isB ? legends.second : legends.first);
        s.badge = isB ? "B" : "A";

        auto ends = [&s] (const char* lo, const char* hi) { s.marks = { { 0.0f, lo, true }, { 1.0f, hi, true } }; };
        auto stepped = [&s] (std::initializer_list<juce::String> labels)
        {
            const auto n = (int) labels.size();
            int i = 0;
            s.marks.clear();

            for (const auto& l : labels)
            {
                const auto t = (float) i / (float) (n - 1);
                s.marks.push_back ({ t, l, i == 0 || i == n - 1 });
                ++i;
            }

            s.ticks = n;
        };

        if (! isB)
        {
            switch (a)
            {
                case Algorithm::room:   ends ("EARLY", "LATE"); s.centreDetent = true; break;
                case Algorithm::hall:   ends ("LESS", "MORE"); break;
                case Algorithm::plate:  ends ("GRAINY", "SMOOTH"); break;
                case Algorithm::spring: stepped ({ "1", "2", "3" }); break;
                case Algorithm::nimbus: ends ("THIN", "DENSE"); break;
                case Algorithm::halo:   stepped ({ minus() + "12", "+5", "+7", "+12", "+19" }); break;
                case Algorithm::rise:   ends ("FAST", "SLOW"); break;
                case Algorithm::billow: ends ("SHORT", "LONG"); break;
                case Algorithm::choir:  stepped ({ "A", "E", "I", "O", "U" }); break;
                case Algorithm::echo:   ends ("SHORT", "LONG"); break;
            }
        }
        else
        {
            switch (a)
            {
                case Algorithm::room:   ends ("SOFT", "HARD"); break;
                case Algorithm::hall:   ends ("FAST", "SLOW"); break;
                case Algorithm::plate:  ends ("LESS", "MORE"); break;
                case Algorithm::spring: ends ("LESS", "MORE"); break;
                case Algorithm::nimbus: ends ("NEAR", "FAR"); break;
                case Algorithm::halo:   ends ("LESS", "MORE"); break;
                case Algorithm::rise:   ends ("WET", "BOTH"); break;
                case Algorithm::billow: ends ("LESS", "MORE"); break;
                case Algorithm::choir:  stepped ({ "1", "2", "3", "4" }); break;
                case Algorithm::echo:   ends ("FEW", "MANY"); break;
            }
        }

        return s;
    }

    KnobStyle preDelayStyle (juce::RangedAudioParameter& p)
    {
        auto s = smallKnob ("PRE-DELAY");
        s.marks = { { 0.0f, "0", true }, { norm (p, 80.0f), "80", false }, { 1.0f, "500", true } };
        return s;
    }
}

MainPanel::MainPanel (PanelController& c)
    : controller (c),
      processor (c.getProcessor()),
      header (processor, c.getPresets(), c.getAb(), dialog),
      drawer (processor, bubble),
      vblank (this, [this] (double t) { animateFrame (t); })
{
    setOpaque (true);
    setSize (metrics::baseWidth, metrics::baseHeight);
    setWantsKeyboardFocus (false);

    // Overlays first, so the knobs draw over their (mouse-transparent) scales.
    addAndMakeVisible (preDelayScale);
    addAndMakeVisible (paramAScale);
    addAndMakeVisible (paramBScale);

    buildControls();
    addAndMakeVisible (tail);
    addChildComponent (drawer);
    addAndMakeVisible (advancedTab);
    addAndMakeVisible (header);
    addChildComponent (bubble);
    addChildComponent (dialog);

    wireHeader (header);

    // Mix lock: a padlock after the MIX legend while it is on.
    if (auto* mix = findKnob (params::mix))
    {
        const auto legendFont = drawing::tracked (fonts->legend (metrics::legendSize), metrics::legendTracking);
        const auto anchor = mix->getLegendAnchor();
        const auto x = anchor.x + juce::GlyphArrangement::getStringWidth (legendFont, "MIX") * 0.5f + 8.0f;
        mixLockBadge.setBounds (juce::Rectangle<int> (LockBadge::width, LockBadge::height)
                                    .withCentre ({ juce::roundToInt (x), juce::roundToInt (anchor.y) - 1 }));
    }
    addChildComponent (mixLockBadge);

    controller.getPresets().onChange = [this]
    {
        header.refresh();
        mixLockBadge.setVisible (controller.getPresets().isMixLocked());
    };
    mixLockBadge.setVisible (controller.getPresets().isMixLocked());

    processor.getState().state.addListener (this);
    updateMeanings (false);
    tail.setShape (controller.currentTailShape());
    applyUiState();

    // Lamps start in their final state (no fade-in when the editor opens).
    for (auto& k : algorithmKeys)
        k->snapLamp();

    freezeKey.snapLamp();
    duckKey.snapLamp();
    wetKey.snapLamp();
    preSyncKey.snapLamp();
    echoSyncKey.snapLamp();
    header.refresh();
}

MainPanel::~MainPanel()
{
    controller.getPresets().onChange = nullptr;
    processor.getState().state.removeListener (this);
}

void MainPanel::buildControls()
{
    auto* um = &processor.getUndoManager();

    auto& decayP    = parameter (processor, params::decay);
    auto& preDelayP = parameter (processor, params::preDelay);
    auto& sizeP     = parameter (processor, params::size);
    auto& modP      = parameter (processor, params::mod);
    auto& toneP     = parameter (processor, params::tone);
    auto& paramAP   = parameter (processor, params::paramA);
    auto& paramBP   = parameter (processor, params::paramB);
    auto& mixP      = parameter (processor, params::mix);
    auto& outputP   = parameter (processor, params::output);

    struct Def
    {
        juce::RangedAudioParameter* param;
        juce::Point<float> centre;
        KnobStyle style;
        const char* tip;
        bool dynamicScale = false;
    };

    std::vector<Def> defs;

    {
        KnobStyle s;
        s.title = "DECAY";
        s.diameter = layout::decayDiameter;
        s.hero = true;
        s.dragPixelsForFullRange = 320.0f;

        const std::pair<float, const char*> labels[] = { { 0.1f, ".1" }, { 0.2f, ".2" }, { 0.5f, ".5" }, { 1.0f, "1" }, { 2.0f, "2" },
                                                         { 5.0f, "5" }, { 10.0f, "10" }, { 20.0f, "20" }, { 60.0f, "60" } };

        for (const auto& [seconds, text] : labels)
        {
            s.marks.push_back ({ norm (decayP, seconds), text, true });
            s.tickPositions.push_back (norm (decayP, seconds));
        }

        for (const auto seconds : { 0.15f, 0.3f, 0.7f, 1.5f, 3.0f, 7.0f, 15.0f, 35.0f })
            s.tickPositions.push_back (norm (decayP, seconds));

        defs.push_back ({ &decayP, layout::decayCentre(), s,
                          "Decay: reverb time (RT60 of the mids), 0.1 s to 60 s. Drag, Shift-drag for fine, double-click to reset." });
    }

    defs.push_back ({ &preDelayP, layout::preDelayCentre(), preDelayStyle (preDelayP),
                      "Pre-delay: the gap before the reverb starts, 0-500 ms. With Sync on (Advanced) it is a note value.", true });
    {
        auto s = smallKnob ("SIZE");
        s.marks = { { 0.0f, "SMALL", true }, { 0.5f, "1" + times(), false }, { 1.0f, "HUGE", true } };
        defs.push_back ({ &sizeP, layout::sizeCentre(), s,
                          "Size: scales the whole space (0.3x - 2x), independent of Decay. Moving it glides the pitch, like tape." });
    }
    {
        auto s = smallKnob ("MOD");
        s.marks = { { 0.0f, "OFF", true }, { 1.0f, "DEEP", true } };
        defs.push_back ({ &modP, layout::modCentre(), s, "Mod: depth of the tank modulation. 0 is static, high is chorused and detuned. Rate is in Advanced." });
    }
    {
        auto s = smallKnob ("TONE");
        s.centreDetent = true;
        s.marks = { { 0.0f, "DARK", true }, { 0.5f, "FLAT", false }, { 1.0f, "BRIGHT", true } };
        defs.push_back ({ &toneP, layout::toneCentre(), s, "Tone: darker or brighter tail (damping in the loop plus a gentle tilt). Sticks at Flat." });
    }

    defs.push_back ({ &paramAP, layout::paramACentre(), characterStyle (Algorithm::hall, false),
                      "Param A: the algorithm's first character control. Its legend changes with the algorithm.", true });
    defs.push_back ({ &paramBP, layout::paramBCentre(), characterStyle (Algorithm::hall, true),
                      "Param B: the algorithm's second character control. Its legend changes with the algorithm.", true });
    {
        auto s = smallKnob ("MIX");
        s.marks = { { 0.0f, "DRY", true }, { 0.5f, "50", false }, { 1.0f, "WET", true } };
        defs.push_back ({ &mixP, layout::mixCentre(), s,
                          "Mix: pedal law. Up to 50 % the dry stays at full level while the reverb comes up; past 50 % the dry fades out." });
    }
    {
        auto s = smallKnob ("OUTPUT");
        s.centreDetent = true;
        s.marks = { { 0.0f, minus() + "24", true }, { 0.5f, "0", false }, { 1.0f, "+24", true } };
        defs.push_back ({ &outputP, layout::outputCentre(), s, "Output: level after the mix, -24 to +24 dB. Sticks at 0 dB." });
    }

    artwork.knobs.clear();

    for (auto& d : defs)
    {
        if (! d.dynamicScale)
            artwork.knobs.push_back ({ d.centre, d.style });

        auto knob = std::make_unique<Knob> (*d.param, um, d.style);
        knob->setValueBubble (&bubble);
        knob->setTooltip (d.tip);
        knob->setBounds (snapped (layout::knobBounds (d.centre, d.style.diameter)));
        addAndMakeVisible (*knob);

        if (d.param == &preDelayP)  preDelayKnob = knob.get();
        if (d.param == &paramAP)    paramAKnob = knob.get();
        if (d.param == &paramBP)    paramBKnob = knob.get();

        knobs.push_back (std::move (knob));
    }

    preDelayScale.setKnobCentre (layout::preDelayCentre());
    paramAScale.setKnobCentre (layout::paramACentre());
    paramBScale.setKnobCentre (layout::paramBCentre());

    // Algorithm keys.
    const auto names = params::algorithmNames();

    for (int i = 0; i < (int) algorithmKeys.size(); ++i)
    {
        auto key = std::make_unique<LampButton> (names[i].toUpperCase(), LampButton::Shape::square);
        key->setKeyBounds (layout::algorithmKey (i));
        key->setTooltip (PanelController::algorithmDescription (static_cast<Algorithm> (i)));
        key->setTitle (names[i] + " algorithm");
        key->setRadioGroupId (1, juce::dontSendNotification);
        key->setClickingTogglesState (false);
        key->onClick = [this, i] { algorithmAttachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (*key);
        algorithmKeys[(size_t) i] = std::move (key);
    }

    algorithmAttachment = std::make_unique<juce::ParameterAttachment> (parameter (processor, params::algorithm), [this] (float v)
    {
        const auto index = juce::roundToInt (v);

        for (int i = 0; i < (int) algorithmKeys.size(); ++i)
            algorithmKeys[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);
    }, um);
    algorithmAttachment->sendInitialUpdate();

    // Freeze: latching, or held while pressed (Freeze Mode). Not undoable: it is a performance switch.
    freezeKey.setKeyBounds (layout::freeze());
    freezeKey.setClickingTogglesState (false);
    freezeKey.setTitle ("Freeze");
    addAndMakeVisible (freezeKey);
    freezeAttachment = std::make_unique<juce::ParameterAttachment> (parameter (processor, params::freeze), [this] (float v)
    {
        freezeKey.setToggleState (v >= 0.5f, juce::dontSendNotification);
    }, nullptr);
    freezeAttachment->sendInitialUpdate();

    freezeKey.onClick = [this]
    {
        if (freezeKey.takeMomentaryClick())
            return;

        freezeAttachment->setValueAsCompleteGesture (freezeKey.getToggleState() ? 0.0f : 1.0f);
    };
    freezeKey.onPress = [this] (bool down)
    {
        if (down)
        {
            freezeAttachment->beginGesture();
            freezeAttachment->setValueAsPartOfGesture (1.0f);
        }
        else
        {
            freezeAttachment->setValueAsPartOfGesture (0.0f);
            freezeAttachment->endGesture();
        }
    };

    auto bindToggle = [this, um] (LampButton& b, const char* id, juce::Rectangle<float> key, const char* tip)
    {
        b.setKeyBounds (key);
        b.setClickingTogglesState (true);
        b.setTooltip (tip);
        addAndMakeVisible (b);
        buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (parameter (processor, id), b, um));
    };

    bindToggle (duckKey, params::duck, layout::duckKey(),
                "Duck: the reverb dips while you play and blooms back when you stop. Amount, threshold and release are in Advanced.");
    bindToggle (wetKey, params::wetOnly, layout::wetKey(), "Wet: kills the dry signal (100 % wet, whatever Mix says), for send/return use.");

    tail.setBounds (snapped (layout::tailDisplay()));

    // ADVANCED, engraved on the bottom edge: opens the deck below the panel.
    advancedTab.setClickingTogglesState (false);
    advancedTab.onClick = [this] { setAdvancedOpen (! drawer.isOpen()); };

    // Sync keys, each under the control it turns into a note value.
    preSyncKey.setTitle ("Pre-delay Sync");
    echoSyncKey.setTitle ("Echo Sync");
    bindToggle (preSyncKey, params::preDelaySync, layout::preDelaySyncKey(),
                "Sync: Pre-delay as a note value at the host tempo (or the BPM in Advanced). The Pre-delay knob then picks the note.");
    bindToggle (echoSyncKey, params::echoSync, layout::echoSyncKey(),
                "Sync: Echo's repeat time as a note value. The Echo Time knob then picks the note.");
    echoSyncKey.setVisible (false);   // only with Echo selected (updateMeanings)

    // Recesses the keys sit in.
    artwork.wells.clear();
    artwork.wells.push_back (layout::algorithmKey (0).getUnion (layout::algorithmKey (9)).expanded (8.0f));
    artwork.wells.push_back (layout::duckKey().expanded (8.0f));
    artwork.wells.push_back (layout::wetKey().expanded (8.0f));
    artwork.wells.push_back (layout::freeze().expanded (7.0f));
    artwork.wells.push_back (layout::preDelaySyncKey().expanded (6.0f));   // Echo's well is drawn only with Echo (paint)
    artwork.legends.clear();
    artwork.legends.push_back ({ "ALGORITHM", juce::Rectangle<float> (200.0f, 14.0f).withCentre ({ layout::centreX, layout::railTopY - 22.0f }) });
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

    // Pre-delay: milliseconds, or a note value at the current tempo.
    if (first || m.preDelaySynced != previous.preDelaySynced)
    {
        auto& p = controller.parameter (PanelController::preDelayParameterId (m));
        const auto style = m.preDelaySynced ? noteStyle ("PRE-DELAY") : preDelayStyle (p);
        preDelayKnob->rebind (p, style);

        if (m.preDelaySynced)
            preDelayKnob->setValueToText ([this] (float v01) { return controller.preDelayText (shown, v01); });
        else
            preDelayKnob->setValueToText (nullptr);

        preDelayScale.setStyle (style, fade);
    }

    // Param A / B relabel with the algorithm. With Echo + Echo Sync, Param A picks the echo note.
    if (first || m.algorithm != previous.algorithm || m.echoNoteOnParamA != previous.echoNoteOnParamA)
    {
        const auto bindingChanged = first || m.echoNoteOnParamA != previous.echoNoteOnParamA;
        const auto algorithm = m.algorithm;

        auto styleA = m.echoNoteOnParamA ? noteStyle (PanelController::characterLegend (algorithm, false), "A") : characterStyle (algorithm, false);
        const auto styleB = characterStyle (algorithm, true);

        if (bindingChanged)
            paramAKnob->rebind (controller.parameter (PanelController::paramAParameterId (m)), styleA);
        else
            paramAKnob->setStyle (styleA);

        paramBKnob->setStyle (styleB);

        paramAKnob->setValueToText ([this] (float v01) { return controller.characterText (shown, false, v01); });
        paramBKnob->setValueToText ([this] (float v01) { return controller.characterText (shown, true, v01); });

        paramAScale.setStyle (styleA, fade);
        paramBScale.setStyle (styleB, fade);
    }

    if (first || m.algorithm != previous.algorithm)
    {
        const auto isEcho = m.algorithm == Algorithm::echo;

        if (echoSyncKey.isVisible() != isEcho)
        {
            echoSyncKey.setVisible (isEcho);
            repaint (layout::echoSyncKey().expanded (10.0f).toNearestInt());
        }
    }

    if (first || m.freezeMode != previous.freezeMode)
    {
        const auto momentary = m.freezeMode == FreezeMode::momentary;
        freezeKey.setMomentary (momentary);
        freezeKey.setTooltip (momentary ? "Freeze (momentary): holds the tail while you hold the key. Dry keeps playing over it."
                                        : "Freeze: holds the tail indefinitely; new input does not add to it. Dry keeps playing over it. "
                                          "Momentary mode is in Advanced.");
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

    // The window grows (or shrinks back) with the deck.
    if (onBaseSizeChanged != nullptr)
        onBaseSizeChanged();

    header.refresh();
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
    const auto open = (bool) ui.getProperty (uistate::advancedOpen, false);

    if (open != drawer.isOpen())
        setAdvancedOpen (open);

    advancedTab.setOpenState (open);
}

void MainPanel::animateFrame (double timeSeconds)
{
    const auto dt = lastFrameTime < 0.0 ? (1.0f / 60.0f)
                                        : (float) juce::jlimit (0.0, 0.1, timeSeconds - lastFrameTime);
    lastFrameTime = timeSeconds;

    updateMeanings (true);

    for (auto& key : algorithmKeys)
        key->tick (dt);

    freezeKey.tick (dt);
    duckKey.tick (dt);
    wetKey.tick (dt);
    preSyncKey.tick (dt);
    echoSyncKey.tick (dt);

    preDelayScale.tick (dt);
    paramAScale.tick (dt);
    paramBScale.tick (dt);

    tail.setShape (controller.currentTailShape());
    tail.tick (dt, processor.getMeters().wetRms.load (std::memory_order_relaxed));

    drawer.tick (dt);

    if (++framesSinceRefresh >= 6)
    {
        framesSinceRefresh = 0;
        header.refresh();
    }
}

//==============================================================================
void MainPanel::resized()
{
    header.setBounds (layout::header().toNearestInt());
    drawer.setBounds (layout::deck().toNearestInt());
    advancedTab.setBounds (layout::advancedTab (drawer.isOpen()).toNearestInt());
    dialog.setBounds (getLocalBounds());
}

void MainPanel::paint (juce::Graphics& g)
{
    // The panel itself is always the top baseHeight; the deck (when open) draws itself below.
    const auto panelArea = juce::Rectangle<float> ((float) metrics::baseWidth, (float) metrics::baseHeight);

    panelLayer.draw (g, panelArea, [this] (juce::Graphics& lg, juce::Rectangle<float> area, float scale)
    {
        const auto panel = area.withTrimmedTop (metrics::headerHeight);
        renderPanelArtwork (lg, panel, artwork, *fonts, scale);

        // The header rail casts a short shadow onto the panel.
        lg.setGradientFill (juce::ColourGradient (juce::Colour (0x70000000), 0.0f, panel.getY(),
                                                  juce::Colour (0x00000000), 0.0f, panel.getY() + 9.0f, false));
        lg.fillRect (panel.withHeight (9.0f));
    });

    // Echo's Sync key has its well only while it is shown (with Echo selected).
    if (echoSyncKey.isVisible())
        drawing::drawRecess (g, layout::echoSyncKey().expanded (6.0f), 7.0f, 3.0f);
}

} // namespace cerulean::ui
