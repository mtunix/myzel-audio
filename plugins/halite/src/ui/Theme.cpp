#include "ui/Theme.h"

#include "common/ui/StudioTheme.h"

#include <HaliteAssets.h>

namespace halite::ui
{

namespace
{
    using C = juce::Colour;

    Tokens makeDark()
    {
        Tokens t;
        t.background   = C (0xff0e121b);
        t.surface      = C (0xff131927);
        t.raised       = C (0xff1b2234);
        t.line         = C (0xff283047);
        t.text         = C (0xffe8eaf0);
        t.secondary    = C (0xff99a1b4);
        t.signal       = C (0xff5c6684);
        t.signalFill   = C (0xff1a2132);
        t.signalCore   = C (0xff8f97ad);
        t.essFill      = C (0xfff0b44c);
        t.essText      = C (0xfff0b44c);
        t.harshFill    = C (0xffc06a2e);
        t.harshText    = C (0xffde8240);
        t.removedEdge  = C (0x00000000);

        t.caution      = C (0xffff7a6e);   // coral red: amber is the accent
        t.lineSoft     = C (0xff1a2030);
        t.canvas       = C (0xff0b0f17);
        t.panel        = C (0xff111623);
        t.track        = C (0xff0b0f17);
        t.trackActive  = C (0xff242c42);
        t.hover        = C (0xff1f2639);
        t.selectedTint = C (0xff2a2418);
        t.faint        = C (0xff586078);
        t.grid         = C (0xff161c2a);
        t.zeroLine     = C (0xff343d57);
        t.onAccent     = C (0xff0e121b);
        t.signalLine   = C (0xff8f97ad);
        t.meterPeak    = C (0xff3a4462);
        t.meterRms     = C (0xff8f97ad);
        t.focus        = C (0xfff0b44c);
        t.overlay      = C (0x99000000);
        return t;
    }

    // Light's amber is split (PLAN.md, N7): the fill only for areas carrying navy text, a dark amber ink for
    // lines, arcs, handles and accent text, and an ink edge on the removed part, since the fill reads 1.7:1 on paper.
    Tokens makeLight()
    {
        Tokens t;
        t.background   = C (0xfff3f4f8);
        t.surface      = C (0xfffafbfd);
        t.raised       = C (0xffffffff);
        t.line         = C (0xffd8dce6);
        t.text         = C (0xff141a2b);
        t.secondary    = C (0xff525a6f);
        t.signal       = C (0xff7d87a3);
        t.signalFill   = C (0xffe3e7f0);
        t.signalCore   = C (0xff4f5872);
        t.essFill      = C (0xfff2b44a);
        t.essText      = C (0xff8f5600);
        t.harshFill    = C (0xffd9783c);
        t.harshText    = C (0xff7a3212);
        t.removedEdge  = C (0xff8f5600);

        t.caution      = C (0xffb3261e);
        t.lineSoft     = C (0xffe8ebf2);
        t.canvas       = C (0xffeef0f5);
        t.panel        = C (0xfff7f8fb);
        t.track        = C (0xffe6e9f0);
        t.trackActive  = C (0xffffffff);
        t.hover        = C (0xffeceff5);
        t.selectedTint = C (0xfffdf3e1);
        t.faint        = C (0xffa2a9ba);
        t.grid         = C (0xffe5e8ef);
        t.zeroLine     = C (0xffc2c8d6);
        t.onAccent     = C (0xff141a2b);
        t.signalLine   = C (0xff4f5872);
        t.meterPeak    = C (0xffc9cfdd);
        t.meterRms     = C (0xff6b7491);
        t.focus        = C (0xff8f5600);
        t.overlay      = C (0x4d141a2b);
        return t;
    }

    ThemeName& active()
    {
        static ThemeName name = ThemeName::dark;
        return name;
    }

    // The shared kit's colours, for what Halite links from it (EditorController); rewritten on a theme change.
    myzel::ui::Palette& kitPalette()
    {
        static myzel::ui::Palette p;
        return p;
    }

    void fillKitPalette (const Tokens& t)
    {
        auto& p = kitPalette();
        p.panelTop = p.panelBottom = t.background;
        p.panelGrain = C (0xffffffff);
        p.panelVignette = t.background;
        p.panelEdgeLight = t.line;
        p.legend = t.text;
        p.legendDim = t.secondary;
        p.legendShadow = p.legendLight = C (0x00000000);
        p.grooveDark = t.line;
        p.grooveLight = t.lineSoft;
        p.wellFill = t.track;
        p.wellShadow = C (0x33000000);
        p.wellLip = t.line;
        p.headerTop = p.headerBottom = t.surface;
        p.headerLegend = t.text;
        p.headerLegendDim = t.secondary;
        p.displayGlass = t.raised;
        p.displayText = t.text;
        p.displayTextDim = t.secondary;
        p.keyTop = p.keyBottom = t.raised;
        p.keyText = t.text;
        p.keyTextActive = t.text;
        p.skirtLight = p.skirtDark = p.capLight = p.capDark = t.raised;
        p.pointer = t.text;
        p.tickMajor = t.text;
        p.tickMinor = t.secondary;
        p.tickShadow = C (0x00000000);
        p.accentCore = t.essText;
        p.accent = t.essFill;
        p.accentDeep = t.harshFill;
        p.primaryTop = p.primaryBottom = t.essFill;
        p.lensOff = t.track;
        p.lensOffRim = t.line;
        p.focus = p.focusOnDark = t.focus;
        p.bubbleFill = t.raised;
        p.bubbleEdge = t.line;
        p.overlayDim = t.overlay;
        p.danger = t.caution;
        p.caution = t.caution;
        p.cautionDeep = t.caution.withAlpha (0.2f);
    }

    double luminance (juce::Colour c)
    {
        auto channel = [] (juce::uint8 v)
        {
            const auto s = v / 255.0;
            return s <= 0.03928 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel (c.getRed()) + 0.7152 * channel (c.getGreen()) + 0.0722 * channel (c.getBlue());
    }
}

namespace theme
{
    const Tokens& tokens (ThemeName name) noexcept
    {
        static const Tokens dark = makeDark(), light = makeLight();
        return name == ThemeName::light ? light : dark;
    }

    const Tokens& current() noexcept { return tokens (active()); }
    ThemeName currentName() noexcept { return active(); }

    void apply (ThemeName name)
    {
        active() = name;
        fillKitPalette (tokens (name));
    }

    ThemeName resolve (ThemeChoice choice)
    {
        switch (choice)
        {
            case ThemeChoice::dark:   return ThemeName::dark;
            case ThemeChoice::light:  return ThemeName::light;
            case ThemeChoice::system: break;
        }

        // JUCE reads the desktop's setting on macOS, Windows and Linux desktops that publish it (XSettings).
        return juce::Desktop::getInstance().isDarkModeActive() ? ThemeName::dark : ThemeName::light;
    }

    juce::String toString (ThemeChoice choice)
    {
        switch (choice)
        {
            case ThemeChoice::light:  return "light";
            case ThemeChoice::system: return "system";
            case ThemeChoice::dark:   break;
        }
        return "dark";
    }

    ThemeChoice choiceFromString (const juce::String& s, ThemeChoice fallback)
    {
        if (s == "dark")   return ThemeChoice::dark;
        if (s == "light")  return ThemeChoice::light;
        if (s == "system") return ThemeChoice::system;
        return fallback;
    }

    double contrast (juce::Colour a, juce::Colour b)
    {
        const auto la = luminance (a), lb = luminance (b);
        return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
    }

    std::vector<ContrastPair> contrastPairs (const Tokens& t)
    {
        constexpr double text = 4.5, graphic = 3.0;
        const auto cautionTint = t.surface.interpolatedWith (t.caution, 0.14f);
        const auto learnRemainder = t.surface.overlaidWith (t.essFill.withMultipliedAlpha (learnRemainderAlpha));
        const auto dimmedHarshChip = t.background.overlaidWith (t.harshFill.withMultipliedAlpha (dimmedChipAlpha));
        // Light's raised is white, so an "on" button falls back to the well there (draw::onFill).
        const auto onButton = t.trackActive == t.raised ? t.track : t.trackActive;

        std::vector<ContrastPair> pairs {
            { "text on background", t.text, t.background, text },
            { "text on surface", t.text, t.surface, text },
            { "text on raised", t.text, t.raised, text },
            { "text on canvas", t.text, t.canvas, text },
            { "text on panel", t.text, t.panel, text },
            { "text on a chosen segment", t.text, t.trackActive, text },
            { "text on hover", t.text, t.hover, text },
            { "text on a selected row", t.text, t.selectedTint, text },
            { "secondary on background", t.secondary, t.background, text },
            { "secondary on surface", t.secondary, t.surface, text },
            { "secondary on raised", t.secondary, t.raised, text },
            { "secondary on canvas", t.secondary, t.canvas, text },
            { "secondary on panel", t.secondary, t.panel, text },
            { "secondary on a segment well", t.secondary, t.track, text },
            { "secondary on a selected row", t.secondary, t.selectedTint, text },
            { "secondary on a hovered row", t.secondary, t.hover, text },
            { "Ess text on a hovered row", t.essText, t.hover, text },
            { "Ess text on background", t.essText, t.background, text },
            { "Ess text on surface", t.essText, t.surface, text },
            { "Ess text on raised", t.essText, t.raised, text },
            { "Ess text on canvas", t.essText, t.canvas, text },
            { "Ess text on panel (also the histogram's threshold, knocked out of the bars)", t.essText, t.panel, text },
            { "Ess text on the selected tint (a selected pill, freeze held)", t.essText, t.selectedTint, text },
            { "Harsh text on background", t.harshText, t.background, text },
            { "Harsh text on surface", t.harshText, t.surface, text },
            { "Harsh text on canvas", t.harshText, t.canvas, text },
            { "Harsh text on panel", t.harshText, t.panel, text },
            { "Harsh text on a selected pill", t.harshText, t.selectedTint, text },
            { "text on the Ess fill (Learn, chips)", t.onAccent, t.essFill, text },
            { "text on the Harsh fill (chips)", t.onAccent, t.harshFill, text },
            { "text on the dimmed Harsh chip (Harsh off)", t.text, dimmedHarshChip, text },
            { "text on an 'on' tool, menu or More button", t.text, onButton, text },
            { "text on Learn's part still to come (listening)", t.text, learnRemainder, text },
            { "caution on surface", t.caution, t.surface, text },
            { "caution on its tint (Bypass on)", t.caution, cautionTint, text },
            { "focus ring on background", t.focus, t.background, graphic },
            { "focus ring on surface", t.focus, t.surface, graphic },
            { "Ess line on surface (knob arcs)", t.essText, t.surface, graphic },
            { "Harsh line on surface (knob arcs, the switch on)", t.harshText, t.surface, graphic },
            { "Harsh line on raised (the pill's dot, the switch on a pill)", t.harshText, t.raised, graphic },
            { "Ess line on background (threshold, Cloud stems)", t.essText, t.background, graphic },
            { "Ess line on canvas (the cut curve, handles)", t.essText, t.canvas, graphic },
            { "Harsh line on canvas (its curve and handle)", t.harshText, t.canvas, graphic },
            { "spectrum output line on canvas", t.signalLine, t.canvas, graphic },
            { "waveform on background", t.signal, t.background, graphic },
            { "histogram bars under the threshold on panel", t.signal, t.panel, graphic },
            { "RMS bar on its track", t.meterRms, t.track, graphic },
            { "Ess cut bar on its track", t.essText, t.track, graphic },
            { "Harsh cut bar on its track", t.harshText, t.track, graphic },
        };

        // Light's removed part is outlined because its fill reads too faint on the background; Dark's needs no edge.
        if (t.removedEdge.isOpaque())
            pairs.push_back ({ "the removed part's edge on background", t.removedEdge, t.background, graphic });

        return pairs;
    }
}

//==============================================================================
namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
}

Fonts::Fonts()
    : regular (load (HaliteAssets::InterRegular_ttf, HaliteAssets::InterRegular_ttfSize)),
      medium (load (HaliteAssets::InterMedium_ttf, HaliteAssets::InterMedium_ttfSize)),
      semiBold (load (HaliteAssets::InterSemiBold_ttf, HaliteAssets::InterSemiBold_ttfSize)),
      monoRegular (load (HaliteAssets::JetBrainsMonoRegular_ttf, HaliteAssets::JetBrainsMonoRegular_ttfSize)),
      monoMed (load (HaliteAssets::JetBrainsMonoMedium_ttf, HaliteAssets::JetBrainsMonoMedium_ttfSize))
{
}

juce::Font Fonts::sans (float px) const         { return juce::Font (juce::FontOptions (regular).withPointHeight (px)); }
juce::Font Fonts::sansMedium (float px) const   { return juce::Font (juce::FontOptions (medium).withPointHeight (px)); }
juce::Font Fonts::sansSemiBold (float px) const { return juce::Font (juce::FontOptions (semiBold).withPointHeight (px)); }
juce::Font Fonts::mono (float px) const         { return juce::Font (juce::FontOptions (monoRegular).withPointHeight (px)); }
juce::Font Fonts::monoMedium (float px) const   { return juce::Font (juce::FontOptions (monoMed).withPointHeight (px)); }

float textWidth (const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (font, text);
}

juce::String fitted (const juce::String& text, const juce::Font& font, float width)
{
    if (textWidth (font, text) <= width)
        return text;

    const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    auto cut = text;

    while (cut.isNotEmpty() && textWidth (font, cut.trimEnd() + ellipsis) > width)
        cut = cut.dropLastCharacters (1);

    return cut.isEmpty() ? juce::String() : cut.trimEnd() + ellipsis;
}

juce::String withMinus (const juce::String& s)
{
    if (s.startsWithChar ('-'))
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + s.substring (1);
    return s;
}

} // namespace halite::ui

// The shared kit's colours: declared in common/ui/Theme.h, defined here for this plugin. Halite draws
// none of the kit's widgets; the palette follows the workbench theme for anything that might.
const myzel::ui::Palette& myzel::ui::Theme::palette() noexcept
{
    static const bool filled = [] { halite::ui::theme::apply (halite::ui::theme::currentName()); return true; }();
    juce::ignoreUnused (filled);
    return halite::ui::kitPalette();
}

const myzel::ui::PanelLook& myzel::ui::Theme::look() noexcept
{
    static const PanelLook l = []
    {
        PanelLook p;
        p.lightFalloff = 0.0f;
        p.grain = 0.0f;
        p.vignetteEdge = p.vignetteMid = 0.0f;
        return p;
    }();
    return l;
}

// Halite has no studio view; the kit's studio pieces are linked but never shown.
const myzel::ui::StudioPalette& myzel::ui::Theme::studio() noexcept
{
    static const StudioPalette s = []
    {
        const auto& t = halite::ui::theme::tokens (halite::ui::ThemeName::dark);
        StudioPalette p;
        p.background = t.background;
        p.card = t.surface;
        p.cardEdge = t.line;
        p.inset = t.canvas;
        p.insetEdge = t.line;
        p.hover = t.hover;
        p.text = t.text;
        p.textDim = t.secondary;
        p.textFaint = t.faint;
        p.title = t.secondary;
        p.track = t.track;
        p.trackFill = p.trackFillEdge = p.marker = t.text;
        p.chipOff = t.track;
        p.chipOffText = t.secondary;
        p.accent = t.essFill;
        p.accentDeep = t.harshFill;
        p.accentSoft = t.essFill.withAlpha (0.25f);
        p.accentText = t.onAccent;
        p.grid = t.grid;
        p.gridText = t.secondary;
        p.meterBack = t.track;
        p.meterFill = p.meterPeak = t.text;
        p.meterHot = t.essFill;
        p.meterClip = p.caution = t.caution;
        p.reduction = t.essFill;
        p.cautionDeep = t.caution.withAlpha (0.2f);
        return p;
    }();
    return s;
}
