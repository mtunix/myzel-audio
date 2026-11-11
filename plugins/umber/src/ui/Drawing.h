#pragma once

#include "ui/Theme.h"

namespace umber::ui::drawing
{

/** Physical pixels per logical unit of the graphics context (editor scale x display scale). */
inline float physicalScale (juce::Graphics& g)
{
    return juce::jmax (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
}

/**
    An image cache for static artwork, rendered at the physical pixel scale so it stays crisp
    at every editor scale and display density. It re-renders only when the size or scale changes
    (or after invalidate()).
*/
class CachedLayer
{
public:
    /** Draws the cached image (rendering it first if needed). Images are drawn with the
        context's current opacity, so it is set explicitly here. */
    template <typename RenderFn>
    void draw (juce::Graphics& g, juce::Rectangle<float> area, RenderFn&& render, float opacity = 1.0f)
    {
        const auto scale = physicalScale (g);

        if (! image.isValid() || std::abs (scale - imageScale) > 0.001f || std::abs (area.getWidth() - size.x) > 0.001f || std::abs (area.getHeight() - size.y) > 0.001f)
        {
            imageScale = scale;
            size = { area.getWidth(), area.getHeight() };
            image = juce::Image (juce::Image::ARGB,
                                 juce::jmax (1, (int) std::ceil (size.x * scale)),
                                 juce::jmax (1, (int) std::ceil (size.y * scale)),
                                 true);
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale (scale));
            render (ig, juce::Rectangle<float> { 0.0f, 0.0f, size.x, size.y }, scale);
        }

        g.setOpacity (opacity);
        g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / imageScale)
                                           .translated (area.getX(), area.getY()));
    }

    void invalidate() noexcept     { image = {}; }
    bool isValid() const noexcept  { return image.isValid(); }

private:
    juce::Image image;
    float imageScale = 0.0f;
    juce::Point<float> size;
};

/** Text with extra letter spacing. */
juce::Font tracked (juce::Font font, float tracking);

/** Printed legend with a faint light edge below (the ink sits on a top-left-lit surface). */
void drawLegend (juce::Graphics& g, const juce::String& text, const juce::Font& font,
                 juce::Rectangle<float> area, juce::Justification just, juce::Colour colour);

/** A slotted panel screw lit from the top-left. */
void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float slotAngle);

/** A well cut into the panel: shadowed upper-left wall, lit lower-right lip. */
void drawRecess (juce::Graphics& g, juce::Rectangle<float> area, float corner, float depth);

/** Soft contact shadow below a raised shape (cast towards the lower right). */
void drawContactShadow (juce::Graphics& g, const juce::Path& shape, float elevation, float strength = 1.0f);

/** Adds monochrome grain (and horizontal brushing when streak > 0) to an ARGB image in place. */
void addGrain (juce::Image& image, float amount, float streak, juce::uint32 seed);

/** Generic warm radial glow (additive-looking) used for lamps and the ember bloom. */
void fillGlow (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float intensity);

/** Maps a value 0..1 to the knob sweep angle (radians, 0 = 12 o'clock, clockwise). */
inline float knobAngle (float proportion) noexcept
{
    return metrics::knobStartAngle + proportion * (metrics::knobEndAngle - metrics::knobStartAngle);
}

/** Point on a circle using knob angles (0 = 12 o'clock, clockwise). */
inline juce::Point<float> polar (juce::Point<float> centre, float radius, float angle) noexcept
{
    return { centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle) };
}

} // namespace umber::ui::drawing
