#include "ui/Knob.h"

namespace umber::ui
{

namespace
{
    constexpr float lightAngle = -0.78539816f;   // top-left, in knob angle convention
    constexpr float detentWidth = 0.035f;        // raw drag distance that sticks at a centre detent
    constexpr float fineFactor = 0.12f;
    constexpr int wheelGestureTimeoutMs = 350;

    //==============================================================================
    class KnobValueInterface final : public juce::AccessibilityValueInterface
    {
    public:
        explicit KnobValueInterface (Knob& k) : knob (k) {}

        bool isReadOnly() const override                   { return false; }
        double getCurrentValue() const override            { return knob.getNormalisedValue(); }
        juce::String getCurrentValueAsString() const override { return knob.getValueText(); }
        void setValue (double v) override                  { knob.setNormalisedValueAsGesture ((float) v); }

        void setValueAsString (const juce::String& s) override
        {
            knob.setNormalisedValueAsGesture (knob.getParameter().getValueForText (s));
        }

        AccessibleValueRange getRange() const override
        {
            return { { 0.0, 1.0 }, 0.01 };
        }

    private:
        Knob& knob;
    };

    class KnobAccessibilityHandler final : public juce::AccessibilityHandler
    {
    public:
        explicit KnobAccessibilityHandler (Knob& k)
            : juce::AccessibilityHandler (k, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                          Interfaces { std::make_unique<KnobValueInterface> (k) })
        {
        }
    };
}

//==============================================================================
Knob::Knob (juce::RangedAudioParameter& p, juce::UndoManager* um, KnobStyle s)
    : parameter (p),
      undoManager (um),
      style (std::move (s)),
      attachment (p, [this] (float v) { valueChanged (v); }, um)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setTitle (style.title);
    setDescription (parameter.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

Knob::~Knob()
{
    if (dragging || inWheelGesture)
        attachment.endGesture();
}

//==============================================================================
void Knob::valueChanged (float denormalised)
{
    const auto v = parameter.convertTo0to1 (denormalised);

    if (std::abs (v - value01) > 1.0e-6f)
    {
        value01 = v;
        repaint();

        if (valueBubble != nullptr && (hovering || dragging))
            updateBubble();
    }
}

juce::String Knob::getValueText() const
{
    return parameter.getText (value01, 32);
}

void Knob::setNormalisedValueAsGesture (float newValue01)
{
    finishWheelGesture();
    attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, newValue01)));
}

juce::Point<float> Knob::getLegendAnchor() const
{
    const auto offset = style.hero ? radius() + 38.0f : radius() + 22.0f;
    return getBounds().toFloat().getCentre().translated (0.0f, offset);
}

void Knob::setHoverForSnapshot (bool shouldHover)
{
    hovering = shouldHover;
    updateBubble();
    repaint();
}

void Knob::updateBubble()
{
    if (valueBubble == nullptr)
        return;

    if (hovering || dragging || textEntry != nullptr)
        valueBubble->show (getLegendAnchor(), getValueText());
    else
        valueBubble->hide();
}

//==============================================================================
float Knob::rawFromValue (float v) const noexcept
{
    if (! style.centreDetent)
        return v;

    if (std::abs (v - 0.5f) < 1.0e-4f)
        return 0.5f;

    const auto halfSpan = 0.5f - detentWidth;
    return v < 0.5f ? v * halfSpan / 0.5f
                    : 0.5f + detentWidth + (v - 0.5f) * halfSpan / 0.5f;
}

float Knob::valueFromRaw (float raw) const noexcept
{
    raw = juce::jlimit (0.0f, 1.0f, raw);

    if (! style.centreDetent)
        return raw;

    const auto halfSpan = 0.5f - detentWidth;

    if (raw < halfSpan)
        return raw * 0.5f / halfSpan;

    if (raw > 0.5f + detentWidth)
        return 0.5f + (raw - 0.5f - detentWidth) * 0.5f / halfSpan;

    return 0.5f;
}

void Knob::beginEdit()
{
    attachment.beginGesture();
}

void Knob::endEdit()
{
    attachment.endGesture();
}

void Knob::finishWheelGesture()
{
    // Any other edit closes a pending wheel gesture first, so host gestures never nest.
    if (inWheelGesture)
    {
        stopTimer();
        inWheelGesture = false;
        endEdit();
    }
}

void Knob::setFromDrag (float newRaw)
{
    dragRaw = juce::jlimit (0.0f, 1.0f, newRaw);
    attachment.setValueAsPartOfGesture (parameter.convertFrom0to1 (valueFromRaw (dragRaw)));
}

void Knob::nudge (float amount)
{
    finishWheelGesture();
    attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (valueFromRaw (rawFromValue (value01) + amount)));
}

//==============================================================================
void Knob::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    updateBubble();
    repaint();
}

void Knob::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    updateBubble();
    repaint();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mods.isAltDown() || e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        showTextEntry();
        return;
    }

    if (textEntry != nullptr)
        textEntry.reset();

    finishWheelGesture();
    dragging = true;
    dragRaw = rawFromValue (value01);
    lastDragY = e.position.y;
    beginEdit();
    e.source.enableUnboundedMouseMovement (true, false);
    updateBubble();
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto dy = lastDragY - e.position.y;
    lastDragY = e.position.y;

    auto speed = 1.0f / juce::jmax (40.0f, style.dragPixelsForFullRange);

    if (e.mods.isShiftDown())
        speed *= fineFactor;

    setFromDrag (dragRaw + dy * speed);
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    dragging = false;
    e.source.enableUnboundedMouseMovement (false, true);
    endEdit();
    hovering = isMouseOver (true);
    updateBubble();
    repaint();
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    finishWheelGesture();
    attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (parameter.getDefaultValue()));
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (dragging)
        return;

    auto delta = (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY);

    if (wheel.isReversed)
        delta = -delta;

    if (std::abs (delta) < 1.0e-6f)
        return;

    if (! inWheelGesture)
    {
        inWheelGesture = true;
        dragRaw = rawFromValue (value01);
        wheelAccumulator = 0.0f;
        beginEdit();
    }

    if (style.steps > 1)
    {
        // A switch: one position per wheel notch, however finely the device reports.
        wheelAccumulator += delta;

        if (std::abs (wheelAccumulator) >= 0.1f)
        {
            const auto direction = wheelAccumulator > 0.0f ? 1.0f : -1.0f;
            wheelAccumulator = 0.0f;
            dragRaw = juce::jlimit (0.0f, 1.0f, value01 + direction / (float) (style.steps - 1));
            setFromDrag (dragRaw);
        }

        startTimer (wheelGestureTimeoutMs);
        return;
    }

    const auto step = (e.mods.isShiftDown() ? 0.01f : 0.06f) * (delta > 0.0f ? 1.0f : -1.0f)
                      * juce::jlimit (0.5f, 3.0f, std::abs (delta) / 0.1f);
    setFromDrag (dragRaw + step);
    startTimer (wheelGestureTimeoutMs);
}

void Knob::timerCallback()
{
    finishWheelGesture();
}

bool Knob::keyPressed (const juce::KeyPress& key)
{
    const auto fine = key.getModifiers().isShiftDown();
    const auto step = style.steps > 1 ? 1.0f / (float) (style.steps - 1) : (fine ? 0.005f : 0.02f);
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)    { nudge (step);  return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)   { nudge (-step); return true; }
    if (code == juce::KeyPress::pageUpKey)                                    { nudge (0.1f);  return true; }
    if (code == juce::KeyPress::pageDownKey)                                  { nudge (-0.1f); return true; }

    if (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey
        || code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        finishWheelGesture();

    if (code == juce::KeyPress::homeKey)
    {
        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (0.0f));
        return true;
    }

    if (code == juce::KeyPress::endKey)
    {
        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (1.0f));
        return true;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (parameter.getDefaultValue()));
        return true;
    }

    if (code == juce::KeyPress::returnKey)
    {
        showTextEntry();
        return true;
    }

    return false;
}

void Knob::focusGained (FocusChangeType cause)
{
    keyboardFocused = (cause != focusChangedByMouseClick);
    repaint();
}

void Knob::focusLost (FocusChangeType)
{
    keyboardFocused = false;
    repaint();
}

//==============================================================================
void Knob::showTextEntry()
{
    const auto& pal = Theme::palette();

    textEntry = std::make_unique<juce::TextEditor> ("value");
    auto& ed = *textEntry;
    ed.setJustification (juce::Justification::centred);
    ed.setColour (juce::TextEditor::backgroundColourId, pal.displayGlass);
    ed.setColour (juce::TextEditor::textColourId, pal.displayText);
    ed.setColour (juce::TextEditor::highlightColourId, pal.ember.withAlpha (0.35f));
    ed.setColour (juce::TextEditor::highlightedTextColourId, pal.emberCore);
    ed.setColour (juce::TextEditor::outlineColourId, pal.bubbleEdge);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, pal.ember);
    ed.setColour (juce::CaretComponent::caretColourId, pal.ember);
    ed.setFont (juce::SharedResourcePointer<Fonts>()->label (13.0f));
    ed.setIndents (4, 3);
    ed.setText (getValueText(), false);
    ed.selectAll();
    ed.setTitle (style.title + " value");

    const auto width = juce::jmax (84.0f, style.diameter * 0.9f);
    ed.setBounds (juce::Rectangle<float> (width, 22.0f).withCentre (centre()).toNearestInt());

    juce::Component::SafePointer<Knob> safeThis (this);

    auto commit = [safeThis]
    {
        if (safeThis == nullptr || safeThis->textEntry == nullptr)
            return;

        const auto text = safeThis->textEntry->getText().trim();

        if (text.isNotEmpty())
            safeThis->setNormalisedValueAsGesture (safeThis->parameter.getValueForText (text));

        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->textEntry.reset();
                safeThis->updateBubble();
            }
        });
    };

    ed.onReturnKey = commit;
    ed.onFocusLost = commit;
    ed.onEscapeKey = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->textEntry.reset();
                safeThis->updateBubble();
            }
        });
    };

    addAndMakeVisible (ed);
    ed.grabKeyboardFocus();
}

//==============================================================================
void Knob::resized()
{
    bodyLayer.invalidate();
    capLayer.invalidate();
}

void Knob::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palette();
    const auto area = getLocalBounds().toFloat();
    const auto angle = drawing::knobAngle (value01);
    const auto c = centre();
    const auto r = radius();

    bodyLayer.draw (g, area, [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderBody (lg, a); });
    drawKnurling (g, angle);
    capLayer.draw (g, area, [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderCap (lg, a); });
    drawPointer (g, angle);

    if (hovering || dragging)
    {
        g.setColour (juce::Colours::white.withAlpha (dragging ? 0.06f : 0.035f));
        g.fillEllipse (juce::Rectangle<float> (r * 1.6f, r * 1.6f).withCentre (c));
        g.setColour (pal.legend.withAlpha (dragging ? 0.30f : 0.16f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f + 5.0f, r * 2.0f + 5.0f).withCentre (c), 1.0f);
    }

    if (keyboardFocused && hasKeyboardFocus (true))
    {
        g.setColour (pal.focus.withAlpha (0.9f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f + 9.0f, r * 2.0f + 9.0f).withCentre (c), 1.6f);
    }
}

void Knob::renderBody (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& pal = Theme::palette();
    const auto c = area.getCentre();
    const auto r = radius();
    const auto disc = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);

    juce::Path outline;
    outline.addEllipse (disc);
    drawing::drawContactShadow (g, outline, style.hero ? 9.0f : 5.5f);

    // Knurled grip band: a black bakelite cylinder lit from the top-left.
    g.setGradientFill (juce::ColourGradient (pal.skirtLight.brighter (0.25f), disc.getX() + r * 0.3f, disc.getY() + r * 0.3f,
                                             pal.skirtDark, disc.getRight() - r * 0.2f, disc.getBottom() - r * 0.2f, false));
    g.fillEllipse (disc);

    // Top face, with a chamfer ring between the grip and the face.
    const auto faceR = r * 0.80f;
    const auto face = juce::Rectangle<float> (faceR * 2.0f, faceR * 2.0f).withCentre (c);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0x40ffffff), face.getX(), face.getY(),
                                             juce::Colour (0x90000000), face.getRight(), face.getBottom(), false));
    g.fillEllipse (face.expanded (r * 0.035f));

    juce::ColourGradient top (pal.skirtLight, c.x - faceR * 0.55f, c.y - faceR * 0.65f,
                              pal.skirtDark, c.x + faceR * 0.6f, c.y + faceR * 0.9f, true);
    top.addColour (0.5, pal.skirtDark.interpolatedWith (pal.skirtLight, 0.35f));
    g.setGradientFill (top);
    g.fillEllipse (face);

    // Broad soft sheen on the bakelite (specular of the light at top-left).
    juce::ColourGradient sheen (juce::Colour (0x30ffffff), c.x - faceR * 0.45f, c.y - faceR * 0.5f,
                                juce::Colour (0x00ffffff), c.x - faceR * 0.45f + faceR * 0.75f, c.y - faceR * 0.5f, true);
    g.setGradientFill (sheen);
    g.fillEllipse (face);

    // Rim highlight on the upper-left edge of the grip and a dark lower-right edge.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x55ffffff), disc.getX(), disc.getY(),
                                             juce::Colour (0x00ffffff), c.x, c.y, false));
    g.drawEllipse (disc.reduced (0.5f), 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00000000), c.x, c.y,
                                             juce::Colour (0xa0000000), disc.getRight(), disc.getBottom(), false));
    g.drawEllipse (disc.reduced (0.5f), 1.2f);
}

void Knob::renderCap (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& pal = Theme::palette();
    const auto c = area.getCentre();
    const auto capR = radius() * (style.hero ? 0.50f : 0.40f);
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);

    // Seat: the cap sits in a shallow counterbore.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xc0000000), cap.getX(), cap.getY(),
                                             juce::Colour (0x40ffffff), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap.expanded (1.6f));

    // Brushed metal: concentric lay, so the reflection is a bow-tie aligned with the light.
    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addEllipse (cap);
    g.reduceClipRegion (clip);

    constexpr int wedges = 144;
    juce::Random rng (style.hero ? 7 : 3);

    // Opaque underlay, then overlapping wedges: anti-aliased seams must not show the seat through.
    g.setColour (pal.capDark.interpolatedWith (pal.capLight, 0.45f));
    g.fillEllipse (cap);

    for (int i = 0; i < wedges; ++i)
    {
        const auto a0 = juce::MathConstants<float>::twoPi * (float) i / (float) wedges;
        const auto a1 = juce::MathConstants<float>::twoPi * (float) (i + 2) / (float) wedges;
        const auto mid = 0.5f * (a0 + a1);
        const auto lobe = 0.5f + 0.5f * std::cos (2.0f * (mid - lightAngle));
        const auto lum = juce::jlimit (0.0f, 1.0f, 0.18f + 0.72f * std::pow (lobe, 1.6f) + 0.05f * (rng.nextFloat() - 0.5f));

        juce::Path w;
        w.startNewSubPath (c);
        w.addCentredArc (c.x, c.y, capR + 1.0f, capR + 1.0f, 0.0f, a0, a1);
        w.closeSubPath();
        g.setColour (pal.capDark.interpolatedWith (pal.capLight, lum));
        g.fillPath (w);
    }

    // Fine concentric turning marks.
    for (float rr = capR * 0.12f; rr < capR; rr += 0.9f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.03f + 0.04f * rng.nextFloat()));
        g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.45f);
    }


    // Lens-like specular and edge shading.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00000000), c.x, c.y,
                                             juce::Colour (0x50000000), cap.getRight(), cap.getBottom(), true));
    g.fillEllipse (cap);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x70ffffff), cap.getX(), cap.getY(),
                                             juce::Colour (0x00ffffff), c.x, c.y, false));
    g.drawEllipse (cap.reduced (0.4f), 0.9f);
}

void Knob::drawKnurling (juce::Graphics& g, float angle) const
{
    const auto c = centre();
    const auto r = radius();
    const auto ridges = style.hero ? 64 : 40;
    const auto inner = r * 0.84f, outer = r * 0.985f;
    const auto width = juce::jmax (0.8f, r * (style.hero ? 0.022f : 0.032f));

    for (int i = 0; i < ridges; ++i)
    {
        const auto a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) ridges;
        const auto facing = std::cos (a - lightAngle);   // 1 = facing the light

        const auto p0 = drawing::polar (c, inner, a);
        const auto p1 = drawing::polar (c, outer, a);

        // Lit flank of the ridge, then the shadowed flank just clockwise of it.
        g.setColour (juce::Colours::white.withAlpha (0.035f + 0.16f * juce::jmax (0.0f, facing)));
        g.drawLine ({ p0, p1 }, width);

        const auto s0 = drawing::polar (c, inner, a + 0.5f * juce::MathConstants<float>::twoPi / (float) ridges);
        const auto s1 = drawing::polar (c, outer, a + 0.5f * juce::MathConstants<float>::twoPi / (float) ridges);
        g.setColour (juce::Colours::black.withAlpha (0.35f + 0.25f * juce::jmax (0.0f, -facing)));
        g.drawLine ({ s0, s1 }, width);
    }
}

void Knob::drawPointer (juce::Graphics& g, float angle) const
{
    const auto& pal = Theme::palette();
    const auto c = centre();
    const auto r = radius();

    const auto start = r * (style.hero ? 0.56f : 0.46f);
    const auto end = r * 0.76f;
    const auto width = juce::jmax (2.2f, r * (style.hero ? 0.05f : 0.075f));

    const auto p0 = drawing::polar (c, start, angle);
    const auto p1 = drawing::polar (c, end, angle);
    juce::Path line;
    line.startNewSubPath (p0);
    line.lineTo (p1);
    const juce::PathStrokeType stroke (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // Inlay groove (shadow towards the upper left, since it is recessed).
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.strokePath (line, juce::PathStrokeType (width + 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (-0.3f, -0.5f));

    g.setColour (pal.pointer);
    g.strokePath (line, stroke);

    // A glint along the lit side of the inlay.
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.strokePath (line, juce::PathStrokeType (width * 0.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (-width * 0.18f, -width * 0.18f));
}

//==============================================================================
std::unique_ptr<juce::AccessibilityHandler> Knob::createAccessibilityHandler()
{
    return std::make_unique<KnobAccessibilityHandler> (*this);
}

} // namespace umber::ui
