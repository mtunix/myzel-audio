#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace myzel::ui
{

/**
    The colours of the shared UI kit, as named tokens. Each plugin sets its own values (its
    ui/Theme.cpp), so the same widgets read as Umber's dark brown or Cerulean's deep blue.

    One accent, reserved for things that are on (lamps, the selected A/B slot, focus, selection).
    Caution is the other colour with a meaning: states that take the sound away (Bypass). It
    never means "on". Colours of a plugin's own displays live in that plugin's Theme::extras().
*/
struct Palette
{
    // Panel (powder coat)
    juce::Colour panelTop, panelBottom, panelGrain, panelVignette, panelEdgeLight;

    // Legends (ink on the panel; legendLight is the drop shadow drawn under the ink)
    juce::Colour legend, legendDim, legendShadow, legendLight;
    juce::Colour grooveDark, grooveLight;

    // Wells (recesses the keys sit in)
    juce::Colour wellFill, wellShadow, wellLip;

    // Header rail and other dark surfaces (header keys, menus, dialogs)
    juce::Colour headerTop, headerBottom, headerLegend, headerLegendDim;
    juce::Colour displayGlass, displayText, displayTextDim;
    juce::Colour keyTop, keyBottom, keyText, keyTextActive;

    // Knobs
    juce::Colour skirtLight, skirtDark, capLight, capDark, pointer;
    juce::Colour tickMajor, tickMinor;

    // The accent: lamps and active states only
    juce::Colour accentCore, accent, accentDeep;
    juce::Colour lensOff, lensOffRim;

    // Feedback
    juce::Colour focus, focusOnDark, bubbleFill, bubbleEdge, overlayDim, danger;

    // Caution: Bypass
    juce::Colour caution, cautionDeep;
};

/** Dimensions shared by every plugin, in base (100 %) editor coordinates. */
namespace metrics
{
    inline constexpr float headerHeight = 50.0f;

    inline constexpr float minScale = 0.75f;
    inline constexpr float maxScale = 2.0f;

    // Light comes from the top-left: every shadow is offset by this (scaled by elevation).
    inline constexpr float shadowDx = 0.45f;
    inline constexpr float shadowDy = 1.0f;

    inline constexpr float legendSize      = 12.0f;
    inline constexpr float legendTracking  = 0.14f;   // extra kerning for small caps legends
    inline constexpr float scaleNumberSize = 9.5f;
    inline constexpr float keyLabelSize    = 10.5f;

    inline constexpr float knobStartAngle = -2.35619449f;   // -135 degrees
    inline constexpr float knobEndAngle   =  2.35619449f;   // +135 degrees

    inline constexpr float lampFadeSeconds   = 0.08f;
    inline constexpr float legendFadeSeconds = 0.18f;   // relabel crossfade
}

/** Shared typefaces (Inter, SIL OFL 1.1), loaded once and shared via SharedResourcePointer. */
class Fonts
{
public:
    Fonts();

    juce::Font legend (float height) const;     // Inter SemiBold
    juce::Font label (float height) const;      // Inter Medium
    juce::Font display (float height) const;    // Inter Display Black (headlines)

    juce::Typeface::Ptr getMedium() const { return medium; }

private:
    juce::Typeface::Ptr medium, semiBold, black;

    JUCE_DECLARE_NON_COPYABLE (Fonts)
};

struct Theme
{
    /** The plugin's kit colours. Each plugin defines this in its ui/Theme.cpp. */
    static const Palette& palette() noexcept;
};

} // namespace myzel::ui
