#include "ui/NeedleMeter.h"

#include <melatonin_blur/melatonin_blur.h>

namespace umber::ui
{

namespace
{
    constexpr float halfSweep = 0.74f;        // radians either side of vertical
    constexpr float fullScale = 1.41254f;     // +3 VU, linear: the right end of the scale
    constexpr float peakHoldSeconds = 0.6f;

    struct Mark
    {
        float vu;
        const char* text;
        bool major;
    };

    constexpr Mark marks[] = {
        { -20.0f, "20", true }, { -10.0f, "10", true }, { -7.0f, "7", true }, { -5.0f, "5", true },
        { -3.0f, "3", true },   { -2.0f, "2", true },   { -1.0f, "1", true }, { 0.0f, "0", true },
        { 1.0f, "1", true },    { 2.0f, "2", true },    { 3.0f, "3", true },
        { -15.0f, nullptr, false }, { -8.5f, nullptr, false }, { -6.0f, nullptr, false }, { -4.0f, nullptr, false },
        { -2.5f, nullptr, false }, { -1.5f, nullptr, false }, { -0.5f, nullptr, false }, { 0.5f, nullptr, false },
        { 1.5f, nullptr, false }, { 2.5f, nullptr, false },
    };

    float linearFromVu (float vu) noexcept { return std::pow (10.0f, vu / 20.0f); }

    class MeterAccessibilityHandler final : public juce::AccessibilityHandler
    {
    public:
        explicit MeterAccessibilityHandler (NeedleMeter& m)
            : juce::AccessibilityHandler (m, juce::AccessibilityRole::button,
                                          juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press,
                                                                                  [&m] { m.toggleMode(); }))
        {
        }
    };
}

NeedleMeter::NeedleMeter()
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Level meter");
    setTooltip ("VU meter. Click to switch between Output level and Drive (the level hitting the circuit).");
    setMode (Mode::output);
}

void NeedleMeter::setMode (Mode newMode)
{
    const auto changed = mode != newMode;
    mode = newMode;
    setDescription (mode == Mode::output ? "Showing output level" : "Showing drive level into the circuit");
    faceLayer.invalidate();
    repaint();

    if (changed && onModeChanged != nullptr)
        onModeChanged (mode);
}

void NeedleMeter::toggleMode()
{
    setMode (mode == Mode::output ? Mode::drive : Mode::output);
}

void NeedleMeter::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && ! e.mods.isPopupMenu())
        toggleMode();
}

bool NeedleMeter::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
    {
        toggleMode();
        return true;
    }

    return false;
}

std::unique_ptr<juce::AccessibilityHandler> NeedleMeter::createAccessibilityHandler()
{
    return std::make_unique<MeterAccessibilityHandler> (*this);
}

void NeedleMeter::tick (float dt, float level, bool peak)
{
    needle = ballistics.process (juce::jmax (0.0f, level), dt);

    if (peak)
    {
        peakHold = peakHoldSeconds;
        peakLamp = 1.0f;
    }
    else if (peakHold > 0.0f)
    {
        peakHold -= dt;
    }
    else
    {
        peakLamp = juce::jmax (0.0f, peakLamp - dt / 0.25f);
    }

    const auto needleMoved = std::abs (angleForPosition (needle) - angleForPosition (shownNeedle)) > 0.0012f;

    if (needleMoved || std::abs (peakLamp - shownPeak) > 0.01f)
    {
        shownNeedle = needle;
        shownPeak = peakLamp;
        repaint (faceArea().toNearestInt());
    }
}

void NeedleMeter::setNeedleImmediately (float level)
{
    ballistics.reset (level);
    needle = level;
    repaint();
}

//==============================================================================
juce::Rectangle<float> NeedleMeter::faceArea() const
{
    return getLocalBounds().toFloat().reduced (bezelWidth);
}

juce::Point<float> NeedleMeter::pivot() const
{
    const auto face = faceArea();
    return { face.getCentreX(), face.getBottom() + face.getHeight() * 0.30f };
}

float NeedleMeter::scaleRadius() const
{
    const auto face = faceArea();
    return pivot().y - face.getY() - 44.0f;
}

float NeedleMeter::angleForPosition (float linear) const noexcept
{
    const auto deflection = juce::jlimit (-0.02f, VuBallistics::pinMax / fullScale, linear / fullScale);
    return -halfSweep + 2.0f * halfSweep * deflection;
}

void NeedleMeter::resized()
{
    bezelLayer.invalidate();
    faceLayer.invalidate();
    glassLayer.invalidate();
}

void NeedleMeter::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palette();
    const auto face = faceArea();

    bezelLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderBezel (lg, a); });

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (face.toNearestInt());

        faceLayer.draw (g, face, [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderFace (lg, a); });

        // Peak lamp.
        const auto lampC = juce::Point<float> (face.getRight() - 20.0f, face.getY() + 17.0f);
        const auto lamp = juce::Rectangle<float> (9.0f, 9.0f).withCentre (lampC);
        g.setColour (juce::Colour (0x50000000));
        g.fillEllipse (lamp.expanded (1.2f));
        g.setColour (pal.lensOff.brighter (0.2f));
        g.fillEllipse (lamp);

        if (peakLamp > 0.01f)
        {
            drawing::fillGlow (g, lampC, 18.0f, pal.ember, 0.8f * peakLamp);
            g.setGradientFill (juce::ColourGradient (pal.emberCore.withAlpha (peakLamp), lampC,
                                                     pal.emberDeep.withAlpha (peakLamp), lampC.translated (4.0f, 0.0f), true));
            g.fillEllipse (lamp);
        }

        g.setColour (juce::Colour (0x90ffffff));
        g.fillEllipse (juce::Rectangle<float> (2.4f, 1.8f).withCentre (lampC.translated (-1.2f, -1.4f)));

        // Needle with its shadow on the face (light from the top-left, so the shadow falls right/down).
        const auto angle = angleForPosition (needle);
        const auto p = pivot();
        const auto tip = drawing::polar (p, scaleRadius() + 13.0f, angle);
        const auto tail = drawing::polar (p, face.getBottom() - p.y + 36.0f, angle);   // hidden under the hood

        g.setColour (juce::Colours::black.withAlpha (0.10f));
        g.drawLine ({ tail.translated (2.6f, 3.8f), tip.translated (2.6f, 3.8f) }, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.14f));
        g.drawLine ({ tail.translated (2.0f, 3.0f), tip.translated (2.0f, 3.0f) }, 1.4f);

        juce::Path needlePath;
        const auto dir = (tip - tail) / tip.getDistanceFrom (tail);
        const auto normal = juce::Point<float> (-dir.y, dir.x);
        needlePath.startNewSubPath (tail + normal * 1.1f);
        needlePath.lineTo (tip + normal * 0.45f);
        needlePath.lineTo (tip - normal * 0.45f);
        needlePath.lineTo (tail - normal * 1.1f);
        needlePath.closeSubPath();
        g.setColour (pal.needle);
        g.fillPath (needlePath);

        // The tip carries a thin ember vane, so it reads at a glance.
        g.setColour (pal.emberDeep);
        g.drawLine ({ drawing::polar (p, scaleRadius() - 6.0f, angle), tip }, 1.3f);

        glassLayer.draw (g, face, [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderGlass (lg, a); });
    }
}

void NeedleMeter::renderBezel (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& pal = Theme::palette();
    const auto outer = area.reduced (1.0f);

    juce::Path frame;
    frame.addRoundedRectangle (outer, 9.0f);
    drawing::drawContactShadow (g, frame, 4.0f);

    g.setGradientFill (juce::ColourGradient (pal.meterBezel.brighter (0.55f), outer.getX(), outer.getY(),
                                             pal.meterBezel, outer.getX() + outer.getWidth() * 0.4f, outer.getBottom(), false));
    g.fillPath (frame);

    juce::Path edge;
    edge.addRoundedRectangle (outer.reduced (0.5f), 8.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x70ffffff), outer.getX(), outer.getY(),
                                             juce::Colour (0x00ffffff), outer.getX() + 40.0f, outer.getY() + 40.0f, false));
    g.strokePath (edge, juce::PathStrokeType (1.0f));

    // Inner chamfer down to the glass: dark on the upper-left (facing away from the light).
    const auto opening = faceArea();
    juce::Path chamfer;
    chamfer.addRoundedRectangle (opening.expanded (2.5f), 5.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050505), opening.getX(), opening.getY(),
                                             juce::Colour (0xff4d3e34), opening.getRight(), opening.getBottom(), false));
    g.fillPath (chamfer);
}

void NeedleMeter::renderFace (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& pal = Theme::palette();
    const auto w = area.getWidth(), h = area.getHeight();

    // Backlit card: brightest in the middle where the lamp sits, warmer at the edges.
    juce::ColourGradient card (pal.meterFaceCentre, w * 0.5f, h * 0.72f, pal.meterFaceEdge, -w * 0.05f, -h * 0.1f, true);
    card.addColour (0.55, pal.meterFaceCentre.interpolatedWith (pal.meterFaceEdge, 0.35f));
    g.setGradientFill (card);
    g.fillRect (area);

    // Translate geometry into face-local coordinates.
    const auto offset = faceArea().getPosition();
    const auto p = pivot() - offset;
    const auto r = scaleRadius();

    // Red zone: a solid band from 0 to +3 VU.
    {
        const auto a0 = angleForPosition (1.0f);
        const auto a1 = angleForPosition (fullScale);
        juce::Path band;
        band.addCentredArc (p.x, p.y, r + 3.0f, r + 3.0f, 0.0f, a0, a1, true);
        g.setColour (pal.meterRed);
        g.strokePath (band, juce::PathStrokeType (6.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    }

    // Main arc.
    {
        juce::Path arc;
        arc.addCentredArc (p.x, p.y, r, r, 0.0f, angleForPosition (linearFromVu (-20.0f)), angleForPosition (fullScale), true);
        g.setColour (pal.meterInk);
        g.strokePath (arc, juce::PathStrokeType (1.1f));
    }

    const auto numberFont = drawing::tracked (fonts->legend (11.0f), 0.0f);
    g.setFont (numberFont);

    for (const auto& m : marks)
    {
        const auto a = angleForPosition (linearFromVu (m.vu));
        const auto red = m.vu > 0.0f;
        const auto len = m.major ? 9.0f : 5.0f;

        g.setColour (red ? pal.meterRed : pal.meterInk);
        g.drawLine ({ drawing::polar (p, r, a), drawing::polar (p, r + len, a) }, m.major ? 1.3f : 0.9f);

        if (m.text != nullptr)
        {
            const auto c = drawing::polar (p, r + 20.0f, a);
            g.drawText (m.text, juce::Rectangle<float> (26.0f, 14.0f).withCentre (c), juce::Justification::centred, false);
        }
    }

    // Lower percentage arc (voltage-linear, as on the real thing).
    {
        const auto r2 = r - 12.0f;
        juce::Path arc;
        arc.addCentredArc (p.x, p.y, r2, r2, 0.0f, angleForPosition (0.0f), angleForPosition (1.0f), true);
        g.setColour (pal.meterInk.withAlpha (0.55f));
        g.strokePath (arc, juce::PathStrokeType (0.8f));

        for (int i = 0; i <= 10; ++i)
        {
            const auto a = angleForPosition ((float) i / 10.0f);
            g.drawLine ({ drawing::polar (p, r2, a), drawing::polar (p, r2 - (i % 5 == 0 ? 5.0f : 3.0f), a) }, 0.8f);
        }
    }

    // End signs.
    g.setFont (fonts->legend (15.0f));
    g.setColour (pal.meterInk);
    g.drawText (juce::String::fromUTF8 ("\xe2\x88\x92"), juce::Rectangle<float> (16.0f, 16.0f).withCentre (drawing::polar (p, r + 22.0f, -halfSweep - 0.13f)),
                juce::Justification::centred, false);
    g.setColour (pal.meterRed);
    g.drawText ("+", juce::Rectangle<float> (16.0f, 16.0f).withCentre (drawing::polar (p, r + 22.0f, halfSweep + 0.12f)),
                juce::Justification::centred, false);

    // Legends.
    g.setColour (pal.meterInk);
    g.setFont (drawing::tracked (fonts->display (25.0f), 0.04f));
    g.drawText ("VU", juce::Rectangle<float> (0.0f, h * 0.53f, w, 26.0f), juce::Justification::centred, false);

    g.setColour (pal.meterInk.withAlpha (0.75f));
    g.setFont (drawing::tracked (fonts->legend (8.5f), 0.28f));
    g.drawText (mode == Mode::output ? "OUTPUT" : "DRIVE", juce::Rectangle<float> (0.0f, h * 0.53f + 25.0f, w, 12.0f),
                juce::Justification::centred, false);

    g.setFont (drawing::tracked (fonts->legend (7.0f), 0.2f));
    g.drawText ("PEAK", juce::Rectangle<float> (w - 54.0f, 12.0f, 28.0f, 10.0f), juce::Justification::centredRight, false);
}

void NeedleMeter::renderGlass (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& pal = Theme::palette();
    const auto w = area.getWidth(), h = area.getHeight();

    // Hood over the needle pivot.
    {
        const auto offset = faceArea().getPosition();
        const auto p = pivot() - offset;
        const auto hoodR = p.y - h + 22.0f;
        juce::Path hood;
        hood.addEllipse (juce::Rectangle<float> (hoodR * 2.0f * 1.9f, hoodR * 2.0f).withCentre (p));
        drawing::drawContactShadow (g, hood, 3.0f, 0.8f);

        g.setGradientFill (juce::ColourGradient (pal.meterBezel.brighter (0.6f), p.x - hoodR, p.y - hoodR,
                                                 pal.meterBezel, p.x + hoodR * 0.6f, p.y, false));
        g.fillPath (hood);
        g.setColour (juce::Colour (0x40ffffff));
        g.strokePath (hood, juce::PathStrokeType (0.8f));

        g.setColour (pal.legend.withAlpha (0.7f));
        g.setFont (drawing::tracked (fonts->legend (7.0f), 0.35f));
        g.drawText ("UMBER", juce::Rectangle<float> (0.0f, h - 14.0f, w, 10.0f), juce::Justification::centred, false);
    }

    // Recess shadow cast by the bezel onto the face.
    juce::Path facePath;
    facePath.addRectangle (area);
    melatonin::InnerShadow recess ({
        { juce::Colour (0xb0000000), 10, { 2, 4 }, 0 },
        { juce::Colour (0x60000000), 2, { 0, 1 }, 0 },
    });
    recess.render (g, facePath);

    // Glass reflection: a broad soft diagonal sheen from the top-left (no hard edges).
    juce::ColourGradient sheen (juce::Colour (0x34ffffff), 0.0f, 0.0f, juce::Colour (0x00ffffff), w * 0.42f, h * 0.62f, false);
    sheen.addColour (0.45, juce::Colour (0x12ffffff));
    g.setGradientFill (sheen);
    g.fillRect (area);

    // A thin bright edge line where the glass meets the bezel (bottom-right catches light).
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00ffffff), w * 0.5f, h * 0.5f,
                                             juce::Colour (0x30ffffff), w, h, false));
    g.drawRect (area, 1.0f);
}

} // namespace umber::ui
