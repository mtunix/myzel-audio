#pragma once

#include <juce_graphics/juce_graphics.h>

namespace halite::ui::raster
{

/**
    Column drawing straight into an opaque image's pixels, for the display's images: a scrolling
    waveform or a spectrum is a few vertical spans per pixel column, and drawing them through
    Graphics costs many times more than the pixels themselves (src/ui/DESIGN.md, Rendering).
    The image is RGB or opaque ARGB (`opaque::make` in DrawCache.h).
*/

namespace detail
{
    template <typename Pixel>
    Pixel* pixel (const juce::Image::BitmapData& bits, int x, int y) noexcept
    {
        return reinterpret_cast<Pixel*> (bits.getPixelPointer (x, y));
    }

    /** Calls `fn` with a pixel of the image's type. */
    template <typename Fn>
    void withPixelType (const juce::Image::BitmapData& bits, Fn&& fn)
    {
        if (bits.pixelFormat == juce::Image::ARGB)
            fn (juce::PixelARGB {});
        else
            fn (juce::PixelRGB {});
    }
}

/** One opaque colour over a whole column. */
inline void fillColumn (const juce::Image::BitmapData& bits, int x, juce::PixelARGB colour) noexcept
{
    detail::withPixelType (bits, [&] (auto p)
    {
        using Pixel = decltype (p);
        for (int y = 0; y < bits.height; ++y)
            detail::pixel<Pixel> (bits, x, y)->set (colour);
    });
}

/** A colour over a column from y0 to y1 (physical px); the end pixels take it in proportion to how much it covers them. */
inline void blendSpan (const juce::Image::BitmapData& bits, int x, float y0, float y1, juce::PixelARGB colour) noexcept
{
    y0 = std::max (y0, 0.0f);
    y1 = std::min (y1, (float) bits.height);
    detail::withPixelType (bits, [&] (auto p)
    {
        using Pixel = decltype (p);
        for (auto y = (int) y0; (float) y < y1; ++y)
        {
            const auto cover = std::min (y1, (float) (y + 1)) - std::max (y0, (float) y);
            detail::pixel<Pixel> (bits, x, y)->blend (colour, (juce::uint32) juce::roundToInt (cover * 255.0f));
        }
    });
}

/** As blendSpan, fading to nothing from `fadeTop` down to `fadeBottom` (a line that thins out towards an axis' floor). */
inline void blendSpanFading (const juce::Image::BitmapData& bits, int x, float y0, float y1, juce::PixelARGB colour,
                             float fadeTop, float fadeBottom) noexcept
{
    y0 = std::max (y0, 0.0f);
    y1 = std::min (y1, (float) bits.height);
    const auto fadeLength = std::max (1.0f, fadeBottom - fadeTop);
    detail::withPixelType (bits, [&] (auto p)
    {
        using Pixel = decltype (p);
        for (auto y = (int) y0; (float) y < y1; ++y)
        {
            const auto cover = std::min (y1, (float) (y + 1)) - std::max (y0, (float) y);
            const auto strength = juce::jlimit (0.0f, 1.0f, (fadeBottom - ((float) y + 0.5f)) / fadeLength);
            detail::pixel<Pixel> (bits, x, y)->blend (colour, (juce::uint32) juce::roundToInt (cover * strength * 255.0f));
        }
    });
}

} // namespace halite::ui::raster
