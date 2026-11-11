#include "ui/Theme.h"
#include "ui/StudioTheme.h"

#include <CeruleanAssets.h>

namespace cerulean::ui
{

namespace
{
    using C = juce::Colour;

    Palette makeCerulean()
    {
        Palette p;

        // Deep cerulean powder coat: a saturated sky blue, darkened, barely lit from the top-left.
        p.panelTop        = C (0xff1b537c);
        p.panelBottom     = C (0xff164669);
        p.panelGrain      = C (0xffffffff);
        p.panelVignette   = C (0xff04101c);
        p.panelEdgeLight  = C (0x40ffffff);
        p.panelEdgeDark   = C (0x99000000);

        // Pale silkscreen on the blue, with a soft drop shadow (drawn below the ink by
        // drawLegend as legendLight). legend >= 7.4:1 on the panel, legendDim >= 4.9:1 (WCAG AA).
        p.legend          = C (0xffeef5fa);
        p.legendDim       = C (0xffb4cde0);
        p.legendShadow    = C (0x14ffffff);
        p.legendLight     = C (0x5c000814);
        p.grooveDark      = C (0x8c061624);
        p.grooveLight     = C (0x2effffff);

        p.wellFill        = C (0xff0f3654);
        p.wellShadow      = C (0xd0020a12);
        p.wellLip         = C (0x40ffffff);

        // Navy rail and key caps: the family's dark surfaces, tinted blue.
        p.headerTop       = C (0xff16212d);
        p.headerBottom    = C (0xff111a24);
        p.headerEdge      = C (0xff2f4054);
        p.headerLegend    = C (0xffe3ecf4);
        p.headerLegendDim = C (0xff8497aa);
        p.displayGlass    = C (0xff0a121b);
        p.displayText     = C (0xffe8f0f7);
        p.displayTextDim  = C (0xff7b8ea2);
        p.keyTop          = C (0xff34404e);
        p.keyBottom       = C (0xff262f3a);
        p.keyText         = C (0xffcbd6e0);
        p.keyTextActive   = C (0xfff2f8fc);

        // Graphite knobs with cool aluminium caps.
        p.skirtLight      = C (0xff3a424c);
        p.skirtDark       = C (0xff0c0f13);
        p.capLight        = C (0xffdde3e9);
        p.capDark         = C (0xff69727c);
        p.pointer         = C (0xfff2f6f9);
        p.tickMajor       = C (0xffdce9f3);
        p.tickMinor       = C (0x99c3d6e6);

        // The only accent: ice blue, for lamps and active states.
        p.iceCore         = C (0xffeefaff);
        p.ice             = C (0xff8fd6ff);
        p.iceDeep         = C (0xff2f8fd0);
        p.lensOff         = C (0xff111b26);
        p.lensOffRim      = C (0xff070c12);

        // Tail display: smoked glass over a deep night-blue screen.
        p.bezel           = C (0xff141b24);
        p.screenTop       = C (0xff0e1924);
        p.screenBottom    = C (0xff080f17);
        p.screenGrid      = C (0x1fa9c8e6);
        p.screenText      = C (0xffb9cfe2);
        p.screenTextDim   = C (0xff5f7488);
        p.traceMid        = C (0xffa8e0ff);
        p.traceBand       = C (0x998fc6ea);
        p.liveFill        = C (0x7a3f9be6);
        p.liveLine        = C (0xff9fd8ff);

        // Advanced deck: a recessed strip of the same blue, a step darker.
        p.drawerTop       = C (0xff123b5a);
        p.drawerBottom    = C (0xff154364);
        p.drawerLegend    = C (0xffeaf2f8);
        p.drawerLegendDim = C (0xffa6c0d4);
        p.drawerValue     = C (0xfff4f9fc);

        p.focus           = C (0xff9edcff);   // light ice: >= 5:1 against the blue panel
        p.focusOnDark     = C (0xff8fd6ff);
        p.bubbleFill      = C (0xff111922);
        p.bubbleText      = C (0xfff0f5f9);
        p.bubbleEdge      = C (0x40ffffff);
        p.overlayDim      = C (0x8a060c14);
        p.danger          = C (0xffff9d8f);   // destructive dialog actions and errors only
        p.caution         = C (0xfff0b45a);   // Bypass: amber, never used for "on"
        p.cautionDeep     = C (0xff4a3a26);
        return p;
    }
}

const Palette& Theme::palette() noexcept
{
    static const Palette cerulean = makeCerulean();
    return cerulean;
}

namespace
{
    StudioPalette makeStudio()
    {
        StudioPalette p;

        // Flat deep navy: cards a step lighter than the ground, hairline edges.
        p.background    = C (0xff0b131d);
        p.card          = C (0xff111c29);
        p.cardEdge      = C (0xff1f2f42);
        p.inset         = C (0xff09111a);
        p.insetEdge     = C (0xff1c2b3c);
        p.hover         = C (0xff182739);

        // Text on a card: text 14:1, textDim 7.1:1, title 5.9:1, textFaint 5.3:1 (all >= WCAG AA).
        p.text          = C (0xffe4e9ee);
        p.textDim       = C (0xff98a9ba);
        p.textFaint     = C (0xff7f91a4);
        p.title         = C (0xff8899ac);

        p.track         = C (0xff203044);
        p.trackFill     = C (0xff2f8fd0);
        p.trackFillEdge = C (0xff8fd6ff);
        p.marker        = C (0xff4f6378);
        p.chipOff       = C (0xff1d2b3b);
        p.chipOffText   = C (0xff98a9ba);

        // The accent: the panel's ice blue.
        p.accent        = C (0xff8fd6ff);
        p.accentDeep    = C (0xff1f73ad);   // lit chips: white text 5.1:1
        p.accentSoft    = C (0x332f8fd0);
        p.accentText    = C (0xff0b1620);

        // Graph: three bands with distinct hue *and* line style; ice is the mid band (the Decay setting).
        p.grid          = C (0x1fa9c8e6);
        p.gridText      = C (0xff7b8c9f);
        p.bandLow       = C (0xffe2a65c);
        p.bandMid       = C (0xff8fd6ff);
        p.bandHigh      = C (0xffc3a6ff);
        p.early         = C (0x99b9cfe2);
        p.liveFill      = C (0x553f9be6);
        p.liveLine      = C (0xcc9fd8ff);

        p.meterBack     = C (0xff08101a);
        p.meterFill     = C (0xff5fb6ea);
        p.meterHot      = C (0xffe8c46a);
        p.meterClip     = C (0xffff7a6b);
        p.meterPeak     = C (0xffd9eefc);
        p.reduction     = C (0xffe2a65c);
        p.caution       = C (0xfff0b45a);   // Bypass badge and dimmed wet meter
        p.cautionDeep   = C (0xff4a3a26);
        return p;
    }
}

const StudioPalette& Theme::studio() noexcept
{
    static const StudioPalette s = makeStudio();
    return s;
}

//==============================================================================
Fonts::Fonts()
    : medium   (juce::Typeface::createSystemTypefaceFor (CeruleanAssets::InterMedium_otf,
                                                         (size_t) CeruleanAssets::InterMedium_otfSize)),
      semiBold (juce::Typeface::createSystemTypefaceFor (CeruleanAssets::InterSemiBold_otf,
                                                         (size_t) CeruleanAssets::InterSemiBold_otfSize)),
      black    (juce::Typeface::createSystemTypefaceFor (CeruleanAssets::InterDisplayBlack_otf,
                                                         (size_t) CeruleanAssets::InterDisplayBlack_otfSize))
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

} // namespace cerulean::ui
