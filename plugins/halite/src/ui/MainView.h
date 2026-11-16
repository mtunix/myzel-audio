#pragma once

#include "ui/DisplayModel.h"
#include "ui/DisplayScales.h"
#include "ui/DrawCache.h"
#include "ui/HaliteController.h"

#include <tuple>

namespace halite::ui
{

/**
    The threshold lines the Timeline and the Cloud share (PLAN.md, Parts): a dashed line per band on
    the sibilance axis, the selected band's with a value chip. Drag a line (Shift fine) or double-click
    it or its chip to type; each drag is one gesture and one undo step on the threshold Detection
    picks (relative or absolute). The Harsh line shows while Harsh is on or selected; grabbing a line
    selects its band.
*/
class ThresholdLines
{
public:
    ThresholdLines (juce::Component& owner, HaliteController&);
    ~ThresholdLines();

    void paint (juce::Graphics&, const scales::DbAxis&, float left, float right);

    /** Whether the point grabs a line (its band), for the cursor and the mouse handlers. */
    std::optional<Band> hit (juce::Point<float>) const;

    bool mouseDown (const juce::MouseEvent&);
    void mouseDrag (const juce::MouseEvent&);
    void mouseUp (const juce::MouseEvent&);
    bool mouseDoubleClick (const juce::MouseEvent&);

    /** Typed entry over the band's chip (double-click; the self-test). */
    void openEntry (Band);
    bool isEntryOpen() const noexcept { return entry != nullptr && entry->isVisible(); }
    void commitEntryForTest (const juce::String&);

    bool isDragging() const noexcept { return dragging.has_value(); }

    /** The threshold the band's line shows, in the axis's unit. */
    float valueOf (Band) const;

    /** Where the band's line and chip were last drawn. */
    float lineY (Band) const;
    juce::Rectangle<float> chipArea (Band b) const { return chips[(size_t) b]; }

    HelpText helpFor (Band) const;

    /** The Cloud's dots are esses: it shows the Ess line alone, whichever band is selected. */
    void setEssOnly (bool e) noexcept { essOnly = e; }

    /** The band whose line carries the chip. */
    Band primary() const;

    static constexpr float grab = 5.0f;

private:
    bool shows (Band) const;
    void finishEntry (bool commit);

    juce::Component& owner;
    HaliteController& controller;
    juce::SharedResourcePointer<Fonts> fonts;

    scales::DbAxis axis;
    std::array<float, 2> ys {};
    std::array<juce::Rectangle<float>, 2> chips {};

    std::optional<Band> dragging;
    const char* dragId = nullptr;
    float grabOffset = 0.0f, fineStartY = 0.0f, fineStartDb = 0.0f;
    bool fine = false;

    std::unique_ptr<juce::TextEditor> entry;
    Band entryBand = Band::ess;
    bool essOnly = false;
    ChipCache chipImages;
    std::array<DashCache, 2> dashes;
};

/**
    The history views' common ground: an image of the data, scrolled by blitting and drawn only in its
    new columns (PLAN.md, Behaviour, Rendering), rebuilt when the size, scale, zoom, theme, mode or the
    model's mirror change.
*/
class HistoryView : public juce::Component
{
public:
    HistoryView (HaliteController&, DisplayModel&);

    /** The frame clock: repaints when the right edge moved a column or what the overlay shows changed. */
    virtual void update() = 0;

    ThresholdLines& getThresholds() noexcept { return thresholds; }

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Full redraws of the image since construction (the self-test checks scrolling does not redraw). */
    int fullRedraws() const noexcept { return rebuilds; }

    /** Repaints the frame clock asked for (the snapshot tool's timing paints only those). */
    std::uint32_t frameRepaints() const noexcept { return repaintsAsked; }

    /** What the frame clock asked to repaint since the last call: the whole view, or the plot alone when it only scrolled. */
    juce::RectangleList<int> takeAskedArea() { return std::exchange (askedArea, {}); }

    /** Paints done, which a real window can coalesce below the repaints asked (the snapshot tool's window timing). */
    std::uint32_t paintsDone() const noexcept { return paints; }

    /** The physical pixel scale the image was last drawn at (the window timing checks the window's scale took). */
    float paintedScale() const noexcept { return cached.scale; }

protected:
    /** What a cached image depends on, apart from the right edge. */
    struct CacheKey
    {
        int width = 0, height = 0, columns = 0;
        float scale = 0.0f;
        scales::ColumnMap map;
        ThemeName theme = ThemeName::dark;
        Detection detection = Detection::voice;
        std::uint32_t generation = 0;
        bool operator== (const CacheKey&) const = default;
    };

    /** The right edge as a column: the last whole column at or before the view's end. */
    std::int64_t endColumn (const scales::ColumnMap&) const;

    /** The frame clock's repaint: the area, or the whole view when it is empty. */
    void ask (juce::Rectangle<int> area = {});

    /** Clicks the view handles itself (an event tag): true if used. */
    virtual bool clickedAt (juce::Point<float>) { return false; }

    void showHelp (const HelpText&);
    void hideHelp();

    HaliteController& controller;
    DisplayModel& model;
    juce::SharedResourcePointer<Fonts> fonts;
    ThresholdLines thresholds;

    juce::Image image;
    CacheKey cached;
    std::int64_t drawnEnd = 0;
    int rebuilds = 0;
    std::uint32_t repaintsAsked = 0, paints = 0;

private:
    juce::RectangleList<int> askedArea;
    bool helpShown = false, pressed = false;
};

/** The Timeline (PLAN.md, Parts): output and removed waveform, the cut, S, the thresholds, tags, the Harsh cut, a time axis. */
class TimelineView final : public HistoryView
{
public:
    TimelineView (HaliteController&, DisplayModel&);

    void update() override;
    void paint (juce::Graphics&) override;

    scales::TimelineGeometry geometry() const;

    /** Where an event's tag was last drawn (empty if it was not), for clicks and the self-test. */
    juce::Rectangle<float> tagArea (std::uint64_t eventNumber) const;

    /** The image as last drawn (the self-test compares a scrolled image with a full redraw). */
    const juce::Image& cachedImage() const noexcept { return image; }
    void invalidate() { cached = {}; }

private:
    bool clickedAt (juce::Point<float>) override;
    void bringUpToDate (float scale);
    void paintAxis (juce::Graphics&, float scale, float zoom);
    void drawColumns (juce::Image::BitmapData&, const scales::TimelineGeometry&, const scales::ColumnMap&, std::int64_t firstColumn,
                      std::int64_t lastColumn, std::int64_t endCol, int columns, float scale, const std::function<bool (int)>& isGrid);

    struct Tag
    {
        std::uint64_t number;
        juce::Rectangle<float> area;
    };

    std::vector<Tag> tags;
    ChipCache chips;
    juce::Image axisImage;
    std::tuple<int, int, float, float, int, ThemeName> axisKey {};
    float shownZoom = 0.0f;
    std::array<float, 2> shownThresholds {};
    std::optional<DisplayModel::Span> shownSelection;
    int shownSecondsBehind = 0;
    bool shownHarsh = false;
    Band shownBand = Band::ess;
};

/** The Cloud (PLAN.md, Parts): every ess of the zoom window as a dot at its S; a cut one with its stem and where Halite left it. */
class CloudView final : public HistoryView
{
public:
    CloudView (HaliteController&, DisplayModel&);

    void update() override;
    void paint (juce::Graphics&) override;

    scales::CloudGeometry geometry() const;

    /** Dots drawn into the image since the last full redraw (the self-test). */
    int dotsDrawn() const noexcept { return dots; }

    static constexpr float pad = 8.0f;      // the image reaches past the plot by this much, so an edge dot is whole

private:
    bool clickedAt (juce::Point<float>) override;
    void bringUpToDate (float scale);
    void drawBackdrop (juce::Graphics&, const scales::CloudGeometry&, juce::Rectangle<float> area);
    void paintAxes (juce::Graphics&, float scale, float zoom);
    void drawDot (juce::Graphics&, const scales::CloudGeometry&, const DisplayModel::Ess&, float x);

    std::uint64_t drawnEsses = 0;
    std::array<float, 3> drawnBackdrop {};
    juce::Image axesImage;
    std::tuple<int, int, float, float, int, ThemeName, int> axesKey {};
    int dots = 0;
    float shownZoom = 0.0f;
    std::array<float, 2> shownThresholds {};
    std::optional<DisplayModel::Span> shownSelection;
    int shownSecondsBehind = 0;
    bool shownHarsh = false;
    Band shownBand = Band::ess;
};

/** The main view: the Timeline or the Cloud, as `displayView` says. */
class MainView final : public juce::Component
{
public:
    MainView (HaliteController&, DisplayModel&);

    void update();
    void resized() override;

    TimelineView& getTimeline() noexcept { return timeline; }
    CloudView& getCloud() noexcept { return cloud; }
    HistoryView& current() noexcept;

private:
    HaliteController& controller;
    TimelineView timeline;
    CloudView cloud;
};

} // namespace halite::ui
