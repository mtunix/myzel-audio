#include "ui/PanelArtwork.h"

namespace umber::ui
{

namespace
{
    void renderGround (juce::Graphics& g, juce::Rectangle<float> area, float scale)
    {
        const auto& pal = Theme::palette();

        // Render the ground into a physical-resolution image so the grain is per device pixel.
        juce::Image ground (juce::Image::ARGB,
                            juce::jmax (1, (int) std::ceil (area.getWidth() * scale)),
                            juce::jmax (1, (int) std::ceil (area.getHeight() * scale)), true);
        {
            juce::Graphics sg (ground);
            sg.addTransform (juce::AffineTransform::scale (scale));
            const auto local = area.withZeroOrigin();

            juce::ColourGradient fill (pal.panelTop, 0.0f, local.getY(), pal.panelBottom, 0.0f, local.getBottom(), false);
            fill.addColour (0.35, pal.panelTop.interpolatedWith (pal.panelBottom, 0.3f));
            sg.setGradientFill (fill);
            sg.fillRect (local);

            // Broad light falloff from the top-left light.
            juce::ColourGradient falloff (pal.panelGrain.withAlpha (0.035f), local.getX(), local.getY(),
                                          pal.panelGrain.withAlpha (0.0f), local.getWidth() * 0.65f, local.getHeight() * 0.9f, true);
            sg.setGradientFill (falloff);
            sg.fillRect (local);
        }

        // Fine matte powder-coat texture: grain only, no brushing.
        drawing::addGrain (ground, 0.022f, 0.0f, 0x5c0c);
        g.drawImageTransformed (ground, juce::AffineTransform::scale (1.0f / scale).translated (area.getX(), area.getY()));
    }

    void renderVignette (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto& pal = Theme::palette();
        juce::ColourGradient v (pal.panelVignette.withAlpha (0.0f), area.getCentreX(), area.getCentreY() - 20.0f,
                                pal.panelVignette.withAlpha (0.22f), area.getX() - 30.0f, area.getBottom() + 40.0f, true);
        v.addColour (0.6, pal.panelVignette.withAlpha (0.05f));
        g.setGradientFill (v);
        g.fillRect (area);
    }

    void renderGroove (juce::Graphics& g, juce::Line<float> line)
    {
        g.setColour (juce::Colour (0x80000000));
        g.drawLine (line, 1.0f);
        g.setColour (Theme::palette().legendLight.withMultipliedAlpha (2.2f));
        g.drawLine (line.getStartX() + 1.0f, line.getStartY() + 1.0f, line.getEndX() + 1.0f, line.getEndY() + 1.0f, 1.0f);
    }
}

void renderKnobScale (juce::Graphics& g, juce::Point<float> c, const KnobStyle& style, const Fonts& fonts)
{
    const auto& pal = Theme::palette();
    const auto r = style.diameter * 0.5f;
    const auto tickInner = r + (style.hero ? 8.0f : 6.0f);

    // Evenly spaced ticks (major ticks every other one on the hero scale).
    for (int i = 0; i < style.ticks; ++i)
    {
        const auto t = style.ticks > 1 ? (float) i / (float) (style.ticks - 1) : 0.0f;
        const auto a = drawing::knobAngle (t);
        const auto major = ! style.hero || (i % 2 == 0);
        const auto len = style.hero ? (major ? 8.0f : 4.5f) : 4.5f;
        const auto p0 = drawing::polar (c, tickInner, a);
        const auto p1 = drawing::polar (c, tickInner + len, a);

        g.setColour (juce::Colour (0x60000000));
        g.drawLine ({ p0.translated (0.0f, -0.6f), p1.translated (0.0f, -0.6f) }, major ? 1.6f : 1.2f);
        g.setColour (major ? pal.tickMajor : pal.tickMinor);
        g.drawLine ({ p0, p1 }, major ? (style.hero ? 1.6f : 1.3f) : 1.0f);
    }

    // Labelled positions.
    const auto numberFont = style.hero ? drawing::tracked (fonts.legend (12.5f), 0.04f)
                                       : drawing::tracked (fonts.legend (metrics::scaleNumberSize), 0.06f);
    const auto textRadius = tickInner + (style.hero ? 20.0f : 13.0f);

    for (const auto& m : style.marks)
    {
        const auto a = drawing::knobAngle (m.position);
        const auto p = drawing::polar (c, textRadius, a);
        const auto s = std::sin (a);
        const auto w = juce::GlyphArrangement::getStringWidth (numberFont, m.text) + 2.0f;
        const auto h = numberFont.getHeight() + 2.0f;

        juce::Rectangle<float> box;
        auto just = juce::Justification::centred;

        if (style.hero || std::abs (s) < 0.35f)
            box = juce::Rectangle<float> (w, h).withCentre (p);
        else if (s < 0.0f)
        {
            box = { p.x - w + w * 0.18f, p.y - h * 0.5f, w, h };
            just = juce::Justification::centredRight;
        }
        else
        {
            box = { p.x - w * 0.18f, p.y - h * 0.5f, w, h };
            just = juce::Justification::centredLeft;
        }

        drawing::drawLegend (g, m.text, numberFont, box, just, m.major ? pal.legend : pal.legendDim);
    }

    // Title.
    const auto titleFont = drawing::tracked (fonts.legend (style.hero ? 14.0f : metrics::legendSize),
                                             style.hero ? 0.30f : metrics::legendTracking);
    const auto titleY = c.y + r + (style.hero ? 38.0f : 22.0f);
    drawing::drawLegend (g, style.title, titleFont, juce::Rectangle<float> (180.0f, 20.0f).withCentre ({ c.x, titleY }),
                         juce::Justification::centred, pal.legend);
}

void renderPanelArtwork (juce::Graphics& g, juce::Rectangle<float> area, const PanelArtworkSpec& spec,
                         const Fonts& fonts, float physicalScale)
{
    const auto& pal = Theme::palette();

    renderGround (g, area, physicalScale);
    renderVignette (g, area);

    // Top edge: the panel meets the header rail.
    g.setColour (pal.panelEdgeLight);
    g.drawHorizontalLine ((int) area.getY(), area.getX(), area.getRight());

    // Column grooves.
    renderGroove (g, { layout::leftGrooveX, area.getY() + 30.0f, layout::leftGrooveX, 440.0f });
    renderGroove (g, { layout::rightGrooveX, area.getY() + 30.0f, layout::rightGrooveX, 440.0f });

    for (const auto& well : spec.wells)
        drawing::drawRecess (g, well, 7.0f, 3.0f);

    for (const auto& k : spec.knobs)
        renderKnobScale (g, k.centre, k.style, fonts);

    // Section legends, flanked by short engraved rules.
    for (const auto& l : spec.legends)
    {
        const auto font = drawing::tracked (fonts.legend (9.5f), 0.36f);
        drawing::drawLegend (g, l.text, font, l.area, juce::Justification::centred, pal.legendDim);

        const auto half = juce::GlyphArrangement::getStringWidth (font, l.text) * 0.5f + 12.0f;
        const auto y = l.area.getCentreY();
        const auto cx = l.area.getCentreX();
        renderGroove (g, { cx - half - 46.0f, y, cx - half, y });
        renderGroove (g, { cx + half, y, cx + half + 46.0f, y });
    }

    // Screws.
    const auto inset = 15.0f;
    drawing::drawScrew (g, { area.getX() + inset, area.getY() + inset }, 5.0f, 0.6f);
    drawing::drawScrew (g, { area.getRight() - inset, area.getY() + inset }, 5.0f, 2.2f);
    drawing::drawScrew (g, { area.getX() + inset, area.getBottom() - inset }, 5.0f, 1.3f);
    drawing::drawScrew (g, { area.getRight() - inset, area.getBottom() - inset }, 5.0f, -0.4f);
}

} // namespace umber::ui
