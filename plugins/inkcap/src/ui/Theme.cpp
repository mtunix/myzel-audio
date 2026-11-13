#include "ui/Theme.h"

#include "common/ui/StudioTheme.h"

#include <InkcapAssets.h>

namespace inkcap::ui
{

namespace
{
    using C = juce::Colour;

    Glass makeGlass()
    {
        Glass g;

        // A warm grey appliance: matte plastic, keys a shade lighter when up, a shade darker when down.
        g.body          = C (0xffe8e5de);
        g.bodyEdgeLight = C (0xccffffff);
        g.bodyEdgeShade = C (0x14000000);
        g.ink           = C (0xff2b2925);
        g.inkDim        = C (0xff6f6b63);
        g.seam          = C (0xffd0ccc3);
        g.keyOff        = C (0xffefece6);
        g.keyOffLip     = C (0xffc9c5bc);
        g.keyOn         = C (0xffd6d2ca);
        g.led           = C (0xffef9a3c);   // a physical LED on the body: orange, not the display's amber
        g.ledOff        = C (0xffc2bdb3);
        g.knobLight     = C (0xfffaf8f3);
        g.knobDark      = C (0xffd6d2ca);
        g.pill          = C (0xffd6d2ca);
        g.pillText      = C (0xff2b2925);
        g.pillDark      = C (0xff2b2925);
        g.pillDarkText  = C (0xffe8e5de);
        g.focusOnBody   = C (0xff9a6c22);   // dark amber: 3.7:1 on the body (focus rings need 3:1)

        // The display: dark glass, warm white text, one amber.
        g.glass         = C (0xff222225);
        g.glassEdge     = C (0xff36363b);
        g.text          = C (0xffecebe4);
        g.textDim       = C (0xff8d8b84);
        g.textFaint     = C (0xff5a5a60);
        g.line          = C (0xff36363b);
        g.track         = C (0xff3a3a40);
        g.gridDot       = C (0xff3a3a3f);
        g.rowSelected   = C (0xff323236);
        g.rowHover      = C (0xff2a2a2e);
        g.amber         = C (0xfff5c26b);
        g.caution       = C (0xffee7a5f);   // Bypass only: coral, apart from the amber
        return g;
    }

    Palette makePalette()
    {
        const auto glass = makeGlass();
        Palette p;

        // The body, for anything of the kit that draws on it.
        p.panelTop        = glass.body;
        p.panelBottom     = glass.body;
        p.panelGrain      = C (0xffffffff);
        p.panelVignette   = C (0xff8f8a80);
        p.panelEdgeLight  = glass.bodyEdgeLight;
        p.legend          = glass.ink;
        p.legendDim       = glass.inkDim;
        p.legendShadow    = C (0x00000000);
        p.legendLight     = C (0x00000000);
        p.grooveDark      = glass.seam;
        p.grooveLight     = C (0x80ffffff);
        p.wellFill        = glass.keyOn;
        p.wellShadow      = C (0x33000000);
        p.wellLip         = C (0x80ffffff);

        // Dialogs, menus and tooltips are pieces of the glass.
        p.headerTop       = C (0xff2e2e33);
        p.headerBottom    = glass.glass;
        p.headerLegend    = glass.text;
        p.headerLegendDim = glass.textDim;
        p.displayGlass    = C (0xff18181b);
        p.displayText     = glass.text;
        p.displayTextDim  = glass.textDim;
        p.keyTop          = C (0xff3c3c42);
        p.keyBottom       = C (0xff313136);
        p.keyText         = C (0xffd9d7cf);
        p.keyTextActive   = C (0xfffbfaf6);

        p.skirtLight      = glass.knobLight;
        p.skirtDark       = glass.knobDark;
        p.capLight        = glass.knobLight;
        p.capDark         = glass.knobDark;
        p.pointer         = glass.ink;
        p.tickMajor       = glass.ink;
        p.tickMinor       = glass.inkDim;
        p.tickShadow      = C (0x00000000);

        p.accentCore      = C (0xffffe9c4);
        p.accent          = glass.amber;
        p.accentDeep      = C (0xff9a6c22);
        p.primaryTop      = C (0xff7a5820);
        p.primaryBottom   = C (0xff5c4218);
        p.lensOff         = glass.ledOff;
        p.lensOffRim      = glass.keyOffLip;

        p.focus           = glass.focusOnBody;
        p.focusOnDark     = glass.amber;
        p.bubbleFill      = C (0xff2a2a2e);
        p.bubbleEdge      = C (0x40ffffff);
        p.overlayDim      = C (0x8c18181a);
        p.danger          = C (0xffff8f7a);
        p.caution         = glass.caution;
        p.cautionDeep     = C (0xff4a2a22);
        return p;
    }

    PanelLook makeLook()
    {
        PanelLook l;
        l.lightFalloff = 0.0f;
        l.grain = 0.0f;
        l.vignetteEdge = 0.0f;
        l.vignetteMid = 0.0f;
        return l;
    }

    // Inkcap has no studio view (PLAN.md, Settled 4); the kit's studio pieces are linked but never shown.
    StudioPalette makeStudio()
    {
        const auto glass = makeGlass();
        StudioPalette s;
        s.background = glass.glass;
        s.card = glass.rowSelected;
        s.cardEdge = glass.line;
        s.inset = C (0xff18181b);
        s.insetEdge = glass.line;
        s.hover = glass.rowSelected;
        s.text = glass.text;
        s.textDim = glass.textDim;
        s.textFaint = glass.textFaint;
        s.title = glass.textDim;
        s.track = glass.track;
        s.trackFill = glass.text;
        s.trackFillEdge = glass.text;
        s.marker = glass.text;
        s.chipOff = glass.track;
        s.chipOffText = glass.textDim;
        s.accent = glass.amber;
        s.accentDeep = C (0xff9a6c22);
        s.accentSoft = glass.amber.withAlpha (0.25f);
        s.accentText = glass.glass;
        s.grid = glass.gridDot;
        s.gridText = glass.textFaint;
        s.meterBack = glass.track;
        s.meterFill = glass.text;
        s.meterHot = glass.amber;
        s.meterClip = glass.caution;
        s.meterPeak = glass.text;
        s.reduction = glass.amber;
        s.caution = glass.caution;
        s.cautionDeep = C (0xff4a2a22);
        return s;
    }
}

const Glass& Theme::glass() noexcept
{
    static const Glass g = makeGlass();
    return g;
}

GlassFonts::GlassFonts()
    : regular (juce::Typeface::createSystemTypefaceFor (InkcapAssets::JetBrainsMonoRegular_ttf,
                                                        (size_t) InkcapAssets::JetBrainsMonoRegular_ttfSize)),
      medium  (juce::Typeface::createSystemTypefaceFor (InkcapAssets::JetBrainsMonoMedium_ttf,
                                                        (size_t) InkcapAssets::JetBrainsMonoMedium_ttfSize))
{
}

juce::Font GlassFonts::mono (float px) const
{
    return juce::Font (juce::FontOptions (regular).withPointHeight (px));
}

juce::Font GlassFonts::monoMedium (float px) const
{
    return juce::Font (juce::FontOptions (medium).withPointHeight (px));
}

juce::Font GlassFonts::sans (float px) const
{
    return inter->legend (px).withPointHeight (px);
}

} // namespace inkcap::ui

// The kit colours: declared in common/ui/Theme.h, defined here for this plugin.
const myzel::ui::Palette& myzel::ui::Theme::palette() noexcept
{
    static const Palette p = inkcap::ui::makePalette();
    return p;
}

const myzel::ui::PanelLook& myzel::ui::Theme::look() noexcept
{
    static const PanelLook l = inkcap::ui::makeLook();
    return l;
}

const myzel::ui::StudioPalette& myzel::ui::Theme::studio() noexcept
{
    static const StudioPalette s = inkcap::ui::makeStudio();
    return s;
}
