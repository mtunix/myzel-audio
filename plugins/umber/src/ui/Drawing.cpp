#include "ui/Drawing.h"

#include <melatonin_blur/melatonin_blur.h>

namespace umber::ui::drawing
{

juce::Font tracked (juce::Font font, float tracking)
{
    return font.withExtraKerningFactor (tracking);
}

void drawLegend (juce::Graphics& g, const juce::String& text, const juce::Font& font,
                 juce::Rectangle<float> area, juce::Justification just, juce::Colour colour)
{
    const auto& pal = Theme::palette();
    g.setFont (font);

    g.setColour (pal.legendShadow.withMultipliedAlpha (0.55f));
    g.drawText (text, area.translated (0.0f, -0.6f), just, false);
    g.setColour (pal.legendLight);
    g.drawText (text, area.translated (0.0f, 0.8f), just, false);
    g.setColour (colour);
    g.drawText (text, area, just, false);
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float slotAngle)
{
    const auto head = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    // Countersink: the panel is cut away, so the upper-left rim is in shadow.
    const auto sink = head.expanded (radius * 0.28f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x90000000), sink.getTopLeft(),
                                             juce::Colour (0x30ffffff), sink.getBottomRight(), false));
    g.fillEllipse (sink);

    // Domed head.
    juce::ColourGradient dome (juce::Colour (0xffd6d4cf), head.getX() + radius * 0.6f, head.getY() + radius * 0.5f,
                               juce::Colour (0xff3b3b3d), head.getRight(), head.getBottom(), true);
    dome.addColour (0.45, juce::Colour (0xff8b8a87));
    g.setGradientFill (dome);
    g.fillEllipse (head);

    // Slot, cut into the head: dark wall plus a lit lower edge.
    const auto d = juce::Point<float> (std::cos (slotAngle), std::sin (slotAngle)) * (radius * 0.82f);
    juce::Line<float> slot (centre - d, centre + d);
    g.setColour (juce::Colour (0x50ffffff));
    g.drawLine (juce::Line<float> (slot.getStart().translated (0.3f, 0.5f), slot.getEnd().translated (0.3f, 0.5f)), radius * 0.30f);
    g.setColour (juce::Colour (0xe0141414));
    g.drawLine (juce::Line<float> (slot.getStart().translated (-0.25f, -0.4f), slot.getEnd().translated (-0.25f, -0.4f)),
                radius * 0.30f);

    g.setColour (juce::Colour (0x60000000));
    g.drawEllipse (head, 0.6f);
}

void drawRecess (juce::Graphics& g, juce::Rectangle<float> area, float corner, float depth)
{
    juce::Path well;
    well.addRoundedRectangle (area, corner);

    g.setColour (juce::Colour (0x55000000));
    g.fillPath (well);

    melatonin::InnerShadow inner (juce::Colour (0xd0000000), juce::roundToInt (depth * 2.2f),
                                  { juce::roundToInt (depth * metrics::shadowDx), juce::roundToInt (depth * metrics::shadowDy) });
    inner.render (g, well);

    // The lit lower lip.
    juce::Path lip;
    lip.addRoundedRectangle (area.expanded (0.6f), corner + 0.6f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00ffffff), area.getX(), area.getCentreY(),
                                             juce::Colour (0x2effffff), area.getRight(), area.getBottom(), false));
    g.strokePath (lip, juce::PathStrokeType (1.0f));
}

void drawContactShadow (juce::Graphics& g, const juce::Path& shape, float elevation, float strength)
{
    melatonin::DropShadow shadow ({
        { juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, 0.55f * strength), juce::roundToInt (elevation * 2.4f),
          { juce::roundToInt (elevation * metrics::shadowDx), juce::roundToInt (elevation * metrics::shadowDy) } },
        { juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, 0.45f * strength), juce::roundToInt (elevation * 0.6f),
          { juce::roundToInt (elevation * 0.2f), juce::roundToInt (elevation * 0.4f) } },
    });
    shadow.render (g, shape);
}

void addGrain (juce::Image& image, float amount, float streak, juce::uint32 seed)
{
    juce::Random rng ((juce::int64) seed);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);

    const auto width = data.width;
    float rowBias = 0.0f;

    for (int y = 0; y < data.height; ++y)
    {
        // Brushing: a slowly wandering per-row offset gives long horizontal streaks.
        rowBias = rowBias * 0.55f + (rng.nextFloat() - 0.5f) * 0.9f;
        const auto rowFreq = 0.002f + rng.nextFloat() * 0.01f;
        const auto rowPhase = rng.nextFloat() * juce::MathConstants<float>::twoPi;

        auto* line = data.getLinePointer (y);

        for (int x = 0; x < width; ++x)
        {
            const auto streakValue = streak * (rowBias + 0.35f * std::sin ((float) x * rowFreq + rowPhase));
            const auto grain = amount * (rng.nextFloat() - 0.5f);
            const auto delta = (int) std::lround ((streakValue + grain) * 255.0f);

            auto* px = reinterpret_cast<juce::PixelARGB*> (line + x * data.pixelStride);
            const auto a = px->getAlpha();

            if (a == 0)
                continue;

            // Premultiplied: keep each channel <= alpha.
            const auto clampTo = [a] (int v) { return (juce::uint8) juce::jlimit (0, (int) a, v); };
            px->setARGB (a,
                         clampTo ((int) px->getRed() + delta),
                         clampTo ((int) px->getGreen() + delta),
                         clampTo ((int) px->getBlue() + delta));
        }
    }
}

void fillGlow (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float intensity)
{
    if (intensity <= 0.001f || radius <= 0.0f)
        return;

    juce::ColourGradient grad (colour.withMultipliedAlpha (intensity), centre,
                               colour.withAlpha (0.0f), centre.translated (radius, 0.0f), true);
    grad.addColour (0.25, colour.withMultipliedAlpha (intensity * 0.55f));
    grad.addColour (0.55, colour.withMultipliedAlpha (intensity * 0.18f));
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
}

} // namespace umber::ui::drawing
