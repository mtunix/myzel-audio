#include "ui/StudioControls.h"

#include "ui/Drawing.h"

namespace cerulean::ui
{

namespace
{
    constexpr float fineFactor = 0.1f;
    constexpr int wheelGestureTimeoutMs = 350;
    constexpr float clickSlop = 3.0f;        // pixels a click may move and still count as a click

    const StudioPalette& pal() { return Theme::studio(); }

    //==============================================================================
    class SliderValueInterface final : public juce::AccessibilityValueInterface
    {
    public:
        explicit SliderValueInterface (StudioSlider& s) : slider (s) {}

        bool isReadOnly() const override                      { return false; }
        double getCurrentValue() const override               { return slider.getNormalisedValue(); }
        juce::String getCurrentValueAsString() const override { return slider.getValueText(); }
        void setValue (double v) override                     { slider.setNormalisedValueAsGesture ((float) v); }
        void setValueAsString (const juce::String& s) override { slider.commitText (s); }
        AccessibleValueRange getRange() const override        { return { { 0.0, 1.0 }, 0.01 }; }

    private:
        StudioSlider& slider;
    };

    class SliderAccessibilityHandler final : public juce::AccessibilityHandler
    {
    public:
        explicit SliderAccessibilityHandler (StudioSlider& s)
            : juce::AccessibilityHandler (s, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                          Interfaces { std::make_unique<SliderValueInterface> (s) })
        {
        }
    };
}

//==============================================================================
StudioSlider::StudioSlider (juce::RangedAudioParameter& p, juce::UndoManager* um, const juce::String& l)
    : parameter (&p),
      undoManager (um),
      label (l)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
    setTitle (label);
    rebind (p);
}

StudioSlider::~StudioSlider()
{
    if (dragging || inWheelGesture)
        attachment->endGesture();
}

void StudioSlider::rebind (juce::RangedAudioParameter& newParameter)
{
    if (attachment != nullptr)
    {
        if (dragging || inWheelGesture)
        {
            stopTimer();
            attachment->endGesture();
        }

        dragging = inWheelGesture = false;
        textEntry.reset();
    }

    parameter = &newParameter;
    setDescription (parameter->getName (64));
    value01 = -1.0f;
    attachment = std::make_unique<juce::ParameterAttachment> (newParameter, [this] (float v) { valueChanged (v); }, undoManager);
    attachment->sendInitialUpdate();
}

void StudioSlider::setLabel (const juce::String& newLabel)
{
    if (label == newLabel)
        return;

    label = newLabel;
    setTitle (label);
    repaint();
}

void StudioSlider::setBipolar (bool b)          { bipolar = b; repaint(); }
void StudioSlider::setFillFromRight (bool b)    { fromRight = b; repaint(); }
void StudioSlider::setBadge (const juce::String& b) { badge = b; repaint(); }
void StudioSlider::setCaption (std::function<juce::String()> fn) { caption = std::move (fn); repaint(); }
juce::String StudioSlider::getCaption() const   { return caption != nullptr ? caption() : juce::String(); }

void StudioSlider::setLook (Look newLook)
{
    look = newLook;
    resized();
    repaint();
}
void StudioSlider::setSteps (int n)             { steps = n > 1 ? n : 0; repaint(); }
void StudioSlider::setTrailingSpace (float w)   { trailing = w; repaint(); }

void StudioSlider::setDimmed (bool d)
{
    if (dimmed != d)
    {
        dimmed = d;
        repaint();
    }
}

void StudioSlider::setValueToText (std::function<juce::String (float)> fn)
{
    valueToText = std::move (fn);
    repaint();
}

void StudioSlider::setTextToValue (std::function<float (const juce::String&)> fn)
{
    textToValue = std::move (fn);
}

//==============================================================================
void StudioSlider::valueChanged (float denormalised)
{
    const auto v = parameter->convertTo0to1 (denormalised);

    if (std::abs (v - value01) > 1.0e-6f)
    {
        value01 = v;
        repaint();
    }
}

juce::String StudioSlider::getValueText() const
{
    return valueToText != nullptr ? valueToText (value01) : parameter->getText (value01, 32);
}

void StudioSlider::setNormalisedValueAsGesture (float newValue01)
{
    finishWheelGesture();
    attachment->setValueAsCompleteGesture (parameter->convertFrom0to1 (quantise (juce::jlimit (0.0f, 1.0f, newValue01))));
}

float StudioSlider::textToValue01 (const juce::String& text) const
{
    if (textToValue != nullptr)
        return textToValue (text);

    const auto t = text.trim().toLowerCase();

    // Words the parameters understand ("Off", "Flat"); anything else needs a number.
    if (! t.containsAnyOf ("0123456789") && t != "off" && t != "flat")
        return -1.0f;

    return parameter->getValueForText (text);
}

bool StudioSlider::commitText (const juce::String& text)
{
    if (text.trim().isEmpty())
        return false;

    const auto v = textToValue01 (text.trim());

    if (v < 0.0f || std::isnan (v))
        return false;

    setNormalisedValueAsGesture (v);
    return true;
}

float StudioSlider::stepSize (bool fine) const noexcept
{
    if (steps > 1)
        return 1.0f / (float) (steps - 1);

    return fine ? 0.002f : 0.01f;
}

float StudioSlider::quantise (float v01) const noexcept
{
    if (steps <= 1)
        return v01;

    const auto n = (float) (steps - 1);
    return std::round (v01 * n) / n;
}

void StudioSlider::finishWheelGesture()
{
    if (inWheelGesture)
    {
        stopTimer();
        inWheelGesture = false;
        attachment->endGesture();
    }
}

void StudioSlider::nudge (float amount)
{
    setNormalisedValueAsGesture (value01 + amount);
}

void StudioSlider::timerCallback()
{
    finishWheelGesture();
}

//==============================================================================
juce::Rectangle<float> StudioSlider::barArea() const
{
    if (look == Look::hero)
        return getLocalBounds().toFloat();

    return { 0.0f, 25.0f, juce::jmax (10.0f, (float) getWidth() - trailing), 6.0f };
}

juce::Rectangle<float> StudioSlider::valueArea() const
{
    if (look == Look::hero)
        return { 0.0f, 15.0f, (float) getWidth(), 31.0f };

    return { (float) getWidth() * 0.4f, 0.0f, (float) getWidth() * 0.6f, 18.0f };
}

void StudioSlider::resized()
{
    if (textEntry != nullptr)
        textEntry->setBounds (valueArea().expanded (2.0f, 1.0f).toNearestInt());
}

void StudioSlider::paint (juce::Graphics& g)
{
    const auto& p = pal();
    const auto alpha = dimmed ? 0.45f : 1.0f;
    const auto active = hovering || dragging;

    if (look == Look::hero)
    {
        // The name, then the value large, underlined with a dashed rule (it can be dragged and typed).
        g.setFont (drawing::tracked (fonts->legend (studio::titleSize), studio::titleTracking));
        g.setColour ((active ? p.text : p.title).withMultipliedAlpha (alpha));
        g.drawText (label, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 13.0f), juce::Justification::centredLeft, true);

        if (textEntry == nullptr)
        {
            const auto font = fonts->label (26.0f);
            const auto text = getValueText();
            const auto w = juce::jmin ((float) getWidth(), juce::GlyphArrangement::getStringWidth (font, text));
            g.setFont (font);
            g.setColour ((active ? p.accent.brighter (0.3f) : p.text).withMultipliedAlpha (alpha));
            g.drawText (text, valueArea(), juce::Justification::centredLeft, false);

            juce::Path rule, dashed;
            rule.startNewSubPath (0.0f, (float) getHeight() - 1.5f);
            rule.lineTo (w, (float) getHeight() - 1.5f);
            const float dash[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, rule, dash, 2);
            g.setColour ((active ? p.accent : p.accentDeep.brighter (0.2f)).withMultipliedAlpha (alpha));
            g.fillPath (dashed);
        }

        if (keyboardFocused)
        {
            g.setColour (p.accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
        }

        return;
    }

    // Value first (it always fits), then the name in what is left.
    const auto valueFont = fonts->label (studio::valueSize);
    const auto valueText = getValueText();
    auto labelWidth = (float) getWidth();

    if (textEntry == nullptr)
    {
        g.setFont (valueFont);
        g.setColour (p.text.withMultipliedAlpha (alpha));
        g.drawText (valueText, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 18.0f), juce::Justification::centredRight, false);
        labelWidth -= juce::GlyphArrangement::getStringWidth (valueFont, valueText) + 8.0f;
    }
    else
    {
        labelWidth = valueArea().getX() - 6.0f;
    }

    auto labelX = 0.0f;

    if (badge.isNotEmpty())
    {
        // The panel's boxed A / B, so the name keeps all the room.
        const auto box = juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ 6.5f, 9.0f });
        g.setColour (p.marker.brighter (0.4f).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (box.reduced (0.5f), 2.5f, 1.0f);
        g.setFont (fonts->legend (8.5f));
        g.setColour ((active ? p.text : p.textDim).withMultipliedAlpha (alpha));
        g.drawText (badge, box, juce::Justification::centred, false);
        labelX = 19.0f;
    }

    g.setFont (drawing::tracked (fonts->legend (studio::labelSize), studio::labelTracking));
    g.setColour ((active ? p.text : p.textDim).withMultipliedAlpha (alpha));
    g.drawText (label, juce::Rectangle<float> (labelX, 0.0f, juce::jmax (12.0f, labelWidth - labelX), 18.0f), juce::Justification::centredLeft, true);

    // Bar.
    const auto bar = barArea();
    g.setColour (p.track.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bar, 3.0f);

    if (steps > 1)
    {
        g.setColour (p.inset.withMultipliedAlpha (alpha));

        for (int i = 1; i < steps - 1; ++i)
        {
            const auto x = bar.getX() + bar.getWidth() * (float) i / (float) (steps - 1);
            g.fillRect (juce::Rectangle<float> (x - 0.5f, bar.getY(), 1.0f, bar.getHeight()));
        }
    }

    const auto xAt = [&bar] (float v) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, v); };
    const auto origin = bipolar ? 0.5f : (fromRight ? 1.0f : 0.0f);
    const auto x0 = xAt (juce::jmin (origin, value01)), x1 = xAt (juce::jmax (origin, value01));

    if (x1 - x0 > 0.5f)
    {
        g.setColour (p.trackFill.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (x0, bar.getY(), x1 - x0, bar.getHeight()), 3.0f);
    }

    // Under the bar: a caption (e.g. a synced note's length), or a small tick at the default.
    if (const auto text = getCaption(); text.isNotEmpty())
    {
        g.setFont (fonts->label (studio::smallSize));
        g.setColour (p.textFaint.withMultipliedAlpha (alpha));
        g.drawText (text, juce::Rectangle<float> (0.0f, bar.getBottom() + 3.0f, (float) getWidth(), 12.0f), juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (p.marker.withMultipliedAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (xAt (parameter->getDefaultValue()) - 0.5f, bar.getBottom() + 3.0f, 1.0f, 4.0f));
    }

    // The value line.
    const auto xv = xAt (value01);
    g.setColour ((active ? p.accent.brighter (0.2f) : p.trackFillEdge).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (juce::Rectangle<float> (xv - 1.5f, bar.getY() - 4.0f, 3.0f, bar.getHeight() + 8.0f), 1.5f);

    if (keyboardFocused)
    {
        g.setColour (p.accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f).expanded (0.0f, 0.0f), 3.0f, 1.0f);
    }
}

//==============================================================================
void StudioSlider::mouseEnter (const juce::MouseEvent&) { hovering = true; repaint(); }
void StudioSlider::mouseExit (const juce::MouseEvent&)  { hovering = false; repaint(); }

void StudioSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mods.isAltDown() || e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        showTextEntry();
        return;
    }

    textEntry.reset();
    finishWheelGesture();
    dragging = true;
    dragValue = value01;
    lastDrag = e.position;
    travelled = 0.0f;
    attachment->beginGesture();
    repaint();
}

void StudioSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto delta = e.position - lastDrag;
    lastDrag = e.position;
    travelled += std::abs (delta.x) + std::abs (delta.y);

    if (travelled < clickSlop)
        return;

    auto speed = steps > 1 ? 1.0f / (16.0f * (float) steps) : 1.0f / fullRangePixels;

    if (e.mods.isShiftDown())
        speed *= fineFactor;

    // Right and up both raise the value.
    dragValue = juce::jlimit (0.0f, 1.0f, dragValue + (delta.x - delta.y) * speed);
    attachment->setValueAsPartOfGesture (parameter->convertFrom0to1 (quantise (dragValue)));
}

void StudioSlider::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    dragging = false;
    attachment->endGesture();
    hovering = isMouseOver (true);

    // A click (no drag) on the value opens the text field.
    if (travelled < clickSlop && valueArea().contains (e.position) && e.getNumberOfClicks() == 1)
        showTextEntry();

    repaint();
}

void StudioSlider::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || valueArea().contains (e.position))
        return;

    textEntry.reset();
    finishWheelGesture();
    attachment->setValueAsCompleteGesture (parameter->convertFrom0to1 (parameter->getDefaultValue()));
}

void StudioSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (dragging)
        return;

    auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;

    if (wheel.isReversed)
        delta = -delta;

    if (std::abs (delta) < 1.0e-6f)
        return;

    if (! inWheelGesture)
    {
        inWheelGesture = true;
        dragValue = value01;
        wheelAccumulator = 0.0f;
        attachment->beginGesture();
    }

    if (steps > 1)
    {
        wheelAccumulator += delta;

        if (std::abs (wheelAccumulator) >= 0.1f)
        {
            dragValue = juce::jlimit (0.0f, 1.0f, quantise (dragValue) + (wheelAccumulator > 0.0f ? 1.0f : -1.0f) * stepSize (false));
            wheelAccumulator = 0.0f;
            attachment->setValueAsPartOfGesture (parameter->convertFrom0to1 (quantise (dragValue)));
        }
    }
    else
    {
        const auto step = (e.mods.isShiftDown() ? 0.004f : 0.03f) * (delta > 0.0f ? 1.0f : -1.0f)
                          * juce::jlimit (0.5f, 3.0f, std::abs (delta) / 0.1f);
        dragValue = juce::jlimit (0.0f, 1.0f, dragValue + step);
        attachment->setValueAsPartOfGesture (parameter->convertFrom0to1 (dragValue));
    }

    startTimer (wheelGestureTimeoutMs);
}

bool StudioSlider::keyPressed (const juce::KeyPress& key)
{
    const auto fine = key.getModifiers().isShiftDown();
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)   { nudge (stepSize (fine));  return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)  { nudge (-stepSize (fine)); return true; }
    if (code == juce::KeyPress::pageUpKey)                                   { nudge (juce::jmax (0.1f, stepSize (false)));  return true; }
    if (code == juce::KeyPress::pageDownKey)                                 { nudge (-juce::jmax (0.1f, stepSize (false))); return true; }
    if (code == juce::KeyPress::homeKey)                                     { setNormalisedValueAsGesture (0.0f); return true; }
    if (code == juce::KeyPress::endKey)                                      { setNormalisedValueAsGesture (1.0f); return true; }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        setNormalisedValueAsGesture (parameter->getDefaultValue());
        return true;
    }

    if (code == juce::KeyPress::returnKey)
    {
        showTextEntry();
        return true;
    }

    return false;
}

void StudioSlider::focusGained (FocusChangeType cause)
{
    keyboardFocused = cause != focusChangedByMouseClick;
    repaint();
}

void StudioSlider::focusLost (FocusChangeType)
{
    keyboardFocused = false;
    repaint();
}

void StudioSlider::showTextEntry()
{
    const auto& p = pal();

    textEntry = std::make_unique<juce::TextEditor> ("value");
    auto& ed = *textEntry;
    ed.setColour (juce::TextEditor::backgroundColourId, p.inset);
    ed.setColour (juce::TextEditor::textColourId, p.text);
    ed.setColour (juce::TextEditor::highlightColourId, p.accentDeep.withAlpha (0.45f));
    ed.setColour (juce::TextEditor::highlightedTextColourId, p.text);
    ed.setColour (juce::TextEditor::outlineColourId, p.accentDeep);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
    ed.setColour (juce::CaretComponent::caretColourId, p.accent);
    ed.setFont (fonts->label (look == Look::hero ? 20.0f : studio::valueSize));
    ed.setJustification (look == Look::hero ? juce::Justification::centredLeft : juce::Justification::centredRight);
    ed.setIndents (4, 1);
    ed.setText (getValueText(), false);
    ed.selectAll();
    ed.setTitle (label + " value");
    ed.setBounds (valueArea().expanded (2.0f, 1.0f).toNearestInt());

    juce::Component::SafePointer<StudioSlider> safeThis (this);

    auto close = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->textEntry.reset();
                safeThis->repaint();
            }
        });
    };

    ed.onReturnKey = [safeThis, close]
    {
        if (safeThis != nullptr && safeThis->textEntry != nullptr)
            safeThis->commitText (safeThis->textEntry->getText());

        close();
    };
    ed.onFocusLost = ed.onReturnKey;
    ed.onEscapeKey = close;

    addAndMakeVisible (ed);
    ed.grabKeyboardFocus();
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> StudioSlider::createAccessibilityHandler()
{
    return std::make_unique<SliderAccessibilityHandler> (*this);
}

//==============================================================================
StudioToggle::StudioToggle (const juce::String& l, Style s)
    : juce::Button (l),
      label (l),
      style (s)
{
    setTitle (l);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void StudioToggle::setLabel (const juce::String& newLabel)
{
    label = newLabel;
    repaint();
}

void StudioToggle::setDimmed (bool d)
{
    if (dimmed != d)
    {
        dimmed = d;
        repaint();
    }
}

void StudioToggle::setMomentary (bool shouldBeMomentary)
{
    if (! shouldBeMomentary && momentaryDown)
    {
        momentaryDown = false;

        if (onPress != nullptr)
            onPress (false);
    }

    momentary = shouldBeMomentary;
}

void StudioToggle::buttonStateChanged()
{
    const auto down = getState() == buttonDown;

    if (! momentary || down == momentaryDown)
        return;

    momentaryDown = down;

    if (down)
        pressedMomentarily = true;

    if (onPress != nullptr)
        onPress (down);
}

void StudioToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& p = pal();
    const auto on = getToggleState();
    const auto alpha = (dimmed ? 0.45f : 1.0f) * (isEnabled() ? 1.0f : 0.5f);
    auto area = getLocalBounds().toFloat();

    const auto corner = style == Style::chip ? 4.0f : 4.0f;
    juce::Path shape;

    if (style == Style::segmentLeft || style == Style::segmentRight)
    {
        const auto left = style == Style::segmentLeft;
        shape.addRoundedRectangle (area.getX(), area.getY(), area.getWidth(), area.getHeight(), corner, corner,
                                   left, ! left, left, ! left);
    }
    else
    {
        shape.addRoundedRectangle (area, corner);
    }

    if (style == Style::toggleSwitch)
    {
        // A pill with a sliding thumb and the state in words: never a bare "ON" that is off.
        auto fill = on ? p.accentDeep : (highlighted ? p.hover : p.chipOff);
        g.setColour (fill.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
        g.setColour ((on ? p.accent.withAlpha (0.0f) : p.cardEdge).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (area.reduced (0.5f), area.getHeight() * 0.5f, 1.0f);

        const auto track = juce::Rectangle<float> (24.0f, 12.0f).withCentre ({ area.getX() + 16.0f, area.getCentreY() });
        g.setColour ((on ? p.accent : p.track.brighter (0.25f)).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (track, 6.0f);
        const auto thumb = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ on ? track.getRight() - 6.0f : track.getX() + 6.0f, track.getCentreY() });
        g.setColour ((on ? p.accentText : p.chipOffText).withMultipliedAlpha (alpha));
        g.fillEllipse (thumb);

        g.setColour ((on ? juce::Colours::white : p.chipOffText).withMultipliedAlpha (alpha));
        g.setFont (drawing::tracked (fonts->legend (9.5f), 0.10f));
        g.drawText (on ? "ON" : "OFF", area.withTrimmedLeft (track.getRight() - area.getX() + 2.0f).withTrimmedRight (4.0f),
                    juce::Justification::centred, false);

        if (hasKeyboardFocus (false))
        {
            g.setColour (p.accent);
            g.drawRoundedRectangle (area.reduced (0.5f), area.getHeight() * 0.5f, 1.5f);
        }

        return;
    }

    // Fill: on = ice, off = a quiet chip; hover lifts it a little.
    auto fill = on ? (style == Style::block ? p.accentSoft : p.accentDeep) : p.chipOff;

    if (highlighted && ! on)
        fill = p.hover;

    if (down)
        fill = fill.brighter (0.08f);

    g.setColour (fill.withMultipliedAlpha (alpha));
    g.fillPath (shape);

    g.setColour ((on ? p.accent.withAlpha (style == Style::block ? 0.9f : 0.0f) : p.cardEdge).withMultipliedAlpha (alpha));
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    auto textArea = area.reduced (6.0f, 0.0f);

    if (style == Style::block)
    {
        // An indicator dot at the left.
        const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ area.getX() + 14.0f, area.getCentreY() });
        g.setColour ((on ? p.accent : p.marker).withMultipliedAlpha (alpha));
        g.fillEllipse (dot);

        if (on)
            drawing::fillGlow (g, dot.getCentre(), 9.0f, p.accent, 0.5f * alpha);

        textArea = area.withTrimmedLeft (26.0f).withTrimmedRight (8.0f);
    }

    const auto textColour = on ? (style == Style::block ? p.text : juce::Colours::white) : p.chipOffText;
    g.setColour (textColour.withMultipliedAlpha (alpha));
    g.setFont (drawing::tracked (fonts->legend (style == Style::block ? 11.0f : 10.0f), 0.10f));
    g.drawText (label, textArea, style == Style::block ? juce::Justification::centredLeft : juce::Justification::centred, false);

    if (hasKeyboardFocus (false))
    {
        g.setColour (p.accent);
        g.strokePath (shape, juce::PathStrokeType (1.5f));
    }
}

//==============================================================================
AlgorithmRow::AlgorithmRow (int i, const juce::String& n, const juce::String& t)
    : juce::Button (n),
      index (i),
      name (n),
      tagline (t)
{
    setTitle (n + " algorithm");
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool AlgorithmRow::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();

    if (onStep != nullptr && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
    {
        onStep (code == juce::KeyPress::upKey ? -1 : 1);
        return true;
    }

    return juce::Button::keyPressed (key);
}

void AlgorithmRow::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& p = pal();
    const auto on = getToggleState();
    const auto area = getLocalBounds().toFloat();

    if (on || highlighted || down)
    {
        g.setColour (on ? p.accentSoft : p.hover);
        g.fillRoundedRectangle (area, 4.0f);
    }

    if (on)
    {
        g.setColour (p.accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (area.getX(), area.getY() + 5.0f, 3.0f, area.getHeight() - 10.0f), 1.5f);
    }

    g.setFont (fonts->label (studio::smallSize));
    g.setColour (on ? p.accent : p.textFaint);
    g.drawText (juce::String (index + 1), juce::Rectangle<float> (area.getX() + 8.0f, area.getY(), 18.0f, area.getHeight()),
                juce::Justification::centredRight, false);

    g.setFont (drawing::tracked (fonts->legend (11.0f), 0.08f));
    g.setColour (on ? p.text : p.text.withAlpha (0.86f));
    g.drawText (name.toUpperCase(), juce::Rectangle<float> (area.getX() + 34.0f, area.getY(), 70.0f, area.getHeight()),
                juce::Justification::centredLeft, false);

    g.setFont (fonts->label (studio::smallSize));
    g.setColour (on ? p.textDim.brighter (0.2f) : p.textFaint);
    g.drawText (tagline, area.withTrimmedLeft (98.0f).withTrimmedRight (8.0f), juce::Justification::centredRight, true);

    if (hasKeyboardFocus (false))
    {
        g.setColour (p.accent);
        g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
    }
}

} // namespace cerulean::ui
