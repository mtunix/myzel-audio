#pragma once

#include "dsp/Modes.h"
#include "dsp/detector/Detector.h"

#include <juce_graphics/juce_graphics.h>

#include <cstdint>

namespace halite::ui::scales
{

/**
    The display's mappings, apart from any component so they can be tested (src/ui/DESIGN.md,
    Display). Everything is in logical px unless a name says physical.
*/

/** A linear dB axis on screen: highDb at `top`, lowDb at `bottom` (y grows downwards). */
struct DbAxis
{
    float lowDb = -36.0f, highDb = 24.0f;
    float top = 0.0f, bottom = 1.0f;

    /** Clamped to the axis. */
    float yOf (float db) const noexcept;

    /** Clamped to the range. */
    float dbAt (float y) const noexcept;
};

/**
    The axis the Timeline's S line, the Cloud and both threshold lines share: the threshold
    parameter's whole range, so the line can be dragged anywhere the parameter goes. Voice: −36 …
    +24 dB over the voice; Allround: −60 … 0 dBFS.
*/
DbAxis sibilanceAxis (Detection, float top, float bottom) noexcept;

/** Grid steps for the sibilance axis: lines every `minor` dB, labels every `major`. */
struct DbTicks
{
    int minor, major;
};

DbTicks sibilanceTicks (float axisHeight) noexcept;

/**
    History records onto pixel columns, in exact integer arithmetic so a column holds the same
    records however it was reached (scrolled into view or drawn in a full redraw): column c holds
    the records r with floor (r · columns / span) == c, `span` being the zoom window in records.
*/
struct ColumnMap
{
    std::int64_t span = 1;      // records in the zoom window
    std::int64_t columns = 1;   // physical pixel columns across the plot

    static ColumnMap make (double zoomSeconds, double recordsPerSecond, int columns) noexcept;

    std::int64_t columnOf (std::int64_t record) const noexcept;
    std::int64_t firstRecordOf (std::int64_t column) const noexcept;

    bool operator== (const ColumnMap&) const = default;
};

/** The Timeline's areas (mock E): the cut hangs from the top line, the Harsh cut rises from the bottom, the waveform is centred. */
struct TimelineGeometry
{
    juce::Rectangle<float> plot;    // from the top line to the time axis
    float centreY = 0.0f, halfHeight = 0.0f;
    float cutPxPerDb = 0.0f;        // the Ess cut, from the top: Range's whole 24 dB in 40 % of the plot
    float harshPxPerDb = 0.0f;      // the Harsh cut, from the bottom: its whole 12 dB in 16 %
    DbAxis sibilance;

    static TimelineGeometry forBounds (juce::Rectangle<float> bounds, Detection) noexcept;

    static constexpr float topLine = 8.0f, axisHeight = 18.0f;
};

/** The Cloud's areas: dB up the left, the zoom window along the bottom (mock D and E). */
struct CloudGeometry
{
    juce::Rectangle<float> plot;
    DbAxis sibilance;

    static CloudGeometry forBounds (juce::Rectangle<float> bounds, Detection) noexcept;

    static constexpr float left = 40.0f, right = 30.0f, top = 12.0f, bottom = 28.0f;
};

/**
    The spectrum's areas: 200 Hz–20 kHz on the feed's log axis (dsp::SpectrumAnalyser) 40 px in from
    the left and 30 from the right; the level axis from 0 dB at the top to −96 dB at the bottom;
    the cut curves hanging from the 0 dB line near the top, Range's whole 24 dB over most of the rest.
*/
struct SpectrumGeometry
{
    juce::Rectangle<float> plot;
    float zeroY = 0.0f;             // the cut curves' 0 dB line
    float cutPxPerDb = 0.0f;
    DbAxis level { -96.0f, 0.0f };

    static SpectrumGeometry forBounds (juce::Rectangle<float> bounds) noexcept;

    float xOf (double hz) const noexcept;
    double hzAt (float x) const noexcept;

    /** y of a cut (dB ≤ 0), clamped to the plot. */
    float cutY (float cutDb) const noexcept;

    static constexpr float left = 40.0f, right = 30.0f, axisHeight = 21.0f;
};

/** What a point on the spectrum grabs, in the order it is tried. */
enum class SpectrumTarget
{
    none,
    essLow,         // Detect's lower edge: its handle or its line
    essHigh,
    harshCentre,    // Harsh Freq: the centre handle
    harshLow,       // Harsh Width: either edge line
    harshHigh,
    essBand,        // a click inside a region selects the band
    harshBand
};

/** Where the spectrum's handles sit. */
struct BandMarks
{
    dsp::Detector::Bands bands;
    bool harshShown = false;

    double harshCentre() const noexcept;
};

/** Handle positions on the 0 dB line. */
juce::Point<float> handlePosition (const SpectrumGeometry&, const BandMarks&, SpectrumTarget);

/**
    Hit-testing: handles within handleGrab px first (the selected band's before the other's), then
    edge lines within edgeGrab px, then the regions (where two overlap, the band whose centre is
    nearer). Harsh takes part only while it is shown.
*/
SpectrumTarget hitTest (const SpectrumGeometry&, const BandMarks&, juce::Point<float>, bool harshSelected) noexcept;

inline constexpr float handleRadius = 5.5f, handleGrab = 9.0f, edgeGrab = 4.0f;

/** Harsh Width for an edge dragged to `hz`, the centre staying put: twice the octaves between them. */
double harshWidthFor (double centreHz, double edgeHz) noexcept;

/** The meters' level axis: 0 dBFS at the top, −60 at the bottom. */
inline constexpr float meterLowDb = -60.0f;

/** The Cut meter's axis: Range's whole 24 dB over the track, hanging from the top. */
inline constexpr float cutMeterDb = 24.0f;

/** "−1 s" … "now": a time label for seconds before the right edge (U+2212 for the minus). */
juce::String secondsLabel (int secondsBack);

/** A dB read-out with its sign, one decimal, U+2212 for the minus ("+6.0", "−17.5"). */
juce::String signedDb (float db);

} // namespace halite::ui::scales
