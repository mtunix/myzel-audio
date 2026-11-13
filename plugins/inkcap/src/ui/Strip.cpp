#include "ui/Strip.h"

#include "plugin/Parameters.h"

namespace inkcap::ui
{

namespace
{
    constexpr float d = layout::keyDiameter;

    /** The round key or knob at the top of an item, the label under it. */
    juce::Rectangle<float> topCircle (const juce::Component& c)
    {
        return { ((float) c.getWidth() - d) * 0.5f, 0.0f, d, d };
    }

    juce::Rectangle<float> labelArea (const juce::Component& c)
    {
        return { 0.0f, d + layout::labelGap, (float) c.getWidth(), layout::labelHeight };
    }

    /** A soft drop shadow under a round shape on the body (CSS: 0 dy blur rgba(0,0,0,alpha)). */
    void dropShadow (juce::Graphics& g, juce::Rectangle<float> circle, float dy, float blur, float alpha)
    {
        constexpr int steps = 6;
        for (int i = steps; i > 0; --i)
        {
            const auto spread = blur * (float) i / (float) steps * 0.5f;
            g.setColour (juce::Colours::black.withAlpha (alpha / (float) steps));
            g.fillEllipse (circle.translated (0.0f, dy).expanded (spread));
        }
    }

    /** An inset shadow from above inside a shape (CSS: inset 0 2px 4px). */
    void innerShadow (juce::Graphics& g, const juce::Path& shape, juce::Rectangle<float> bounds, float alpha)
    {
        g.saveState();
        g.reduceClipRegion (shape);
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (alpha * 0.45f * (1.0f - (float) i / 4.0f)));
            g.drawEllipse (bounds.translated (0.0f, 2.0f).reduced ((float) i + 0.5f), 1.0f);
        }
        g.restoreState();
    }

    void drawLabel (juce::Graphics& g, const juce::Component& c, const GlassFonts& fonts, const juce::String& text)
    {
        g.setColour (Theme::glass().ink);
        g.setFont (fonts.sans (metrics::stripLabelSize));
        g.drawText (text, labelArea (c), juce::Justification::centredTop, false);
    }

    void bodyFocus (juce::Graphics& g, juce::Rectangle<float> circle)
    {
        g.setColour (Theme::glass().focusOnBody);
        g.drawEllipse (circle.expanded (3.0f), 1.5f);
    }
}

//==============================================================================
ModuleKey::ModuleKey (InkcapController& c, Module m)
    : juce::Button (moduleInfo (m).name), controller (c), module (m)
{
    setTitle (moduleInfo (m).name);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setClickingTogglesState (false);
    onClick = [this]
    {
        controller.toggle (params::onIds[(size_t) module]);
        update();
        if (isMouseOver())
            controller.setHint (hint());
    };
    update();
}

juce::String ModuleKey::hint() const
{
    return InkcapController::moduleName (module) + (on ? " on " : " off ") + juce::String::fromUTF8 ("\xc2\xb7 ") + controller.typeName (module)
         + " " + juce::String (juce::roundToInt (controller.amount (module) * 100.0f)) + " %";
}

void ModuleKey::update()
{
    const auto newOn = controller.isOn (module);
    const auto newActivity = newOn ? juce::jlimit (0.0f, 1.0f, controller.getTelemetry().modules[(size_t) module].activity.load (std::memory_order_relaxed))
                                   : 0.0f;

    if (newOn != on || std::abs (newActivity - activity) > 0.03f)
    {
        on = newOn;
        activity = newActivity;
        setToggleState (on, juce::dontSendNotification);
        repaint();
    }
}

void ModuleKey::mouseEnter (const juce::MouseEvent& e)
{
    juce::Button::mouseEnter (e);
    controller.setHint (hint());
}

void ModuleKey::mouseExit (const juce::MouseEvent& e)
{
    juce::Button::mouseExit (e);
    controller.setHint ({});
}

void ModuleKey::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& c = Theme::glass();
    const auto circle = topCircle (*this);
    juce::Path face;
    face.addEllipse (circle);

    if (on)
    {
        // Pressed in: the darker key, shaded from above.
        g.setColour (down ? c.keyOn.darker (0.04f) : c.keyOn);
        g.fillPath (face);
        innerShadow (g, face, circle, 0.2f);
    }
    else
    {
        // Up: a solid lip 3 px under it and a soft shadow (CSS: 0 3px 0 lip, 0 4px 8px .12).
        dropShadow (g, circle, 4.0f, 8.0f, 0.12f);
        g.setColour (c.keyOffLip);
        g.fillEllipse (circle.translated (0.0f, down ? 1.5f : 3.0f));
        g.setColour (highlighted ? c.keyOff.brighter (0.15f) : c.keyOff);
        g.fillEllipse (circle.translated (0.0f, down ? 1.5f : 0.0f));
    }

    // The LED in the middle of the key.
    const auto led = juce::Rectangle<float> (6.0f, 6.0f).withCentre (circle.getCentre().translated (0.0f, ! on && down ? 1.5f : 0.0f));
    if (on)
    {
        const auto glow = 0.35f + 0.4f * activity;
        g.setGradientFill (juce::ColourGradient (c.led.withAlpha (glow), led.getCentreX(), led.getCentreY(), c.led.withAlpha (0.0f),
                                                 led.getCentreX() + 9.0f, led.getCentreY(), true));
        g.fillEllipse (led.expanded (6.0f));
        g.setColour (c.led.brighter (0.15f * activity));
        g.fillEllipse (led);
    }
    else
    {
        g.setColour (c.ledOff);
        g.fillEllipse (led);
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.drawEllipse (led.reduced (0.25f), 0.5f);
    }

    drawLabel (g, *this, *fonts, moduleInfo (module).name);

    if (hasKeyboardFocus (false))
        bodyFocus (g, circle);
}

//==============================================================================
StripKnob::StripKnob (InkcapController& c, const char* parameterId, const juce::String& text)
    : controller (c), id (parameterId), label (text)
{
    setTitle (controller.parameter (id).getName (64));
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    update();
}

StripKnob::~StripKnob()
{
    if (dragging || inWheelGesture)
        controller.endEdit (id);
}

juce::Rectangle<float> StripKnob::knobArea() const
{
    return topCircle (*this);
}

void StripKnob::setLocked (bool shouldBeLocked)
{
    if (locked != shouldBeLocked)
    {
        locked = shouldBeLocked;
        setDescription (locked ? "Locked: presets and A/B keep it" : "");
        repaint();
    }
}

void StripKnob::update()
{
    const auto v = controller.normalised (id);
    if (! juce::approximatelyEqual (v, value))
    {
        value = v;
        repaint();
        if (hover || dragging)
            showHint();
    }
}

void StripKnob::showHint()
{
    controller.setHint (label.toLowerCase() + " " + controller.text (id));
}

void StripKnob::paint (juce::Graphics& g)
{
    const auto& c = Theme::glass();
    const auto circle = knobArea();

    dropShadow (g, circle, 3.0f, 6.0f, 0.18f);

    // Lit from the top left (the mock's radial gradient at 40 % 32 %).
    const auto light = circle.getRelativePoint (0.40f, 0.32f);
    g.setGradientFill (juce::ColourGradient (c.knobLight, light.x, light.y, c.knobDark, light.x + d * 0.75f * 0.75f, light.y, true));
    g.fillEllipse (circle);

    const auto angle = metrics::knobStartAngle + value * (metrics::knobEndAngle - metrics::knobStartAngle);
    const auto centre = circle.getCentre();
    juce::Path pointer;
    pointer.addRectangle (-1.0f, -d * 0.5f + 5.0f, 2.0f, 9.0f);
    g.setColour (c.ink);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    if (! entry.isOpen())
    {
        drawLabel (g, *this, *fonts, label);

        if (locked)
        {
            const auto font = fonts->sans (metrics::stripLabelSize);
            const auto textRight = (float) getWidth() * 0.5f + glassdraw::textWidth (font, label) * 0.5f;
            glassdraw::padlock (g, juce::Rectangle<float> (textRight + 3.0f, labelArea (*this).getY() + 0.5f, 8.0f, 10.0f), c.inkDim);
        }
    }

    if (hasKeyboardFocus (false))
        bodyFocus (g, circle);
}

void StripKnob::mouseEnter (const juce::MouseEvent&)
{
    hover = true;
    showHint();
}

void StripKnob::mouseExit (const juce::MouseEvent&)
{
    hover = false;
    if (! dragging)
        controller.setHint ({});
}

void StripKnob::setFromGesture (float value01)
{
    auto& p = controller.parameter (id);
    value01 = juce::jlimit (0.0f, 1.0f, value01);
    if (! juce::approximatelyEqual (p.getValue(), value01))
        p.setValueNotifyingHost (value01);
    update();
}

void StripKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mods.isAltDown())
    {
        openTextEntry();
        return;
    }

    if (inWheelGesture)
        timerCallback();

    dragging = true;
    dragValue = controller.normalised (id);
    lastY = e.position.y;
    controller.beginEdit (id);
    e.source.enableUnboundedMouseMovement (true, false);
    showHint();
}

void StripKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto factor = e.mods.isShiftDown() ? metrics::fineFactor : 1.0f;
    dragValue += (lastY - e.position.y) / dragPixels * factor;
    dragValue = juce::jlimit (0.0f, 1.0f, dragValue);
    lastY = e.position.y;
    setFromGesture (dragValue);
}

void StripKnob::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    dragging = false;
    e.source.enableUnboundedMouseMovement (false, true);
    controller.endEdit (id);
    if (! hover)
        controller.setHint ({});
}

void StripKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    controller.resetToDefault (id);
    update();
}

void StripKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (dragging)
        return;

    if (! inWheelGesture)
    {
        inWheelGesture = true;
        controller.beginEdit (id);
    }
    startTimer (300);

    const auto delta = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    setFromGesture (controller.normalised (id) + delta * 0.25f * (e.mods.isShiftDown() ? metrics::fineFactor : 1.0f));
}

void StripKnob::timerCallback()
{
    stopTimer();
    if (inWheelGesture)
    {
        inWheelGesture = false;
        controller.endEdit (id);
    }
}

bool StripKnob::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto step = key.getModifiers().isShiftDown() ? 0.002f : 0.01f;

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
    {
        controller.setNormalised (id, controller.normalised (id) + step);
        update();
        return true;
    }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
    {
        controller.setNormalised (id, controller.normalised (id) - step);
        update();
        return true;
    }
    if (code == juce::KeyPress::returnKey)
    {
        openTextEntry();
        return true;
    }
    return false;
}

void StripKnob::openTextEntry()
{
    const auto area = labelArea (*this).withSizeKeepingCentre (74.0f, 20.0f).translated (0.0f, -2.0f).toNearestInt();
    entry.open (*this, area.withX ((getWidth() - area.getWidth()) / 2), controller.text (id), label.toLowerCase(),
                [this] (const juce::String& t)
                {
                    controller.setFromText (id, t);
                    update();
                    repaint();
                },
                juce::Justification::centred);
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> StripKnob::createAccessibilityHandler()
{
    class Value final : public juce::AccessibilityValueInterface
    {
    public:
        explicit Value (StripKnob& k) : knob (k) {}

        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return knob.controller.normalised (knob.id); }
        juce::String getCurrentValueAsString() const override { return knob.controller.text (knob.id); }
        void setValue (double v) override { knob.controller.setNormalised (knob.id, (float) v); knob.update(); }
        void setValueAsString (const juce::String& s) override { knob.controller.setFromText (knob.id, s); knob.update(); }
        AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, 0.01 }; }

    private:
        StripKnob& knob;
    };

    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions {},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

//==============================================================================
Pill::Pill (const juce::String& label, bool isDark, bool isLatching)
    : juce::Button (label), dark (isDark), latching (isLatching)
{
    setTitle (label);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

void Pill::setLit (bool shouldBeLit)
{
    if (lit != shouldBeLit)
    {
        lit = shouldBeLit;
        setToggleState (lit, juce::dontSendNotification);
        repaint();
    }
}

float Pill::idealWidth() const
{
    return std::ceil (glassdraw::textWidth (fonts->sans (metrics::textSize), getButtonText())) + 36.0f + (latching ? 12.0f : 0.0f);
}

void Pill::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& c = Theme::glass();
    const auto area = getLocalBounds().toFloat().withHeight (layout::pillHeight);
    const auto corner = area.getHeight() * 0.5f;
    const auto pressed = down || lit;

    juce::Path shape;
    shape.addRoundedRectangle (area, corner);

    auto fill = dark ? c.pillDark : c.pill;
    if (highlighted && ! pressed)
        fill = dark ? fill.brighter (0.25f) : fill.brighter (0.06f);
    if (pressed)
        fill = fill.darker (dark ? 0.0f : 0.05f);

    g.setColour (fill);
    g.fillPath (shape);

    if (pressed)
    {
        g.saveState();
        g.reduceClipRegion (shape);
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha ((dark ? 0.35f : 0.18f) * (1.0f - (float) i / 4.0f) * 0.5f));
            g.drawRoundedRectangle (area.translated (0.0f, 2.0f).reduced ((float) i + 0.5f), corner - (float) i, 1.0f);
        }
        g.restoreState();
    }

    auto content = area.withTrimmedLeft (18.0f).withTrimmedRight (18.0f);

    if (latching)
    {
        const auto led = content.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f);
        content.removeFromLeft (6.0f);
        if (lit)
        {
            g.setGradientFill (juce::ColourGradient (c.led.withAlpha (0.5f), led.getCentreX(), led.getCentreY(), c.led.withAlpha (0.0f),
                                                     led.getCentreX() + 8.0f, led.getCentreY(), true));
            g.fillEllipse (led.expanded (5.0f));
        }
        g.setColour (lit ? c.led : c.ledOff);
        g.fillEllipse (led);
    }

    g.setColour (dark ? c.pillDarkText : c.pillText);
    g.setFont (fonts->sans (metrics::textSize));
    g.drawText (getButtonText(), content, juce::Justification::centred, false);

    if (hasKeyboardFocus (false))
    {
        g.setColour (c.focusOnBody);
        g.drawRoundedRectangle (area.expanded (3.0f), corner + 3.0f, 1.5f);
    }
}

//==============================================================================
Strip::Strip (InkcapController& c)
    : controller (c),
      mix (c, params::mix, "MIX"),
      output (c, params::output, "OUT")
{
    setTitle ("Keys");

    for (int i = 0; i < numModules; ++i)
    {
        keys[(size_t) i] = std::make_unique<ModuleKey> (c, (Module) i);
        addAndMakeVisible (*keys[(size_t) i]);
    }

    lock.setTooltip ("Lock the seed: presets and A/B keep it");
    reroll.setTooltip ("A new seed: new chaos, the same settings");
    lock.onClick = [this] { controller.setSeedLocked (! controller.isSeedLocked()); update(); };
    reroll.onClick = [this] { controller.rerollSeed(); };

    addAndMakeVisible (mix);
    addAndMakeVisible (output);
    addAndMakeVisible (lock);
    addAndMakeVisible (reroll);
    update();
}

void Strip::update()
{
    for (auto& k : keys)
        k->update();

    mix.update();
    output.update();
    mix.setLocked (controller.getPresets().isMixLocked());
    lock.setLit (controller.isSeedLocked());
}

void Strip::resized()
{
    // The mock's flex row: 14 px padding, 18 px gaps, items of 42 with their label underneath.
    const auto itemHeight = d + layout::labelGap + layout::labelHeight;
    auto x = layout::stripPadX;
    auto place = [&] (juce::Component& c)
    {
        constexpr float itemWidth = 48.0f;   // a label as wide as "Warble" overhangs its key a little
        c.setBounds (juce::Rectangle<float> (x - (itemWidth - d) * 0.5f, layout::itemTop, itemWidth, itemHeight).toNearestInt());
        x += d + layout::stripGap;
    };

    for (auto& k : keys)
        place (*k);

    seamX = x;
    x += 1.0f + layout::stripGap;
    place (mix);
    place (output);

    // Like the mock's flex row, the pills and the seam centre on the strip, not on the keys.
    const auto pillY = ((float) getHeight() - layout::pillHeight) * 0.5f;
    auto right = (float) getWidth() - layout::stripPadX;
    const auto rw = reroll.idealWidth();
    reroll.setBounds (juce::Rectangle<float> (right - rw, pillY, rw, layout::pillHeight).toNearestInt());
    right -= rw + layout::pillGap;
    const auto lw = lock.idealWidth();
    lock.setBounds (juce::Rectangle<float> (right - lw, pillY, lw, layout::pillHeight).toNearestInt());
}

void Strip::paint (juce::Graphics& g)
{
    // The seam between the module keys and the knobs.
    const auto centreY = (float) getHeight() * 0.5f;
    g.setColour (Theme::glass().seam);
    g.fillRect (juce::Rectangle<float> (seamX, centreY - layout::seamHeight * 0.5f, 1.0f, layout::seamHeight));
}

} // namespace inkcap::ui
