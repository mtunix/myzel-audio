#include "ui/Theme.h"

#include <UmberAssets.h>

namespace umber::ui
{

namespace
{
    using C = juce::Colour;

    Palette makeStudio()
    {
        Palette p;

        // Matte dark umber powder coat: a deep earth brown, barely lit from the top-left.
        p.panelTop        = C (0xff3b2a1f);
        p.panelBottom     = C (0xff2e2018);
        p.panelGrain      = C (0xffffffff);
        p.panelVignette   = C (0xff0c0705);
        p.panelEdgeLight  = C (0x26fff0dc);
        p.panelEdgeDark   = C (0xa0000000);

        // Warm cream silkscreen.
        p.legend          = C (0xffeee3d3);
        p.legendDim       = C (0xffad9a85);
        p.legendShadow    = C (0x80000000);
        p.legendLight     = C (0x0affffff);
        p.rule            = C (0x24eee3d3);

        p.headerTop       = C (0xff21170f);
        p.headerBottom    = C (0xff1b130d);
        p.headerEdge      = C (0xff47362b);
        p.displayGlass    = C (0xff120c08);
        p.displayText     = C (0xfff1e8db);
        p.displayTextDim  = C (0xff8a7a6a);
        p.keyTop          = C (0xff42312a);
        p.keyBottom       = C (0xff33251e);
        p.keyText         = C (0xffd6c9b8);
        p.keyTextActive   = C (0xfff5ece0);

        p.skirtLight      = C (0xff3d322b);
        p.skirtDark       = C (0xff110c09);
        p.capLight        = C (0xffd8d3cc);
        p.capDark         = C (0xff6f6760);
        p.pointer         = C (0xfff5efe6);
        p.tickMajor       = C (0xffe3d8c9);
        p.tickMinor       = C (0x80e3d8c9);

        // The only accent: amber, used for lamps and active states.
        p.emberCore       = C (0xffffe2b8);
        p.ember           = C (0xffffa040);
        p.emberDeep       = C (0xffc8601c);
        p.lensOff         = C (0xff2a1a10);
        p.lensOffRim      = C (0xff0f0a07);

        p.meterFaceCentre = C (0xfff4ead2);
        p.meterFaceEdge   = C (0xffe2d1ab);
        p.meterInk        = C (0xff26211c);
        p.meterRed        = C (0xffb5341a);
        p.needle          = C (0xff1a1714);
        p.meterBezel      = C (0xff1a120d);

        p.focus           = C (0xffffa040);
        p.bubbleFill      = C (0xf01a120d);
        p.bubbleText      = C (0xfff5efe6);
        p.bubbleEdge      = C (0x40ffffff);
        p.overlayDim      = C (0x9a0a0604);
        return p;
    }
}

const Palette& Theme::palette() noexcept
{
    static const Palette studio = makeStudio();
    return studio;
}

//==============================================================================
Fonts::Fonts()
    : medium   (juce::Typeface::createSystemTypefaceFor (UmberAssets::InterMedium_otf,
                                                         (size_t) UmberAssets::InterMedium_otfSize)),
      semiBold (juce::Typeface::createSystemTypefaceFor (UmberAssets::InterSemiBold_otf,
                                                         (size_t) UmberAssets::InterSemiBold_otfSize)),
      black    (juce::Typeface::createSystemTypefaceFor (UmberAssets::InterDisplayBlack_otf,
                                                         (size_t) UmberAssets::InterDisplayBlack_otfSize))
{
}

juce::Font Fonts::legend (float height) const
{
    return juce::Font (juce::FontOptions (semiBold).withHeight (height));
}

juce::Font Fonts::label (float height) const
{
    return juce::Font (juce::FontOptions (medium).withHeight (height));
}

juce::Font Fonts::display (float height) const
{
    return juce::Font (juce::FontOptions (black).withHeight (height));
}

} // namespace umber::ui
