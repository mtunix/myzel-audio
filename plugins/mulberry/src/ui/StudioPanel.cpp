#include "ui/StudioPanel.h"

#include "dsp/NoteValues.h"
#include "plugin/PluginProcessor.h"

namespace mulberry::ui
{

namespace
{
    constexpr int machineRadioGroup = 1101, modeRadioGroup = 1102;

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
        return index < 0 ? -1.0f : (float) index / (float) (notes::count - 1);
    }

    int noteIndex (float v01) { return juce::roundToInt (v01 * (float) (notes::count - 1)); }
}

//==============================================================================
StudioEcho::StudioEcho()
{
    setTitle ("Echo graph");
    setTooltip ("The echoes of a click, from your settings: left output on top, right below. In Pattern mode, drag the steps.");
}

void StudioEcho::setShape (const EchoShape& s)
{
    if (hasShape && s == shape)
        return;
    hasShape = true;
    shape = s;
    span = chooseSpan (shape);
    events = echoEvents (shape, span);
    repaint();
}

juce::Rectangle<float> StudioEcho::lane (int index) const
{
    auto a = getLocalBounds().toFloat().withTrimmedBottom (16.0f).withTrimmedLeft (18.0f);
    const auto h = (a.getHeight() - 8.0f) * 0.5f;
    return index == 0 ? a.withHeight (h) : a.withTrimmedTop (h + 8.0f);
}

float StudioEcho::xFor (float ms) const
{
    const auto a = lane (0);
    return a.getX() + a.getWidth() * std::clamp (ms / span, 0.0f, 1.0f);
}

juce::Rectangle<float> StudioEcho::stepArea (int k) const
{
    const auto n = std::clamp (shape.plan.steps, 1, dsp::maxPatternSteps);
    if (shape.plan.mode != Mode::pattern || k < 0 || k >= n)
        return {};
    const auto stepLength = 1.0f / (float) n;
    const auto t = ((float) (k + 1) * stepLength + ((k % 2 == 1) ? shape.plan.groove * stepLength : 0.0f)) * shape.timeMs[0] + shape.plan.feelMs;
    const auto top = lane (0).getY(), bottom = lane (1).getBottom();
    return { xFor (t) - 6.0f, top, 12.0f, bottom - top };
}

void StudioEcho::paint (juce::Graphics& g)
{
    const auto& sp = Theme::studio();
    const auto& ex = Theme::studioExtras();
    g.setColour (sp.inset);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    const auto font = drawing::tracked (fonts->label (studio::smallSize), 0.02f);
    g.setFont (font);
    for (int l = 0; l < 2; ++l)
    {
        const auto a = lane (l);
        g.setColour (sp.grid);
        for (float db = -12.0f; db > -48.0f; db -= 12.0f)
            g.drawHorizontalLine (juce::roundToInt (a.getBottom() - a.getHeight() * (db + 48.0f) / 48.0f), a.getX(), a.getRight());
        g.drawHorizontalLine (juce::roundToInt (a.getBottom()), a.getX(), a.getRight());
        g.setColour (sp.gridText);
        g.drawText (l == 0 ? "L" : "R", juce::Rectangle<float> (16.0f, 14.0f).withPosition (2.0f, a.getY() + 2.0f), juce::Justification::centred, false);
    }

    // Time axis: beats when synced, else round milliseconds.
    const auto top = lane (0).getY(), bottom = lane (1).getBottom();
    auto tick = [&] (float ms, const juce::String& text)
    {
        const auto x = xFor (ms);
        g.setColour (sp.grid);
        g.drawVerticalLine (juce::roundToInt (x), top, bottom);
        if (text.isNotEmpty())
        {
            g.setColour (sp.gridText);
            g.drawText (text, juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ x, bottom + 8.0f }).constrainedWithin (getLocalBounds().toFloat()),
                        juce::Justification::centred, false);
        }
    };
    if (shape.synced && shape.bpm > 0.0)
    {
        const auto beat = (float) (60000.0 / shape.bpm);
        const auto beats = (int) std::round (span / beat);
        for (int b = 0; b <= beats; ++b)
            tick ((float) b * beat, (beats <= 8 || b % 4 == 0) ? juce::String (b) : juce::String());
    }
    else
    {
        const auto step = span > 6000.0f ? 1000.0f : span > 2500.0f ? 500.0f : span > 1000.0f ? 250.0f : 100.0f;
        for (float t = 0.0f; t <= span + 0.5f; t += step)
            tick (t, params::formatMs (t));
    }

    for (const auto& e : events)
        for (int l = 0; l < 2; ++l)
        {
            const auto level = l == 0 ? e.left : e.right;
            if (level < 0.004f)
                continue;
            const auto a = lane (l);
            const auto db = juce::Decibels::gainToDecibels (level, -120.0f);
            const auto h = a.getHeight() * std::clamp ((db + 48.0f) / 48.0f, 0.0f, 1.0f);
            g.setColour (l == 0 ? ex.lineLeft : ex.lineRight);
            g.fillRect (juce::Rectangle<float> (2.0f, h).withPosition (xFor (e.timeMs) - 1.0f, a.getBottom() - h));
        }

    if (shape.plan.mode == Mode::pattern)
        for (int k = 0; k < std::clamp (shape.plan.steps, 1, dsp::maxPatternSteps); ++k)
        {
            const auto area = stepArea (k);
            const auto level = std::clamp (shape.plan.stepLevels[(size_t) k], 0.0f, 1.0f);
            g.setColour (ex.step);
            g.fillRoundedRectangle (area.withTop (area.getBottom() - area.getHeight() * level).reduced (2.0f, 0.0f), 2.0f);
        }

    if (shape.bypassed || shape.hold)
    {
        const auto text = shape.bypassed ? juce::String::fromUTF8 ("BYPASSED \xc2\xb7 DRY ONLY") : juce::String ("HOLD");
        const auto colour = shape.bypassed ? sp.caution : sp.accent;
        const auto f = drawing::tracked (fonts->legend (studio::titleSize), studio::titleTracking);
        const auto w = juce::GlyphArrangement::getStringWidth (f, text) + 20.0f;
        const auto box = juce::Rectangle<float> (w, 22.0f).withCentre (getLocalBounds().toFloat().getCentre());
        g.setColour (sp.inset.withAlpha (0.9f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (box, 4.0f, 1.2f);
        g.setFont (f);
        g.drawText (text, box, juce::Justification::centred, false);
    }
}

void StudioEcho::mouseDown (const juce::MouseEvent& e)
{
    for (int k = 0; k < dsp::maxPatternSteps; ++k)
        if (stepArea (k).expanded (3.0f, 0.0f).contains (e.position))
        {
            dragging = k;
            if (onStepGestureStart != nullptr)
                onStepGestureStart (k);
            mouseDrag (e);
            return;
        }
}

void StudioEcho::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || onStepLevel == nullptr)
        return;
    const auto a = stepArea (dragging);
    onStepLevel (dragging, std::clamp ((a.getBottom() - e.position.y) / a.getHeight(), 0.0f, 1.0f));
}

void StudioEcho::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0 && onStepGestureEnd != nullptr)
        onStepGestureEnd (dragging);
    dragging = -1;
}

//==============================================================================
StudioPanel::StudioPanel (PanelController& c)
    : controller (c),
      processor (c.getProcessor()),
      meters ({ { "IN",  [&m = processor.getMeters()] { return m.inputRms.load (std::memory_order_relaxed); },
                         [&m = processor.getMeters()] { return m.inputPeak.exchange (0.0f, std::memory_order_relaxed); } },
                { "WET", [&m = processor.getMeters()] { return m.wetRms.load (std::memory_order_relaxed); },
                         [&m = processor.getMeters()] { return m.wetPeak.exchange (0.0f, std::memory_order_relaxed); } },
                { "OUT", [&m = processor.getMeters()] { return m.outputRms.load (std::memory_order_relaxed); },
                         [&m = processor.getMeters()] { return m.outputPeak.exchange (0.0f, std::memory_order_relaxed); } } },
              StudioMeters::Reduction { "DUCK", [&m = processor.getMeters()] { return m.duckGain.load (std::memory_order_relaxed); } },
              "Input, wet and output level (bar: RMS, line: peak hold, number: peak) and the ducker's gain reduction. Click to reset the peaks.")
{
    setOpaque (true);
    setSize (metrics::baseWidth, metrics::baseHeight);
    setWantsKeyboardFocus (false);

    buildControls();
    graph.setBounds (studio::graph().toNearestInt());
    addAndMakeVisible (graph);
    meters.setBounds (studio::meters().toNearestInt());
    addAndMakeVisible (meters);

    const auto labelFont = drawing::tracked (fonts->legend (studio::labelSize), studio::labelTracking);
    const auto row = studio::outputRow (0);
    mixLockAnchor = { juce::roundToInt (row.getX() + juce::GlyphArrangement::getStringWidth (labelFont, "MIX") + 10.0f), juce::roundToInt (row.getY() + 8.5f) };

    updateMeanings();
    graph.setShape (controller.currentShape());
}

StudioPanel::~StudioPanel() = default;

StudioSlider& StudioPanel::addSlider (const char* id, const juce::String& label, juce::Rectangle<float> bounds, const juce::String& tip)
{
    auto slider = std::make_unique<StudioSlider> (controller.parameter (id), &controller.getUndoManager(), label);
    slider->setBounds (bounds.toNearestInt());
    slider->setTooltip (tip);
    addAndMakeVisible (*slider);
    sliders.push_back (std::move (slider));
    return *sliders.back();
}

void StudioPanel::addToggle (StudioToggle& toggle, const char* id, juce::Rectangle<float> bounds, const juce::String& tip)
{
    toggle.setBounds (bounds.toNearestInt());
    toggle.setClickingTogglesState (true);
    toggle.setTooltip (tip);
    addAndMakeVisible (toggle);
    buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (controller.parameter (id), toggle, &controller.getUndoManager()));
}

void StudioPanel::buildControls()
{
    using namespace studio;
    auto* um = &controller.getUndoManager();

    for (int i = 0; i < numMachines; ++i)
    {
        const auto m = static_cast<Machine> (i);
        auto row = std::make_unique<SelectorRow> (i, info (m).name, info (m).tagline);
        row->setBounds (machineRow (i).toNearestInt());
        row->setTooltip (PanelController::machineDescription (m));
        row->setRadioGroupId (machineRadioGroup, juce::dontSendNotification);
        row->setClickingTogglesState (false);
        row->onClick = [this, i] { machineAttachment->setValueAsCompleteGesture ((float) i); };
        row->onStep = [this, i] (int delta)
        {
            const auto next = juce::jlimit (0, numMachines - 1, i + delta);
            machineAttachment->setValueAsCompleteGesture ((float) next);
            machineRows[(size_t) next]->grabKeyboardFocus();
        };
        addAndMakeVisible (*row);
        machineRows[(size_t) i] = std::move (row);
    }
    machineAttachment = std::make_unique<juce::ParameterAttachment> (controller.parameter (params::machine), [this] (float v)
    {
        for (int i = 0; i < numMachines; ++i)
            machineRows[(size_t) i]->setToggleState (i == juce::roundToInt (v), juce::dontSendNotification);
    }, um);
    machineAttachment->sendInitialUpdate();

    const std::array<std::pair<const char*, const char*>, numModes> modes { { { "Single", "Each side itself" }, { "Dual", "Two times" },
                                                                             { "Ping-Pong", "Left, right, left" }, { "Pattern", "Up to 16 steps" } } };
    for (int i = 0; i < numModes; ++i)
    {
        auto row = std::make_unique<SelectorRow> (i, modes[(size_t) i].first, modes[(size_t) i].second);
        row->setBounds (modeRow (i).toNearestInt());
        row->setTooltip (PanelController::modeDescription (static_cast<Mode> (i)));
        row->setRadioGroupId (modeRadioGroup, juce::dontSendNotification);
        row->setClickingTogglesState (false);
        row->onClick = [this, i] { modeAttachment->setValueAsCompleteGesture ((float) i); };
        row->onStep = [this, i] (int delta)
        {
            const auto next = juce::jlimit (0, numModes - 1, i + delta);
            modeAttachment->setValueAsCompleteGesture ((float) next);
            modeRows[(size_t) next]->grabKeyboardFocus();
        };
        addAndMakeVisible (*row);
        modeRows[(size_t) i] = std::move (row);
    }
    modeAttachment = std::make_unique<juce::ParameterAttachment> (controller.parameter (params::mode), [this] (float v)
    {
        for (int i = 0; i < numModes; ++i)
            modeRows[(size_t) i]->setToggleState (i == juce::roundToInt (v), juce::dontSendNotification);
    }, um);
    modeAttachment->sendInitialUpdate();

    timeSlider = &addSlider (params::time, "TIME", timeHero(), "Time: the echo time, 1 ms to 4 s, or a note value with SYNC. Click the value to type one (\"375 ms\", \"1/8D\").");
    timeSlider->setLook (StudioSlider::Look::hero);
    rightSlider = &addSlider (params::timeRight, "RIGHT", rightTimeHero(), "Right time (Dual): the right side's echo time, or its note value with SYNC.");
    rightSlider->setLook (StudioSlider::Look::hero);

    addToggle (syncKey, params::sync, syncChip(), "Sync: Time as a note value at the host tempo (or BPM).");
    tapKey.setBounds (tapChip().toNearestInt());
    tapKey.setClickingTogglesState (false);
    tapKey.setTooltip ("Tap: tap twice or more to set Time (free; Sync turns off). The average of the last four taps.");
    tapKey.onClick = [this] { controller.tap (juce::Time::getMillisecondCounterHiRes() * 0.001); };
    addAndMakeVisible (tapKey);

    holdKey.setBounds (holdBlock().toNearestInt());
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
        if (down) { holdAttachment->beginGesture(); holdAttachment->setValueAsPartOfGesture (1.0f); }
        else      { holdAttachment->setValueAsPartOfGesture (0.0f); holdAttachment->endGesture(); }
    };

    addSlider (params::feedback, "FEEDBACK", paramRow (0, 0), "Feedback: up to 100 % trails fade; above, the machine runs away into its own saturation.");
    addSlider (params::drive, "DRIVE", paramRow (0, 1), "Drive: record level into the machine (colour, not echo level).").setBipolar (true);
    addSlider (params::wobble, "WOBBLE", paramRow (0, 2), "Wobble: transport or clock instability; the middle is the machine's spec.");
    addSlider (params::wobbleRate, "RATE", paramRow (0, 3), "Wobble Rate: the machine's own rates, 0.25x - 4x.").setBipolar (true);
    addSlider (params::wobbleShape, "SHAPE", paramRow (0, 4), "Wobble Shape: periodic against random; 50 % is the machine's own mix.").setBipolar (true);

    paramASlider = &addSlider (params::paramA, "A", paramRow (1, 0), "Param A: the machine's first character control.");
    paramBSlider = &addSlider (params::paramB, "B", paramRow (1, 1), "Param B: the machine's second character control.");
    addSlider (params::diffuse, "DIFFUSE", paramRow (1, 2), "Diffuse: an allpass smear inside the loop: each repeat blurs more.");
    addSlider (params::lowCut, "LOW CUT", paramRow (1, 3), "Low Cut: in the loop; every repeat thins out more. Far left is off.");
    addSlider (params::highCut, "HIGH CUT", paramRow (1, 4), "High Cut: in the loop; every repeat darkens more. Far right is off.").setFillFromRight (true);

    addSlider (params::width, "WIDTH", paramRow (2, 0), "Width: how far the echoes spread; 0 is a mono echo.");
    addSlider (params::crossFeed, "CROSS-FEED", paramRow (2, 1), "Cross-feed (Dual): how much of each side's feedback goes into the other.");
    addSlider (params::panRule, "PANS", paramRow (2, 2), "Pan rule (Pattern): centre, alternate, sweep or scatter.").setSteps (numPanRules);
    addSlider (params::steps, "STEPS", paramRow (2, 3), "Steps (Pattern): 1 to 16, evenly over Time.").setSteps (params::maxSteps);

    addSlider (params::groove, "GROOVE", paramRow (3, 0), "Groove: swing, 50 % (straight) to 75 %.");
    addSlider (params::feel, "FEEL", paramRow (3, 1), "Feel: pushes or drags every echo, up to 30 ms (a 1/64 note when synced).").setBipolar (true);
    addSlider (params::noise, "NOISE", paramRow (3, 2), "Noise: the machine's constant floor; 50 % is its spec, 0 is silent.");
    addSlider (params::bpm, "TEMPO", paramRow (3, 3), "BPM: the tempo for synced times when the host gives none.");

    addSlider (params::duckAmount, "AMOUNT", paramRow (4, 0), "Duck Amount: how far the echoes dip while you play.");
    addSlider (params::duckThreshold, "THRESH", paramRow (4, 1), "Duck Threshold: the playing level where ducking starts.");
    addSlider (params::duckRelease, "RELEASE", paramRow (4, 2), "Duck Release: how fast the echoes bloom back.")
        .setTextToValue (millisecondsFromText (controller.parameter (params::duckRelease)));
    addToggle (duckKey, params::duck, titleChip (paramCard (4), 44.0f), "Duck: the echoes dip while you play and bloom back when you stop.");

    addSlider (params::mix, "MIX", outputRow (0), "Mix: pedal law. Up to 50 % the dry stays at full level; past 50 % it fades.");
    addSlider (params::output, "OUTPUT", outputRow (1), "Output: level after the mix, -24 to +24 dB.").setBipolar (true);
    addToggle (wetKey, params::wetOnly, wetButton(), "Wet only: kills the dry signal, for send/return use.");

    addToggle (trailsKey, params::trails, trailsSwitch(), "Trails: on Bypass, machine and preset changes the old echoes ring out.");
    for (auto* k : { &latchKey, &momentaryKey })
    {
        addAndMakeVisible (*k);
        k->setClickingTogglesState (false);
    }
    latchKey.setBounds (holdModeLatch().toNearestInt());
    momentaryKey.setBounds (holdModeMomentary().toNearestInt());
    latchKey.setTooltip ("Hold latches on each press.");
    momentaryKey.setTooltip ("Hold lasts while the key is held.");
    holdModeAttachment = std::make_unique<juce::ParameterAttachment> (controller.parameter (params::holdMode), [this] (float v)
    {
        latchKey.setToggleState (v < 0.5f, juce::dontSendNotification);
        momentaryKey.setToggleState (v >= 0.5f, juce::dontSendNotification);
    }, um);
    holdModeAttachment->sendInitialUpdate();
    latchKey.onClick = [this] { holdModeAttachment->setValueAsCompleteGesture (0.0f); };
    momentaryKey.onClick = [this] { holdModeAttachment->setValueAsCompleteGesture (1.0f); };

    for (int k = 0; k < params::maxSteps; ++k)
        stepAttachments[(size_t) k] = std::make_unique<juce::ParameterAttachment> (
            controller.parameter (params::stepId (k).toRawUTF8()), [] (float) {}, um);
    graph.onStepGestureStart = [this] (int k) { stepAttachments[(size_t) k]->beginGesture(); };
    graph.onStepLevel = [this] (int k, float level) { stepAttachments[(size_t) k]->setValueAsPartOfGesture (level * 100.0f); };
    graph.onStepGestureEnd = [this] (int k) { stepAttachments[(size_t) k]->endGesture(); };
}

StudioSlider* StudioPanel::findSlider (const juce::String& parameterId) const
{
    for (const auto& s : sliders)
        if (s->getParameter().getParameterID() == parameterId)
            return s.get();
    return nullptr;
}

void StudioPanel::updateMeanings()
{
    const auto m = controller.readMeanings();
    if (meaningsShown && m == shown)
        return;
    const auto first = ! meaningsShown;
    const auto previous = shown;
    shown = m;
    meaningsShown = true;

    if (first || m.synced != previous.synced)
    {
        timeSlider->rebind (controller.parameter (PanelController::timeParameterId (m)));
        rightSlider->rebind (controller.parameter (m.synced ? params::noteRight : params::timeRight));
        for (auto* s : { timeSlider, rightSlider })
        {
            if (m.synced)
            {
                s->setSteps (notes::count);
                s->setValueToText ([] (float v01) { return PanelController::noteName (noteIndex (v01)); });
                s->setTextToValue (noteFromText);
                s->setCaption ([this, s] { return "= " + controller.noteLengthText (noteIndex (s->getNormalisedValue())); });
            }
            else
            {
                s->setSteps (0);
                s->setValueToText (nullptr);
                s->setTextToValue (millisecondsFromText (s->getParameter()));
                s->setCaption (nullptr);
            }
        }
    }

    rightSlider->setDimmed (m.mode != Mode::dual);

    if (first || m.machine != previous.machine)
    {
        paramASlider->setLabel (PanelController::characterLegend (m.machine, false));
        paramBSlider->setLabel (PanelController::characterLegend (m.machine, true));
        paramASlider->setBadge ("A");
        paramBSlider->setBadge ("B");
        paramASlider->setSteps (PanelController::characterSteps (m.machine, false));
        paramBSlider->setSteps (PanelController::characterSteps (m.machine, true));
        paramASlider->setValueToText ([this] (float v01) { return controller.characterText (shown, false, v01); });
        paramBSlider->setValueToText ([this] (float v01) { return controller.characterText (shown, true, v01); });
        paramASlider->setTextToValue ([this] (const juce::String& t)
        {
            const auto pct = params::characterPercentFromText (shown.machine, false, t);
            return pct < 0.0f ? -1.0f : pct / 100.0f;
        });
        paramBSlider->setTextToValue ([this] (const juce::String& t)
        {
            const auto pct = params::characterPercentFromText (shown.machine, true, t);
            return pct < 0.0f ? -1.0f : pct / 100.0f;
        });
        repaint (studio::paramCard (1).toNearestInt());
    }

    if (first || m.holdMode != previous.holdMode)
    {
        const auto momentary = m.holdMode == HoldMode::momentary;
        holdKey.setMomentary (momentary);
        holdKey.setTooltip (momentary ? "Hold (Press): the current loop plays on while you hold the key." : "Hold: the current loop plays on, losslessly, while you play over it.");
    }
}

void StudioPanel::animateFrame (float dt)
{
    updateMeanings();
    graph.setShape (controller.currentShape());
    meters.tick (dt);

    if (++frame % 6 == 0)
    {
        const auto hostTempo = controller.hostHasTempo();
        if (auto* bpm = findSlider (params::bpm))
        {
            bpm->setDimmed (hostTempo);
            bpm->setEnabled (! hostTempo);
        }
        meters.setReductionEnabled (controller.flag (params::duck));
        meters.setChannelMuted (1, controller.flag (params::bypass));
        for (auto* id : { params::duckAmount, params::duckThreshold, params::duckRelease })
            if (auto* s = findSlider (id))
                s->setDimmed (! controller.flag (params::duck));
        if (auto* s = findSlider (params::crossFeed))
            s->setDimmed (shown.mode != Mode::dual);
        for (auto* id : { params::panRule, params::steps })
            if (auto* s = findSlider (id))
                s->setDimmed (shown.mode != Mode::pattern);
    }
}

void StudioPanel::paint (juce::Graphics& g)
{
    using namespace studio;
    const auto& sp = Theme::studio();
    g.fillAll (sp.background);

    drawCard (g, machineCard(), "MACHINE", *fonts);
    drawCard (g, modeCard(), "MODE", *fonts);
    drawCard (g, echoCard(), {}, *fonts);
    drawCard (g, outputCard(), "OUTPUT", *fonts);
    const std::array<const char*, numParamCards> titles { "LOOP", "MACHINE", "STEREO", "TIME", "DUCK" };
    for (int i = 0; i < numParamCards; ++i)
        drawCard (g, paramCard (i), titles[(size_t) i], *fonts);

    const auto labelFont = drawing::tracked (fonts->legend (labelSize), labelTracking);
    g.setFont (labelFont);
    g.setColour (sp.textDim);
    g.drawText ("TRAILS", behaviourLabel (0), juce::Justification::centredLeft, false);
    g.drawText ("HOLD", behaviourLabel (1), juce::Justification::centredLeft, false);

    // The machine's name, right-aligned in its card's title.
    g.setColour (sp.accent);
    g.setFont (drawing::tracked (fonts->legend (titleSize), titleTracking));
    g.drawText (juce::String (info (shown.machine).name).toUpperCase(),
                paramCard (1).withHeight (cardTitleHeight).reduced (8.0f, 0.0f), juce::Justification::centredRight, false);
}

} // namespace mulberry::ui
