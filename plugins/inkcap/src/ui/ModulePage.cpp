#include "ui/ModulePage.h"

#include "plugin/Parameters.h"

namespace inkcap::ui
{

namespace
{
    constexpr float labelX = 8.0f, labelWidth = 80.0f, valueWidth = 104.0f;

    struct RowSpec
    {
        const char* id;
        const char* label;
        ValueRow::Kind kind;
    };

    std::vector<RowSpec> rowsFor (Module m)
    {
        using K = ValueRow::Kind;
        namespace p = params;

        switch (m)
        {
            case Module::glitch: return { { p::glitchAmount, "amount", K::continuous }, { p::glitchRate, "rate", K::steps },
                                          { p::glitchLength, "length", K::continuous }, { p::glitchGroove, "groove", K::continuous } };
            case Module::warble: return { { p::warbleAmount, "amount", K::continuous }, { p::warbleRate, "rate", K::continuous },
                                          { p::warbleSync, "sync", K::toggle }, { p::warbleSpread, "spread", K::continuous } };
            case Module::crush:  return { { p::crushAmount, "amount", K::continuous }, { p::crushDrive, "drive", K::continuous },
                                          { p::crushTone, "tone", K::bipolar } };
            case Module::bits:   return { { p::bitsAmount, "amount", K::continuous }, { p::bitsRate, "rate", K::continuous },
                                          { p::bitsDepth, "depth", K::continuous }, { p::bitsJitter, "jitter", K::continuous } };
            case Module::hiss:   return { { p::hissAmount, "amount", K::continuous }, { p::hissTone, "tone", K::bipolar },
                                          { p::hissFollow, "follow", K::bipolar } };
            case Module::room:   return { { p::roomAmount, "amount", K::continuous }, { p::roomSize, "size", K::continuous },
                                          { p::roomDuck, "duck", K::continuous } };
        }
        return {};
    }

    const char* tagline (Module m)
    {
        switch (m)
        {
            case Module::glitch: return "the medium fails";
            case Module::warble: return "wow and flutter";
            case Module::crush:  return "saturation";
            case Module::bits:   return "digital decay";
            case Module::hiss:   return "the noise floor";
            case Module::room:   return "a small space";
        }
        return "";
    }
}

//==============================================================================
ValueRow::ValueRow (InkcapController& c, const char* parameterId, const juce::String& rowLabel, Kind k)
    : controller (c), id (parameterId), label (rowLabel), kind (k)
{
    setTitle (controller.parameter (id).getName (64));
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setMouseCursor (kind == Kind::toggle ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::LeftRightResizeCursor);
    update();
}

ValueRow::~ValueRow()
{
    if (dragging || inWheelGesture)
        controller.endEdit (id);
}

juce::Rectangle<float> ValueRow::valueArea() const
{
    return { (float) getWidth() - labelX - valueWidth, 0.0f, valueWidth, (float) getHeight() };
}

juce::Rectangle<float> ValueRow::barArea() const
{
    const auto left = labelX + labelWidth + 8.0f;
    return { left, 0.0f, valueArea().getX() - 12.0f - left, (float) getHeight() };
}

int ValueRow::numSteps() const
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&controller.parameter (id)))
        return choice->choices.size();
    return 0;
}

void ValueRow::update()
{
    const auto v = controller.normalised (id);
    const auto t = controller.text (id);
    const auto r = reach != nullptr ? reach() : -1.0f;

    if (! juce::approximatelyEqual (v, value) || t != valueText || ! juce::approximatelyEqual (r, reached))
    {
        value = v;
        valueText = t;
        reached = r;
        repaint();
    }
}

void ValueRow::paint (juce::Graphics& g)
{
    const auto& c = Theme::glass();
    const auto area = getLocalBounds().toFloat();
    const auto active = dragging || inWheelGesture;

    if (hover || active)
        g.setColour (c.rowHover), g.fillRoundedRectangle (area, metrics::rowCorner);

    const auto font = fonts->mono (metrics::textSize);
    g.setFont (font);
    g.setColour (c.textDim);
    g.drawText (label, juce::Rectangle<float> (labelX, 0.0f, labelWidth, area.getHeight()), juce::Justification::centredLeft, false);

    const auto barRect = barArea();
    const auto bar = barRect.withSizeKeepingCentre (barRect.getWidth(), 3.0f);
    const auto fill = active ? c.amber : c.text;

    if (kind == Kind::toggle)
    {
        // A small switch: a track with the knob left (off) or right and amber (on).
        const auto isOn = value >= 0.5f;
        const auto track = juce::Rectangle<float> (barRect.getX(), barRect.getCentreY() - 6.0f, 26.0f, 12.0f);
        g.setColour (isOn ? c.amber.withAlpha (0.25f) : c.track);
        g.fillRoundedRectangle (track, 6.0f);
        g.setColour (isOn ? c.amber : c.textDim);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ isOn ? track.getRight() - 6.0f : track.getX() + 6.0f,
                                                                         track.getCentreY() }));
    }
    else
    {
        g.setColour (c.track);
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (fill);

        if (kind == Kind::bipolar)
        {
            const auto mid = bar.getCentreX();
            const auto x = bar.getX() + bar.getWidth() * value;
            g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (mid, x), bar.getY(), juce::jmax (2.0f, std::abs (x - mid)), 3.0f), 1.5f);
            g.setColour (c.textDim);
            g.fillRect (juce::Rectangle<float> (1.0f, 9.0f).withCentre ({ mid, bar.getCentreY() }));
        }
        else if (reached >= 0.0f && ! active)
        {
            g.setColour (c.textFaint);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (3.0f, bar.getWidth() * value)), 1.5f);
            if (reached > 0.0f)
            {
                g.setColour (fill);
                g.fillRoundedRectangle (bar.withWidth (juce::jmax (3.0f, bar.getWidth() * reached)), 1.5f);
            }
        }
        else
        {
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (3.0f, bar.getWidth() * value)), 1.5f);

            if (kind == Kind::steps)
            {
                const auto n = numSteps();
                g.setColour (c.textFaint.withMultipliedAlpha (0.7f));
                for (int i = 0; i < n; ++i)
                    g.fillRect (juce::Rectangle<float> (1.0f, 3.0f).withCentre ({ bar.getX() + bar.getWidth() * (float) i / (float) juce::jmax (1, n - 1),
                                                                                  bar.getY() - 4.0f }));
            }
        }
    }

    if (! entry.isOpen())
    {
        g.setColour (active ? c.amber : c.text);
        g.drawText (valueText, valueArea(), juce::Justification::centredRight, false);
    }

    if (hasKeyboardFocus (false))
        glassdraw::focusRing (g, area, metrics::rowCorner);
}

void ValueRow::setFromGesture (float value01)
{
    auto& p = controller.parameter (id);
    value01 = juce::jlimit (0.0f, 1.0f, value01);

    if (kind == Kind::steps)
    {
        const auto n = juce::jmax (2, numSteps());
        value01 = (float) juce::roundToInt (value01 * (float) (n - 1)) / (float) (n - 1);
    }

    if (! juce::approximatelyEqual (p.getValue(), value01))
        p.setValueNotifyingHost (value01);

    update();
}

void ValueRow::mouseDown (const juce::MouseEvent& e)
{
    pressed = true;

    if (e.mods.isAltDown())
    {
        controller.resetToDefault (id);
        update();
        pressed = false;
        return;
    }

    if (kind == Kind::toggle)
        return;

    if (inWheelGesture)
        timerCallback();

    dragging = true;
    dragValue = controller.normalised (id);
    lastX = e.position.x;
    controller.beginEdit (id);
    repaint();
}

void ValueRow::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto factor = e.mods.isShiftDown() ? metrics::fineFactor : 1.0f;
    dragValue += (e.position.x - lastX) / juce::jmax (40.0f, barArea().getWidth()) * factor;
    dragValue = juce::jlimit (-0.25f, 1.25f, dragValue);
    lastX = e.position.x;
    setFromGesture (dragValue);
}

void ValueRow::mouseUp (const juce::MouseEvent& e)
{
    if (kind == Kind::toggle && pressed && getLocalBounds().toFloat().contains (e.position) && e.getNumberOfClicks() == 1)
    {
        controller.toggle (id);
        update();
    }

    pressed = false;

    if (! dragging)
        return;

    dragging = false;
    controller.endEdit (id);
    repaint();
}

void ValueRow::mouseDoubleClick (const juce::MouseEvent&)
{
    if (kind != Kind::toggle)
        openTextEntry();
}

void ValueRow::openTextEntry()
{
    entry.open (*this, valueArea().expanded (4.0f, -3.0f).toNearestInt(), valueText, label,
                [this] (const juce::String& t)
                {
                    controller.setFromText (id, t);
                    update();
                });
    repaint();
}

void ValueRow::step (int direction, bool fine)
{
    if (kind == Kind::toggle)
    {
        controller.setNormalised (id, direction > 0 ? 1.0f : 0.0f);
    }
    else if (kind == Kind::steps)
    {
        const auto n = juce::jmax (2, numSteps());
        controller.setNormalised (id, controller.normalised (id) + (float) direction / (float) (n - 1));
    }
    else
    {
        controller.setNormalised (id, controller.normalised (id) + (float) direction * (fine ? 0.002f : 0.01f));
    }
    update();
}

void ValueRow::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (dragging || kind == Kind::toggle)
        return;

    // A wheel movement is one gesture until it rests; stepped rows move one step per notch.
    if (! inWheelGesture)
    {
        inWheelGesture = true;
        wheelAccumulator = 0.0f;
        controller.beginEdit (id);
    }
    startTimer (300);

    const auto delta = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f);

    if (kind == Kind::steps)
    {
        wheelAccumulator += delta;
        if (std::abs (wheelAccumulator) >= 0.12f)
        {
            const auto n = juce::jmax (2, numSteps());
            setFromGesture (controller.normalised (id) + (wheelAccumulator > 0.0f ? 1.0f : -1.0f) / (float) (n - 1));
            wheelAccumulator = 0.0f;
        }
    }
    else
    {
        setFromGesture (controller.normalised (id) + delta * 0.25f * (e.mods.isShiftDown() ? metrics::fineFactor : 1.0f));
    }
    repaint();
}

void ValueRow::timerCallback()
{
    stopTimer();
    if (inWheelGesture)
    {
        inWheelGesture = false;
        controller.endEdit (id);
        repaint();
    }
}

bool ValueRow::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto fine = key.getModifiers().isShiftDown();

    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey)   { step (1, fine);  return true; }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey)  { step (-1, fine); return true; }

    if (code == juce::KeyPress::returnKey)
    {
        if (kind == Kind::toggle)
            controller.toggle (id), update();
        else
            openTextEntry();
        return true;
    }
    if (code == juce::KeyPress::spaceKey && kind == Kind::toggle)
    {
        controller.toggle (id);
        update();
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> ValueRow::createAccessibilityHandler()
{
    class Value final : public juce::AccessibilityValueInterface
    {
    public:
        explicit Value (ValueRow& r) : row (r) {}

        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return row.controller.normalised (row.id); }
        juce::String getCurrentValueAsString() const override { return row.controller.text (row.id); }
        void setValue (double v) override { row.controller.setNormalised (row.id, (float) v); row.update(); }
        void setValueAsString (const juce::String& s) override { row.controller.setFromText (row.id, s); row.update(); }
        AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, 0.01 }; }

    private:
        ValueRow& row;
    };

    if (kind == Kind::toggle)
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::toggleButton,
            juce::AccessibilityActions().addAction (juce::AccessibilityActionType::toggle, [this] { controller.toggle (id); update(); }));

    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions {},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

//==============================================================================
/** The page's title: a back chevron and the module's name. */
class ModulePage::BackKey final : public juce::Button
{
public:
    explicit BackKey (ModulePage& p) : juce::Button ("back"), page (p)
    {
        setTitle ("Back to the module list");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& c = Theme::glass();
        const auto area = getLocalBounds().toFloat();
        glassdraw::chevron (g, juce::Rectangle<float> (8.0f, 0.0f, 10.0f, area.getHeight()), true, highlighted ? c.text : c.textDim);

        const auto font = page.fonts->mono (metrics::textSize);
        g.setFont (font);
        g.setColour (c.text);
        const juce::String name (moduleInfo (page.module).name);
        g.drawText (name, juce::Rectangle<float> (30.0f, 0.0f, 200.0f, area.getHeight()), juce::Justification::centredLeft, false);
        g.setColour (c.textDim);
        g.drawText (tagline (page.module), juce::Rectangle<float> (30.0f + glassdraw::textWidth (font, name) + 14.0f, 0.0f, 220.0f, area.getHeight()),
                    juce::Justification::centredLeft, false);

        if (hasKeyboardFocus (false))
            glassdraw::focusRing (g, area, metrics::rowCorner);
    }

private:
    ModulePage& page;
};

ModulePage::ModulePage (InkcapController& c)
    : controller (c),
      backKey (std::make_unique<BackKey> (*this)),
      events (c)
{
    setWantsKeyboardFocus (false);

    backKey->onClick = [this] { if (onClose != nullptr) onClose(); };
    onKey.onClick = [this]
    {
        controller.toggle (params::onIds[(size_t) module]);
        update();
    };

    addAndMakeVisible (*backKey);
    addAndMakeVisible (onKey);
    addChildComponent (events);
    rebuild();
}

ModulePage::~ModulePage() = default;

juce::Button& ModulePage::getBackKey() noexcept
{
    return *backKey;
}

void ModulePage::setModule (Module m)
{
    if (m == module && ! rows.empty())
        return;

    module = m;
    rebuild();
}

void ModulePage::rebuild()
{
    for (auto& t : tabs)
        removeChildComponent (t.get());
    for (auto& r : rows)
        removeChildComponent (r.get());
    tabs.clear();
    rows.clear();

    const auto& info = moduleInfo (module);
    setTitle (juce::String (info.name) + " page");

    for (int t = 0; t < (int) info.types.size(); ++t)
    {
        auto key = std::make_unique<SoftKey> (InkcapController::typeName (module, t), juce::String (info.name) + " type " + info.types[(size_t) t]);
        key->onClick = [this, t]
        {
            const auto* typeId = params::typeIds[(size_t) module];
            if (controller.type (module) != t)
                controller.setPlain (typeId, (float) t);
            update();
        };
        addAndMakeVisible (*key);
        tabs.push_back (std::move (key));
    }

    for (const auto& spec : rowsFor (module))
    {
        rows.push_back (std::make_unique<ValueRow> (controller, spec.id, spec.label, spec.kind));
        addAndMakeVisible (*rows.back());
    }

    rows.front()->setReach ([this] { return controller.intensity (module); });

    events.setVisible (module == Module::glitch);
    type = -1;
    resized();
    update();
    repaint();
}

SoftKey* ModulePage::getTab (int typeIndex)
{
    return juce::isPositiveAndBelow (typeIndex, (int) tabs.size()) ? tabs[(size_t) typeIndex].get() : nullptr;
}

ValueRow* ModulePage::findRow (const char* parameterId)
{
    for (auto& r : rows)
        if (juce::String (r->getParameterId()) == parameterId)
            return r.get();
    return nullptr;
}

void ModulePage::update()
{
    const auto newOn = controller.isOn (module);
    const auto newType = controller.type (module);

    if (newOn != on)
    {
        on = newOn;
        onKey.setText (on ? "on" : "off");
        onKey.setLit (on);
        onKey.setToggleState (on, juce::dontSendNotification);
        resized();
        repaint();
    }

    if (newType != type)
    {
        type = newType;
        for (int t = 0; t < (int) tabs.size(); ++t)
        {
            tabs[(size_t) t]->setLit (t == type);
            tabs[(size_t) t]->setToggleState (t == type, juce::dontSendNotification);
        }
    }

    for (auto& r : rows)
        r->update();

    // Glitch's events say it themselves; the other pages say it in words under their rows.
    juce::String newCaption;
    if (module != Module::glitch)
    {
        const auto damage = juce::roundToInt (controller.plain (params::damage));
        if (! on)
            newCaption = "off: it does nothing until switched on";
        else if (damage == 0)
            newCaption = "damage 0: it does nothing yet";
        else
            newCaption = "damage " + juce::String (damage) + " brings it to " + juce::String (juce::roundToInt (controller.intensity (module) * 100.0f)) + " %";
    }

    if (newCaption != caption)
    {
        caption = newCaption;
        repaint();
    }

    if (events.isVisible())
        events.update();
}

void ModulePage::resized()
{
    const auto w = (float) getWidth();
    const auto onWidth = onKey.idealWidth();
    onKey.setBounds (juce::Rectangle<float> (w - layout::rowPadX - onWidth, 3.0f, onWidth, 24.0f).toNearestInt());
    backKey->setBounds (juce::Rectangle<float> (0.0f, 0.0f, w - onWidth - 30.0f, layout::rowHeight).toNearestInt());

    auto x = layout::rowPadX;
    for (auto& t : tabs)
    {
        const auto tw = t->idealWidth();
        t->setBounds (juce::Rectangle<float> (x, tabsY, tw, 24.0f).toNearestInt());
        x += tw + 16.0f;
    }

    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (juce::Rectangle<float> (0.0f, rowsY + (float) i * layout::rowPitch, w, layout::rowHeight).toNearestInt());

    events.setBounds (juce::Rectangle<float> (0.0f, eventsY, w, (float) getHeight() - eventsY).toNearestInt());
}

void ModulePage::paint (juce::Graphics& g)
{
    // The title row keeps the selected row's ground, so the page reads as that row opened.
    const auto& c = Theme::glass();
    g.setColour (c.rowSelected);
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), layout::rowHeight), metrics::rowCorner);

    if (caption.isNotEmpty())
    {
        g.setColour (c.textDim);
        g.setFont (fonts->mono (metrics::textSize));
        g.drawText (caption, juce::Rectangle<float> (labelX, rowsY + (float) rows.size() * layout::rowPitch + 6.0f, (float) getWidth() - 2.0f * labelX, 16.0f),
                    juce::Justification::centredLeft, false);
    }
}

bool ModulePage::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose != nullptr)
    {
        onClose();
        return true;
    }
    return false;
}

} // namespace inkcap::ui
