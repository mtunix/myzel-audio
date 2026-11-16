#include "ui/DisplayParts.h"

#include "plugin/PluginProcessor.h"
#include "ui/DisplayScales.h"

#include <limits>

namespace halite::ui
{

namespace
{
    juce::Font caps (const Fonts& fonts, float px)
    {
        return fonts.sansMedium (px).withExtraKerningFactor (0.08f);
    }

    const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
    const juce::String inside (juce::CharPointer_UTF8 ("\xc2\xb7"));
    constexpr float inset = 14.0f;

    /** A read-only text for screen readers (totals, the histogram, the list's marked row). */
    class TextValue final : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit TextValue (std::function<juce::String()> f) : read (std::move (f)) {}

        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return read(); }
        void setValueAsString (const juce::String&) override {}

    private:
        std::function<juce::String()> read;
    };

    std::unique_ptr<juce::AccessibilityHandler> textHandler (juce::Component& c, juce::AccessibilityRole role, std::function<juce::String()> value,
                                                             juce::AccessibilityActions actions = {})
    {
        return std::make_unique<juce::AccessibilityHandler> (c, role, std::move (actions),
                                                             juce::AccessibilityHandler::Interfaces { std::make_unique<TextValue> (std::move (value)) });
    }

    /** "+9.4 dB" over the voice in Voice, "−23.1 dBFS" in Allround. */
    juce::String levelText (float db, Detection d)
    {
        return scales::signedDb (db) + (d == Detection::voice ? " dB" : " dBFS");
    }
}

//==============================================================================
EventTotals::EventTotals (DisplayModel& m)
    : model (m)
{
    setTitle ("Totals");
    setDescription (helpFor ("totals").text);
    setOpaque (true);
}

bool EventTotals::update()
{
    if (model.rowsGeneration() == shownGeneration)
        return false;

    // texts() reads the model at paint time: a new generation repaints only if what it reads changed.
    shownGeneration = model.rowsGeneration();
    if (texts() == shownTexts)
        return false;

    shownTexts = texts();
    ++repaints;
    repaint();
    return true;
}

std::array<juce::String, 3> EventTotals::texts() const
{
    const auto& t = model.totals();
    if (t.caught == 0)
        return { "0", dash, dash };
    return { juce::String (t.caught), scales::signedDb (-t.averageCutDb), scales::signedDb (-t.largestCutDb) };
}

void EventTotals::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    g.fillAll (t.panel);
    auto area = getLocalBounds().toFloat().reduced (inset, 0.0f).withTrimmedTop (10.0f);
    const auto column = area.getWidth() / 3.0f;
    const auto values = texts();
    const char* captions[] { "caught", "avg dB", "max dB" };

    for (int i = 0; i < 3; ++i)
    {
        auto cell = area.removeFromLeft (column);
        // The largest cut in the accent's ink, as the list's cuts.
        g.setColour (i == 2 && values[2] != dash ? t.essText : t.text);
        g.setFont (fonts->mono (type::total));
        g.drawText (values[(size_t) i], cell.removeFromTop (22.0f), juce::Justification::centredLeft, false);
        g.setColour (t.secondary);
        g.setFont (fonts->sans (type::caps));
        g.drawText (captions[i], cell.removeFromTop (14.0f), juce::Justification::centredLeft, false);
    }
}

void EventTotals::mouseEnter (const juce::MouseEvent&)
{
    reportHelp (*this, helpFor ("totals"), true, false);
}

void EventTotals::mouseExit (const juce::MouseEvent&)
{
    reportHelp (*this, {}, false, false);
}

std::unique_ptr<juce::AccessibilityHandler> EventTotals::createAccessibilityHandler()
{
    return textHandler (*this, juce::AccessibilityRole::staticText, [this]
    {
        const auto& t = model.totals();
        if (t.caught == 0)
            return juce::String ("No esses caught in the last minute");
        return juce::String (t.caught) + (t.caught == 1 ? " ess" : " esses") + " caught in the last minute, average cut "
               + juce::String (t.averageCutDb, 1) + " dB, largest " + juce::String (t.largestCutDb, 1) + " dB";
    });
}

//==============================================================================
EventList::EventList (HaliteController& c, DisplayModel& m)
    : controller (c), model (m)
{
    setTitle ("Events");
    setDescription (helpFor ("eventList").text);
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);   // as the kit: the focus ring means the keyboard is here
}

juce::String EventList::clockText (double seconds)
{
    const auto centis = (std::int64_t) std::floor (std::abs (seconds) * 100.0 + 1.0e-6);
    const auto text = juce::String (centis / 6000) + ":" + juce::String ((int) ((centis / 100) % 60)).paddedLeft ('0', 2) + "."
                      + juce::String ((int) (centis % 100)).paddedLeft ('0', 2);
    return seconds < 0.0 && centis > 0 ? withMinus ("-" + text) : text;
}

std::array<juce::String, 3> EventList::textsOf (const DisplayModel::Row& r)
{
    return { (r.songPosition ? juce::String() : juce::String ("~")) + clockText (r.seconds), scales::signedDb (r.peakDb),
             r.caught ? scales::signedDb (-r.cutDb) : dash };
}

int EventList::firstShown() const noexcept
{
    const auto n = (int) model.rows().size();
    return std::max (0, n - visibleRows() - scroll);
}

juce::Rectangle<int> EventList::rowArea (int index) const
{
    const auto first = firstShown();
    if (index < first || index >= std::min ((int) model.rows().size(), first + visibleRows()))
        return {};
    return { 0, (index - first) * rowHeight, getWidth(), rowHeight };
}

int EventList::rowAt (juce::Point<int> p) const noexcept
{
    if (p.y < 0 || p.x < 0 || p.x >= getWidth())
        return -1;
    const auto index = firstShown() + p.y / rowHeight;
    return p.y / rowHeight < visibleRows() && index < (int) model.rows().size() ? index : -1;
}

int EventList::markedRow() const noexcept
{
    const auto mark = model.selected();
    if (! mark.has_value())
        return -1;

    const auto& rows = model.rows();
    for (int i = (int) rows.size(); --i >= 0;)
        if (mark->overlaps (rows[(size_t) i].span.first, rows[(size_t) i].span.last))
            return i;
    return -1;
}

bool EventList::update (bool readRows)
{
    const auto& rows = model.rows();
    const auto newest = rows.empty() ? (std::int64_t) -1 : rows.back().span.first;
    const auto mark = model.selected();

    if ((! readRows || model.rowsGeneration() == shownGeneration) && mark == shownMark)
        return false;

    // Scrolled back, the rows in view stay in view as new ones arrive below them.
    if (scroll > 0 && newest != shownNewest && shownNewest >= 0)
    {
        auto arrived = 0;
        for (int i = (int) rows.size(); --i >= 0 && rows[(size_t) i].span.first > shownNewest;)
            ++arrived;
        scroll += arrived;
    }
    scroll = juce::jlimit (0, std::max (0, (int) rows.size() - visibleRows()), scroll);

    shownGeneration = model.rowsGeneration();
    shownNewest = newest;
    shownMark = mark;
    ++repaints;
    repaint();
    return true;
}

void EventList::choose (int index)
{
    const auto& rows = model.rows();
    if (! juce::isPositiveAndBelow (index, (int) rows.size()))
        return;

    model.reveal (rows[(size_t) index].span);
    scrollTo (index);
    update();
}

void EventList::scrollBy (int rows)
{
    // Rows that arrived since the last read are placed first, so the scroll counts from what is drawn.
    update();
    const auto limit = std::max (0, (int) model.rows().size() - visibleRows());
    const auto next = juce::jlimit (0, limit, scroll + rows);
    if (next != scroll)
    {
        scroll = next;
        repaint();
    }
}

void EventList::scrollTo (int index)
{
    const auto first = firstShown(), shown = visibleRows();
    if (index < first)
        scrollBy (first - index);
    else if (index >= first + shown)
        scrollBy (first + shown - 1 - index);
}

void EventList::resized()
{
    scroll = juce::jlimit (0, std::max (0, (int) model.rows().size() - visibleRows()), scroll);
}

void EventList::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto& rows = model.rows();
    const auto scale = std::max (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto first = firstShown(), last = std::min ((int) rows.size(), first + visibleRows());
    const auto marked = markedRow();

    if (! juce::approximatelyEqual (scale, imageScale))
    {
        rowImages.clear();
        imageScale = scale;
    }
    for (auto& [record, image] : rowImages)
        image.used = false;

    const auto font = fonts->mono (type::label);
    const auto inner = (float) getWidth() - 2.0f * inset;
    const auto column = inner / 3.0f;

    const auto rowsBottom = (last - first) * rowHeight;
    g.setColour (t.panel);
    g.fillRect (0, rowsBottom, getWidth(), getHeight() - rowsBottom);

    for (int i = first; i < last; ++i)
    {
        const auto& r = rows[(size_t) i];
        const auto area = rowArea (i).toFloat();
        const auto ground = i == marked ? t.selectedTint : i == hovered ? t.hover : t.panel;

        auto& cached = rowImages[r.span.first];
        cached.used = true;
        const auto same = cached.image.isValid() && cached.width == getWidth() && cached.ground == ground.getARGB() && cached.theme == theme::currentName()
                          && cached.row.span == r.span && juce::exactlyEqual (cached.row.seconds, r.seconds) && cached.row.songPosition == r.songPosition
                          && juce::exactlyEqual (cached.row.peakDb, r.peakDb) && juce::exactlyEqual (cached.row.cutDb, r.cutDb) && cached.row.caught == r.caught;
        if (! same)
        {
            const auto texts = textsOf (r);
            const auto timeInk = r.songPosition ? t.text : t.secondary;
            const auto cutInk = r.caught ? t.essText : t.secondary;
            cached.row = r;
            cached.ground = ground.getARGB();
            cached.width = getWidth();
            cached.theme = theme::currentName();
            cached.image = juce::Image (juce::Image::RGB, juce::roundToInt ((float) getWidth() * scale), juce::roundToInt ((float) rowHeight * scale), false);
            juce::Graphics ig (cached.image);
            ig.addTransform (juce::AffineTransform::scale (scale));
            ig.fillAll (ground);
            ig.setColour (t.lineSoft);
            ig.fillRect (0.0f, (float) rowHeight - 1.0f, (float) getWidth(), 1.0f);
            ig.setFont (font);
            auto cells = juce::Rectangle<float> (inset, 0.0f, inner, (float) rowHeight - 1.0f);
            ig.setColour (timeInk);
            ig.drawText (texts[0], cells.removeFromLeft (column), juce::Justification::centredLeft, false);
            ig.setColour (t.text);
            ig.drawText (texts[1], cells.removeFromLeft (column), juce::Justification::centredRight, false);
            ig.setColour (cutInk);
            ig.drawText (texts[2], cells, juce::Justification::centredRight, false);
        }

        // On whole physical pixels: anything else takes the renderer's resampling path.
        g.drawImageTransformed (cached.image, juce::AffineTransform::translation (std::round (area.getX() * scale), std::round (area.getY() * scale))
                                                  .scaled (1.0f / scale));
    }

    for (auto it = rowImages.begin(); it != rowImages.end();)
        it = it->second.used ? std::next (it) : rowImages.erase (it);

    if (rows.empty() && getHeight() >= 30)
    {
        g.setColour (t.secondary);
        g.setFont (fonts->sans (type::label));
        g.drawText ("No esses in the last minute", getLocalBounds(), juce::Justification::centred, false);
    }

    // Scrolled: a slim bar at the right edge shows where the rows in view lie.
    if ((int) rows.size() > visibleRows() && visibleRows() > 0)
    {
        const auto h = (float) getHeight();
        const auto share = (float) visibleRows() / (float) rows.size();
        const auto top = (float) first / (float) rows.size() * h;
        g.setColour (t.line);
        g.fillRoundedRectangle ((float) getWidth() - 5.0f, top + 2.0f, 3.0f, std::max (8.0f, share * h - 4.0f), 1.5f);
    }

    if (hasKeyboardFocus (false))
        draw::focusRing (g, getLocalBounds().toFloat().reduced (3.0f), 4.0f);
}

void EventList::mouseDown (const juce::MouseEvent& e)
{
    const auto index = rowAt (e.getPosition());
    if (index >= 0)
    {
        choose (index);
        return;
    }

    if (model.selected().has_value())
    {
        model.select (std::nullopt);
        update();
    }
}

void EventList::mouseMove (const juce::MouseEvent& e)
{
    update();
    if (const auto index = rowAt (e.getPosition()); index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void EventList::mouseEnter (const juce::MouseEvent& e)
{
    reportHelp (*this, helpFor ("eventList"), true, false);
    mouseMove (e);
}

void EventList::mouseExit (const juce::MouseEvent&)
{
    reportHelp (*this, {}, false, false);
    if (hovered >= 0)
    {
        hovered = -1;
        repaint();
    }
}

void EventList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // Up shows older rows. A notch is about 0.1 to 0.25 of deltaY on most systems: at least a row each.
    const auto rows = wheel.deltaY * 12.0f;
    scrollBy (rows > 0.0f ? std::max (1, juce::roundToInt (rows)) : std::min (-1, juce::roundToInt (rows)));
}

bool EventList::keyPressed (const juce::KeyPress& key)
{
    const auto n = (int) model.rows().size();
    if (n == 0)
        return false;

    const auto current = markedRow();
    auto target = -1;

    if (key == juce::KeyPress::upKey)
        target = current < 0 ? n - 1 : std::max (0, current - 1);
    else if (key == juce::KeyPress::downKey)
        target = current < 0 ? n - 1 : std::min (n - 1, current + 1);
    else if (key == juce::KeyPress::pageUpKey)
        target = std::max (0, (current < 0 ? n - 1 : current) - std::max (1, visibleRows() - 1));
    else if (key == juce::KeyPress::pageDownKey)
        target = std::min (n - 1, (current < 0 ? n - 1 : current) + std::max (1, visibleRows() - 1));
    else if (key == juce::KeyPress::homeKey)
        target = 0;
    else if (key == juce::KeyPress::endKey)
        target = n - 1;
    else if (key == juce::KeyPress::returnKey && current >= 0)
        target = current;
    else if (key == juce::KeyPress::escapeKey && current >= 0)
    {
        model.select (std::nullopt);
        update();
        return true;
    }

    if (target < 0)
        return false;

    choose (target);
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    return true;
}

void EventList::focusGained (FocusChangeType)
{
    reportHelp (*this, helpFor ("eventList"), true, true);
    repaint();
}

void EventList::focusLost (FocusChangeType)
{
    reportHelp (*this, {}, false, true);
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> EventList::createAccessibilityHandler()
{
    return textHandler (*this, juce::AccessibilityRole::list, [this]
    {
        const auto& rows = model.rows();
        const auto marked = markedRow();
        if (marked < 0)
            return juce::String ((int) rows.size()) + (rows.size() == 1 ? " ess" : " esses") + " in the last minute";

        const auto& r = rows[(size_t) marked];
        const auto texts = textsOf (r);
        return texts[0] + ", peak " + levelText (r.peakDb, controller.detection()) + ", "
               + (r.caught ? "cut " + juce::String (r.cutDb, 1) + " dB" : juce::String ("left alone"));
    });
}

//==============================================================================
LearnHistogram::LearnHistogram (HaliteController& c, DisplayModel& m)
    : controller (c), model (m)
{
    setTitle ("Learn histogram");
    setDescription (helpFor ("learnHistogram").text);
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

juce::Range<float> LearnHistogram::axis() const noexcept
{
    // The candidates' own floor up to the threshold's top in Voice; the whole absolute range in Allround.
    const dsp::LearnTuning tuning;
    return shownDetection == Detection::voice ? juce::Range<float> (tuning.floorDb, params::thresholdMaxDb)
                                              : juce::Range<float> (tuning.floorAbsDb, 0.0f);
}

bool LearnHistogram::update()
{
    auto& learner = controller.getProcessor().getLearn();
    const auto& learn = controller.getProcessor().getChain().learn();
    const auto state = learner.state();

    const auto next = state == LearnState::listening ? Mode::listening
                      : state == LearnState::learned ? Mode::learned
                      : state == LearnState::tooFew  ? Mode::tooFew
                                                     : Mode::live;

    const auto capture = next != Mode::live;
    const auto detection = capture ? learn.capturedDetection() : controller.detection();
    const auto progress = next == Mode::listening ? learner.progress() : 0.0f;

    // Frozen, the live histogram holds with the rest of the display; Learn's own states always show.
    auto bars = shownBars;
    auto counted = shownTotal;
    auto median = shownMedian;
    if (! (next == Mode::live && model.isFrozen() && shownMode == Mode::live && shownTotal >= 0 && detection == shownDetection))
    {
        // Until the audio thread starts the capture its histogram still holds the last one.
        if (next == Mode::listening && progress <= 0.0f)
            counts.fill (0);
        else
            (capture ? learn.capturedEsses() : learn.live()).read (counts);

        shownDetection = detection;
        const auto range = axis();
        const auto barDb = range.getLength() / (float) numBars;
        bars = {};
        auto n = 0u;
        for (int bin = 0; bin < dsp::LevelHistogram::numBins; ++bin)
        {
            if (counts[(size_t) bin] == 0)
                continue;
            const auto db = dsp::LevelHistogram::lowEdgeOf (bin) + 0.5f * dsp::LevelHistogram::binDb;
            bars[(size_t) juce::jlimit (0, numBars - 1, (int) std::floor ((db - range.getStart()) / barDb))] += (int) counts[(size_t) bin];
            n += counts[(size_t) bin];
        }
        counted = (int) n;
        median = n > 0 ? dsp::histogramLevelDb (counts, n, 0.5) : std::numeric_limits<float>::quiet_NaN();
    }

    const auto threshold = controller.plain (detection == Detection::voice ? params::threshold : params::thresholdAbs);
    const auto secondsLeft = next == Mode::listening ? (int) std::ceil ((1.0f - progress) * (float) learn.getTuning().captureSeconds - 0.001f) : 0;
    const auto progressPx = next == Mode::listening ? juce::roundToInt (progress * barsArea().getWidth()) : -1;
    const auto& result = learner.result();

    const auto sameMedian = (std::isnan (median) && std::isnan (shownMedian)) || std::abs (median - shownMedian) < 0.05f;
    if (next == shownMode && bars == shownBars && counted == shownTotal && sameMedian && std::abs (threshold - shownThreshold) < 0.01f
        && secondsLeft == shownSecondsLeft && progressPx == shownProgressPx && result.esses == shownEsses && result.harshSet == shownHarshSet)
        return false;

    const auto modeChanged = next != shownMode;
    shownMode = next;
    shownBars = bars;
    shownTotal = counted;
    shownMedian = median;
    shownThreshold = threshold;
    shownSecondsLeft = secondsLeft;
    shownProgressPx = progressPx;
    shownEsses = result.esses;
    shownHarshSet = result.harshSet;

    if (modeChanged && hovering)
        reportHelp (*this, help(), true, false);
    ++repaints;
    repaint();
    return true;
}

juce::Rectangle<float> LearnHistogram::barsArea() const
{
    const auto h = (float) getHeight();
    const auto labels = h >= 80.0f;
    return { inset, 30.0f, (float) getWidth() - 2.0f * inset, std::max (8.0f, h - 30.0f - (labels ? 20.0f : 8.0f)) };
}

juce::String LearnHistogram::readout() const
{
    const auto unit = shownDetection == Detection::voice ? juce::String (" dB") : juce::String (" dBFS");
    switch (shownMode)
    {
        case Mode::listening:  return juce::String (shownSecondsLeft) + " s left " + inside + " " + juce::String (std::max (0, shownTotal))
                                      + (shownTotal == 1 ? " ess" : " esses");
        case Mode::tooFew:     return "too few esses";
        case Mode::live:
        case Mode::learned:    break;
    }
    return std::isnan (shownMedian) ? "median " + dash : "median " + scales::signedDb (shownMedian) + unit;
}

HelpText LearnHistogram::help() const
{
    return helpFor (shownMode == Mode::learned || shownMode == Mode::tooFew ? "learnHistogramDone" : "learnHistogram");
}

void LearnHistogram::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const auto header = juce::Rectangle<float> (inset, 8.0f, bounds.getWidth() - 2.0f * inset, 14.0f);
    const auto done = shownMode == Mode::learned || shownMode == Mode::tooFew;
    g.fillAll (t.panel);
    g.setColour (t.lineSoft);
    g.fillRect (0.0f, 0.0f, bounds.getWidth(), 1.0f);

    g.setFont (caps (*fonts, type::caps));
    g.setColour (shownMode == Mode::live ? t.secondary : t.essText);
    g.drawText (shownMode == Mode::listening ? "LISTENING" : shownMode == Mode::learned ? "LEARNED" : "LEARN", header,
                juce::Justification::centredLeft, false);
    g.setColour (shownMode == Mode::tooFew ? t.text : t.secondary);
    g.setFont (fonts->sans (type::caps));
    g.drawText (readout(), header, juce::Justification::centredRight, false);

    const auto area = barsArea();
    const auto range = axis();
    auto xOf = [&] (float db) { return area.getX() + (db - range.getStart()) / range.getLength() * area.getWidth(); };

    // Bars, each over its share of the axis; those over the threshold in the accent. A few esses stay short.
    const auto tallest = std::max (5, *std::max_element (shownBars.begin(), shownBars.end()));
    const auto pitch = area.getWidth() / (float) numBars;
    const auto barWidth = std::max (2.0f, std::round (pitch - 2.6f));
    for (int b = 0; b < numBars; ++b)
    {
        if (shownBars[(size_t) b] == 0)
            continue;
        const auto h = std::max (2.0f, std::round ((float) shownBars[(size_t) b] / (float) tallest * (area.getHeight() - 4.0f)));
        const auto centre = range.getStart() + ((float) b + 0.5f) * range.getLength() / (float) numBars;
        const auto x = std::round (area.getX() + (float) b * pitch + 0.5f * (pitch - barWidth));
        g.setColour (centre >= shownThreshold ? t.essFill : t.signal);
        g.fillRoundedRectangle (x, area.getBottom() - h, barWidth, h, 1.0f);
    }

    // The threshold across it, held at the axis' edge when it lies beyond; knocked out of the bars by a pixel
    // of panel each side, since in Dark the line and the accent's fill are one colour.
    const auto tx = juce::jlimit (area.getX(), area.getRight() - 2.0f, std::round (xOf (shownThreshold)) - 1.0f);
    g.setColour (t.panel);
    g.fillRect (tx - 1.0f, area.getY() - 2.0f, 4.0f, area.getHeight() + 2.0f);
    g.setColour (t.essText);
    g.fillRect (tx, area.getY() - 2.0f, 2.0f, area.getHeight() + 3.0f);

    g.setColour (t.line);
    g.fillRect (area.getX(), area.getBottom(), area.getWidth(), 1.0f);
    if (shownMode == Mode::listening && shownProgressPx > 0)
    {
        g.setColour (t.essText);
        g.fillRect (area.getX(), area.getBottom() - 0.5f, (float) shownProgressPx, 2.0f);
    }

    if (getHeight() >= 80)
    {
        g.setColour (t.secondary);
        g.setFont (fonts->mono (type::meter));
        const auto labels = area.withY (area.getBottom() + 4.0f).withHeight (11.0f);
        auto whole = [] (float db) { return withMinus ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db))); };
        g.drawText (whole (range.getStart()), labels, juce::Justification::centredLeft, false);
        g.drawText (whole (range.getStart() + 0.5f * range.getLength()), labels, juce::Justification::centred, false);
        g.drawText (whole (range.getEnd()), labels, juce::Justification::centredRight, false);
    }

    // A message where the bars say nothing yet, or say too little.
    auto message = [&] (const juce::String& first, const juce::String& second)
    {
        auto box = area.reduced (0.0f, 2.0f);
        g.setFont (fonts->sans (type::label));
        g.setColour (t.text);
        g.drawText (first, second.isEmpty() ? box : box.removeFromTop (box.getHeight() * 0.5f), second.isEmpty() ? juce::Justification::centred
                                                                                                                   : juce::Justification::centredBottom, false);
        if (second.isNotEmpty())
        {
            g.setColour (t.secondary);
            g.drawText (second, box, juce::Justification::centredTop, false);
        }
    };

    const auto capture = juce::String (juce::roundToInt (dsp::LearnTuning {}.captureSeconds)) + " s";
    if (shownMode == Mode::tooFew)
        message (shownEsses == 0 ? "No esses in " + capture : "Only " + juce::String (shownEsses) + (shownEsses == 1 ? " ess in " : " esses in ") + capture,
                 shownHarshSet ? "Threshold stays; Harsh threshold set" : "Threshold stays");
    else if (shownTotal == 0 && shownMode == Mode::live && area.getHeight() >= 14.0f)
        message ("No esses in the last minute", {});

    if (done && hovering)
    {
        g.setColour (t.line);
        g.drawRoundedRectangle (bounds.reduced (4.0f, 3.0f), 4.0f, 1.0f);
    }

    if (hasKeyboardFocus (false))
        draw::focusRing (g, bounds.reduced (4.0f, 3.0f), 4.0f);
}

bool LearnHistogram::dismiss()
{
    auto& learner = controller.getProcessor().getLearn();
    if (learner.state() != LearnState::learned && learner.state() != LearnState::tooFew)
        return false;

    learner.dismiss();
    update();
    return true;
}

void LearnHistogram::mouseDown (const juce::MouseEvent&)
{
    dismiss();
}

void LearnHistogram::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    reportHelp (*this, help(), true, false);
    repaint();
}

void LearnHistogram::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    reportHelp (*this, {}, false, false);
    repaint();
}

bool LearnHistogram::keyPressed (const juce::KeyPress& key)
{
    return (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) && dismiss();
}

void LearnHistogram::focusGained (FocusChangeType)
{
    reportHelp (*this, help(), true, true);
    repaint();
}

void LearnHistogram::focusLost (FocusChangeType)
{
    reportHelp (*this, {}, false, true);
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> LearnHistogram::createAccessibilityHandler()
{
    juce::AccessibilityActions actions;
    actions.addAction (juce::AccessibilityActionType::press, [this] { dismiss(); });

    return textHandler (*this, juce::AccessibilityRole::staticText, [this]
    {
        const auto threshold = "threshold " + levelText (shownThreshold, shownDetection);
        switch (shownMode)
        {
            case Mode::listening:  return "Learn is listening, " + juce::String (shownSecondsLeft) + " s left, "
                                          + juce::String (std::max (0, shownTotal)) + " esses so far";
            case Mode::tooFew:     return "Too few esses: " + juce::String (shownEsses) + " in 8 s; Threshold stays";
            case Mode::learned:    return "Learned from " + juce::String (shownTotal) + " esses, " + readout() + ", " + threshold;
            case Mode::live:       break;
        }
        return juce::String (std::max (0, shownTotal)) + " esses in the last minute, " + readout() + ", " + threshold;
    }, std::move (actions));
}

//==============================================================================
EventsSidebar::EventsSidebar (HaliteController& c, DisplayModel& m)
    : totals (m), list (c, m), histogram (c, m)
{
    setTitle ("Events");
    setOpaque (true);
    setFocusContainerType (FocusContainerType::focusContainer);
    list.setExplicitFocusOrder (1);
    histogram.setExplicitFocusOrder (2);

    for (auto* part : std::initializer_list<juce::Component*> { &totals, &list, &histogram })
        addAndMakeVisible (part);
}

void EventsSidebar::update (double dt)
{
    // A list read a few times a second is calm and costs a fraction: esses arrive a few a second at most.
    sinceData += dt;
    auto asked = false;
    if (sinceData >= 1.0 / dataHz - 0.002)
    {
        sinceData = 0.0;
        asked = totals.update();
        asked = list.update() || asked;
        asked = histogram.update() || asked;
    }
    else
    {
        asked = list.update (false);
    }

    if (asked)
        ++repaints;
}

void EventsSidebar::resized()
{
    // The list keeps three rows where it can; the histogram gives way down to 56 px (no axis labels under 80).
    const auto h = getHeight();
    const auto learnHeight = juce::jlimit (56, 104, h - totalsHeight - headerHeight - 3 * EventList::rowHeight);
    totals.setBounds (1, 0, getWidth() - 1, totalsHeight);
    list.setBounds (1, totalsHeight + headerHeight + 1, getWidth() - 1, std::max (0, h - learnHeight - totalsHeight - headerHeight - 1));
    histogram.setBounds (1, h - learnHeight, getWidth() - 1, learnHeight);
}

void EventsSidebar::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto area = getLocalBounds().toFloat();
    g.fillAll (t.panel);
    g.setColour (t.line);
    g.fillRect (area.withWidth (1.0f));

    const auto inner = area.reduced (inset, 0.0f);
    const auto column = inner.getWidth() / 3.0f;

    g.setColour (t.lineSoft);
    g.fillRect (area.getX() + 1.0f, (float) totalsHeight, area.getWidth(), 1.0f);

    auto header = inner.withY ((float) totalsHeight + 1.0f).withHeight ((float) headerHeight - 1.0f);
    g.setColour (t.secondary);
    g.setFont (caps (*fonts, type::axis));
    g.drawText ("TIME", header.removeFromLeft (column), juce::Justification::centredLeft, false);
    g.drawText ("PEAK", header.removeFromLeft (column), juce::Justification::centredRight, false);
    g.drawText ("CUT", header.removeFromLeft (column), juce::Justification::centredRight, false);
    g.setColour (t.lineSoft);
    g.fillRect (area.getX() + 1.0f, (float) (totalsHeight + headerHeight), area.getWidth(), 1.0f);
}

} // namespace halite::ui
