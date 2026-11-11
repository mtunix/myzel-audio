#include "ui/StudioPanel.h"

#include "plugin/PluginProcessor.h"

namespace cerulean::ui
{

namespace
{
    /** Reads "85 ms" or "0.2 s" (no unit = ms) for a parameter in milliseconds. */
    std::function<float (const juce::String&)> millisecondsFromText (juce::RangedAudioParameter& p)
    {
        return [&p] (const juce::String& text)
        {
            const auto t = text.trim().toLowerCase();

            if (! t.containsAnyOf ("0123456789"))
                return -1.0f;

            const auto value = t.getFloatValue();
            return p.convertTo0to1 ((t.endsWith ("s") && ! t.endsWith ("ms")) ? value * 1000.0f : value);
        };
    }

    float noteFromText (const juce::String& text)
    {
        const auto index = PanelController::noteIndexFromText (text);
        return index < 0 ? -1.0f : (float) index / (float) (numNoteValues - 1);
    }

    constexpr int algorithmRadioGroup = 1001;

    int noteIndex (float v01)
    {
        return juce::roundToInt (v01 * (float) (numNoteValues - 1));
    }
}

StudioPanel::StudioPanel (PanelController& c)
    : controller (c),
      processor (c.getProcessor()),
      header (processor, c.getPresets(), c.getAb(), dialog),
      meters (processor.getMeters()),
      vblank (this, [this] (double t) { animateFrame (t); })
{
    setOpaque (true);
    setSize (metrics::baseWidth, metrics::baseHeight);
    setWantsKeyboardFocus (false);

    buildControls();
    addAndMakeVisible (tail);
    addAndMakeVisible (meters);
    addAndMakeVisible (header);
    addChildComponent (dialog);

    wireHeader (header);

    // Mix lock: a padlock after the MIX label while it is on.
    {
        const auto labelFont = drawing::tracked (fonts->legend (studio::labelSize), studio::labelTracking);
        const auto row = studio::outputRow (0);
        const auto x = row.getX() + juce::GlyphArrangement::getStringWidth (labelFont, "MIX") + 10.0f;
        mixLockBadge.setBounds (juce::Rectangle<int> (LockBadge::width, LockBadge::height)
                                    .withCentre ({ juce::roundToInt (x), juce::roundToInt (row.getY() + 8.5f) }));
    }
    addChildComponent (mixLockBadge);

    controller.getPresets().onChange = [this]
    {
        header.refresh();
        mixLockBadge.setVisible (controller.getPresets().isMixLocked());
    };
    mixLockBadge.setVisible (controller.getPresets().isMixLocked());
    processor.getState().state.addListener (this);

    updateMeanings();
    updateTempo();
    tail.setShape (controller.currentTailShape());
    header.refresh();
}

StudioPanel::~StudioPanel()
{
    controller.getPresets().onChange = nullptr;
    processor.getState().state.removeListener (this);
}

//==============================================================================
StudioSlider& StudioPanel::addSlider (const char* id, const juce::String& label, juce::Rectangle<float> bounds, const juce::String& tip)
{
    auto slider = std::make_unique<StudioSlider> (controller.parameter (id), &controller.getUndoManager(), label);
    slider->setBounds (bounds.toNearestInt());
    slider->setTooltip (tip);
    addAndMakeVisible (*slider);
    sliders.push_back (std::move (slider));
    return *sliders.back();
}

StudioToggle& StudioPanel::addToggle (StudioToggle& toggle, const char* id, juce::Rectangle<float> bounds, const juce::String& tip)
{
    toggle.setBounds (bounds.toNearestInt());
    toggle.setClickingTogglesState (true);
    toggle.setTooltip (tip);
    addAndMakeVisible (toggle);
    buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (controller.parameter (id), toggle, &controller.getUndoManager()));
    toggles.push_back ({ id, &toggle });
    return toggle;
}

void StudioPanel::buildControls()
{
    using namespace studio;

    // Algorithm list.
    const auto names = params::algorithmNames();

    for (int i = 0; i < numAlgorithms; ++i)
    {
        const auto a = static_cast<Algorithm> (i);
        auto row = std::make_unique<AlgorithmRow> (i, names[i], PanelController::algorithmTagline (a));
        row->setBounds (algorithmRow (i).toNearestInt());
        row->setTooltip (PanelController::algorithmDescription (a));
        row->setRadioGroupId (algorithmRadioGroup, juce::dontSendNotification);
        row->setClickingTogglesState (false);
        row->onClick = [this, i] { algorithmAttachment->setValueAsCompleteGesture ((float) i); };
        row->onStep = [this, i] (int delta)
        {
            const auto next = juce::jlimit (0, numAlgorithms - 1, i + delta);
            algorithmAttachment->setValueAsCompleteGesture ((float) next);
            algorithmRows[(size_t) next]->grabKeyboardFocus();
        };
        addAndMakeVisible (*row);
        algorithmRows[(size_t) i] = std::move (row);
    }

    algorithmAttachment = std::make_unique<juce::ParameterAttachment> (controller.parameter (params::algorithm), [this] (float v)
    {
        const auto index = juce::roundToInt (v);

        for (int i = 0; i < numAlgorithms; ++i)
            algorithmRows[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);
    }, &controller.getUndoManager());
    algorithmAttachment->sendInitialUpdate();

    // DECAY: the tail card's headline value (drag it, or the handle on the graph).
    addSlider (params::decay, "DECAY", decayHero(), "Decay: reverb time (RT60 of the mids), 0.1 s to 60 s. Drag the value, "
                                                    "or the handle where the mid line meets the floor. Click the value to type one.")
        .setLook (StudioSlider::Look::hero);

    // SPACE
    preDelaySlider = &addSlider (params::preDelay, "PRE-DELAY", paramRow (0, 0),
                                 "Pre-delay: the gap before the reverb starts, 0-500 ms, or a note value with SYNC.");
    addSlider (params::size, "SIZE", paramRow (0, 1), "Size: scales the whole space (0.3x - 2x), independent of Decay. Moving it glides the pitch, like tape.");
    addSlider (params::diffusion, "DIFFUSION", paramRow (0, 2), "Diffusion: how quickly the tail smears into a dense wash. Low is more grainy and echoey.");
    addSlider (params::width, "WIDTH", paramRow (0, 3), "Width: stereo width of the tail, 0 % (mono) to 150 %.");

    // CHARACTER
    paramASlider = &addSlider (params::paramA, "A", paramRow (1, 0), "Param A: the algorithm's first character control. Its name changes with the algorithm.");
    paramBSlider = &addSlider (params::paramB, "B", paramRow (1, 1), "Param B: the algorithm's second character control. Its name changes with the algorithm.");
    addSlider (params::mod, "MOD", paramRow (1, 2), "Mod: depth of the tank modulation. 0 is static, high is chorused and detuned.");
    addSlider (params::modRate, "MOD RATE", paramRow (1, 3), "Mod Rate: speed of the tank modulation relative to the algorithm's own rate (0.25x - 4x).")
        .setBipolar (true);

    // TONE
    addSlider (params::tone, "TONE", paramRow (2, 0), "Tone: darker or brighter tail (damping in the loop plus a gentle tilt).").setBipolar (true);
    addSlider (params::lowEnd, "LOW END", paramRow (2, 1), "Low End: decay time of the lows relative to Decay (0.5x - 2x).").setBipolar (true);
    addSlider (params::inLowCut, "LOW CUT", paramRow (2, 2), "Input Low Cut: high-pass in front of the reverb, so lows stay out of the tail. Far left is off.");
    addSlider (params::inHighCut, "HIGH CUT", paramRow (2, 3), "Input High Cut: low-pass in front of the reverb. Far right is off.")
        .setFillFromRight (true);   // the fill is what is cut: Off is empty, as on Low Cut

    // DUCK
    {
        auto& amount = addSlider (params::duckAmount, "AMOUNT", paramRow (3, 0), "Duck Amount: how far the wet dips while you play (with Duck on).");
        auto& amountParam = controller.parameter (params::duckAmount);
        amount.setTextToValue ([&amountParam] (const juce::String& text)
        {
            if (! text.containsAnyOf ("0123456789"))
                return -1.0f;

            return amountParam.convertTo0to1 (std::abs (text.getFloatValue()));
        });
    }
    addSlider (params::duckThreshold, "THRESHOLD", paramRow (3, 1), "Duck Threshold: the playing level where ducking starts. The detector adapts to your level.");
    addSlider (params::duckRelease, "RELEASE", paramRow (3, 2), "Duck Release: how fast the tail blooms back when you stop playing.")
        .setTextToValue (millisecondsFromText (controller.parameter (params::duckRelease)));
    addToggle (duckKey, params::duck, titleChip (paramCard (3), 56.0f),
               "Duck: the reverb dips while you play and blooms back when you stop.");

    // OUTPUT
    addSlider (params::mix, "MIX", outputRow (0), "Mix: pedal law. Up to 50 % the dry stays at full level while the reverb comes up; past 50 % the dry fades out.");
    addSlider (params::output, "OUTPUT", outputRow (1), "Output: level after the mix, -24 to +24 dB.").setBipolar (true);
    addToggle (wetKey, params::wetOnly, wetButton (), "Wet only: kills the dry signal (100 % wet, whatever Mix says), for send/return use.");

    // Sync keys sit at the right end of their slider's bar.
    constexpr float chipWidth = 40.0f;
    preSyncKey.setTitle ("Pre-delay Sync");
    echoSyncKey.setTitle ("Echo Sync");
    preDelaySlider->setTrailingSpace (chipWidth + 8.0f);
    const auto chipFor = [chipWidth] (juce::Rectangle<float> row) { return juce::Rectangle<float> (row.getRight() - chipWidth, row.getY() + 19.0f, chipWidth, 18.0f); };
    addToggle (preSyncKey, params::preDelaySync, chipFor (paramRow (0, 0)),
               "Pre-delay Sync: pre-delay as a note value at the host tempo (or Tempo). The Pre-delay value then picks the note.");
    addToggle (echoSyncKey, params::echoSync, chipFor (paramRow (1, 0)),
               "Echo Sync: Echo's repeat time as a note value. Param A then picks the note.");

    // FREEZE: in the tail card's head, next to the decay it holds.
    freezeKey.setBounds (freezeButton().toNearestInt());
    freezeKey.setClickingTogglesState (false);
    freezeKey.setTitle ("Freeze");
    addAndMakeVisible (freezeKey);
    toggles.push_back ({ params::freeze, &freezeKey });

    // Not undoable: a performance switch, as on the panel.
    freezeAttachment = std::make_unique<juce::ParameterAttachment> (controller.parameter (params::freeze), [this] (float v)
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

    latchKey.setBounds (freezeModeLatch().toNearestInt());
    momentaryKey.setBounds (freezeModeMomentary().toNearestInt());
    // BEHAVIOUR: set-and-forget settings.
    latchKey.setTooltip ("Freeze Mode Latch: each press of Freeze toggles it.");
    momentaryKey.setTooltip ("Freeze Mode Hold (momentary): Freeze holds only while the key is held.");
    latchKey.setTitle ("Freeze Mode Latch");
    momentaryKey.setTitle ("Freeze Mode Momentary");
    addAndMakeVisible (latchKey);
    addAndMakeVisible (momentaryKey);

    freezeModeAttachment = std::make_unique<juce::ParameterAttachment> (controller.parameter (params::freezeMode), [this] (float v)
    {
        const auto momentary = juce::roundToInt (v) == (int) FreezeMode::momentary;
        latchKey.setToggleState (! momentary, juce::dontSendNotification);
        momentaryKey.setToggleState (momentary, juce::dontSendNotification);
    }, &controller.getUndoManager());
    freezeModeAttachment->sendInitialUpdate();
    latchKey.onClick = [this] { freezeModeAttachment->setValueAsCompleteGesture ((float) FreezeMode::latch); };
    momentaryKey.onClick = [this] { freezeModeAttachment->setValueAsCompleteGesture ((float) FreezeMode::momentary); };

    addToggle (trailsKey, params::trails, trailsSwitch(),
               "Trails: on Bypass, algorithm and preset changes the old tail rings out instead of being cut.");
    addToggle (layerKey, params::freezeLayer, layerSwitch(),
               "Freeze Layer: while frozen, what you play adds onto the held sound instead of being shut out. "
               "The pad holds its level: older layers make room for new ones.");

    bpmSlider = &addSlider (params::bpm, "TEMPO", tempoRow(), "Tempo for synced times when the host gives none (e.g. Standalone). Shows the host's tempo when it has one.");
    bpmSlider->setValueToText ([this] (float v01)
    {
        if (controller.hostHasTempo())
            return "Host " + juce::String (controller.currentBpm(), 1) + " BPM";

        return controller.parameter (params::bpm).getText (v01, 32);
    });

    tail.setBounds (displayCard().toNearestInt());
    tail.attachHandles (controller.parameter (params::decay), controller.parameter (params::preDelay), &controller.getUndoManager());
    tail.setHeadLayout (decayHero().getRight() - displayCard().getX() + 12.0f, freezeButton().getX() - displayCard().getX() - 18.0f);
    meters.setBounds (studio::meters().toNearestInt());
}

StudioSlider* StudioPanel::findSlider (const juce::String& id) const
{
    for (const auto& s : sliders)
        if (s->getParameter().getParameterID() == id)
            return s.get();

    return nullptr;
}

StudioToggle* StudioPanel::findToggle (const juce::String& id) noexcept
{
    for (auto& [toggleId, toggle] : toggles)
        if (toggleId == id)
            return toggle;

    return nullptr;
}

juce::String StudioPanel::getCharacterTitle() const
{
    return "CHARACTER " + params::algorithmNames()[(int) shown.algorithm];
}

//==============================================================================
void StudioPanel::updateMeanings()
{
    const auto m = controller.readMeanings();
    const auto first = ! meaningsShown;

    if (! first && m == shown)
        return;

    const auto previous = shown;
    shown = m;
    meaningsShown = true;

    if (first || m.preDelaySynced != previous.preDelaySynced)
    {
        preDelaySlider->rebind (controller.parameter (PanelController::preDelayParameterId (m)));
        preDelaySlider->setSteps (m.preDelaySynced ? numNoteValues : 0);
        tail.setPreDelayDraggable (! m.preDelaySynced);

        if (m.preDelaySynced)
        {
            // The note as the value, its length under the bar, so the row never truncates.
            preDelaySlider->setValueToText ([] (float v01) { return PanelController::noteName (noteIndex (v01)); });
            preDelaySlider->setCaption ([this] { return "= " + controller.noteLengthText (noteIndex (preDelaySlider->getNormalisedValue())); });
            preDelaySlider->setTextToValue (noteFromText);
        }
        else
        {
            preDelaySlider->setValueToText ([this] (float v01) { return controller.preDelayText (shown, v01); });
            preDelaySlider->setCaption (nullptr);
            preDelaySlider->setTextToValue (millisecondsFromText (controller.parameter (params::preDelay)));
        }
    }

    if (first || m.algorithm != previous.algorithm || m.echoNoteOnParamA != previous.echoNoteOnParamA)
    {
        const auto a = m.algorithm;

        if (first || m.echoNoteOnParamA != previous.echoNoteOnParamA)
            paramASlider->rebind (controller.parameter (PanelController::paramAParameterId (m)));

        paramASlider->setLabel (PanelController::characterLegend (a, false));
        paramBSlider->setLabel (PanelController::characterLegend (a, true));
        paramASlider->setBadge ("A");
        paramBSlider->setBadge ("B");
        paramASlider->setSteps (m.echoNoteOnParamA ? numNoteValues : PanelController::characterSteps (a, false));
        paramBSlider->setSteps (PanelController::characterSteps (a, true));
        paramASlider->setBipolar (a == Algorithm::room);   // Early <-> Late, even in the middle

        if (m.echoNoteOnParamA)
        {
            paramASlider->setValueToText ([] (float v01) { return PanelController::noteName (noteIndex (v01)); });
            paramASlider->setCaption ([this] { return "= " + controller.noteLengthText (noteIndex (paramASlider->getNormalisedValue())); });
        }
        else
        {
            paramASlider->setValueToText ([this] (float v01) { return controller.characterText (shown, false, v01); });
            paramASlider->setCaption (nullptr);
        }

        paramBSlider->setValueToText ([this] (float v01) { return controller.characterText (shown, true, v01); });

        if (m.echoNoteOnParamA)
            paramASlider->setTextToValue (noteFromText);
        else
            paramASlider->setTextToValue ([a] (const juce::String& t) { const auto pct = params::characterPercentFromText (a, false, t); return pct < 0.0f ? -1.0f : pct / 100.0f; });

        paramBSlider->setTextToValue ([a] (const juce::String& t) { const auto pct = params::characterPercentFromText (a, true, t); return pct < 0.0f ? -1.0f : pct / 100.0f; });

        // Echo's SYNC key sits on Param A only while Echo is selected.
        const auto isEcho = a == Algorithm::echo;
        echoSyncKey.setVisible (isEcho);
        paramASlider->setTrailingSpace (isEcho ? 48.0f : 0.0f);

        repaint (studio::paramCard (1).withHeight (studio::cardTitleHeight).toNearestInt());
    }

    if (first || m.freezeMode != previous.freezeMode)
    {
        const auto momentary = m.freezeMode == FreezeMode::momentary;
        freezeKey.setMomentary (momentary);
        freezeKey.setLabel (momentary ? "FREEZE (HOLD)" : "FREEZE");
        freezeKey.setTooltip (momentary ? "Freeze (momentary): holds the tail while you hold the key. Dry keeps playing over it."
                                        : "Freeze: holds the tail indefinitely; new input does not add to it. Dry keeps playing over it.");
    }
}

void StudioPanel::updateTempo()
{
    const auto host = controller.hostHasTempo();
    const auto bpm = controller.currentBpm();

    if (host != shownHostTempo || (host && std::abs (bpm - shownBpm) > 0.05))
    {
        shownHostTempo = host;
        shownBpm = bpm;
        bpmSlider->setEnabled (! host);
        bpmSlider->setDimmed (host);
        bpmSlider->repaint();
    }

    // Duck off: its controls and meter are shown greyed out (still editable).
    const auto duck = controller.flag (params::duck);

    if (duck != shownDuck || ! statusShown)
    {
        statusShown = true;
        shownDuck = duck;

        for (const auto* id : { params::duckAmount, params::duckThreshold, params::duckRelease })
            if (auto* s = findSlider (id))
                s->setDimmed (! duck);

        meters.setDuckEnabled (duck);
    }

    // Synced pre-delay / echo values show their length at the current tempo.
    if (shown.preDelaySynced || shown.echoNoteOnParamA)
    {
        preDelaySlider->repaint();
        paramASlider->repaint();
    }
}

void StudioPanel::valueTreeRedirected (juce::ValueTree&)
{
    header.refresh();
}

void StudioPanel::animateFrame (double timeSeconds)
{
    const auto dt = lastFrameTime < 0.0 ? (1.0f / 60.0f)
                                        : (float) juce::jlimit (0.0, 0.1, timeSeconds - lastFrameTime);
    lastFrameTime = timeSeconds;

    updateMeanings();
    tail.setShape (controller.currentTailShape());
    tail.tick (dt, processor.getMeters().wetRms.load (std::memory_order_relaxed));
    meters.setWetMuted (tail.getShape().bypassed);
    meters.tick (dt);

    if (++framesSinceRefresh >= 6)
    {
        framesSinceRefresh = 0;
        updateTempo();
        header.refresh();
    }
}

//==============================================================================
void StudioPanel::resized()
{
    header.setBounds (layout::header().toNearestInt());
    dialog.setBounds (getLocalBounds());
    background.invalidate();
}

void StudioPanel::paint (juce::Graphics& g)
{
    background.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float)
    {
        renderBackground (lg, a);
    });

    // The Character card's title names the algorithm (at the right, so the long names fit).
    const auto card = studio::paramCard (1);
    const auto titleArea = juce::Rectangle<float> (card.getX() + studio::cardPad, card.getY(), card.getWidth() - 2.0f * studio::cardPad, studio::cardTitleHeight);
    g.setFont (drawing::tracked (fonts->legend (studio::titleSize), studio::titleTracking));
    g.setColour (Theme::studio().title);
    g.drawText ("CHARACTER", titleArea, juce::Justification::centredLeft, false);
    g.setFont (fonts->label (studio::valueSize - 1.0f));
    g.setColour (Theme::studio().accent);
    g.drawText (params::algorithmNames()[(int) shown.algorithm], titleArea, juce::Justification::centredRight, false);
}

void StudioPanel::renderBackground (juce::Graphics& g, juce::Rectangle<float> area) const
{
    using namespace studio;
    const auto& p = Theme::studio();

    g.setColour (p.background);
    g.fillRect (area);

    // The header rail sits on a hairline.
    g.setColour (juce::Colours::black);
    g.drawHorizontalLine ((int) metrics::headerHeight, 0.0f, area.getRight());

    auto card = [&] (juce::Rectangle<float> r, const juce::String& title)
    {
        g.setColour (p.card);
        g.fillRoundedRectangle (r, cardCorner);
        g.setColour (p.cardEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), cardCorner, 1.0f);

        if (title.isNotEmpty())
        {
            g.setFont (drawing::tracked (fonts->legend (titleSize), titleTracking));
            g.setColour (p.title);
            g.drawText (title, juce::Rectangle<float> (r.getX() + cardPad, r.getY(), r.getWidth() - 2.0f * cardPad, cardTitleHeight),
                        juce::Justification::centredLeft, false);
        }
    };

    card (algorithmCard(), "ALGORITHM");
    card (behaviourCard(), "BEHAVIOUR");
    card (displayCard(), {});                  // the tail graph draws its own title and read-outs
    card (outputCard(), "OUTPUT");
    card (paramCard (0), "SPACE");
    card (paramCard (1), {});                  // titled per algorithm in paint()
    card (paramCard (2), "TONE");
    card (paramCard (3), "DUCK");

    // Output: a rule between the meters and the controls.
    {
        const auto m = studio::meters();
        g.setColour (p.cardEdge);
        g.drawHorizontalLine (juce::roundToInt (m.getBottom() + 4.0f), m.getX(), m.getRight());
    }

    // Duck: a note in its free row that explains both states.
    {
        const auto r = paramRow (3, 3);
        g.setFont (fonts->label (studio::smallSize));
        g.setColour (p.textFaint);
        g.drawFittedText ("Off: the wet stays at full level. On: it dips while you play and blooms back when you stop.",
                          r.withTrimmedTop (-4.0f).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }

    // Tone: the input filters under their own small heading, as on the panel (INPUT).
    {
        const auto above = paramRow (2, 1), below = paramRow (2, 2);
        const auto y = 0.5f * (above.getBottom() + below.getY()) + 1.0f;
        const auto font = drawing::tracked (fonts->legend (8.5f), 0.18f);
        const auto w = juce::GlyphArrangement::getStringWidth (font, "INPUT") + 6.0f;
        g.setColour (p.cardEdge);
        g.drawHorizontalLine (juce::roundToInt (y), above.getX() + w + 2.0f, above.getRight());
        g.setFont (font);
        g.setColour (p.textFaint);
        g.drawText ("INPUT", juce::Rectangle<float> (above.getX(), y - 6.0f, w, 12.0f), juce::Justification::centredLeft, false);
    }

    // Behaviour: labels left of their controls.
    {
        g.setFont (drawing::tracked (fonts->legend (studio::labelSize), studio::labelTracking));
        g.setColour (p.textDim);
        g.drawText ("TRAILS", behaviourLabel (0), juce::Justification::centredLeft, false);
        g.drawText ("FREEZE", behaviourLabel (1), juce::Justification::centredLeft, false);
        g.drawText ("LAYER", behaviourLabel (2), juce::Justification::centredLeft, false);
    }
}

} // namespace cerulean::ui
