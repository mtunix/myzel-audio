#include "ui/Theme.h"
#include "ui/StudioTheme.h"

namespace mulberry::ui
{

namespace
{
    using C = juce::Colour;

    Palette makePalette()
    {
        Palette p;

        // Deep mulberry powder coat: the ripe fruit, dark purple-red, barely lit from the top-left.
        p.panelTop        = C (0xff4e1a37);
        p.panelBottom     = C (0xff41152e);
        p.panelGrain      = C (0xffffffff);
        p.panelVignette   = C (0xff12040c);
        p.panelEdgeLight  = C (0x40ffffff);

        // Pale silkscreen with a soft drop shadow. legend >= 7:1 on the panel, legendDim >= 4.5:1.
        p.legend          = C (0xfff7eef3);
        p.legendDim       = C (0xffd6b9c9);
        p.legendShadow    = C (0x14ffffff);
        p.legendLight     = C (0x5c14000a);
        p.grooveDark      = C (0x8c1a0612);
        p.grooveLight     = C (0x2effffff);

        p.wellFill        = C (0xff32101f);
        p.wellShadow      = C (0xd00e0208);
        p.wellLip         = C (0x40ffffff);

        // Plum rail and key caps: the family's dark surfaces, tinted mulberry.
        p.headerTop       = C (0xff241722);
        p.headerBottom    = C (0xff1d121b);
        p.headerLegend    = C (0xfff0e4ea);
        p.headerLegendDim = C (0xffa48f9b);
        p.displayGlass    = C (0xff140b12);
        p.displayText     = C (0xfff3e9ee);
        p.displayTextDim  = C (0xff94808b);
        p.keyTop          = C (0xff433541);
        p.keyBottom       = C (0xff312631);
        p.keyText         = C (0xffdccdd5);
        p.keyTextActive   = C (0xfffbf5f8);

        // Graphite knobs with warm aluminium caps.
        p.skirtLight      = C (0xff403a3f);
        p.skirtDark       = C (0xff0f0c0e);
        p.capLight        = C (0xffe6e0e3);
        p.capDark         = C (0xff736a6f);
        p.pointer         = C (0xfff8f4f6);
        p.tickMajor       = C (0xfff0e2ea);
        p.tickMinor       = C (0x99e0c6d4);

        // The only accent: leaf green (the mulberry leaf), for lamps and active states.
        p.accentCore      = C (0xfff2ffe6);
        p.accent          = C (0xffa6e07a);
        p.accentDeep      = C (0xff4f9b33);
        p.lensOff         = C (0xff201219);
        p.lensOffRim      = C (0xff0d070a);

        p.focus           = C (0xffbfeaa0);   // light leaf: >= 5:1 against the panel
        p.focusOnDark     = C (0xffa6e07a);
        p.bubbleFill      = C (0xff1d1219);
        p.bubbleEdge      = C (0x40ffffff);
        p.overlayDim      = C (0x8a12060d);
        p.danger          = C (0xffff9d8f);   // destructive dialog actions and errors only
        p.caution         = C (0xfff0b45a);   // Bypass: amber, never used for "on"
        p.cautionDeep     = C (0xff4a3a26);

        p.tickShadow      = p.legendLight;
        p.primaryTop      = p.accentDeep;
        p.primaryBottom   = p.accentDeep.darker (0.35f);
        return p;
    }

    Extras makeExtras()
    {
        Extras p;

        // Echo map: smoked glass over a deep aubergine screen.
        p.bezel           = C (0xff1e131a);
        p.screenTop       = C (0xff1a0f17);
        p.screenBottom    = C (0xff10080e);
        p.screenGrid      = C (0x1fe6c3d6);
        p.screenGridBeat  = C (0x3de6c3d6);
        p.screenText      = C (0xffe2cbd7);
        p.screenTextDim   = C (0xff8a7380);
        p.spike           = C (0xffc8f0a6);
        p.spikeDim        = C (0x66c8f0a6);
        p.spikeRight      = C (0xffe9c6f2);
        p.liveFill        = C (0x5a7fb85a);
        p.liveLine        = C (0xccb6e690);
        p.stepFill        = C (0x40a6e07a);
        p.stepEdge        = C (0xccbfeaa0);

        p.runHatch        = C (0xffd6b9c9);
        p.drawerValue     = C (0xfffaf4f7);
        return p;
    }

    PanelLook makeLook()
    {
        PanelLook l;
        l.lightFalloff     = 0.10f;
        l.grain            = 0.026f;
        l.grainSeed        = 0x3b1e;
        l.vignetteEdge     = 0.16f;
        l.vignetteMid      = 0.03f;
        l.tickShadowOffset = { 0.3f, 0.8f };
        l.tickShadowMajor  = 1.4f;
        l.tickShadowMinor  = 1.0f;
        l.hoverRing        = 0.22f;
        l.hoverRingDrag    = 0.40f;
        return l;
    }

    StudioPalette makeStudio()
    {
        StudioPalette p;

        // Flat deep plum: cards a step lighter than the ground, hairline edges.
        p.background      = C (0xff140c12);
        p.card            = C (0xff1d121a);
        p.cardEdge        = C (0xff33222d);
        p.inset           = C (0xff110a0f);
        p.insetEdge       = C (0xff2c1d27);
        p.hover           = C (0xff281923);

        p.text            = C (0xffeee5ea);
        p.textDim         = C (0xffb4a1ab);
        p.textFaint       = C (0xff998590);
        p.title           = C (0xffa08d98);

        p.track           = C (0xff352430);
        p.trackFill       = C (0xff4f9b33);
        p.trackFillEdge   = C (0xffa6e07a);
        p.marker          = C (0xff6a5662);
        p.chipOff         = C (0xff2c1e28);
        p.chipOffText     = C (0xffb4a1ab);

        p.accent          = C (0xffa6e07a);
        p.accentDeep      = C (0xff3d7a27);   // lit chips: white text >= 4.5:1
        p.accentSoft      = C (0x334f9b33);
        p.accentText      = C (0xff10180b);

        p.grid            = C (0x1fe6c3d6);
        p.gridText        = C (0xff9a8691);

        p.meterBack       = C (0xff0f080d);
        p.meterFill       = C (0xff86c862);
        p.meterHot        = C (0xffe8c46a);
        p.meterClip       = C (0xffff7a6b);
        p.meterPeak       = C (0xffe9f6dc);
        p.reduction       = C (0xffe2a65c);
        p.caution         = C (0xfff0b45a);
        p.cautionDeep     = C (0xff4a3a26);
        return p;
    }

    StudioExtras makeStudioExtras()
    {
        StudioExtras p;
        p.lineLeft        = C (0xffa6e07a);
        p.lineRight       = C (0xffd9a8ec);
        p.feedbackSlope   = C (0x99e2cbd7);
        p.liveFill        = C (0x447fb85a);
        p.liveLine        = C (0xccb6e690);
        p.step            = C (0x66a6e07a);
        return p;
    }
}

const Extras& Theme::extras() noexcept
{
    static const Extras extras = makeExtras();
    return extras;
}

const StudioExtras& Theme::studioExtras() noexcept
{
    static const StudioExtras s = makeStudioExtras();
    return s;
}

} // namespace mulberry::ui

// The kit colours: declared in common/ui/Theme.h, defined here for this plugin.
const myzel::ui::Palette& myzel::ui::Theme::palette() noexcept
{
    static const Palette palette = mulberry::ui::makePalette();
    return palette;
}

const myzel::ui::PanelLook& myzel::ui::Theme::look() noexcept
{
    static const PanelLook look = mulberry::ui::makeLook();
    return look;
}

const myzel::ui::StudioPalette& myzel::ui::Theme::studio() noexcept
{
    static const StudioPalette s = mulberry::ui::makeStudio();
    return s;
}
