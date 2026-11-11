#include "ui/PanelArtwork.h"

namespace cerulean::ui
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
            juce::ColourGradient falloff (pal.panelGrain.withAlpha (0.10f), local.getX(), local.getY(),
                                          pal.panelGrain.withAlpha (0.0f), local.getWidth() * 0.65f, local.getHeight() * 0.9f, true);
            sg.setGradientFill (falloff);
            sg.fillRect (local);
        }

        // Fine matte powder-coat texture: grain only, no brushing.
        drawing::addGrain (ground, 0.026f, 0.0f, 0xc1c0);
        g.drawImageTransformed (ground, juce::AffineTransform::scale (1.0f / scale).translated (area.getX(), area.getY()));
    }

    void renderVignette (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto& pal = Theme::palette();
        juce::ColourGradient v (pal.panelVignette.withAlpha (0.0f), area.getCentreX(), area.getCentreY() - 20.0f,
                                pal.panelVignette.withAlpha (0.16f), area.getX() - 30.0f, area.getBottom() + 40.0f, true);
        v.addColour (0.6, pal.panelVignette.withAlpha (0.03f));
        g.setGradientFill (v);
        g.fillRect (area);
    }

    void renderGroove (juce::Graphics& g, juce::Line<float> line)
    {
        drawing::drawGroove (g, line);
    }
}

void renderKnobScale (juce::Graphics& g, juce::Point<float> c, const KnobStyle& style, const Fonts& fonts)
{
    const auto& pal = Theme::palette();
    const auto r = style.diameter * 0.5f;
    const auto tickInner = r + (style.hero ? 8.0f : 6.0f);

    auto drawTick = [&] (float t, bool major)
    {
        const auto a = drawing::knobAngle (t);
        const auto len = style.hero ? (major ? 8.0f : 4.5f) : 4.5f;
        const auto p0 = drawing::polar (c, tickInner, a);
        const auto p1 = drawing::polar (c, tickInner + len, a);

        g.setColour (pal.legendLight);
        g.drawLine ({ p0.translated (0.3f, 0.8f), p1.translated (0.3f, 0.8f) }, major ? 1.4f : 1.0f);
        g.setColour (major ? pal.tickMajor : pal.tickMinor);
        g.drawLine ({ p0, p1 }, major ? (style.hero ? 1.6f : 1.3f) : 1.0f);
    };

    if (! style.tickPositions.empty())
    {
        // Explicit ticks (e.g. a log scale): major where a mark is printed, minor elsewhere.
        for (const auto t : style.tickPositions)
        {
            auto major = ! style.hero;

            for (const auto& m : style.marks)
                major = major || std::abs (m.position - t) < 1.0e-3f;

            drawTick (t, major);
        }
    }
    else
    {
        // Evenly spaced ticks (major ticks every other one on the hero scale).
        for (int i = 0; i < style.ticks; ++i)
        {
            const auto t = style.ticks > 1 ? (float) i / (float) (style.ticks - 1) : 0.0f;
            drawTick (t, ! style.hero || (i % 2 == 0));
        }
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

        if (! style.hero && std::cos (a) < -0.5f)
        {
            // End labels below the knob sit centred under their tick, so neighbouring knobs'
            // labels never run into each other.
            box = juce::Rectangle<float> (w, h).withCentre (drawing::polar (c, textRadius + 5.0f, a));
        }
        else if (style.hero || std::abs (s) < 0.35f)
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
    if (style.title.isNotEmpty())
    {
        const auto titleFont = drawing::tracked (fonts.legend (style.hero ? 14.0f : metrics::legendSize),
                                                 style.hero ? 0.30f : metrics::legendTracking);
        const auto titleY = c.y + Knob::defaultLegendOffset (style);
        auto titleX = c.x;

        if (style.badge.isNotEmpty())
        {
            // A boxed letter before the title (Param A / B), the pair centred under the knob.
            constexpr float box = 13.0f, gap = 6.0f;
            const auto tw = juce::GlyphArrangement::getStringWidth (titleFont, style.title);
            titleX = c.x + (box + gap) * 0.5f;
            const auto badge = juce::Rectangle<float> (box, box).withCentre ({ titleX - tw * 0.5f - gap - box * 0.5f, titleY });
            g.setColour (pal.legendLight);
            g.drawRoundedRectangle (badge.translated (0.0f, 0.8f), 2.5f, 1.0f);
            g.setColour (pal.legendDim);
            g.drawRoundedRectangle (badge, 2.5f, 1.0f);
            g.setFont (fonts.legend (9.0f));
            g.drawText (style.badge, badge.translated (0.2f, 0.0f), juce::Justification::centred, false);
        }

        drawing::drawLegend (g, style.title, titleFont, juce::Rectangle<float> (180.0f, 20.0f).withCentre ({ titleX, titleY }),
                             juce::Justification::centred, pal.legend);
    }
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
    renderGroove (g, { layout::leftGrooveX, layout::grooveTop, layout::leftGrooveX, layout::grooveBottom });
    renderGroove (g, { layout::rightGrooveX, layout::grooveTop, layout::rightGrooveX, layout::grooveBottom });

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

} // namespace cerulean::ui
