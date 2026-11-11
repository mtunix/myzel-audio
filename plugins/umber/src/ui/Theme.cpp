#include "ui/Theme.h"

#include "common/ui/StudioTheme.h"

namespace umber::ui
{

namespace
{
    using C = juce::Colour;

    Palette makePalette()
    {
        Palette p;

        // Matte dark umber powder coat: a deep earth brown, barely lit from the top-left.
        p.panelTop        = C (0xff3b2a1f);
        p.panelBottom     = C (0xff2e2018);
        p.panelGrain      = C (0xffffffff);
        p.panelVignette   = C (0xff0c0705);
        p.panelEdgeLight  = C (0x26fff0dc);

        // Warm cream silkscreen.
        p.legend          = C (0xffeee3d3);
        p.legendDim       = C (0xffad9a85);
        p.legendShadow    = C (0x80000000);
        p.legendLight     = C (0x0affffff);

        p.headerTop       = C (0xff21170f);
        p.headerBottom    = C (0xff1b130d);
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
        p.accentCore      = C (0xffffe2b8);
        p.accent          = C (0xffffa040);
        p.accentDeep      = C (0xffc8601c);
        p.lensOff         = C (0xff2a1a10);
        p.lensOffRim      = C (0xff0f0a07);

        p.focus           = C (0xffffa040);
        p.bubbleFill      = C (0xf01a120d);
        p.bubbleEdge      = C (0x40ffffff);
        p.overlayDim      = C (0x9a0a0604);

        // Engraved grooves (a dark cut with a faint lit edge) and flat dark wells.
        p.grooveDark      = C (0x80000000);
        p.grooveLight     = C (0x16ffffff);
        p.wellFill        = C (0x55000000);
        p.wellShadow      = C (0xd0000000);
        p.wellLip         = C (0x2effffff);
        p.tickShadow      = C (0x60000000);

        // The header and its dialogs use the panel's ink; a lit amber key is the primary action.
        p.headerLegend    = p.legend;
        p.headerLegendDim = p.legendDim;
        p.focusOnDark     = p.focus;
        p.danger          = p.accent;
        p.primaryTop      = p.accent.darker (0.15f);
        p.primaryBottom   = p.accentDeep;

        // Caution (Bypass): a signal red, so a bypassed plugin never looks like a lit amber key.
        p.caution         = C (0xffe8603f);
        p.cautionDeep     = C (0xff4c2117);
        return p;
    }

    Extras makeExtras()
    {
        Extras p;

        p.meterFaceCentre = C (0xfff4ead2);
        p.meterFaceEdge   = C (0xffe2d1ab);
        p.meterInk        = C (0xff26211c);
        p.meterRed        = C (0xffb5341a);
        p.needle          = C (0xff1a1714);
        p.meterBezel      = C (0xff1a120d);
        return p;
    }

    StudioPalette makeStudio()
    {
        StudioPalette p;

        // Flat near-black umber: cards a step lighter than the ground, hairline edges.
        p.background    = C (0xff16110d);
        p.card          = C (0xff1e1712);
        p.cardEdge      = C (0xff32271e);
        p.inset         = C (0xff120e0b);
        p.insetEdge     = C (0xff2c2219);
        p.hover         = C (0xff281f18);

        // Text on a card: text 14:1, textDim 7.2:1, title 6.1:1, textFaint 5.2:1 (all >= WCAG AA).
        p.text          = C (0xffeee3d3);
        p.textDim       = C (0xffb5a28d);
        p.textFaint     = C (0xff9c8873);
        p.title         = C (0xffa8947f);

        p.track         = C (0xff30251c);
        p.trackFill     = C (0xffc8601c);
        p.trackFillEdge = C (0xffffa040);
        p.marker        = C (0xff6b5747);
        p.chipOff       = C (0xff2b2119);
        p.chipOffText   = C (0xffb5a28d);

        // The accent: the panel's amber.
        p.accent        = C (0xffffa040);
        p.accentDeep    = C (0xffa14f15);   // lit chips: white text 5.8:1
        p.accentSoft    = C (0x33c8601c);
        p.accentText    = C (0xff1a0f08);

        p.grid          = C (0x1fe6c9a6);
        p.gridText      = C (0xff98846f);

        p.meterBack     = C (0xff100c09);
        p.meterFill     = C (0xffc98545);
        p.meterHot      = C (0xffeccb6e);
        p.meterClip     = C (0xffff6a52);
        p.meterPeak     = C (0xfff6e6cf);
        p.reduction     = C (0xffd9a35c);
        p.caution       = C (0xffe8603f);   // Bypass, as on the panel
        p.cautionDeep   = C (0xff4c2117);
        return p;
    }

    PanelLook makeLook()
    {
        PanelLook l;
        l.lightFalloff     = 0.035f;
        l.grain            = 0.022f;
        l.grainSeed        = 0x5c0c;
        l.vignetteEdge     = 0.22f;
        l.vignetteMid      = 0.05f;
        l.tickShadowOffset = { 0.0f, -0.6f };   // engraved: the cut's dark upper edge
        l.tickShadowMajor  = 1.6f;
        l.tickShadowMinor  = 1.2f;
        l.hoverRing        = 0.16f;
        l.hoverRingDrag    = 0.30f;
        l.keyLedY          = 16.0f;   // tall (58 px) keys: the label sits low, under a gap
        l.keyLabelY        = 38.0f;
        return l;
    }
}

const Extras& Theme::extras() noexcept
{
    static const Extras extras = makeExtras();
    return extras;
}

} // namespace umber::ui

// The kit colours: declared in common/ui/Theme.h, defined here for this plugin.
const myzel::ui::Palette& myzel::ui::Theme::palette() noexcept
{
    static const Palette palette = umber::ui::makePalette();
    return palette;
}

const myzel::ui::PanelLook& myzel::ui::Theme::look() noexcept
{
    static const PanelLook look = umber::ui::makeLook();
    return look;
}

const myzel::ui::StudioPalette& myzel::ui::Theme::studio() noexcept
{
    static const StudioPalette s = umber::ui::makeStudio();
    return s;
}
