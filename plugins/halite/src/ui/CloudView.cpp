#include "ui/MainView.h"

namespace halite::ui
{

CloudView::CloudView (HaliteController& c, DisplayModel& m)
    : HistoryView (c, m)
{
    setTitle ("Cloud");
    thresholds.setEssOnly (true);
    setDescription ("Every ess of the zoom window as a dot at its sibilance; a cut one shows where Halite left it.");
}

scales::CloudGeometry CloudView::geometry() const
{
    return scales::CloudGeometry::forBounds (getLocalBounds().toFloat(), model.essDetection());
}

void CloudView::update()
{
    const auto zoom = controller.getZoom (DisplayView::cloud);
    const auto geo = geometry();
    const auto scale = cached.scale > 0.0f ? cached.scale : 1.0f;
    const auto map = scales::ColumnMap::make (zoom, model.recordsPerSecond(), std::max (1, juce::roundToInt (geo.plot.getWidth() * scale)));
    const std::array<float, 2> limits { thresholds.valueOf (Band::ess), thresholds.valueOf (Band::harsh) };
    const auto behind = (int) std::floor (model.secondsBehindLive() + 0.5);

    const auto scrolled = endColumn (map) != drawnEnd || model.essesFound() != drawnEsses;
    const auto changed = map != cached.map || model.generation() != cached.generation
                      || model.essDetection() != cached.detection || theme::currentName() != cached.theme
                      || ! juce::approximatelyEqual (zoom, shownZoom) || limits != shownThresholds
                      || model.selected() != shownSelection || behind != shownSecondsBehind || controller.harshOn() != shownHarsh
                      || controller.getBand() != shownBand;

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
        // The dots and the selection's ring (8.75 px out from a dot's centre) lie within the plot and 9 px of it; the axes stay.
        ask (geo.plot.expanded (pad + 1.0f).getSmallestIntegerContainer().getIntersection (getLocalBounds()));
    }
}

void CloudView::drawDot (juce::Graphics& g, const scales::CloudGeometry& geo, const DisplayModel::Ess& e, float x)
{
    const auto& t = theme::current();
    const auto& axis = geo.sibilance;
    const auto y = axis.yOf (e.peakDb);

    // An ess left alone: grey and quiet, but data, so at the waveform's 3:1 (Settled 159).
    if (e.cutDb < DisplayModel::cutDrawnDb)
    {
        g.setColour (t.signal);
        g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        return;
    }

    // Where it was (hollow), the stem of the cut, where Halite left it (filled; a mark, so in ink: PLAN.md, N7).
    const auto left = axis.yOf (e.peakDb - e.cutDb);
    g.setColour (t.essText.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> (x - 1.0f, y, 2.0f, std::max (0.0f, left - y)));
    g.setColour (t.essText.withAlpha (0.6f));
    g.drawEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f, 1.5f);
    g.setColour (t.essText);
    g.fillEllipse (x - 4.0f, left - 4.0f, 8.0f, 8.0f);
}

void CloudView::drawBackdrop (juce::Graphics& g, const scales::CloudGeometry& geo, juce::Rectangle<float> area)
{
    const auto& t = theme::current();
    const auto& axis = geo.sibilance;
    const auto plot = geo.plot;
    const auto inPlot = area.getIntersection (plot.withY (area.getY()).withHeight (area.getHeight()));

    g.setColour (t.background);
    g.fillRect (area);

    // Over the threshold, a faint wash: what would be cut.
    const auto y = axis.yOf (thresholds.valueOf (Band::ess));
    g.setGradientFill (juce::ColourGradient (t.essFill.withAlpha (0.08f), 0.0f, plot.getY(), t.essFill.withAlpha (0.02f), 0.0f, y, false));
    g.fillRect (inPlot.getIntersection (plot.withBottom (y)));

    const auto ticks = scales::sibilanceTicks (plot.getHeight());
    for (auto db = (int) std::ceil (axis.lowDb / (float) ticks.minor) * ticks.minor; db <= (int) axis.highDb; db += ticks.minor)
    {
        g.setColour (db == 0 ? t.zeroLine : t.grid);
        g.fillRect (inPlot.getX(), std::round (axis.yOf ((float) db)), inPlot.getWidth(), 1.0f);
    }
}

void CloudView::bringUpToDate (float scale)
{
    const auto geo = geometry();
    const auto columns = std::max (1, juce::roundToInt (geo.plot.getWidth() * scale));
    const auto padPx = juce::roundToInt (pad * scale);
    const auto width = columns + 2 * padPx;
    const auto rows = std::max (1, juce::roundToInt ((geo.plot.getHeight() + 2.0f * pad) * scale));
    const auto map = scales::ColumnMap::make (controller.getZoom (DisplayView::cloud), model.recordsPerSecond(), columns);
    const CacheKey key { getWidth(), getHeight(), columns, scale, map, theme::currentName(), model.essDetection(), model.generation() };
    const auto end = endColumn (map);
    const auto left = end - columns + 1;
    const auto& esses = model.esses();
    const auto imageArea = geo.plot.expanded (pad);

    // What the wash depends on: a change redraws the image.
    const std::array<float, 3> backdrop { 0.0f, 0.0f, std::round (geo.sibilance.yOf (thresholds.valueOf (Band::ess)) * scale) };

    // The image's Graphics draws in the component's logical px.
    auto graphics = [&]
    {
        auto g = std::make_unique<juce::Graphics> (image);
        g->addTransform (juce::AffineTransform::translation (pad - geo.plot.getX(), pad - geo.plot.getY()).scaled (scale));
        return g;
    };

    auto drawFrom = [&] (juce::Graphics& g, int firstIndex)
    {
        for (int i = std::max (0, firstIndex); i < esses.size(); ++i)
        {
            const auto& e = esses[i];
            const auto col = map.columnOf (e.peak);
            if (col < left || col > end)
                continue;

            drawDot (g, geo, e, geo.plot.getX() + ((float) (col - left) + 0.5f) / scale);
            ++dots;
        }
    };

    const auto shift = end - drawnEnd;

    if (! (key == cached) || ! image.isValid() || shift < 0 || shift >= columns || backdrop != drawnBackdrop)
    {
        image = opaque::make (width, rows, scale);
        dots = 0;
        auto g = graphics();
        drawBackdrop (*g, geo, imageArea);
        drawFrom (*g, 0);
        cached = key;
        drawnBackdrop = backdrop;
        ++rebuilds;
    }
    else
    {
        auto g = graphics();
        if (shift > 0)
        {
            const auto k = (int) shift;
            image.moveImageSection (0, 0, k, 0, width - k, rows);
            drawBackdrop (*g, geo, imageArea.withLeft (imageArea.getRight() - (float) k / scale));
        }

        const auto fresh = (int) std::min<std::uint64_t> ((std::uint64_t) esses.size(), model.essesFound() - drawnEsses);
        drawFrom (*g, esses.size() - fresh);
    }

    drawnEnd = end;
    drawnEsses = model.essesFound();
}

void CloudView::paint (juce::Graphics& g)
{
    ++paints;
    const auto& t = theme::current();
    const auto scale = std::max (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    bringUpToDate (scale);

    const auto geo = geometry();
    const auto plot = geo.plot;
    const auto& axis = geo.sibilance;
    const auto zoom = controller.getZoom (DisplayView::cloud);
    const auto left = drawnEnd - cached.columns + 1;
    const auto imageArea = plot.expanded (pad);

    // The image holds the plot and its pad (backdrop and dots); the cached axes the margins at the left and
    // below; the thin margins above and at the right are plain. The one above runs under the axes too, so at a
    // fractional scale the pixel where they meet has an opaque ground, rounded alike in a partial and a whole repaint.
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (t.background);
    g.fillRect (bounds.withBottom (imageArea.getY()));
    g.fillRect (bounds.withLeft (imageArea.getRight()).withTop (imageArea.getY()).withBottom (imageArea.getBottom()));
    opaque::draw (g, image, (int) std::round (imageArea.getX() * scale), (int) std::round (imageArea.getY() * scale), scale);

    paintAxes (g, scale, zoom);

    // The selected ess: a ring around its dot.
    if (const auto selected = model.selected())
    {
        for (int i = model.esses().size(); --i >= 0;)
        {
            const auto& d = model.esses()[i];
            if (! selected->overlaps (d.first, d.last))
                continue;

            const auto col = cached.map.columnOf (d.peak);
            if (col >= left && col <= drawnEnd)
            {
                const auto x = plot.getX() + ((float) (col - left) + 0.5f) / scale;
                const auto y = axis.yOf (d.cutDb >= DisplayModel::cutDrawnDb ? d.peakDb - d.cutDb : d.peakDb);
                g.setColour (t.text);
                g.drawEllipse (x - 8.0f, y - 8.0f, 16.0f, 16.0f, 1.5f);
            }
            break;
        }
    }

    thresholds.paint (g, axis, plot.getX(), plot.getRight());
}

void CloudView::paintAxes (juce::Graphics& g, float scale, float zoom)
{
    // The dB labels at the left and the time axis below, cached: they change with the size, the mode, the zoom
    // and once a second while frozen.
    const auto geo = geometry();
    const auto plot = geo.plot;
    const auto key = std::tuple (getWidth(), getHeight(), scale, zoom, shownSecondsBehind, theme::currentName(), (int) model.essDetection());

    if (! axesImage.isValid() || key != axesKey)
    {
        const auto& t = theme::current();
        axesImage = opaque::make (juce::roundToInt ((float) getWidth() * scale), juce::roundToInt ((float) getHeight() * scale), scale);
        juce::Graphics ag (axesImage);
        ag.addTransform (juce::AffineTransform::scale (scale));
        ag.fillAll (t.background);
        ag.setFont (fonts->mono (type::axis));
        ag.setColour (t.secondary);

        const auto& axis = geo.sibilance;
        const auto ticks = scales::sibilanceTicks (plot.getHeight());
        for (auto db = (int) std::ceil (axis.lowDb / (float) ticks.major) * ticks.major; db <= (int) axis.highDb; db += ticks.major)
        {
            const auto y = std::round (axis.yOf ((float) db)) + 0.5f;
            ag.drawText (db == 0 ? juce::String ("0") : scales::signedDb ((float) db).upToFirstOccurrenceOf (".", false, false),
                         juce::Rectangle<float> (2.0f, y - 7.0f, plot.getX() - 12.0f, 12.0f), juce::Justification::centredRight, false);
        }

        const auto step = zoom > 30.0f ? 15 : 10;
        for (int s = 0; s <= (int) zoom; s += step)
        {
            const auto x = plot.getRight() - plot.getWidth() * (float) s / zoom;
            const auto text = scales::secondsLabel (s + shownSecondsBehind);
            const auto w = textWidth (ag.getCurrentFont(), text) + 2.0f;
            ag.drawText (text, juce::Rectangle<float> (juce::jlimit (0.0f, (float) getWidth() - w, x - w * 0.5f), (float) getHeight() - 16.0f, w, 12.0f),
                         juce::Justification::centred, false);
        }
        axesKey = key;
    }

    // The margins at the left and below, labels and all; the image's own area and the thin margins are the image's or plain.
    const auto labelsRight = juce::roundToInt ((plot.getX() - pad) * scale);
    const auto below = juce::roundToInt ((plot.getBottom() + pad) * scale);
    for (const auto& r : { juce::Rectangle<int> (0, 0, labelsRight, axesImage.getHeight()),
                           juce::Rectangle<int> (labelsRight, below, axesImage.getWidth() - labelsRight, axesImage.getHeight() - below) })
        if (! r.isEmpty())
            opaque::draw (g, axesImage.getClippedImage (r), r.getX(), r.getY(), scale);
}

bool CloudView::clickedAt (juce::Point<float> p)
{
    // A dot marks its ess, caught or left alone.
    const auto geo = geometry();
    const auto left = drawnEnd - cached.columns + 1;
    const auto scale = cached.scale > 0.0f ? cached.scale : 1.0f;
    const DisplayModel::Ess* nearest = nullptr;
    auto best = 9.0f;

    for (int i = 0; i < model.esses().size(); ++i)
    {
        const auto& d = model.esses()[i];
        const auto x = geo.plot.getX() + ((float) (cached.map.columnOf (d.peak) - left) + 0.5f) / scale;
        for (const auto db : { d.peakDb, d.peakDb - d.cutDb })
            if (const auto dist = p.getDistanceFrom ({ x, geo.sibilance.yOf (db) }); dist < best)
            {
                best = dist;
                nearest = &d;
            }
    }

    std::optional<DisplayModel::Span> chosen;
    if (nearest != nullptr)
        chosen = DisplayModel::Span { nearest->first, nearest->last };

    model.select (chosen);
    repaint();
    return chosen.has_value();
}

} // namespace halite::ui
