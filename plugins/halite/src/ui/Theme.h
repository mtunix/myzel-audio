#pragma once

#include "common/ui/Theme.h"

#include <vector>

namespace halite::ui
{

/**
    The workbench's colours as named tokens: Navy (PLAN.md, UI restyle: Navy; src/ui/DESIGN.md, Tokens).
    Drawing code takes every colour from here, never a literal (black and white shading alphas excepted).
*/
struct Tokens
{
    juce::Colour background, surface, raised, line;
    juce::Colour text, secondary;
    juce::Colour signal, signalFill;            // the waveform's peak; the spectrum's fill
    juce::Colour signalCore;                    // the waveform's RMS core, inside the peak
    juce::Colour essFill, essText;              // the accent: fills and graphics; lines and text
    juce::Colour harshFill, harshText;          // the same accent, deeper
    juce::Colour removedEdge;                   // the removed part's outer pixel; transparent where the fill reads alone

    juce::Colour caution;                       // Bypass only, apart from the accent
    juce::Colour lineSoft;                      // dividers inside a part
    juce::Colour canvas;                        // the spectrum and the status line, a step under the background
    juce::Colour panel;                         // the meters and the events sidebar
    juce::Colour track, trackActive;            // segmented controls and tabs: the well, the chosen segment
    juce::Colour hover;                         // a control under the pointer
    juce::Colour selectedTint;                  // a selected band pill or event row
    juce::Colour faint;                         // inactive only (disabled text, unlit marks): no 4.5:1 claim
    juce::Colour grid, zeroLine;                // display grids, the 0 dB line
    juce::Colour onAccent;                      // text on an accent fill (Learn, value chips)
    juce::Colour signalLine;                    // the spectrum's output line
    juce::Colour meterPeak, meterRms;           // the meters' peak and RMS bars; meterPeak also an ess left alone in the Cloud
    juce::Colour focus;                         // keyboard focus ring
    juce::Colour overlay;                       // behind a dialog or the overlay sidebar
};

enum class ThemeName
{
    dark = 0,
    light
};

/** ⚙ › Theme. Stored by name in the user settings (UserSettings), never in the session. */
enum class ThemeChoice
{
    dark = 0,
    light,
    system      // the desktop's dark mode where JUCE can read it, otherwise Light (JUCE reports no dark mode)
};

namespace theme
{
    const Tokens& tokens (ThemeName) noexcept;

    /** The theme every Halite editor in this process draws with now (message thread). */
    const Tokens& current() noexcept;
    ThemeName currentName() noexcept;

    /** Switches the drawing theme (message thread); also restyles the shared kit's palette. */
    void apply (ThemeName);

    /** What a choice resolves to here and now. */
    ThemeName resolve (ThemeChoice);

    juce::String toString (ThemeChoice);
    ThemeChoice choiceFromString (const juce::String&, ThemeChoice fallback);

    /** WCAG 2 contrast ratio of two opaque colours (1 … 21). */
    double contrast (juce::Colour a, juce::Colour b);

    /** A text colour over its background, as the self-test checks it (PLAN.md, Colours and themes). */
    struct ContrastPair
    {
        const char* what;
        juce::Colour foreground, background;
        double minimum;
    };

    /** Every text and value read-out pairing the editor draws, with the ratio it must meet. */
    std::vector<ContrastPair> contrastPairs (const Tokens&);

    /** The accent fill's strength over the part of Learn's button still to come while it listens. */
    inline constexpr float learnRemainderAlpha = 0.45f;

    /** The Harsh threshold chip's fill strength while Harsh is off. */
    inline constexpr float dimmedChipAlpha = 0.6f;
}

/** The bundled faces: Inter for labels, JetBrains Mono for values (src/ui/assets/fonts). Sizes are px. */
class Fonts
{
public:
    Fonts();

    juce::Font sans (float px) const;          // Inter Regular
    juce::Font sansMedium (float px) const;
    juce::Font sansSemiBold (float px) const;
    juce::Font mono (float px) const;          // JetBrains Mono Regular
    juce::Font monoMedium (float px) const;

    juce::Typeface::Ptr defaultSans() const { return regular; }

private:
    juce::Typeface::Ptr regular, medium, semiBold, monoRegular, monoMed;

    JUCE_DECLARE_NON_COPYABLE (Fonts)
};

/** Type sizes, in px at 100 % display scale (the host's scale multiplies them; resizing never does). */
namespace type
{
    inline constexpr float wordmark = 14.0f;
    inline constexpr float preset = 12.5f;
    inline constexpr float button = 11.5f;
    inline constexpr float control = 11.0f;        // tabs, segments, fields, the status line
    inline constexpr float label = 10.5f;          // control labels, the legend
    inline constexpr float caps = 10.0f;           // part titles in tracked capitals (SPECTRUM, LEARN)
    inline constexpr float axis = 9.5f;
    inline constexpr float meter = 8.0f;           // the meters' In, Out, Cut under 48 px of strip
    inline constexpr float knobLarge = 13.0f;
    inline constexpr float knobSmall = 11.0f;
    inline constexpr float total = 17.0f;
}

/** Text width in px. */
float textWidth (const juce::Font&, const juce::String&);

/** The text cut to fit a width, with an ellipsis if it had to be cut. */
juce::String fitted (const juce::String&, const juce::Font&, float width);

/** Display spelling of a read-out: a leading hyphen becomes the minus sign (U+2212). */
juce::String withMinus (const juce::String&);

} // namespace halite::ui
