#include "ui/DrawCache.h"

namespace halite::ui
{

void ChipCache::draw (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour fill, juce::Colour ink,
                      const juce::Font& font, float corner)
{
    const auto s = std::max (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! juce::approximatelyEqual (s, scale) || images.size() > 96)
    {
        images.clear();
        scale = s;
    }

    auto hundredths = [] (float v) { return juce::roundToInt (v * 100.0f); };
    auto& entry = images[Key (text, fill.getARGB(), ink.getARGB(), hundredths (area.getWidth()) * 100000 + hundredths (area.getHeight()), hundredths (corner),
                              hundredths (font.getHeight()))];

    if (! entry.image.isValid())
    {
        const auto w = juce::roundToInt (area.getWidth() * s), h = juce::roundToInt (area.getHeight() * s);
        entry.image = juce::Image (juce::Image::ARGB, w, h, true);
        {
            // The shape fills the image exactly, so a round end is whole however the logical width rounds.
            juce::Graphics ig (entry.image);
            ig.addTransform (juce::AffineTransform::scale (s));
            const auto local = juce::Rectangle<float> ((float) w / s, (float) h / s);
            ig.setColour (fill);
            ig.fillRoundedRectangle (local, std::min (corner, 0.5f * local.getHeight()));
            ig.setColour (ink);
            ig.setFont (font);
            ig.drawText (text, local, juce::Justification::centred, false);
        }

        // Blending every pixel of an ARGB chip took a tenth of a Timeline repaint; an opaque chip is copied
        // instead, and only its corners are blended. A round chip's corners meet: its two ends are blended whole.
        const auto c = (int) std::ceil (corner * s);
        if (fill.isOpaque() && c > 0 && w > 2 * c)
        {
            juce::Image opaque (juce::Image::RGB, w, h, false);
            {
                juce::Graphics og (opaque);
                og.fillAll (fill);
                og.drawImageAt (entry.image, 0, 0);
            }
            auto add = [&] (const juce::Image& from, juce::Rectangle<int> r)
            {
                if (! r.isEmpty())
                    entry.pieces.push_back ({ from.getClippedImage (r), r.getPosition() });
            };

            if (h > 2 * c)
            {
                add (opaque, { 0, c, w, h - 2 * c });
                add (opaque, { c, 0, w - 2 * c, c });
                add (opaque, { c, h - c, w - 2 * c, c });
                for (const auto at : { juce::Point<int> (0, 0), { w - c, 0 }, { 0, h - c }, { w - c, h - c } })
                    add (entry.image, { at.x, at.y, c, c });
            }
            else
            {
                add (opaque, { c, 0, w - 2 * c, h });
                add (entry.image, { 0, 0, c, h });
                add (entry.image, { w - c, 0, c, h });
            }
        }
    }

    // On whole physical pixels: anything else takes the renderer's resampling path, many times slower.
    const auto x = std::round (area.getX() * s), y = std::round (area.getY() * s);
    if (entry.pieces.empty())
    {
        g.drawImageTransformed (entry.image, juce::AffineTransform::translation (x, y).scaled (1.0f / s));
        return;
    }
    for (const auto& piece : entry.pieces)
        g.drawImageTransformed (piece.image, juce::AffineTransform::translation (x + (float) piece.at.x, y + (float) piece.at.y).scaled (1.0f / s));
}

float ChipCache::widthOf (const juce::String& text, const juce::Font& font)
{
    if (widths.size() > 256)
        widths.clear();

    const auto key = std::pair (text, juce::roundToInt (font.getHeight() * 100.0f));
    if (const auto it = widths.find (key); it != widths.end())
        return it->second;
    return widths[key] = textWidth (font, text);
}

void opaque::draw (juce::Graphics& g, const juce::Image& image, int x, int y, float scale)
{
    // Only the part the clip can show: the renderer builds an edge table over the whole image and cuts the clip's
    // complement out of it row by row, which for a large image under a small clip costs more than the copy.
    const auto shown = (g.getClipBounds().toFloat() * scale).getSmallestIntegerContainer().translated (-x, -y)
                           .getIntersection (image.getBounds());
    if (shown.isEmpty())
        return;

    const auto part = shown == image.getBounds() ? image : image.getClippedImage (shown);
    const auto transform = juce::AffineTransform::translation ((float) (x + shown.getX()), (float) (y + shown.getY())).scaled (1.0f / scale);

    // Copied only in JUCE's software renderer, where it was measured: Direct2D's source copy clears the rest of the
    // clip to transparent, and CoreGraphics was never tried. Blended, an opaque image gives the same pixels.
    if (image.getFormat() != juce::Image::ARGB || dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) == nullptr)
    {
        g.drawImageTransformed (part, transform);
        return;
    }

    // The source mode copies rows; the renderer takes the image's opacity from the fill's.
    juce::Graphics::ScopedSaveState state (g);
    g.setOpacity (1.0f);
    g.setImageBlendMode (juce::BlendMode::source);
    g.drawImageTransformed (part, transform);
}

void DashCache::draw (juce::Graphics& g, float left, float right, float y, float thickness, juce::Colour colour)
{
    const auto s = std::max (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto centre = y * s;
    const auto row = std::floor (centre) - 2.0f;
    const auto offset = centre - row;   // where the line's centre falls inside the strip, in physical px
    const auto k = std::tuple (left, right, thickness, colour.getARGB(), s, std::round (offset * 1000.0f));

    if (k != key || ! image.isValid())
    {
        image = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt ((right - left) * s)), 5, true);
        juce::Graphics ig (image);
        ig.setColour (colour);
        for (auto x = 0.0f; x < right - left; x += 10.0f)
            ig.fillRect (juce::Rectangle<float> (x * s, offset - thickness * s * 0.5f, std::min (6.0f, right - left - x) * s, thickness * s));
        key = k;
    }

    g.drawImageTransformed (image, juce::AffineTransform::translation (std::round (left * s), row).scaled (1.0f / s));
}

} // namespace halite::ui
