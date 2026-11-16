#include "ui/MainView.h"

#include "plugin/PluginProcessor.h"
#include "ui/Widgets.h"

namespace halite::ui
{

namespace
{
    juce::Colour lineColour (Band b) { return b == Band::harsh ? theme::current().harshText : theme::current().essText; }
    juce::Colour fillColour (Band b) { return b == Band::harsh ? theme::current().harshFill : theme::current().essFill; }
}

//==============================================================================
ThresholdLines::ThresholdLines (juce::Component& o, HaliteController& c)
    : owner (o), controller (c)
{
}

ThresholdLines::~ThresholdLines()
{
    if (dragging.has_value() && dragId != nullptr)
        controller.endEdit (dragId);
}

bool ThresholdLines::shows (Band b) const
{
    return b == Band::ess || (! essOnly && (controller.harshOn() || controller.getBand() == Band::harsh));
}

Band ThresholdLines::primary() const
{
    return essOnly ? Band::ess : controller.getBand();
}

float ThresholdLines::valueOf (Band b) const
{
    return controller.plain (controller.thresholdId (b));
}

float ThresholdLines::lineY (Band b) const
{
    return ys[(size_t) b];
}

HelpText ThresholdLines::helpFor (Band b) const
{
    return withHint (ui::helpFor (controller.thresholdId (b)), "drag the line, or double-click to type");
}

void ThresholdLines::paint (juce::Graphics& g, const scales::DbAxis& a, float left, float right)
{
    axis = a;
    const auto& t = theme::current();
    const auto selected = primary();
    const auto unit = controller.detection() == Detection::voice ? juce::String (" dB") : juce::String (" dBFS");

    // The other band's line first, quieter and without a chip.
    for (const auto b : { selected == Band::ess ? Band::harsh : Band::ess, selected })
    {
        chips[(size_t) b] = {};
        if (! shows (b))
        {
            ys[(size_t) b] = -1000.0f;
            continue;
        }

        const auto y = std::round (axis.yOf (valueOf (b))) + 0.5f;
        ys[(size_t) b] = y;

        const auto primary = b == selected;
        const auto off = b == Band::harsh && ! controller.harshOn();
        auto colour = lineColour (b).withMultipliedAlpha (primary ? 1.0f : 0.5f);
        if (off)
            colour = colour.withMultipliedAlpha (0.5f);

        dashes[(size_t) b].draw (g, left, right, y, primary ? 1.2f : 1.0f, colour);

        if (! primary)
            continue;

        const auto text = scales::signedDb (valueOf (b)) + unit;
        const auto font = fonts->monoMedium (type::control);
        const auto w = chipImages.widthOf (text, font) + 14.0f;
        auto chip = juce::Rectangle<float> (right - 10.0f - w, y - 8.0f, w, 16.0f);
        chip.setY (juce::jlimit (axis.top - 8.0f, std::max (axis.top - 8.0f, axis.bottom - 8.0f), chip.getY()));
        chips[(size_t) b] = chip;

        if (isEntryOpen() && entryBand == b)
            continue;

        // Harsh off dims the chip's fill below what onAccent reads on in Dark, so its text turns to `text` (Settled 161).
        chipImages.draw (g, chip, text, off ? fillColour (b).withMultipliedAlpha (theme::dimmedChipAlpha) : fillColour (b),
                         off ? t.text : t.onAccent, font, chip.getHeight() * 0.5f);
    }
}

std::optional<Band> ThresholdLines::hit (juce::Point<float> p) const
{
    const auto selected = primary();
    const auto other = selected == Band::ess ? Band::harsh : Band::ess;

    if (chips[(size_t) selected].expanded (2.0f).contains (p))
        return selected;

    for (const auto b : { selected, other })
        if (shows (b) && std::abs (p.y - ys[(size_t) b]) <= grab)
            return b;

    return std::nullopt;
}

bool ThresholdLines::mouseDown (const juce::MouseEvent& e)
{
    if (isEntryOpen())
        finishEntry (true);

    const auto b = hit (e.position);
    if (! b.has_value() || e.mods.isPopupMenu())
        return false;

    if (controller.getBand() != *b)
        controller.setBand (*b);

    // The gesture begins once the pointer moves (a click or a double-click is no edit).
    dragging = *b;
    dragId = nullptr;
    grabOffset = ys[(size_t) *b] - e.position.y;
    fine = false;
    return true;
}

void ThresholdLines::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging.has_value())
        return;

    if (dragId == nullptr)
    {
        if (e.getDistanceFromDragStart() < 3)
            return;
        dragId = controller.thresholdId (*dragging);
        controller.beginEdit (dragId);
    }

    const auto y = e.position.y + grabOffset;
    if (e.mods.isShiftDown() != fine)
    {
        fine = e.mods.isShiftDown();
        fineStartY = y;
        fineStartDb = controller.plain (dragId);
    }

    const auto db = fine ? fineStartDb + (axis.dbAt (y) - axis.dbAt (fineStartY)) * interaction::fineFactor : axis.dbAt (y);
    auto& p = controller.parameter (dragId);
    p.setValueNotifyingHost (p.convertTo0to1 (juce::jlimit (axis.lowDb, axis.highDb, db)));
    owner.repaint();
}

void ThresholdLines::mouseUp (const juce::MouseEvent&)
{
    if (dragId != nullptr)
        controller.endEdit (dragId);

    dragging.reset();
    dragId = nullptr;
}

bool ThresholdLines::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto b = hit (e.position))
    {
        openEntry (*b);
        return true;
    }
    return false;
}

void ThresholdLines::openEntry (Band b)
{
    const auto& t = theme::current();

    if (entry == nullptr)
    {
        entry = std::make_unique<juce::TextEditor> ("threshold");
        entry->setIndents (6, 0);
        entry->setBorder ({ 1, 1, 1, 1 });
        entry->setJustification (juce::Justification::centredRight);
        entry->onReturnKey = [this] { finishEntry (true); };
        entry->onEscapeKey = [this] { finishEntry (false); };
        entry->onFocusLost = [this] { finishEntry (true); };
        owner.addChildComponent (*entry);
    }

    if (controller.getBand() != b)
        controller.setBand (b);

    entryBand = b;
    auto& f = *entry;
    f.setColour (juce::TextEditor::backgroundColourId, t.raised);
    f.setColour (juce::TextEditor::textColourId, t.text);
    f.setColour (juce::TextEditor::highlightColourId, t.essFill.withAlpha (0.35f));
    f.setColour (juce::TextEditor::highlightedTextColourId, t.text);
    f.setColour (juce::TextEditor::outlineColourId, t.focus);
    f.setColour (juce::TextEditor::focusedOutlineColourId, t.focus);
    f.setColour (juce::CaretComponent::caretColourId, t.focus);
    f.setFont (fonts->mono (type::control));
    f.setTitle (helpFor (b).title + " value");
    f.setText (controller.text (controller.thresholdId (b)), false);

    auto area = chips[(size_t) b];
    if (area.isEmpty())
        area = { (float) owner.getWidth() - 100.0f, ys[(size_t) b] - 9.0f, 90.0f, 18.0f };
    area = area.withSizeKeepingCentre (std::max (area.getWidth(), 96.0f), 20.0f);
    area.setX (std::min (area.getX(), (float) owner.getWidth() - area.getWidth() - 4.0f));
    f.setBounds (area.toNearestInt());
    f.setVisible (true);
    f.toFront (false);
    f.selectAll();

    if (owner.isShowing())
        f.grabKeyboardFocus();
    owner.repaint();
}

void ThresholdLines::finishEntry (bool commit)
{
    if (! isEntryOpen())
        return;

    const auto typed = entry->getText();
    entry->setVisible (false);

    if (commit)
        controller.setFromText (controller.thresholdId (entryBand), typed);

    owner.repaint();
}

void ThresholdLines::commitEntryForTest (const juce::String& text)
{
    if (isEntryOpen())
    {
        entry->setText (text, false);
        finishEntry (true);
    }
}

//==============================================================================
HistoryView::HistoryView (HaliteController& c, DisplayModel& m)
    : controller (c), model (m), thresholds (*this, c)
{
    setOpaque (true);
}

void HistoryView::ask (juce::Rectangle<int> area)
{
    if (area.isEmpty())
        area = getLocalBounds();
    ++repaintsAsked;
    repaint (area);
    askedArea.add (area);
}

std::int64_t HistoryView::endColumn (const scales::ColumnMap& map) const
{
    return map.columnOf (model.viewEnd() + 1) - 1;
}

void HistoryView::showHelp (const HelpText& h)
{
    reportHelp (*this, h, true, false);
    helpShown = true;
}

void HistoryView::hideHelp()
{
    if (helpShown)
        reportHelp (*this, {}, false, false);
    helpShown = false;
}

void HistoryView::mouseMove (const juce::MouseEvent& e)
{
    if (const auto b = thresholds.hit (e.position))
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        showHelp (thresholds.helpFor (*b));
        return;
    }

    setMouseCursor (juce::MouseCursor::NormalCursor);
    hideHelp();
}

void HistoryView::mouseExit (const juce::MouseEvent&)
{
    if (! thresholds.isDragging())
        hideHelp();
}

void HistoryView::mouseDown (const juce::MouseEvent& e)
{
    pressed = ! thresholds.mouseDown (e);
}

void HistoryView::mouseDrag (const juce::MouseEvent& e)
{
    thresholds.mouseDrag (e);
}

void HistoryView::mouseUp (const juce::MouseEvent& e)
{
    if (thresholds.isDragging())
        thresholds.mouseUp (e);
    else if (pressed && e.getDistanceFromDragStart() < 3)
        clickedAt (e.position);

    pressed = false;
}

void HistoryView::mouseDoubleClick (const juce::MouseEvent& e)
{
    thresholds.mouseDoubleClick (e);
}

//==============================================================================
MainView::MainView (HaliteController& c, DisplayModel& m)
    : controller (c), timeline (c, m), cloud (c, m)
{
    setTitle ("Main view");
    setOpaque (true);
    addAndMakeVisible (timeline);
    addChildComponent (cloud);
}

HistoryView& MainView::current() noexcept
{
    return controller.getDisplayView() == DisplayView::cloud ? static_cast<HistoryView&> (cloud) : timeline;
}

void MainView::update()
{
    const auto isCloud = controller.getDisplayView() == DisplayView::cloud;
    timeline.setVisible (! isCloud);
    cloud.setVisible (isCloud);
    current().update();
}

void MainView::resized()
{
    timeline.setBounds (getLocalBounds());
    cloud.setBounds (getLocalBounds());
}

} // namespace halite::ui
