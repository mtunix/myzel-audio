#include "ui/MainView.h"

#include "dsp/detector/Levels.h"
#include "ui/Raster.h"

namespace halite::ui
{

namespace
{
    // The record under a column's centre, for columns narrower than a record (a short zoom on a wide window).
    std::int64_t centreRecord (const scales::ColumnMap& map, std::int64_t column)
    {
        const auto a = (2 * column + 1) * map.span, b = 2 * map.columns;
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    }

    bool known (float sibilanceDb) { return sibilanceDb > dsp::silentDb + 1.0f; }

    int ringIndex (std::int64_t column, int columns)
    {
        return (int) (((column % columns) + columns) % columns);
    }
}

TimelineView::TimelineView (HaliteController& c, DisplayModel& m)
    : HistoryView (c, m)
{
    setTitle ("Timeline");
    setDescription ("The last seconds: output and removed signal, the cut, sibilance and the threshold.");
}

scales::TimelineGeometry TimelineView::geometry() const
{
    return scales::TimelineGeometry::forBounds (getLocalBounds().toFloat(), controller.detection());
}

void TimelineView::update()
{
    const auto zoom = controller.getZoom (DisplayView::timeline);
    const auto geo = geometry();
    const auto scale = cached.scale > 0.0f ? cached.scale : 1.0f;
    const auto map = scales::ColumnMap::make (zoom, model.recordsPerSecond(), std::max (1, juce::roundToInt (geo.plot.getWidth() * scale)));
    const std::array<float, 2> limits { thresholds.valueOf (Band::ess), thresholds.valueOf (Band::harsh) };
    const auto behind = (int) std::floor (model.secondsBehindLive() + 0.5);

    const auto scrolled = endColumn (map) != drawnEnd;
    const auto changed = map != cached.map || model.generation() != cached.generation
                      || controller.detection() != cached.detection || theme::currentName() != cached.theme
                      || ! juce::approximatelyEqual (zoom, shownZoom) || limits != shownThresholds || model.selected() != shownSelection
                      || behind != shownSecondsBehind || controller.harshOn() != shownHarsh || controller.getBand() != shownBand;

    if (changed)
    {
        shownZoom = zoom;
        shownThresholds = limits;
        shownSelection = model.selected();
        shownSecondsBehind = behind;
        shownHarsh = controller.harshOn();
        shownBand = controller.getBand();
        ask();
    }
    else if (scrolled)
    {
        // Everything that scrolls (the data, tags, the selection) lies in the plot; the time axis stays.
        ask (geo.plot.getSmallestIntegerContainer());
    }
}

void TimelineView::bringUpToDate (float scale)
{
    const auto geo = geometry();
    const auto columns = std::max (1, juce::roundToInt (geo.plot.getWidth() * scale));
    const auto rows = std::max (1, juce::roundToInt (geo.plot.getHeight() * scale));
    const auto map = scales::ColumnMap::make (controller.getZoom (DisplayView::timeline), model.recordsPerSecond(), columns);
    const CacheKey key { getWidth(), getHeight(), columns, scale, map, theme::currentName(), controller.detection(), model.generation() };
    const auto end = endColumn (map);

    // The grid's image columns: static on screen while the data scrolls under them.
    const auto zoom = controller.getZoom (DisplayView::timeline);
    const auto gridWidth = std::max (1, juce::roundToInt (scale));
    std::vector<int> grid;
    for (int s = 1; s < (int) std::ceil (zoom); ++s)
        grid.push_back (columns - juce::roundToInt ((float) columns * (float) s / zoom));

    auto isGrid = [&] (int i)
    {
        return std::any_of (grid.begin(), grid.end(), [&] (int gx) { return i >= gx && i < gx + gridWidth; });
    };

    auto draw = [&] (std::int64_t from, std::int64_t to)
    {
        juce::Image::BitmapData bits (image, juce::Image::BitmapData::readWrite);
        drawColumns (bits, geo, map, from, to, end, columns, scale, isGrid);
    };

    const auto shift = end - drawnEnd;

    if (! (key == cached) || ! image.isValid() || shift < 0 || shift >= columns)
    {
        image = opaque::make (columns, rows, scale);
        draw (end - columns + 1, end);
        cached = key;
        ++rebuilds;
    }
    else if (shift > 0)
    {
        // A ring: column c lives at c mod columns, so scrolling moves no pixels. Each column is whole in
        // itself (background, grid, data), so a scroll draws the new columns and those the static grid
        // lines left or reached.
        const auto k = (int) shift;
        const auto left = end - columns + 1;
        draw (drawnEnd + 1, end);

        for (const auto gx : grid)
            for (const auto from : { gx - k, gx })
            {
                const auto first = std::max (0, from), last = std::min (columns - k, from + gridWidth) - 1;
                if (first <= last)
                    draw (left + first, left + last);
            }
    }

    drawnEnd = end;
}

void TimelineView::drawColumns (juce::Image::BitmapData& bits, const scales::TimelineGeometry& geo, const scales::ColumnMap& map,
                                std::int64_t firstColumn, std::int64_t lastColumn, std::int64_t endCol, int columns, float scale,
                                const std::function<bool (int)>& isGrid)
{
    const auto& t = theme::current();
    const auto left = endCol - columns + 1;
    const auto& axis = geo.sibilance;
    const auto top = geo.plot.getY();

    // In the image's physical rows.
    auto row = [&] (float y) { return (y - top) * scale; };
    const auto lineHalf = 0.65f * scale;
    const auto cutPx = geo.cutPxPerDb * scale, harshPx = geo.harshPxPerDb * scale;
    const auto centre = row (geo.centreY), halfHeight = geo.halfHeight * scale, bottom = row (geo.plot.getBottom());

    const auto essRemoved = t.essFill.withAlpha (0.9f).getPixelARGB();
    const auto harshRemoved = t.harshFill.withAlpha (0.9f).getPixelARGB();
    const auto cutFill = t.essFill.withAlpha (0.85f).getPixelARGB();
    const auto harshFill = t.harshFill.withAlpha (0.6f).getPixelARGB();
    const auto signal = t.signal.getPixelARGB(), core = t.signalCore.getPixelARGB();
    const auto background = t.background.getPixelARGB(), grid = t.grid.getPixelARGB();

    // Light's Ess fill reads 1.7:1 on the paper, so its removed part and its hanging cut take an ink edge one logical
    // px wide; Harsh's deeper fill reads alone as a broad area (PLAN.md, N7; Settled 155, 158).
    const auto removedEdge = t.removedEdge.getPixelARGB();
    const auto edged = t.removedEdge.getAlpha() > 0;
    const auto edgeWidth = scale;

    // Under 4 px of half-height the core and the peak merge: the column is one tone.
    const auto coreFrom = 4.0f * scale;

    auto span = [&] (int x, float y0, float y1, juce::PixelARGB colour) { raster::blendSpan (bits, x, y0, y1, colour); };

    auto recordsOf = [&] (std::int64_t c)
    {
        auto a = map.firstRecordOf (c), b = map.firstRecordOf (c + 1);
        if (b <= a)
        {
            a = centreRecord (map, c);
            b = a + 1;
        }
        return std::pair (std::max (a, model.oldest()), std::min (b, model.newest() + 1));
    };

    // S, one value a column (its loudest record), so hop-to-hop jitter inside a column draws no solid blob.
    // NaN where S is unknown or under the axis: the line shows only what a threshold can reach.
    constexpr auto none = std::numeric_limits<float>::quiet_NaN();

    struct Column
    {
        bool any = false;
        float in = 0.0f, out = 0.0f, rms = 0.0f, cut = 0.0f, harsh = 0.0f;
        float s = std::numeric_limits<float>::quiet_NaN();
    };

    auto summarise = [&] (std::int64_t c)
    {
        Column col;
        const auto [a, b] = recordsOf (c);
        for (auto r = a; r < b; ++r)
        {
            const auto& rec = model.record (r);
            col.any = true;
            col.in = std::max (col.in, rec.inputPeak);
            col.out = std::max (col.out, rec.outputPeak);
            col.rms = std::max (col.rms, rec.outputRms);
            col.cut = std::max (col.cut, rec.essDb);
            col.harsh = std::max (col.harsh, rec.harshDb);
            if (known (rec.sibilanceDb) && rec.sibilanceDb >= axis.lowDb && (std::isnan (col.s) || rec.sibilanceDb > col.s))
                col.s = rec.sibilanceDb;
        }
        return col;
    };

    auto inHalfOf = [&] (const Column& col) { return std::min (col.in, 1.0f) * halfHeight; };
    auto outHalfOf = [&] (const Column& col) { return std::max (std::min (col.out, 1.0f) * halfHeight, 0.5f); };

    // The half-height of a column's edged removed part, or none. A part thinner than the edge would be edge alone,
    // a dark cap on a vowel's peak, so it has none.
    auto edgeHalfOf = [&] (const Column& col)
    {
        const auto inHalf = inHalfOf (col);
        return edged && col.any && col.harsh <= col.cut && inHalf - outHalfOf (col) >= edgeWidth ? inHalf : none;
    };

    auto cutDepthOf = [&] (const Column& col) { return std::min (col.cut, 24.0f) * cutPx; };

    // The hanging cut takes the same edge along its lower outline, where it is at least as deep as the edge.
    auto cutEdgeOf = [&] (const Column& col)
    {
        const auto depth = cutDepthOf (col);
        return edged && col.any && depth >= edgeWidth ? depth : none;
    };

    // The line fades out over the axis' bottom 12 dB (in Voice, under Learn's −24 dB ess floor), so a vowel's
    // low S stays quiet and the line never ends in a bare stroke at the floor.
    const auto sLine = t.text.withAlpha (0.85f).getPixelARGB();
    const auto fadeTop = row (axis.yOf (axis.lowDb + 12.0f)), fadeBottom = row (axis.yOf (axis.lowDb));

    const auto first = summarise (firstColumn - 1);
    auto previousS = first.s;
    auto previousEdge = edgeHalfOf (first);
    auto previousCutEdge = cutEdgeOf (first);

    for (auto c = firstColumn; c <= lastColumn; ++c)
    {
        const auto x = ringIndex (c, columns);
        raster::fillColumn (bits, x, isGrid ((int) (c - left)) ? grid : background);

        const auto col = summarise (c);
        const auto before = std::exchange (previousS, col.s);
        const auto edgeHalf = edgeHalfOf (col);
        const auto edgeBefore = std::exchange (previousEdge, edgeHalf);
        const auto cutEdge = cutEdgeOf (col);
        const auto cutEdgeBefore = std::exchange (previousCutEdge, cutEdge);
        if (! col.any)
            continue;

        if (col.cut > 0.0f)
            span (x, 0.0f, cutDepthOf (col), cutFill);

        if (! std::isnan (cutEdge))
        {
            const auto deeper = std::isnan (cutEdgeBefore) ? cutEdge : std::max (cutEdge, cutEdgeBefore);
            const auto shallower = std::isnan (cutEdgeBefore) ? cutEdge : std::min (cutEdge, cutEdgeBefore);
            span (x, shallower - edgeWidth, deeper, removedEdge);
        }

        // The removed part takes the colour of the band that took more here (PLAN.md, Colours: Harsh is the deeper accent).
        const auto inHalf = inHalfOf (col), outHalf = outHalfOf (col);
        if (inHalf > outHalf)
            span (x, centre - inHalf, centre + inHalf, col.harsh > col.cut ? harshRemoved : essRemoved);

        // The edge joins the column before inside a removed run, so a steep flank stays outlined; the peak covers its inner side.
        if (! std::isnan (edgeHalf))
        {
            const auto outer = std::isnan (edgeBefore) ? edgeHalf : std::max (edgeHalf, edgeBefore);
            const auto inner = std::isnan (edgeBefore) ? edgeHalf : std::min (edgeHalf, edgeBefore);
            span (x, centre - outer, centre - inner + edgeWidth, removedEdge);
            span (x, centre + inner - edgeWidth, centre + outer, removedEdge);
        }

        // The peak around its RMS core, each pixel drawn once but for the two the core's edges share.
        const auto coreHalf = outHalf >= coreFrom ? std::min (std::min (col.rms, 1.0f) * halfHeight, outHalf) : 0.0f;
        if (coreHalf > 0.0f)
        {
            span (x, centre - outHalf, std::ceil (centre - coreHalf), signal);
            span (x, std::floor (centre + coreHalf), centre + outHalf, signal);
            span (x, centre - coreHalf, centre + coreHalf, core);
        }
        else
        {
            span (x, centre - outHalf, centre + outHalf, signal);
        }

        if (col.harsh > 0.0f)
            span (x, bottom - std::min (col.harsh, 12.0f) * harshPx, bottom, harshFill);

        // The line joins the column before it; where it rises from or falls under the floor it runs to the floor,
        // fading out on the way.
        if (! std::isnan (col.s) || ! std::isnan (before))
        {
            const auto y = std::isnan (col.s) ? fadeBottom : row (axis.yOf (col.s));
            const auto yBefore = std::isnan (before) ? fadeBottom : row (axis.yOf (before));
            raster::blendSpanFading (bits, x, std::min (y, yBefore) - lineHalf, std::max (y, yBefore) + lineHalf, sLine, fadeTop, fadeBottom);
        }
    }
}

void TimelineView::paint (juce::Graphics& g)
{
    ++paints;
    const auto& t = theme::current();
    const auto scale = std::max (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    bringUpToDate (scale);

    const auto geo = geometry();
    const auto plot = geo.plot;
    const auto zoom = controller.getZoom (DisplayView::timeline);
    const auto left = drawnEnd - cached.columns + 1;

    // The image holds the plot (background and grid with it) as a ring, drawn in two pieces from the
    // left edge's column; the band above is background, the one below the cached time axis.
    g.setColour (t.background);
    g.fillRect (getLocalBounds().toFloat().withBottom (plot.getY()));

    const auto columns = cached.columns;
    const auto p0 = ringIndex (left, columns);
    auto blit = [&] (int from, int width, int at)
    {
        if (width > 0)
            opaque::draw (g, image.getClippedImage ({ from, 0, width, image.getHeight() }), at + (int) std::round (plot.getX() * scale),
                          (int) std::round (plot.getY() * scale), scale);
    };
    blit (p0, columns - p0, 0);
    blit (0, p0, columns - p0);

    paintAxis (g, scale, zoom);

    // The selected ess: a light band over its span.
    const auto selected = model.selected();
    auto xOfRecord = [&] (std::int64_t r) { return plot.getX() + ((float) (cached.map.columnOf (r) - left) + 0.5f) / scale; };

    if (selected.has_value())
    {
        const auto x0 = xOfRecord (selected->first), x1 = xOfRecord (selected->last);
        if (x1 >= plot.getX() && x0 <= plot.getRight())
        {
            g.setColour (t.essFill.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (x0 - 3.0f, plot.getY(), x1 - x0 + 6.0f, plot.getHeight()));
        }
    }

    g.setColour (t.zeroLine);
    g.fillRect (0.0f, plot.getY(), (float) getWidth(), 1.0f);

    // A tag on every ess with its cut, under the cut it hangs from; a tag that would overlap the last or a
    // threshold chip is left out, and a plot too low for them shows the cut alone.
    tags.clear();
    thresholds.paint (g, geo.sibilance, plot.getX(), plot.getRight());
    const auto tagFont = fonts->monoMedium (10.0f);
    auto lastRight = -1.0e9f;

    for (int i = 0; i < model.events().size() && plot.getHeight() >= 56.0f; ++i)
    {
        const auto& e = model.events()[i];
        const auto peak = model.peakRecord (e);
        const auto col = cached.map.columnOf (peak);
        if (col < left || col > drawnEnd)
            continue;

        const auto x = xOfRecord (peak);
        const auto text = scales::signedDb (-e.ess.peakReductionDb);
        const auto w = chips.widthOf (text, tagFont) + 10.0f;
        auto area = juce::Rectangle<float> (x - w * 0.5f, plot.getY() + std::min (e.ess.peakReductionDb, 24.0f) * geo.cutPxPerDb + 4.0f, w, 14.0f);
        area.setX (juce::jlimit (0.0f, std::max (0.0f, (float) getWidth() - w), area.getX()));
        area.setY (std::min (area.getY(), plot.getBottom() - 16.0f));

        if (area.getX() < lastRight + 2.0f || area.intersects (thresholds.chipArea (Band::ess).expanded (3.0f))
            || area.intersects (thresholds.chipArea (Band::harsh).expanded (3.0f)))
            continue;

        lastRight = area.getRight();
        tags.push_back ({ e.number, area });

        chips.draw (g, area, text, t.essFill, t.onAccent, tagFont, area.getHeight() * 0.5f);

        if (selected.has_value() && selected->overlaps (e.first, e.last))
        {
            const auto ring = area.expanded (1.5f);
            g.setColour (t.text);
            g.drawRoundedRectangle (ring, ring.getHeight() * 0.5f, 1.2f);
        }
    }
}

void TimelineView::paintAxis (juce::Graphics& g, float scale, float zoom)
{
    // The time axis: seconds before now (frozen, the labels count how far the picture lies behind). Cached:
    // it changes with the zoom, the size and once a second while frozen.
    const auto band = getLocalBounds().toFloat().withTop (geometry().plot.getBottom());
    const auto key = std::tuple (getWidth(), getHeight(), scale, zoom, shownSecondsBehind, theme::currentName());

    if (! axisImage.isValid() || key != axisKey)
    {
        const auto& t = theme::current();
        axisImage = opaque::make (juce::roundToInt (band.getWidth() * scale), juce::roundToInt (band.getHeight() * scale), scale);
        juce::Graphics ag (axisImage);
        ag.addTransform (juce::AffineTransform::translation (-band.getX(), -band.getY()).scaled (scale));
        ag.fillAll (t.background);
        ag.setFont (fonts->mono (type::axis));
        ag.setColour (t.secondary);

        const auto plot = geometry().plot;
        const auto labelStep = zoom > 6.0f ? 2 : 1;
        for (int s = 0; s < (int) std::ceil (zoom); s += labelStep)
        {
            const auto x = plot.getRight() - plot.getWidth() * (float) s / zoom;
            const auto text = scales::secondsLabel (s + shownSecondsBehind);
            const auto w = textWidth (ag.getCurrentFont(), text) + 2.0f;
            ag.drawText (text, juce::Rectangle<float> (s == 0 ? x - w - 10.0f : x - w * 0.5f, (float) getHeight() - 16.0f, w, 12.0f),
                         juce::Justification::centred, false);
        }
        axisKey = key;
    }

    opaque::draw (g, axisImage, (int) std::round (band.getX() * scale), (int) std::round (band.getY() * scale), scale);
}

juce::Rectangle<float> TimelineView::tagArea (std::uint64_t eventNumber) const
{
    for (const auto& tag : tags)
        if (tag.number == eventNumber)
            return tag.area;
    return {};
}

bool TimelineView::clickedAt (juce::Point<float> p)
{
    for (const auto& tag : tags)
        if (tag.area.expanded (2.0f).contains (p))
        {
            if (const auto* e = model.findEvent (tag.number))
                model.select (DisplayModel::spanOf (*e));
            repaint();
            return true;
        }

    if (model.selected().has_value())
    {
        model.select (std::nullopt);
        repaint();
    }
    return false;
}

} // namespace halite::ui
