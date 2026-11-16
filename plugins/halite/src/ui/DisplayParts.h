#pragma once

#include "dsp/detector/Learn.h"
#include "ui/DisplayModel.h"
#include "ui/HaliteController.h"
#include "ui/MainView.h"
#include "ui/MeterStrip.h"
#include "ui/SpectrumView.h"
#include "ui/Widgets.h"

#include <array>
#include <map>

namespace halite::ui
{

/** The sidebar's totals: esses caught in the last 60 s, their average cut and the largest (DisplayModel::totals). */
class EventTotals final : public juce::Component
{
public:
    explicit EventTotals (DisplayModel&);

    /** Repaints when the totals changed; true if it asked for one. */
    bool update();

    /** "37", "−3.4", "−8.0" (an em dash where there is nothing to average). */
    std::array<juce::String, 3> texts() const;

    /** Repaints update() asked for (the snapshot tool's timing). */
    std::uint32_t frameRepaints() const noexcept { return repaints; }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    DisplayModel& model;
    juce::SharedResourcePointer<Fonts> fonts;
    std::uint32_t shownGeneration = ~0u, repaints = 0;
    std::array<juce::String, 3> shownTexts;
};

/**
    The event list (src/ui/DESIGN.md, Events sidebar): every ess of the last 60 s, caught or left alone,
    newest at the bottom, at most 256. A row reads its time (m:ss.cc song position, or `~` and the
    secondary colour for elapsed time), its peak S and its cut (an em dash for none). A click marks the
    ess in the views and scrolls a frozen display back to it if it lies outside the zoom window; the
    wheel scrolls; Up and Down move the mark, Escape clears it.
*/
class EventList final : public juce::Component
{
public:
    EventList (HaliteController&, DisplayModel&);

    /** Repaints when the rows or the mark changed (`rows` false: the mark alone); true if it asked for one. */
    bool update (bool rows = true);

    static constexpr int rowHeight = 24;

    /** Rows that fit, the first one shown (an index into DisplayModel::rows), and a row's area (empty when not shown). */
    int visibleRows() const noexcept { return std::max (0, getHeight() / rowHeight); }
    int firstShown() const noexcept;
    juce::Rectangle<int> rowArea (int index) const;

    /** The row index the mark overlaps, or −1. */
    int markedRow() const noexcept;

    /** Marks a row's ess and brings it into view (DisplayModel::reveal). */
    void choose (int index);

    /** Rows from the bottom the list is scrolled by (0: following the newest). */
    int scrolled() const noexcept { return scroll; }

    /** Repaints update() asked for (the snapshot tool's timing). */
    std::uint32_t frameRepaints() const noexcept { return repaints; }

    /** A row's three read-outs: time, peak, cut. */
    static std::array<juce::String, 3> textsOf (const DisplayModel::Row&);

    /** m:ss.cc of a time in seconds (a minus sign before the host's pre-roll). */
    static juce::String clockText (double seconds);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    int rowAt (juce::Point<int>) const noexcept;
    void scrollBy (int rows);
    void scrollTo (int index);

    HaliteController& controller;
    DisplayModel& model;
    juce::SharedResourcePointer<Fonts> fonts;

    int scroll = 0, hovered = -1;
    std::uint32_t shownGeneration = ~0u, repaints = 0;
    std::int64_t shownNewest = -1;
    std::optional<DisplayModel::Span> shownMark;

    // A row, opaque, rendered once at the display's scale: a new row moves the others by a copy, no text drawn.
    struct RowImage
    {
        DisplayModel::Row row;
        juce::uint32 ground = 0;
        int width = 0;
        ThemeName theme = ThemeName::dark;
        juce::Image image;
        bool used = false;
    };
    std::map<std::int64_t, RowImage> rowImages;
    float imageScale = 0.0f;
};

/**
    The Learn histogram (PLAN.md, Learn; Settled 54–56, 60): the peak S of the esses of the last 60 s
    (Learn's live histogram), or while Learn listens its capture filling, then the capture marked
    Learned with the new threshold across it, or what too few esses left; bars over the threshold in
    the accent, the threshold as a line, the median as a read-out. A finished capture stays until the
    next edit or a click on it (LearnControl::dismiss).
*/
class LearnHistogram final : public juce::Component
{
public:
    LearnHistogram (HaliteController&, DisplayModel&);

    enum class Mode
    {
        live,
        listening,
        learned,
        tooFew
    };

    /** Reads Learn (lock-free histograms) and repaints on a change; true if it asked for one. */
    bool update();

    static constexpr int numBars = 24;

    Mode mode() const noexcept { return shownMode; }
    int total() const noexcept { return shownTotal; }
    float medianDb() const noexcept { return shownMedian; }                  // NaN with no esses
    const std::array<int, numBars>& bars() const noexcept { return shownBars; }
    juce::Range<float> axis() const noexcept;                                 // dB: Voice −24 … +24, Allround −60 … 0

    /** The header's right-hand read-out ("median +8.5 dB", "5 s left", "too few esses"). */
    juce::String readout() const;

    /** Repaints update() asked for (the snapshot tool's timing). */
    std::uint32_t frameRepaints() const noexcept { return repaints; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    HelpText help() const;

    /** A finished capture leaves for the live histogram (a click, Return or Space); false if there was none. */
    bool dismiss();

private:
    juce::Rectangle<float> barsArea() const;

    HaliteController& controller;
    DisplayModel& model;
    juce::SharedResourcePointer<Fonts> fonts;

    dsp::LevelHistogram::Counts counts {};
    Mode shownMode = Mode::live;
    Detection shownDetection = Detection::voice;
    std::array<int, numBars> shownBars {};
    int shownTotal = -1, shownSecondsLeft = 0, shownProgressPx = -1, shownEsses = 0;
    std::uint32_t repaints = 0;
    float shownMedian = 0.0f, shownThreshold = 0.0f;
    bool shownHarshSet = false, hovering = false;
};

/**
    The events sidebar (260 px; docked or an overlay): totals, the event list, the Learn histogram, all
    read from the display model and Learn once per frame, each repainting only on a change. The other
    display parts: MainView.h, SpectrumView.h, MeterStrip.h.
*/
class EventsSidebar final : public juce::Component
{
public:
    EventsSidebar (HaliteController&, DisplayModel&);

    void setOverlay (bool o) { overlay = o; repaint(); }
    bool isOverlay() const noexcept { return overlay; }

    /** The frame clock (after the model's update): the mark at once, what the data changed at most dataHz times a second. */
    void update (double dt);

    static constexpr double dataHz = 4.0;

    /** Reads the data now (the next update() does, as the frame clock does once dataHz allows). */
    void refresh() noexcept { sinceData = 1.0; }

    EventTotals& getTotals() noexcept { return totals; }
    EventList& getList() noexcept { return list; }
    LearnHistogram& getHistogram() noexcept { return histogram; }

    /** Repaints asked for by any of its parts (each part is opaque and repaints alone). */
    std::uint32_t frameRepaints() const noexcept { return repaints; }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 260;
    static constexpr int totalsHeight = 60, headerHeight = 25;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    EventTotals totals;
    EventList list;
    LearnHistogram histogram;
    bool overlay = false;
    std::uint32_t repaints = 0;
    double sinceData = 1.0;
};

} // namespace halite::ui
