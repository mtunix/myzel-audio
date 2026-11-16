#include "ui/SpectrumView.h"

#include "plugin/PluginProcessor.h"
#include "ui/Widgets.h"

namespace halite::ui
{

namespace
{
    using T = scales::SpectrumTarget;

    juce::Font caps (const Fonts& fonts, float px)
    {
        return fonts.sansMedium (px).withExtraKerningFactor (0.08f);
    }

    bool isHandle (T t) { return t == T::essLow || t == T::essHigh || t == T::harshCentre || t == T::harshLow || t == T::harshHigh; }
    bool isHarsh (T t)  { return t == T::harshCentre || t == T::harshLow || t == T::harshHigh || t == T::harshBand; }

    // A handle's ink disc and the canvas ring around it, in px.
    constexpr float handleInk = scales::handleRadius - 0.5f, handleRing = 2.0f;
}

const juce::Path& SpectrumView::ribbon (const juce::Point<float>* p, int n, float thickness, bool dashed)
{
    // Offset vertically by half the thickness, widened by the slope so the line keeps its weight where it is steep.
    line.clear();

    auto half = [&] (int i)
    {
        const auto& a = p[std::max (0, i - 1)];
        const auto& b = p[std::min (n - 1, i + 1)];
        const auto dx = std::max (1.0e-3f, b.x - a.x), slope = (b.y - a.y) / dx;
        return 0.5f * thickness * std::min (8.0f, std::sqrt (1.0f + slope * slope));
    };

    auto piece = [&] (int from, int to)
    {
        line.startNewSubPath (p[from].x, p[from].y - half (from));
        for (int i = from + 1; i <= to; ++i)
            line.lineTo (p[i].x, p[i].y - half (i));
        for (int i = to; i >= from; --i)
            line.lineTo (p[i].x, p[i].y + half (i));
        line.closeSubPath();
    };

    // Dashed: every other segment.
    if (dashed)
        for (int i = 0; i + 1 < n; i += 2)
            piece (i, i + 1);
    else if (n >= 2)
        piece (0, n - 1);

    return line;
}

SpectrumView::SpectrumView (HaliteController& c, DisplayModel& m)
    : controller (c), model (m)
{
    setTitle ("Spectrum");
    setDescription ("Input and output spectrum, each band's cut, and the bands' handles.");
    setOpaque (true);
}

SpectrumView::~SpectrumView()
{
    // Closed mid-drag: the host and the undo recorder must see the gesture end.
    if (dragging && dragId != nullptr)
        controller.endEdit (dragId);
}

scales::SpectrumGeometry SpectrumView::geometry() const
{
    return scales::SpectrumGeometry::forBounds (getLocalBounds().toFloat());
}

scales::BandMarks SpectrumView::marks() const
{
    auto& processor = controller.getProcessor();
    const auto rate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    return { dsp::Detector::bandsFor (processor.readSettings().detector, rate), controller.harshOn() };
}

void SpectrumView::update()
{
    const auto m = marks();

    if (model.spectrumFrame() != shownFrame || m.bands != shownBands || controller.getBand() != shownBand || m.harshShown != shownHarsh
        || theme::currentName() != shownTheme)
    {
        shownFrame = model.spectrumFrame();
        shownBands = m.bands;
        shownBand = controller.getBand();
        shownHarsh = m.harshShown;
        shownTheme = theme::currentName();
        titleCuts = { model.essCut(), model.harshCut() };
        ++repaintsAsked;

        // A new frame over an unchanged static layer repaints where the curves moved; anything else, the whole view.
        const auto geo = geometry();
        if (canCompare && keyFor (backgroundKey.scale, m) == backgroundKey)
            ask (changedArea (painted, trace (geo, m)));
        else
            ask (getLocalBounds());
    }
}

void SpectrumView::ask (const juce::RectangleList<int>& area)
{
    for (const auto& r : area)
        repaint (r);
    askedArea.add (area);
}

SpectrumView::BackgroundKey SpectrumView::keyFor (float scale, const scales::BandMarks& m) const
{
    return { getWidth(), getHeight(), scale, theme::currentName(), m.bands, m.harshShown, controller.getBand(),
             hover == T::harshLow || hover == T::harshHigh || (dragging && (pressed == T::harshLow || pressed == T::harshHigh)) };
}

SpectrumView::Trace SpectrumView::trace (const scales::SpectrumGeometry& geo, const scales::BandMarks& m) const
{
    Trace t;
    const auto& spectrum = model.spectrum();
    const auto* hz = spectrum.frequencies();
    t.count = std::min (spectrum.numPoints(), DisplayModel::spectrumPoints);
    t.target = model.hasTarget();
    t.harsh = m.harshShown;

    for (int i = 0; i < t.count; ++i)
    {
        t.x[(size_t) i] = geo.xOf (hz[i]);
        t.input[(size_t) i] = geo.level.yOf (spectrum.inputDb()[i]);
        t.output[(size_t) i] = geo.level.yOf (spectrum.outputDb()[i]);
        t.targetY[(size_t) i] = t.target ? geo.level.yOf (model.targetLine()[i]) : 0.0f;
        t.essY[(size_t) i] = geo.cutY (model.essCurve()[i]);
        t.harshY[(size_t) i] = t.harsh ? geo.cutY (model.harshCurve()[i]) : 0.0f;
    }

    auto title = [] (const char* name, float cut)
    {
        return cut >= 0.05f ? juce::String (name) + " " + scales::signedDb (-cut) : juce::String (name);
    };
    t.essTitle = title ("ESS", titleCuts[0]);
    t.harshTitle = t.harsh ? title ("HARSH", titleCuts[1]) : juce::String();
    return t;
}

juce::RectangleList<int> SpectrumView::changedArea (const Trace& before, const Trace& now) const
{
    if (before.count != now.count || before.target != now.target || before.harsh != now.harsh)
        return getLocalBounds();

    // The rows any moved curve spans, before and after, across the width: the renderer's cost follows the clip's
    // bounds and its number of rectangles, so one band of rows costs less than narrower strips (measured). A
    // ribbon reaches half its thickness times at most 8 (its slope) past its points.
    constexpr float reach = 9.0f;
    auto top = std::numeric_limits<float>::max(), bottom = std::numeric_limits<float>::lowest();

    auto span = [&] (const std::array<float, DisplayModel::spectrumPoints>& was, const std::array<float, DisplayModel::spectrumPoints>& is)
    {
        if (std::equal (was.begin(), was.begin() + now.count, is.begin(), [] (float x, float y) { return juce::exactlyEqual (x, y); }))
            return;
        for (int i = 0; i < now.count; ++i)
        {
            top = std::min ({ top, was[(size_t) i], is[(size_t) i] });
            bottom = std::max ({ bottom, was[(size_t) i], is[(size_t) i] });
        }
    };

    span (before.input, now.input);
    span (before.output, now.output);
    if (now.target)
        span (before.targetY, now.targetY);
    span (before.essY, now.essY);
    if (now.harsh)
        span (before.harshY, now.harshY);

    juce::RectangleList<int> area;
    if (top <= bottom)
        area.add (juce::Rectangle<int>::leftTopRightBottom (0, (int) std::floor (top - reach), getWidth(), (int) std::ceil (bottom + reach)));

    // The titles' row: both titles, since the Ess title's width decides whether Harsh's fits.
    if (before.essTitle != now.essTitle || before.harshTitle != now.harshTitle)
        area.add (juce::Rectangle<int>::leftTopRightBottom (0, titleTop, getWidth(), titleBottom));

    area.clipTo (getLocalBounds());
    return area;
}

void SpectrumView::paintBackground (juce::Graphics& g, const scales::SpectrumGeometry& geo, const scales::BandMarks& m)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const auto plot = geo.plot;
    const auto essSelected = controller.getBand() == Band::ess;
    g.fillAll (t.canvas);

    g.setColour (t.grid);
    for (const auto hz : { 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        g.fillRect (std::round (geo.xOf (hz)), plot.getY(), 1.0f, plot.getHeight());
    for (const auto db : { -24.0f, -48.0f, -72.0f })
        g.fillRect (plot.getX(), std::round (geo.level.yOf (db)), plot.getWidth(), 1.0f);

    // The bands: the selected one in a stronger tint; Harsh only while it is on.
    // Light's fills are paler on paler ground, so its tints are 1.3 times Dark's (the mocks' 0.10 / 0.14; Settled 160).
    const auto tintScale = theme::currentName() == ThemeName::light ? 1.3f : 1.0f;
    auto region = [&] (double lo, double hi, juce::Colour fill, juce::Colour edge, float tint, float edgeAlpha, bool lit)
    {
        const auto x0 = geo.xOf (lo), x1 = geo.xOf (hi);
        g.setColour (fill.withAlpha (tint * tintScale));
        g.fillRect (juce::Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()));
        g.setColour (edge.withAlpha (lit ? 1.0f : edgeAlpha));
        g.fillRect (std::round (x0), plot.getY(), 1.0f, plot.getHeight());
        g.fillRect (std::round (x1), plot.getY(), 1.0f, plot.getHeight());
    };

    const auto harshEdgeLit = hover == T::harshLow || hover == T::harshHigh || (dragging && (pressed == T::harshLow || pressed == T::harshHigh));
    if (m.harshShown)
        region (m.bands.harshLow, m.bands.harshHigh, t.harshFill, t.harshText, essSelected ? 0.06f : 0.16f, essSelected ? 0.3f : 0.8f, harshEdgeLit);
    region (m.bands.essLow, m.bands.essHigh, t.essFill, t.essText, essSelected ? 0.11f : 0.05f, essSelected ? 0.7f : 0.3f, false);

    g.setColour (t.line);
    g.fillRect (bounds.withHeight (1.0f));

    g.setColour (t.secondary);
    g.setFont (fonts->mono (type::axis));
    for (const auto& [f, text] : { std::pair { 200.0, "200" }, { 500.0, "500" }, { 1000.0, "1k" }, { 2000.0, "2k" },
                                   { 5000.0, "5k" }, { 10000.0, "10k" }, { 20000.0, "20k" } })
        g.drawText (text, juce::Rectangle<float> (geo.xOf (f) - 20.0f, plot.getBottom() + 4.0f, 40.0f, 12.0f), juce::Justification::centred, false);

    // The level axis in the left margin, where the plot has room for it.
    if (plot.getHeight() >= 110.0f)
        for (const auto db : { -24, -48, -72 })
            g.drawText (scales::signedDb ((float) db).upToFirstOccurrenceOf (".", false, false),
                        juce::Rectangle<float> (2.0f, geo.level.yOf ((float) db) - 6.0f, plot.getX() - 8.0f, 12.0f),
                        juce::Justification::centredRight, false);
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto geo = geometry();
    const auto plot = geo.plot;
    const auto m = marks();
    const auto essSelected = controller.getBand() == Band::ess;
    const auto scale = std::max (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());

    // Grid, regions and axes change with the bands, the selection and the size, not with the audio: cached.
    const auto key = keyFor (scale, m);
    const auto rebuilt = ! (key == backgroundKey) || ! background.isValid();
    if (rebuilt)
    {
        background = opaque::make (juce::roundToInt ((float) getWidth() * scale), juce::roundToInt ((float) getHeight() * scale), scale);
        juce::Graphics bg (background);
        bg.addTransform (juce::AffineTransform::scale (scale));
        paintBackground (bg, geo, m);
        backgroundKey = key;
    }
    // What the frame clock compares the next frame with (changedArea). A paint that rebuilt the static layer may
    // be another target's (a snapshot at another scale), so the next frame repaints whole.
    painted = trace (geo, m);
    canCompare = ! rebuilt;
    const auto& tr = painted;
    const auto n = tr.count;
    const auto* hz = model.spectrum().frequencies();

    opaque::draw (g, background, 0, 0, scale);

    // Input filled, output as a line, on the level axis. Lines are filled ribbons: stroking a 384-point
    // path costs more than the rest of the frame.
    if (n > 1)
    {
        spectrumFill.clear();
        spectrumFill.startNewSubPath (tr.x[0], plot.getBottom());
        for (int i = 0; i < n; ++i)
        {
            spectrumFill.lineTo (tr.x[(size_t) i], tr.input[(size_t) i]);
            points[(size_t) i] = { tr.x[(size_t) i], tr.output[(size_t) i] };
        }
        spectrumFill.lineTo (tr.x[(size_t) n - 1], plot.getBottom());
        spectrumFill.closeSubPath();

        g.setColour (t.signalFill);
        g.fillPath (spectrumFill);
        g.setColour (t.signalLine);
        g.fillPath (ribbon (points.data(), n, 1.1f));
    }

    // Spectral's reference over the Ess band: the voice's own top end no bin is cut below.
    if (n > 1 && tr.target)
    {
        auto count = 0;
        for (int i = 0; i < n; ++i)
            if (hz[i] >= m.bands.essLow && hz[i] <= m.bands.essHigh)
                points[(size_t) count++] = { tr.x[(size_t) i], tr.targetY[(size_t) i] };
        g.setColour (t.text.withAlpha (0.7f));
        g.fillPath (ribbon (points.data(), count, 1.0f, true));
    }

    // The 0 dB line the cut curves hang from.
    g.setColour (t.zeroLine);
    g.fillRect (plot.getX(), geo.zeroY - 0.5f, plot.getWidth(), 1.0f);

    auto curve = [&] (const std::array<float, DisplayModel::spectrumPoints>& y, juce::Colour colour, bool filled)
    {
        if (n < 2)
            return;

        // A flat stretch (most of a curve, most of the time) is one segment.
        auto count = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto flat = i > 0 && i < n - 1 && std::abs (y[(size_t) i] - y[(size_t) i - 1]) < 0.01f
                              && std::abs (y[(size_t) i + 1] - y[(size_t) i]) < 0.01f;
            if (! flat)
                points[(size_t) count++] = { tr.x[(size_t) i], y[(size_t) i] };
        }

        if (filled)
        {
            cutArea.clear();
            cutArea.startNewSubPath (points[0].x, geo.zeroY);
            for (int i = 0; i < count; ++i)
                cutArea.lineTo (points[(size_t) i]);
            cutArea.lineTo (points[(size_t) count - 1].x, geo.zeroY);
            cutArea.closeSubPath();
            g.setColour (t.essFill.withAlpha (0.2f));
            g.fillPath (cutArea);
        }
        g.setColour (colour);
        g.fillPath (ribbon (points.data(), count, 2.0f));
    };

    if (tr.harsh)
        curve (tr.harshY, t.harshText, false);
    curve (tr.essY, t.essText, true);

    // Handles: discs in the band's ink, filled for the selected band, hollow for the other, larger under the pointer or
    // while dragged; a ring of canvas parts them from the band's tint and the curve they sit on.
    auto handle = [&] (T target, juce::Colour colour, bool selected)
    {
        const auto p = scales::handlePosition (geo, m, target);
        const auto active = hover == target || (dragging && pressed == target);
        const auto r = handleInk + (active ? 1.5f : 0.0f);
        auto disc = [&] (float radius) { return juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (p); };
        g.setColour (t.canvas);
        g.fillEllipse (disc (r + handleRing));
        g.setColour (colour);
        if (selected)
            g.fillEllipse (disc (r));
        else
            g.drawEllipse (disc (r - 1.0f), 2.0f);
    };

    if (m.harshShown)
        handle (T::harshCentre, t.harshText, ! essSelected);
    handle (T::essLow, t.essText, essSelected);
    handle (T::essHigh, t.essText, essSelected);

    // Titles: the band names with the cut they take now, as cached text.
    const auto titleFont = caps (*fonts, type::caps);
    auto label = [&] (const juce::String& text, float x, juce::Colour colour)
    {
        const auto w = labels.widthOf (text, titleFont) + 2.0f;
        labels.draw (g, { x, 8.0f, w, 12.0f }, text, juce::Colours::transparentBlack, colour, titleFont, 0.0f);
        return w;
    };

    // Under 64 px of plot the input and the 0 dB line cross the titles' row: the handles and curves say it all.
    if (plot.getHeight() < 64.0f)
        return;

    // In a low spectrum the 0 dB line rises to the titles: start them past the edge handles.
    const auto handleTop = geo.zeroY - handleInk - 1.5f - handleRing;
    const auto inset = handleTop < 21.0f ? scales::handleRadius + 5.5f : 6.0f;
    const auto essX = geo.xOf (m.bands.essLow) + inset;
    const auto essW = label (tr.essTitle, essX, t.essText);
    // Over the fill: in a low spectrum the input rises into the caption's row.
    label ("SPECTRUM", 10.0f, t.secondary);

    if (tr.harsh)
    {
        const auto harshX = geo.xOf (m.bands.harshLow) + inset;
        const auto w = labels.widthOf (tr.harshTitle, titleFont) + 2.0f;
        if (harshX + w + 6.0f < essX || harshX > essX + essW + 6.0f)
            label (tr.harshTitle, harshX, t.harshText);
    }
}

//==============================================================================
const char* SpectrumView::parameterFor (T target) const
{
    switch (target)
    {
        case T::essLow:      return lowId;
        case T::essHigh:     return lowId == params::detectLow ? params::detectHigh : params::detectLow;
        case T::harshCentre: return params::harshFreq;
        case T::harshLow:
        case T::harshHigh:   return params::harshWidth;
        case T::none:
        case T::essBand:
        case T::harshBand:   break;
    }
    return nullptr;
}

HelpText SpectrumView::helpFor (T target) const
{
    HelpText h;
    switch (target)
    {
        case T::essLow:      h = withHint (ui::helpFor (params::detectLow), "drag the handle sideways"); break;
        case T::essHigh:     h = withHint (ui::helpFor (params::detectHigh), "drag the handle sideways"); break;
        case T::harshCentre: h = withHint (ui::helpFor (params::harshFreq), "drag the handle sideways"); break;
        case T::harshLow:
        case T::harshHigh:   h = withHint (ui::helpFor (params::harshWidth), "drag an edge to change it"); break;
        case T::essBand:     h = { "Ess band", "Click to show its controls; drag its handles to move its edges." }; break;
        case T::harshBand:   h = { "Harsh band", "Click to show its controls; drag its centre or an edge." }; break;
        case T::none:        break;
    }
    return h;
}

void SpectrumView::setHover (T target)
{
    if (target == hover)
        return;

    hover = target;
    setMouseCursor (isHandle (target) ? juce::MouseCursor::LeftRightResizeCursor
                    : target == T::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);

    if (target != T::none)
    {
        reportHelp (*this, helpFor (target), true, false);
        helpShown = true;
    }
    else if (helpShown)
    {
        reportHelp (*this, {}, false, false);
        helpShown = false;
    }
    repaint();
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    setHover (scales::hitTest (geometry(), marks(), e.position, controller.getBand() == Band::harsh));
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    if (! dragging)
        setHover (T::none);
}

void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    const auto geo = geometry();
    const auto m = marks();
    pressed = scales::hitTest (geo, m, e.position, controller.getBand() == Band::harsh);
    dragging = false;
    fine = false;

    if (! isHandle (pressed) || e.mods.isPopupMenu())
        return;

    // Grabbing a band's handle selects the band.
    const auto band = isHarsh (pressed) ? Band::harsh : Band::ess;
    if (controller.getBand() != band)
        controller.setBand (band);

    lowId = controller.plain (params::detectLow) <= controller.plain (params::detectHigh) ? params::detectLow : params::detectHigh;
    dragId = parameterFor (pressed);
    anchorPointer = e.position.x;
    anchorHandle = scales::handlePosition (geo, m, pressed).x;
}

void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    if (! isHandle (pressed) || dragId == nullptr)
        return;

    if (! dragging)
    {
        if (e.getDistanceFromDragStart() < 3)
            return;
        dragging = true;
        controller.beginEdit (dragId);
    }

    const auto geo = geometry();

    // Shift drags a fifth as far; toggling it mid-drag re-anchors where the handle is.
    if (e.mods.isShiftDown() != fine)
    {
        anchorHandle = anchorHandle + (e.position.x - anchorPointer) * (fine ? interaction::fineFactor : 1.0f);
        anchorPointer = e.position.x;
        fine = e.mods.isShiftDown();
    }

    const auto x = anchorHandle + (e.position.x - anchorPointer) * (fine ? interaction::fineFactor : 1.0f);
    const auto hz = geo.hzAt (x);
    float value = 0.0f;

    if (pressed == T::harshLow || pressed == T::harshHigh)
        value = (float) scales::harshWidthFor (controller.plain (params::harshFreq), hz);
    else
        value = (float) hz;

    auto& p = controller.parameter (dragId);
    p.setValueNotifyingHost (p.convertTo0to1 (p.getNormalisableRange().snapToLegalValue (value)));
    repaint();
}

void SpectrumView::mouseUp (const juce::MouseEvent& e)
{
    if (dragging && dragId != nullptr)
        controller.endEdit (dragId);
    else if (e.getDistanceFromDragStart() < 3 && ! e.mods.isPopupMenu())
    {
        if (pressed == T::essBand && controller.getBand() != Band::ess)
            controller.setBand (Band::ess);
        else if (pressed == T::harshBand && controller.getBand() != Band::harsh)
            controller.setBand (Band::harsh);
    }

    dragging = false;
    dragId = nullptr;
    pressed = T::none;
    setHover (scales::hitTest (geometry(), marks(), e.position, controller.getBand() == Band::harsh));
    repaint();
}

} // namespace halite::ui
