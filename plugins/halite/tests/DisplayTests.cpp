// The display's mappings (src/ui/DisplayScales.h): the dB and frequency axes, the history's
// columns, and what a point on the spectrum grabs. The views themselves are checked by the UI
// snapshot tool's self-test.

#include "plugin/Parameters.h"
#include "ui/DisplayScales.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace halite;
using namespace halite::ui::scales;
using Catch::Approx;

TEST_CASE ("Display: the sibilance axis spans the threshold's range", "[display]")
{
    const auto voice = sibilanceAxis (Detection::voice, 20.0f, 220.0f);
    CHECK (voice.lowDb == Approx (params::thresholdMinDb));
    CHECK (voice.highDb == Approx (params::thresholdMaxDb));
    CHECK (voice.yOf (params::thresholdMaxDb) == Approx (20.0f));
    CHECK (voice.yOf (params::thresholdMinDb) == Approx (220.0f));
    CHECK (voice.yOf (-6.0f) == Approx (20.0f + 200.0f * 30.0f / 60.0f));

    const auto allround = sibilanceAxis (Detection::allround, 0.0f, 120.0f);
    CHECK (allround.lowDb == Approx (params::absThresholdMinDb));
    CHECK (allround.highDb == Approx (0.0f));

    // Round trip, and clamping at both ends.
    for (auto db = -36.0f; db <= 24.0f; db += 0.7f)
        CHECK (voice.dbAt (voice.yOf (db)) == Approx (db).margin (1.0e-4));
    CHECK (voice.yOf (-300.0f) == Approx (220.0f));
    CHECK (voice.yOf (60.0f) == Approx (20.0f));
    CHECK (voice.dbAt (-50.0f) == Approx (24.0f));
    CHECK (voice.dbAt (500.0f) == Approx (-36.0f));

    // The Harsh threshold's range lies inside the Ess one, so both lines share the axis.
    CHECK (params::harshThresholdMinDb >= voice.lowDb);
    CHECK (params::harshThresholdMaxDb <= voice.highDb);
}

TEST_CASE ("Display: grid ticks keep labels apart", "[display]")
{
    CHECK (sibilanceTicks (228.0f).major == 12);
    CHECK (sibilanceTicks (228.0f).minor == 6);
    CHECK (sibilanceTicks (80.0f).major == 24);
    CHECK (sibilanceTicks (30.0f).minor == 24);

    // Down to the Cloud's plot in a minimum window; below that the axis keeps its two or three labels.
    for (const auto h : { 60.0f, 80.0f, 120.0f, 228.0f, 600.0f })
    {
        const auto t = sibilanceTicks (h);
        CHECK (h * (float) t.major / 60.0f >= 20.0f - 1.0e-3f);
        CHECK (t.major % t.minor == 0);
    }
}

TEST_CASE ("Display: every record lands in exactly one column", "[display]")
{
    // 48 kHz (1500 records/s), 44.1 kHz (fractional), 96 kHz (64-sample records), at the zooms and widths the views use.
    for (const auto perSecond : { 1500.0, 44100.0 / 32.0, 96000.0 / 64.0, 192000.0 / 128.0 })
        for (const auto zoom : { 2.0, 6.0, 10.0, 30.0, 60.0 })
            for (const auto columns : { 1, 37, 826, 1652, 3600, 7200 })
            {
                const auto map = ColumnMap::make (zoom, perSecond, columns);
                INFO ("records/s " << perSecond << ", zoom " << zoom << ", columns " << columns);
                REQUIRE (map.span == std::llround (zoom * perSecond));

                // Columns tile the records: each column starts where the last ended, and a record's column holds it
                // (with more columns than records, some columns hold none and show the record under their centre).
                for (std::int64_t c = -3; c < 2 * columns; c += std::max (1, columns / 97))
                {
                    const auto a = map.firstRecordOf (c), b = map.firstRecordOf (c + 1);
                    CHECK (b >= a);
                    if (b > a)
                    {
                        CHECK (map.columnOf (a) == c);
                        CHECK (map.columnOf (b - 1) == c);
                        CHECK (map.columnOf (b) > c);
                    }
                }

                // Far into a long session (a day at 1563 records/s), too.
                for (const auto r : { std::int64_t { 0 }, std::int64_t { 1 }, std::int64_t { 135'000'000 }, std::int64_t { 135'000'007 } })
                {
                    const auto c = map.columnOf (r);
                    CHECK (map.firstRecordOf (c) <= r);
                    CHECK (map.firstRecordOf (c + 1) > r);
                }

                // The window holds `span` records across `columns` columns.
                CHECK (map.firstRecordOf (columns) - map.firstRecordOf (0) == map.span);
            }
}

TEST_CASE ("Display: the Timeline's areas follow mock E", "[display]")
{
    const auto g = TimelineGeometry::forBounds ({ 0.0f, 0.0f, 896.0f, 268.0f }, Detection::voice);
    CHECK (g.plot.getY() == Approx (TimelineGeometry::topLine));
    CHECK (g.plot.getBottom() == Approx (268.0f - TimelineGeometry::axisHeight));
    CHECK (g.centreY == Approx (g.plot.getCentreY()));

    // Range's whole 24 dB hangs 40 % of the plot down; Harsh's 12 dB rises 16 %; S stays inside the plot.
    CHECK (24.0f * g.cutPxPerDb == Approx (0.40f * g.plot.getHeight()));
    CHECK (12.0f * g.harshPxPerDb == Approx (0.16f * g.plot.getHeight()));
    CHECK (g.sibilance.top > g.plot.getY());
    CHECK (g.sibilance.bottom < g.plot.getBottom());
}

TEST_CASE ("Display: the spectrum's axes", "[display]")
{
    const auto g = SpectrumGeometry::forBounds ({ 0.0f, 0.0f, 896.0f, 220.0f });
    CHECK (g.plot.getX() == Approx (SpectrumGeometry::left));
    CHECK (g.plot.getRight() == Approx (896.0f - SpectrumGeometry::right));

    CHECK (g.xOf (200.0) == Approx (g.plot.getX()));
    CHECK (g.xOf (20000.0) == Approx (g.plot.getRight()));
    CHECK (g.xOf (2000.0) == Approx (g.plot.getCentreX()));   // two decades, the middle one
    CHECK (g.xOf (1000.0) == Approx (g.plot.getX() + g.plot.getWidth() * (float) (std::log (5.0) / std::log (100.0))));

    for (const auto hz : { 200.0, 317.0, 1500.0, 4500.0, 12000.0, 20000.0 })
        CHECK (g.hzAt (g.xOf (hz)) == Approx (hz).epsilon (1.0e-4));

    // The cut hangs from the 0 dB line, deeper further down, never out of the plot.
    CHECK (g.cutY (0.0f) == Approx (g.zeroY));
    CHECK (g.cutY (-8.0f) == Approx (g.zeroY + 8.0f * g.cutPxPerDb));
    CHECK (g.cutY (-24.0f) <= g.plot.getBottom());
    CHECK (g.cutY (-120.0f) == Approx (g.plot.getBottom()));
    CHECK (g.cutY (3.0f) == Approx (g.zeroY));
    CHECK (g.zeroY > g.plot.getY());
    CHECK (g.zeroY < g.plot.getY() + 40.0f);

    CHECK (g.level.yOf (0.0f) == Approx (g.plot.getY()));
    CHECK (g.level.yOf (-96.0f) == Approx (g.plot.getBottom()));
    CHECK (g.level.yOf (-150.0f) == Approx (g.plot.getBottom()));
}

TEST_CASE ("Display: what a point on the spectrum grabs", "[display]")
{
    using T = SpectrumTarget;
    const auto g = SpectrumGeometry::forBounds ({ 0.0f, 0.0f, 896.0f, 220.0f });

    BandMarks m;
    m.bands.essLow = 4500.0;
    m.bands.essHigh = 12000.0;
    m.bands.harshLow = 2000.0;
    m.bands.harshHigh = 3000.0;
    m.harshShown = true;

    const auto essLow = handlePosition (g, m, T::essLow);
    const auto essHigh = handlePosition (g, m, T::essHigh);
    const auto harshCentre = handlePosition (g, m, T::harshCentre);
    CHECK (essLow.x == Approx (g.xOf (4500.0)));
    CHECK (essLow.y == Approx (g.zeroY));
    CHECK (harshCentre.x == Approx (g.xOf (std::sqrt (2000.0 * 3000.0))));

    SECTION ("handles, within their reach")
    {
        CHECK (hitTest (g, m, essLow, false) == T::essLow);
        CHECK (hitTest (g, m, essHigh + juce::Point<float> (6.0f, 5.0f), false) == T::essHigh);
        CHECK (hitTest (g, m, harshCentre, false) == T::harshCentre);
        CHECK (hitTest (g, m, harshCentre + juce::Point<float> (0.0f, 8.5f), true) == T::harshCentre);
    }

    SECTION ("edges along their whole height, then regions")
    {
        const auto low = g.plot.getBottom() - 10.0f;
        CHECK (hitTest (g, m, { essLow.x + 3.0f, low }, false) == T::essLow);
        CHECK (hitTest (g, m, { g.xOf (m.bands.harshHigh) - 3.5f, low }, false) == T::harshHigh);
        CHECK (hitTest (g, m, { g.xOf (m.bands.harshLow), low }, false) == T::harshLow);
        CHECK (hitTest (g, m, { g.xOf (7000.0), low }, false) == T::essBand);
        CHECK (hitTest (g, m, { g.xOf (2900.0), low }, false) == T::harshBand);
        CHECK (hitTest (g, m, { g.xOf (500.0), low }, false) == T::none);
        CHECK (hitTest (g, m, { g.xOf (7000.0), g.plot.getBottom() + 10.0f }, false) == T::none);
    }

    SECTION ("the selected band's handle wins where two handles meet")
    {
        auto close = m;
        close.bands.harshLow = 3600.0;
        close.bands.harshHigh = 5300.0;   // its centre about 4368 Hz, beside Detect Low's 4500
        const auto between = (handlePosition (g, close, T::harshCentre) + handlePosition (g, close, T::essLow)) * 0.5f;
        REQUIRE (handlePosition (g, close, T::harshCentre).getDistanceFrom (handlePosition (g, close, T::essLow)) < 2.0f * handleGrab);
        CHECK (hitTest (g, close, between, true) == T::harshCentre);
        CHECK (hitTest (g, close, between, false) == T::essLow);
    }

    SECTION ("overlapping regions: the band whose centre is nearer")
    {
        auto overlap = m;
        overlap.bands.harshLow = 3000.0;
        overlap.bands.harshHigh = 6000.0;
        const auto y = g.plot.getBottom() - 10.0f;
        CHECK (hitTest (g, overlap, { g.xOf (4700.0), y }, false) == T::harshBand);
        CHECK (hitTest (g, overlap, { g.xOf (5800.0), y }, false) == T::essBand);
    }

    SECTION ("Harsh off: nothing of it grabs")
    {
        auto off = m;
        off.harshShown = false;
        CHECK (hitTest (g, off, harshCentre, true) == T::none);
        CHECK (hitTest (g, off, { g.xOf (2900.0), g.plot.getBottom() - 10.0f }, true) == T::none);
        CHECK (hitTest (g, off, essLow, true) == T::essLow);
    }
}

TEST_CASE ("Display: an edge dragged sets Harsh Width around its centre", "[display]")
{
    CHECK (harshWidthFor (3200.0, 3200.0 * std::sqrt (2.0)) == Approx (1.0));
    CHECK (harshWidthFor (3200.0, 3200.0 / 2.0) == Approx (2.0));
    CHECK (harshWidthFor (3200.0, 3200.0) == Approx (0.0));
}

TEST_CASE ("Display: read-outs typeset the minus sign", "[display]")
{
    const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    CHECK (signedDb (6.0f) == "+6.0");
    CHECK (signedDb (-17.04f) == minus + "17.0");
    CHECK (signedDb (0.04f) == "0.0");
    CHECK (signedDb (-0.04f) == "0.0");
    CHECK (signedDb (-0.06f) == minus + "0.1");
    CHECK (secondsLabel (0) == "now");
    CHECK (secondsLabel (5) == minus + "5 s");
}
