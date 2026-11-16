#include "ui/ControlBar.h"

#include "plugin/PluginProcessor.h"

namespace halite::ui
{

namespace
{
    int labelWidth (const juce::String& text)
    {
        const juce::SharedResourcePointer<Fonts> fonts;
        return (int) std::ceil (textWidth (fonts->sans (type::label), text)) + 4;
    }
}

//==============================================================================
MoreButton::MoreButton()
    : HelpedButton ("More")
{
    setHelp (helpFor ("more"));
}

void MoreButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    const auto vertical = area.getHeight() > area.getWidth();
    const auto ink = open || highlighted ? t.text : t.secondary;

    g.setColour (draw::buttonFill (t, open, down));
    g.fillRoundedRectangle (area, draw::buttonCorner);
    g.setColour (highlighted ? t.zeroLine : t.line);
    g.drawRoundedRectangle (area, draw::buttonCorner, 1.0f);

    // Closed, the caret points up: the row opens above the bar.
    const auto caret = open ? Icon::caretDown : Icon::caretUp;
    g.setFont (fonts->sansMedium (type::axis).withExtraKerningFactor (0.06f));

    if (vertical)
    {
        const auto centre = area.getCentre();
        draw::icon (g, caret, juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ centre.x, centre.y - 18.0f }), ink);

        g.setColour (ink);
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, centre.x, centre.y + 6.0f));
        g.drawText ("MORE", juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ centre.x, centre.y + 6.0f }), juce::Justification::centred, false);
        g.restoreState();
    }
    else
    {
        auto content = area.reduced (10.0f, 0.0f);
        draw::icon (g, caret, content.removeFromRight (10.0f).withSizeKeepingCentre (10.0f, 10.0f), ink);
        g.setColour (ink);
        g.setFont (fonts->sans (type::button));
        g.drawText ("More", content, juce::Justification::centred, false);
    }

    if (hasKeyboardFocus (false))
        draw::focusRing (g, area, draw::buttonCorner);
}

//==============================================================================
ControlBar::ControlBar (HaliteController& c)
    : controller (c),
      essThresholdValue (c, [&c] { return c.thresholdId (Band::ess); }),
      essRangeValue (c, params::range),
      focusValue (c, params::focus),
      lowValue (c, params::detectLow),
      highValue (c, params::detectHigh),
      harshThresholdValue (c, [&c] { return c.thresholdId (Band::harsh); }),
      harshRangeValue (c, params::harshRange),
      freqValue (c, params::harshFreq),
      widthValue (c, params::harshWidth),
      harshSpeedValue (c, params::harshSpeedAuto, params::harshRelease),
      essThreshold (essThresholdValue, "Threshold"),
      essRange (essRangeValue, "Range"),
      focus (focusValue, "Focus"),
      harshThreshold (harshThresholdValue, "Threshold"),
      harshRange (harshRangeValue, "Range"),
      harshSpeed (harshSpeedValue, "Speed"),
      low (lowValue, "Low"),
      high (highValue, "High"),
      freq (freqValue, "Freq"),
      width (widthValue, "Width"),
      shape ("Shape", params::shapeNames()),
      detection ("Detection", params::detectionNames())
{
    setTitle ("Control bar");
    setFocusContainerType (FocusContainerType::focusContainer);

    const auto essInk = [] { return theme::current().essText; };
    const auto harshInk = [] { return theme::current().harshText; };

    for (auto* k : { &essThreshold, &essRange, &focus })
        k->accent = essInk;
    for (auto* k : { &harshThreshold, &harshRange, &harshSpeed })
        k->accent = harshInk;

    // Accessible names say which band; the labels under the knobs need not.
    essThreshold.setTitle ("Ess threshold");
    essRange.setTitle ("Ess range");
    harshThreshold.setTitle ("Harsh threshold");
    harshRange.setTitle ("Harsh range");
    harshSpeed.setTitle ("Harsh speed");
    low.setTitle ("Detect low");
    high.setTitle ("Detect high");
    freq.setTitle ("Harsh freq");
    width.setTitle ("Harsh width");

    for (auto* f : { &low, &high, &freq, &width })
        f->setWidths ((float) fieldLabelWidth, (float) fieldBoxWidth);

    essPill.ink = essInk;
    essPill.setTitle ("Ess band");
    essPill.setHelp (helpFor ("bandEss"));
    essPill.onClick = [this] { controller.setBand (Band::ess); };

    harshPill.ink = harshInk;
    harshPill.setTitle ("Harsh band");
    harshPill.setHelp (helpFor ("bandHarsh"));
    harshPill.onClick = [this] { controller.setBand (Band::harsh); };

    auto& harshSwitch = *harshPill.getSwitch();
    harshSwitch.setTitle ("Harsh on");
    harshSwitch.setHelp (helpFor (params::harshOn));
    harshSwitch.accent = harshInk;   // a state indicator: the band's line colour, 3:1 or more in both themes
    harshSwitch.onClick = [this] { controller.toggle (params::harshOn); };

    shape.setHelp (helpFor (params::shape));
    shape.getIndex = [this] { return (int) controller.shape(); };
    shape.setIndex = [this] (int i) { controller.setPlain (params::shape, (float) i); };
    shape.isItemDimmed = [this] (int i) { return i == (int) Shape::spectral && controller.latency() == LatencyMode::live; };

    detection.setHelp (helpFor (params::detection));
    detection.getIndex = [this] { return (int) controller.detection(); };
    detection.setIndex = [this] (int i) { controller.setPlain (params::detection, (float) i); };

    learn.setHelp (helpFor ("learn"));
    learn.onClick = [this] { controller.getProcessor().getLearn().start(); };

    listen.setHelp (helpFor (params::listen));
    listen.onClick = [this]
    {
        listenMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&listen),
                                    [safe = juce::Component::SafePointer<ControlBar> (this)] (int result)
                                    {
                                        if (safe != nullptr && result > 0)
                                            safe->chooseListen (result);
                                    });
    };

    more.onClick = [this] { controller.setAdvancedOpen (! controller.isAdvancedOpen()); };

    for (auto* comp : std::initializer_list<juce::Component*> { &essPill, &harshPill, &essThreshold, &essRange, &focus, &low, &high,
                                                                &harshThreshold, &harshRange, &harshSpeed, &freq, &width,
                                                                &shape, &detection, &learn, &listen, &more })
        addAndMakeVisible (comp);

    // Tab runs left to right; the default order (top to bottom first) would jump between the bar's columns.
    auto order = 1;
    for (auto* comp : std::initializer_list<juce::Component*> { &essPill, &harshPill, &essThreshold, &harshThreshold, &essRange, &harshRange,
                                                                &focus, &harshSpeed, &low, &freq, &high, &width, &shape, &detection,
                                                                &learn, &listen, &more })
        comp->setExplicitFocusOrder (order++);

    showBand (controller.getBand());
}

std::vector<ValueControl*> ControlBar::valueControls()
{
    return { &essThreshold, &essRange, &focus, &low, &high, &harshThreshold, &harshRange, &harshSpeed, &freq, &width };
}

void ControlBar::showBand (Band band)
{
    shownBand = band;
    const auto ess = band == Band::ess;

    for (auto* comp : std::initializer_list<juce::Component*> { &essThreshold, &essRange, &focus, &low, &high })
        comp->setVisible (ess);
    for (auto* comp : std::initializer_list<juce::Component*> { &harshThreshold, &harshRange, &harshSpeed, &freq, &width })
        comp->setVisible (! ess);
}

void ControlBar::setCompact (bool c)
{
    compact = c;
}

ControlBar::Sizes ControlBar::sizes() const
{
    const auto large = compact ? 60.0f : 76.0f, small = compact ? 46.0f : 56.0f;
    return { large, small, (int) large + 14, (int) small + 14 };
}

int ControlBar::widthFor (Mode m) const
{
    const auto s = sizes();
    const auto wells = (int) std::ceil (std::max (shape.wellWidth(), detection.wellWidth()));

    if (m == Mode::wide)
        return 2 * 16 + pillsWidth + 1 + s.largeCell + 2 * s.smallCell + fieldsWidth + 1 + (64 + wells) + 132 + 34 + 9 * 18;

    return 2 * 12 + pillsWidth + 1 + s.largeCell + 2 * s.smallCell + fieldsWidth + 1 + wells + 112 + 34 + 9 * 12;
}

ControlBar::Mode ControlBar::modeFor (int w) const
{
    if (w >= widthFor (Mode::wide))
        return Mode::wide;
    if (w >= widthFor (Mode::medium))
        return Mode::medium;
    return Mode::wrapped;
}

int ControlBar::preferredHeight (int w) const
{
    if (modeFor (w) != Mode::wrapped)
        return compact ? 100 : 128;

    return compact ? 130 : 152;
}

void ControlBar::resized()
{
    mode = modeFor (getWidth());
    const auto s = sizes();
    dividers.clear();

    auto knobHeight = [this] (float d) { return RingKnob::heightFor (d, compact); };
    auto placeKnobs = [&] (int x, int cy)
    {
        const auto hl = knobHeight (s.large);
        for (auto* k : { &essThreshold, &harshThreshold })
        {
            k->setDiameter (s.large, compact);
            k->setBounds (x, cy - hl / 2, s.largeCell, hl);
        }
        x += s.largeCell;
        return x;
    };

    auto placeSmallKnob = [&] (std::initializer_list<RingKnob*> knobs, int x, int cy)
    {
        const auto hs = knobHeight (s.small);
        for (auto* k : knobs)
        {
            k->setDiameter (s.small, compact);
            k->setBounds (x, cy - hs / 2, s.smallCell, hs);
        }
    };

    auto placeFields = [&] (int x, int cy)
    {
        const auto h = (int) ValueField::height;
        low.setBounds (x, cy - h - 3, fieldsWidth, h);
        high.setBounds (x, cy + 3, fieldsWidth, h);
        freq.setBounds (low.getBounds());
        width.setBounds (high.getBounds());
    };

    auto placePills = [&] (int x, int cy)
    {
        const auto h = (int) BandPill::height;
        essPill.setBounds (x, cy - h - 3, pillsWidth, h);
        harshPill.setBounds (x, cy + 3, pillsWidth, h);
    };

    // The band's group: pills | knobs, fields. Returns the x after it.
    auto placeBandGroup = [&] (int x, int cy, int gap, int dividerHeight)
    {
        placePills (x, cy);
        x += pillsWidth + gap;
        dividers.push_back ({ x, cy - dividerHeight / 2, 1, dividerHeight });
        x += 1 + gap;
        x = placeKnobs (x, cy) + gap;
        placeSmallKnob ({ &essRange, &harshRange }, x, cy);
        x += s.smallCell + gap;
        placeSmallKnob ({ &focus, &harshSpeed }, x, cy);
        x += s.smallCell + gap;
        placeFields (x, cy);
        return x + fieldsWidth + gap;
    };

    if (mode != Mode::wrapped)
    {
        const auto wide = mode == Mode::wide;
        const auto pad = wide ? 16 : 12, gap = wide ? 18 : 12;
        const auto cy = getHeight() / 2;
        const auto dividerHeight = compact ? 72 : 84;

        auto x = placeBandGroup (pad, cy, gap, dividerHeight);
        dividers.push_back ({ x, cy - dividerHeight / 2, 1, dividerHeight });
        x += 1 + gap;

        const auto wells = (int) std::ceil (std::max (shape.wellWidth(), detection.wellWidth()));
        if (wide)
        {
            shape.setLabel ("Shape", 64.0f, false);
            detection.setLabel ("Detection", 64.0f, false);
            shape.setBounds (x, cy - 30, 64 + wells, 26);
            detection.setBounds (x, cy + 4, 64 + wells, 26);
        }
        else
        {
            shape.setLabel ("Shape", 0.0f, true);
            detection.setLabel ("Detection", 0.0f, true);
            shape.setBounds (x, cy - 45, wells, 42);
            detection.setBounds (x, cy + 3, wells, 42);
        }

        auto right = getWidth() - pad;
        const auto moreHeight = compact ? 72 : 84;
        more.setBounds (right - 34, cy - moreHeight / 2, 34, moreHeight);
        right -= 34 + gap;

        const auto column = wide ? 132 : 112;
        learn.setBounds (right - column, cy - 32, column, 28);
        listen.setBounds (right - column, cy + 4, column, 28);
        return;
    }

    // Wrapped: the band's group on the first row; Shape, Detection, Learn, Listen and More on the second.
    const auto pad = 12, gap = 12;
    const auto rowOne = knobHeight (s.large);
    const auto rowTwo = compact ? 32 : 36;
    const auto top = std::max (4, (getHeight() - rowOne - 6 - rowTwo) / 2);
    const auto cy1 = top + rowOne / 2;
    placeBandGroup (pad, cy1, gap, rowOne - 8);

    const auto rowTwoTop = top + rowOne + 6;
    const auto cy2 = rowTwoTop + rowTwo / 2;
    auto x = pad;

    for (auto* seg : { &shape, &detection })
    {
        const auto labelSpace = (float) labelWidth (seg->getTitle()) + 6.0f;
        seg->setLabel (seg->getTitle(), labelSpace, false);
        const auto w = (int) std::ceil (seg->idealWidth());
        seg->setBounds (x, cy2 - 13, w, 26);
        x += w + 10;
    }

    auto right = getWidth() - pad;
    more.setBounds (right - 72, cy2 - 14, 72, 28);
    right -= 72 + 6;
    const auto listenWidth = juce::jlimit (110, 150, right - x - 86);
    listen.setBounds (right - listenWidth, cy2 - 14, listenWidth, 28);
    right -= listenWidth + 6;
    learn.setBounds (right - 80, cy2 - 14, 80, 28);
}

void ControlBar::update()
{
    if (const auto band = controller.getBand(); band != shownBand)
    {
        showBand (band);
        repaint();
    }

    essPill.setSelected (shownBand == Band::ess);
    harshPill.setSelected (shownBand == Band::harsh);
    harshPill.getSwitch()->setToggleState (controller.harshOn(), juce::dontSendNotification);

    // A control that does nothing in this mode stays editable, drawn quieter.
    focus.setDimmed (controller.detection() == Detection::allround);
    for (auto* comp : std::initializer_list<Control*> { &harshThreshold, &harshRange, &harshSpeed, &freq, &width })
        comp->setDimmed (! controller.harshOn());

    for (auto* v : valueControls())
        if (v->isVisible())
            v->update();

    shape.update();
    detection.update();

    auto& learner = controller.getProcessor().getLearn();
    if (learner.state() == LearnState::listening)
    {
        learn.setText ("Listening");
        learn.setProgress (learner.progress());
    }
    else
    {
        learn.setText ("Learn");
        learn.setProgress (-1.0f);
    }

    const auto listening = juce::jlimit (0, numListenModes - 1, controller.choice (params::listen));
    listen.setText ("Listen: " + params::listenNames()[listening]);
    listen.setAccented (listening != (int) Listen::output);
    more.setOpen (controller.isAdvancedOpen());
}

juce::PopupMenu ControlBar::listenMenu() const
{
    juce::PopupMenu menu;
    const auto current = controller.choice (params::listen);
    const auto names = params::listenNames();

    for (int i = 0; i < names.size(); ++i)
    {
        if (i == 1 || i == 4)
            menu.addSeparator();
        menu.addItem (listenItemBase + i, names[i], true, i == current);
    }
    return menu;
}

void ControlBar::chooseListen (int itemId)
{
    const auto index = itemId - listenItemBase;
    if (juce::isPositiveAndBelow (index, numListenModes))
        controller.setPlain (params::listen, (float) index);
}

void ControlBar::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    g.fillAll (t.surface);
    g.setColour (t.line);
    g.fillRect (0, 0, getWidth(), 1);

    for (const auto& d : dividers)
        g.fillRect (d);
}

//==============================================================================
AdvancedRow::AdvancedRow (HaliteController& c)
    : controller (c),
      speedValue (c, params::speedAuto, params::release),
      lookaheadValue (c, params::lookahead),
      keyHpfValue (c, params::keyHpf),
      outputValue (c, params::output),
      speed (speedValue, "Ess speed"),
      lookahead (lookaheadValue, "Look-ahead"),
      keyHpf (keyHpfValue, "Key HPF"),
      output (outputValue, "Output"),
      stereo ("Stereo", params::stereoModeNames()),
      key ("Key", params::keySourceNames())
{
    setTitle ("Advanced");
    setFocusContainerType (FocusContainerType::focusContainer);

    for (auto* f : { &speed, &lookahead, &keyHpf, &output })
        f->setWidths ((float) labelWidth (f->getTitle()), (float) boxWidth);

    stereo.setHelp (helpFor (params::stereo));
    stereo.setLabel ("Stereo", (float) labelWidth ("Stereo") + 8.0f, false);
    stereo.getIndex = [this] { return controller.choice (params::stereo); };
    stereo.setIndex = [this] (int i) { controller.setPlain (params::stereo, (float) i); };

    key.setHelp (helpFor (params::key));
    key.setLabel ("Key", (float) labelWidth ("Key") + 8.0f, false);
    key.getIndex = [this] { return controller.choice (params::key); };
    key.setIndex = [this] (int i) { controller.setPlain (params::key, (float) i); };

    auto order = 1;
    for (auto* comp : items())
    {
        addAndMakeVisible (comp);
        comp->setExplicitFocusOrder (order++);
    }
}

std::vector<juce::Component*> AdvancedRow::items()
{
    return { &speed, &lookahead, &stereo, &key, &keyHpf, &output };
}

int AdvancedRow::itemWidth (juce::Component* comp) const
{
    if (auto* f = dynamic_cast<ValueField*> (comp))
        return (int) std::ceil (f->idealWidth());
    if (auto* s = dynamic_cast<Segmented*> (comp))
        return (int) std::ceil (s->idealWidth());
    return comp->getWidth();
}

int AdvancedRow::rowsFor (int w) const
{
    auto rows = 1, x = pad;

    for (auto* comp : const_cast<AdvancedRow*> (this)->items())
    {
        const auto iw = itemWidth (comp);
        if (x > pad && x + iw > w - pad)
        {
            ++rows;
            x = pad;
        }
        x += iw + gap;
    }
    return rows;
}

int AdvancedRow::preferredHeight (int w) const
{
    const auto rows = rowsFor (w);
    return rows * rowHeight() + (rows - 1) * 2 + (compact ? 8 : 10);
}

void AdvancedRow::resized()
{
    auto x = pad, row = 0;
    const auto top = compact ? 4 : 5;

    for (auto* comp : items())
    {
        const auto iw = itemWidth (comp);
        if (x > pad && x + iw > getWidth() - pad)
        {
            ++row;
            x = pad;
        }
        comp->setBounds (x, top + row * (rowHeight() + 2), iw, rowHeight());
        x += iw + gap;
    }
}

void AdvancedRow::update()
{
    lookahead.setDimmed (controller.latency() == LatencyMode::live);

    for (auto* v : valueControls())
        v->update();

    stereo.update();
    key.update();
}

void AdvancedRow::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    g.fillAll (t.surface);
    g.setColour (t.lineSoft);
    g.fillRect (0, 0, getWidth(), 1);
}

} // namespace halite::ui
