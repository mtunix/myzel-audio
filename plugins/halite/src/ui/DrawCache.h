#pragma once

#include "ui/Theme.h"

#include <map>
#include <vector>
#include <tuple>

namespace halite::ui
{

/**
    Value chips (a rounded fill with text on it: the threshold's, the ess tags, round at corner = height / 2) rendered
    once per text, colours and display scale, then drawn as images: text is the costliest thing a frame draws.
*/
class ChipCache
{
public:
    void draw (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, juce::Colour fill, juce::Colour ink,
               const juce::Font&, float corner = 4.0f);

    /** The text's width in the font, measured once. */
    float widthOf (const juce::String& text, const juce::Font&);

private:
    using Key = std::tuple<juce::String, juce::uint32, juce::uint32, int, int, int>;   // text, fill, ink; size, corner and font in 1/100 px

    /** A chip, and for an opaque fill the same as opaque pieces around its blended corners (a round chip's two ends). */
    struct Entry
    {
        struct Piece
        {
            juce::Image image;
            juce::Point<int> at;
        };

        juce::Image image;
        std::vector<Piece> pieces;
    };

    std::map<Key, Entry> images;
    std::map<std::pair<juce::String, int>, float> widths;
    float scale = 0.0f;
};

/**
    The display's large opaque images (src/ui/DESIGN.md, Rendering). JUCE's Linux window paints into an ARGB
    image, and copying an opaque ARGB image into it with the source blend mode takes a quarter of the cycles of
    converting an RGB one pixel by pixel. At a fractional scale a component's edge pixels are partly its
    neighbour's, and a copy would leave them half transparent, so there the image stays RGB and is blended.
    Other renderers (Direct2D, CoreGraphics) blend the ARGB image too.
*/
namespace opaque
{
    /** Whether images at this physical scale are ARGB, copied in the software renderer: only at whole scales. */
    inline bool copies (float scale) noexcept { return juce::approximatelyEqual (scale, std::round (scale)); }

    /** An image to be painted opaque all over before it is drawn (it is not cleared). */
    inline juce::Image make (int width, int height, float scale)
    {
        return { copies (scale) ? juce::Image::ARGB : juce::Image::RGB, std::max (1, width), std::max (1, height), false };
    }

    /** Draws an image from `make` (or a clipped part of one) with its top left on a whole physical pixel. */
    void draw (juce::Graphics&, const juce::Image&, int x, int y, float scale);
}

/** A dashed horizontal line (6 on, 4 off) rendered once per width, thickness, colour and sub-pixel offset. */
class DashCache
{
public:
    void draw (juce::Graphics&, float left, float right, float y, float thickness, juce::Colour);

private:
    juce::Image image;
    std::tuple<float, float, float, juce::uint32, float, float> key {};
};

} // namespace halite::ui
