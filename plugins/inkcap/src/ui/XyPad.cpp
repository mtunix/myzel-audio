#include "ui/XyPad.h"

#include "plugin/Parameters.h"
#include "ui/GlassWidgets.h"

namespace inkcap::ui
{

/** One axis of the pad in the accessibility tree: a slider over its parameter. Draws nothing, takes no clicks. */
class XyPad::Axis final : public juce::Component
{
public:
    Axis (InkcapController& c, const char* parameterId, const juce::String& name)
        : controller (c), id (parameterId)
    {
        setTitle (name);
        setInterceptsMouseClicks (false, false);
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        class Value final : public juce::AccessibilityValueInterface
        {
        public:
            explicit Value (Axis& a) : axis (a) {}

            bool isReadOnly() const override { return false; }
            double getCurrentValue() const override { return axis.controller.normalised (axis.id); }
            juce::String getCurrentValueAsString() const override { return axis.controller.text (axis.id); }
            void setValue (double v) override { axis.controller.setNormalised (axis.id, (float) v); }
            void setValueAsString (const juce::String& s) override { axis.controller.setFromText (axis.id, s); }
            AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, keyStep }; }

        private:
            Axis& axis;
        };

        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions {},
                                                             juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
    }

private:
    InkcapController& controller;
    const char* id;
};

//==============================================================================
XyPad::XyPad (InkcapController& c)
    : controller (c),
      damageAxis (std::make_unique<Axis> (c, params::damage, "Damage")),
      chaosAxis (std::make_unique<Axis> (c, params::chaos, "Chaos"))
{
    setTitle ("Damage and Chaos");
    setDescription ("Damage to the right, Chaos up. Arrow keys move the puck, Shift for fine steps, double-click resets to 0 / 0.");
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    addAndMakeVisible (*damageAxis);
    addAndMakeVisible (*chaosAxis);
    update();
}

XyPad::~XyPad()
{
    if (dragging || inWheelGesture)
        endGesture();
}

juce::Rectangle<float> XyPad::travel() const
{
    const auto a = getLocalBounds().toFloat();
    return a.reduced (a.getWidth() * edge, a.getHeight() * edge);
}

juce::Point<float> XyPad::pointFor (float d, float c) const
{
    const auto t = travel();
    return { t.getX() + d * t.getWidth(), t.getBottom() - c * t.getHeight() };
}

void XyPad::update()
{
    const auto d = controller.damage();
    const auto c = controller.chaos();
    const auto w = controller.wander();
    const auto s = controller.getSeeds().current();
    const auto b = controller.flag (params::bypass);
    const auto on = controller.anyModuleOn();

    if (juce::approximatelyEqual (d, damage) && juce::approximatelyEqual (c, chaos) && juce::approximatelyEqual (w, wander)
        && s == seed && b == bypassed && on == anyOn && ! rings[0].isEmpty())
        return;

    damage = d, chaos = c, wander = w, seed = s, bypassed = b, anyOn = on;
    rebuildRings();
    repaint();
}

void XyPad::rebuildRings()
{
    // The mock's contour: a circle bent by a 2nd and a 5th harmonic. Their phases are the seed's,
    // so the outline is this seed's; its size is the wander (5 % and 9 % of the height when calm).
    const auto phase1 = juce::MathConstants<float>::twoPi * (float) (seed & 0xffffu) / 65536.0f;
    const auto phase2 = juce::MathConstants<float>::twoPi * (float) (seed >> 16) / 65536.0f;
    const auto centre = puckCentre();
    const auto h = (float) getHeight();
    const std::array<float, 2> sizes { 0.05f + 0.21f * wander, 0.09f + 0.40f * wander };

    for (size_t r = 0; r < rings.size(); ++r)
    {
        auto& p = rings[r];
        p.clear();
        constexpr int points = 96;

        for (int i = 0; i <= points; ++i)
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) i / (float) points;
            const auto radius = sizes[r] * h * (1.0f + 0.2f * std::sin (a * 2.0f + phase1) + 0.1f * std::sin (a * 5.0f + phase2));
            const juce::Point<float> pt (centre.x + std::cos (a) * radius, centre.y + std::sin (a) * radius);
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        p.closeSubPath();
    }
}

void XyPad::resized()
{
    background.invalidate();
    damageAxis->setBounds (getLocalBounds());
    chaosAxis->setBounds (getLocalBounds());
    rebuildRings();
}

void XyPad::paint (juce::Graphics& g)
{
    const auto& c = Theme::glass();
    const auto area = getLocalBounds().toFloat();

    background.draw (g, area, [this, &c] (juce::Graphics& lg, juce::Rectangle<float> a, float)
    {
        // Dots every 20 px, in the middle of each tile (the mock's radial-gradient grid).
        lg.setColour (c.gridDot);
        const auto inner = a.reduced (1.0f);
        for (auto y = inner.getY() + 10.0f; y < inner.getBottom() - 2.0f; y += 20.0f)
            for (auto x = inner.getX() + 10.0f; x < inner.getRight() - 2.0f; x += 20.0f)
                lg.fillEllipse (x - 1.2f, y - 1.2f, 2.4f, 2.4f);

        lg.setColour (c.line);
        lg.drawRoundedRectangle (a.reduced (0.5f), metrics::padCorner, 1.0f);

        const auto font = fonts->mono (metrics::textSize);
        lg.setFont (font);
        lg.setColour (c.textDim);
        lg.drawText ("fresh", juce::Rectangle<float> (10.0f, a.getBottom() - 8.0f - 15.0f, 100.0f, 15.0f), juce::Justification::centredLeft);
        lg.drawText ("worn", juce::Rectangle<float> (a.getRight() - 110.0f, a.getBottom() - 8.0f - 15.0f, 100.0f, 15.0f),
                     juce::Justification::centredRight);
        lg.drawText ("unstable", juce::Rectangle<float> (10.0f, 8.0f, 100.0f, 15.0f), juce::Justification::centredLeft);
    });

    const auto dim = bypassed ? 0.4f : 1.0f;

    // Near an edge the rings run out of the pad: the border cuts them, as a window would.
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path inside;
        inside.addRoundedRectangle (area.reduced (1.0f), metrics::padCorner - 1.0f);
        g.reduceClipRegion (inside);
        g.setColour (c.amber.withMultipliedAlpha (0.25f * dim));
        g.strokePath (rings[0], juce::PathStrokeType (1.0f));
        g.setColour (c.amber.withMultipliedAlpha (0.15f * dim));
        g.strokePath (rings[1], juce::PathStrokeType (1.0f));
    }

    // With every module off the puck still sets the macros, but nothing wears: it reads as a ghost.
    const auto puck = (anyOn ? c.amber : c.textDim).withMultipliedAlpha (dim);
    const auto p = puckCentre();
    g.setColour (puck);
    g.drawEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (p), 2.0f);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));

    if (bypassed)
    {
        g.setColour (c.caution);
        g.setFont (fonts->mono (metrics::textSize));
        g.drawText ("bypassed", juce::Rectangle<float> (area.getRight() - 110.0f, 8.0f, 100.0f, 15.0f), juce::Justification::centredRight);
    }

    if (hasKeyboardFocus (false))
        glassdraw::focusRing (g, area, metrics::padCorner);
}

//==============================================================================
void XyPad::beginGesture()
{
    controller.getUndoManager().beginNewTransaction();
    controller.parameter (params::damage).beginChangeGesture();
    controller.parameter (params::chaos).beginChangeGesture();
}

void XyPad::endGesture()
{
    controller.parameter (params::damage).endChangeGesture();
    controller.parameter (params::chaos).endChangeGesture();
}

void XyPad::moveTo (float newDamage, float newChaos)
{
    // Damage and Chaos are 0 .. 100 % on a linear range: the normalised value is the share.
    auto& d = controller.parameter (params::damage);
    auto& c = controller.parameter (params::chaos);
    newDamage = juce::jlimit (0.0f, 1.0f, newDamage);
    newChaos = juce::jlimit (0.0f, 1.0f, newChaos);

    if (! juce::approximatelyEqual (d.getValue(), newDamage))
        d.setValueNotifyingHost (newDamage);
    if (! juce::approximatelyEqual (c.getValue(), newChaos))
        c.setValueNotifyingHost (newChaos);

    update();
}

void XyPad::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)
        return;

    if (inWheelGesture)
    {
        stopTimer();
        inWheelGesture = false;
        endGesture();
    }

    dragging = true;
    beginGesture();

    const auto t = travel();
    if (e.position.getDistanceFrom (puckCentre()) > grabRadius)
        moveTo ((e.position.x - t.getX()) / t.getWidth(), (t.getBottom() - e.position.y) / t.getHeight());

    dragDamage = controller.normalised (params::damage);
    dragChaos = controller.normalised (params::chaos);
    lastDrag = e.position;
    setMouseCursor (juce::MouseCursor::NoCursor);
}

void XyPad::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto t = travel();
    const auto factor = e.mods.isShiftDown() ? metrics::fineFactor : 1.0f;
    const auto delta = e.position - lastDrag;
    lastDrag = e.position;

    // Accumulate unclamped so the puck comes back off the edge where the hand does.
    dragDamage += delta.x / t.getWidth() * factor;
    dragChaos -= delta.y / t.getHeight() * factor;
    moveTo (dragDamage, dragChaos);
}

void XyPad::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    endGesture();
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void XyPad::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (e.position.getDistanceFrom (puckCentre()) <= grabRadius ? juce::MouseCursor::DraggingHandCursor
                                                                            : juce::MouseCursor::CrosshairCursor);
}

void XyPad::mouseDoubleClick (const juce::MouseEvent&)
{
    beginGesture();
    moveTo (0.0f, 0.0f);
    endGesture();
}

void XyPad::nudge (float dDamage, float dChaos)
{
    if (! inWheelGesture)
        beginGesture();

    moveTo (controller.normalised (params::damage) + dDamage, controller.normalised (params::chaos) + dChaos);

    if (! inWheelGesture)
        endGesture();
}

void XyPad::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (dragging)
        return;

    // A wheel movement is one gesture and one undo step until it rests for a moment.
    if (! inWheelGesture)
    {
        inWheelGesture = true;
        beginGesture();
    }
    startTimer (300);

    const auto factor = (e.mods.isShiftDown() ? metrics::fineFactor : 1.0f) * 0.25f * (w.isReversed ? -1.0f : 1.0f);
    moveTo (controller.normalised (params::damage) + w.deltaX * factor, controller.normalised (params::chaos) + w.deltaY * factor);
}

void XyPad::timerCallback()
{
    stopTimer();
    if (inWheelGesture)
    {
        inWheelGesture = false;
        endGesture();
    }
}

bool XyPad::keyPressed (const juce::KeyPress& key)
{
    const auto step = key.getModifiers().isShiftDown() ? keyStep * metrics::fineFactor : keyStep;
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::leftKey)  { nudge (-step, 0.0f); return true; }
    if (code == juce::KeyPress::rightKey) { nudge (step, 0.0f);  return true; }
    if (code == juce::KeyPress::upKey)    { nudge (0.0f, step);  return true; }
    if (code == juce::KeyPress::downKey)  { nudge (0.0f, -step); return true; }

    if (code == juce::KeyPress::homeKey)
    {
        nudge (-1.0f, -1.0f);
        return true;
    }

    return false;
}

std::unique_ptr<juce::AccessibilityHandler> XyPad::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

} // namespace inkcap::ui
