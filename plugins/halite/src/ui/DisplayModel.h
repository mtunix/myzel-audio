#pragma once

#include "dsp/detector/Learn.h"
#include "dsp/feed/DisplayFeed.h"
#include "dsp/feed/SpectrumAnalyser.h"
#include "ui/HaliteController.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace halite::ui
{

/** A fixed-capacity ring, oldest first; pushing onto a full ring drops the oldest. Allocates once. */
template <typename T>
class Ring
{
public:
    explicit Ring (int capacity) : items ((size_t) capacity) {}

    void push (const T& item) noexcept
    {
        items[(size_t) ((first + count) % capacity())] = item;
        if (count < capacity())
            ++count;
        else
            first = (first + 1) % capacity();
    }

    void dropFront() noexcept
    {
        if (count > 0)
        {
            first = (first + 1) % capacity();
            --count;
        }
    }

    void clear() noexcept { first = count = 0; }
    int size() const noexcept { return count; }
    bool empty() const noexcept { return count == 0; }
    int capacity() const noexcept { return (int) items.size(); }

    const T& operator[] (int i) const noexcept { return items[(size_t) ((first + i) % capacity())]; }
    T& operator[] (int i) noexcept { return items[(size_t) ((first + i) % capacity())]; }

private:
    std::vector<T> items;
    int first = 0, count = 0;
};

/**
    What the display shows, read from the display feed once per frame (src/dsp/feed/NOTES.md, Using
    it): the editor's one reader of the feed, shared by the Timeline, the Cloud, the spectrum, the
    meters and the events sidebar. It mirrors the last 60 s and more of the history (so a frozen or
    scrolled-back view keeps its records while the feed moves on), keeps the events and the ess
    candidates of the same span, follows the cut, runs the meters every frame and the spectrum at 30
    fps. Freeze stops the history, the events and the spectrum; the meters run on. Allocates only at
    construction and when the sample rate changes. Message thread.
*/
class DisplayModel
{
public:
    explicit DisplayModel (HaliteController&);

    /** One frame of the frame clock. */
    void update (double dt);

    //==============================================================================
    double recordsPerSecond() const noexcept { return perSecond; }

    /** The newest record held (−1 before any), and the oldest. */
    std::int64_t newest() const noexcept { return mirrorEnd - 1; }
    std::int64_t oldest() const noexcept { return mirrorStart; }
    bool holds (std::int64_t record) const noexcept { return record >= mirrorStart && record < mirrorEnd; }

    /** A record the mirror holds (holds() first). */
    const dsp::HistoryRecord& record (std::int64_t r) const noexcept { return mirror[(size_t) (r & mirrorMask)]; }

    /** Bumped whenever the mirror restarts (a new sample rate, a gap in the feed): a view redraws everything. */
    std::uint32_t generation() const noexcept { return restarts; }

    /** The record at the views' right edge: the newest, or the one held while frozen. */
    std::int64_t viewEnd() const noexcept { return frozen ? frozenEnd : newest(); }

    /** How far the view's right edge lies behind the feed's newest record, in seconds (0 when live). */
    double secondsBehindLive() const noexcept;

    bool isFrozen() const noexcept { return frozen; }
    void setFrozen (bool);

    //==============================================================================
    /** One caught ess (the event ring's), placed in the history. */
    struct Event
    {
        std::uint64_t number = 0;           // a serial since the editor opened: unique, increasing
        dsp::EssEvent ess;
        std::int64_t first = 0, last = 0;   // its records, from start to the end of its hangover
        std::int64_t peak = -1;             // where its cut was deepest, once all its records are in (-1 before)
    };

    /** The events of the mirror's last 60 s, oldest first (at most the ring's 256 and a margin). */
    const Ring<Event>& events() const noexcept { return eventList; }
    const Event* findEvent (std::uint64_t number) const noexcept;

    /** Where an event's cut was deepest, from the records (its first record until they arrive). */
    std::int64_t peakRecord (const Event&) const noexcept;

    /** An ess's records, from its start to the end of its hangover: what a click marks in every view. */
    struct Span
    {
        std::int64_t first = 0, last = 0;

        bool overlaps (std::int64_t f, std::int64_t l) const noexcept { return l >= first && f <= last; }
        bool operator== (const Span&) const = default;
    };

    static Span spanOf (const Event& e) noexcept { return { e.first, e.last }; }

    /** Where a span's cut was deepest, or its S highest if nothing was cut. */
    std::int64_t peakOf (Span) const noexcept;

    /** The ess marked in the views (a row in the sidebar, a tag, a dot): caught or left alone. */
    std::optional<Span> selected() const noexcept { return selection; }
    void select (std::optional<Span> span) noexcept { selection = span; }

    /**
        Marks an ess and brings it into view: one outside the current view's zoom window freezes the
        display and scrolls back to it (PLAN.md, Parts, Events sidebar); unfreezing returns to live.
    */
    void reveal (Span);

    //==============================================================================
    /** An ess candidate of the history (Learn's rule, dsp::EssCandidates): the Cloud's dots. */
    struct Ess
    {
        std::int64_t first = 0, last = 0, peak = 0;     // records; `peak` where S was highest
        float peakDb = 0.0f;                            // S there
        float cutDb = 0.0f;                             // the deepest Ess cut during it (0: left alone)
    };

    const Ring<Ess>& esses() const noexcept { return essList; }

    /** Candidates found since the model was made: a view draws those after the count it drew. */
    std::uint64_t essesFound() const noexcept { return essCount; }

    /** The mode the candidates were found in (they are cleared when Detection changes). */
    Detection essDetection() const noexcept { return candidateMode; }

    //==============================================================================
    /**
        A row of the events sidebar (src/ui/DESIGN.md, Events sidebar): every ess of the last 60 s, a caught
        one (an event, with the time the chain stamped on it) or one left alone (an ess candidate no event
        overlaps, its time from the feed's transport log, stamped the same way).
    */
    struct Row
    {
        Span span;
        float peakDb = 0.0f;            // S at its peak: dB over the voice (Voice), dBFS (Allround)
        float cutDb = 0.0f;             // the deepest Ess cut, dB >= 0; 0 for an ess left alone
        double seconds = 0.0;           // song position or elapsed time, as EssEvent::seconds
        bool songPosition = false;
        bool caught = false;
    };

    /** Oldest first, the newest maxRows; rebuilt when the events or the candidates change. */
    const std::vector<Row>& rows() const noexcept { return rowList; }

    /** Bumped whenever rows() or totals() changed. */
    std::uint32_t rowsGeneration() const noexcept { return rowGeneration; }

    /** Over the events of the last 60 s (PLAN.md, Parts, Events sidebar). */
    struct Totals
    {
        int caught = 0;
        float averageCutDb = 0.0f, largestCutDb = 0.0f;
    };

    const Totals& totals() const noexcept { return totalCounts; }

    /** The time a record shows as, stamped as an event's: song position while the host played, else elapsed. */
    double timeOf (std::int64_t record, bool& songPosition) const noexcept;

    static constexpr int maxRows = 256;

    static constexpr float cutDrawnDb = 0.5f;           // the event rule's start level: a dot with less is "left alone"

    //==============================================================================
    /** The cut now, dB ≥ 0: each frame's deepest record, released at cutReleaseDbPerSecond. */
    float essCut() const noexcept { return cut[0]; }
    float harshCut() const noexcept { return cut[1]; }

    struct Meter
    {
        float peakDb = dsp::LevelMeters::floorDb, rmsDb = dsp::LevelMeters::floorDb, holdDb = dsp::LevelMeters::floorDb;
    };

    /** 0: In, 1: Out (the louder channel); the hold is the largest peak since it was reset. */
    const Meter& meter (int which) const noexcept { return meters[(size_t) which]; }

    /** The deepest cut since the hold was reset (0: Ess, 1: Harsh). */
    float cutHold (int band) const noexcept { return cutHolds[(size_t) band]; }

    /** 0: In, 1: Out, 2: Cut. */
    void resetHold (int which) noexcept;

    static constexpr float peakReleaseDbPerSecond = 20.0f / 1.5f;    // DIN-style fall
    static constexpr float cutReleaseDbPerSecond = 30.0f;

    //==============================================================================
    /** Bumped at each spectrum update (30 fps while the display runs). */
    std::uint32_t spectrumFrame() const noexcept { return spectrumFrames; }

    const dsp::SpectrumAnalyser& spectrum() const noexcept { return analyser; }
    const float* essCurve() const noexcept { return curves[0].data(); }      // dB ≤ 0 at spectrum().frequencies()
    const float* harshCurve() const noexcept { return curves[1].data(); }
    const float* targetLine() const noexcept { return target.data(); }       // Spectral's reference on the spectrum's scale
    bool hasTarget() const noexcept { return targetShown; }

    static constexpr int spectrumPoints = 160;              // 24 an octave, four to each sixth-octave band: no more is seen
    static constexpr double spectrumInterval = 1.0 / 30.0;
    static constexpr double keepSeconds = 60.0;

private:
    void restart (double rate);
    void readHistory();
    void readEvents();
    void findEsses (std::int64_t from, std::int64_t to);
    void followCut (float essDb, float harshDb, double dt);
    void updateMeters (double dt);
    void updateSpectrum();
    void prune();
    void readTransport();
    void buildRows();

    HaliteController& controller;
    const dsp::DisplayFeed& feed;

    double rate = 48000.0, perSecond = 1500.0;
    std::uint32_t epoch = 0, restarts = 0;
    bool started = false;

    static constexpr int mirrorSize = 1 << 17;          // ≥ 83 s at any rate (DisplayFeed keeps under 1563 records a second)
    static constexpr std::int64_t mirrorMask = mirrorSize - 1;
    std::vector<dsp::HistoryRecord> mirror, chunk;
    std::int64_t mirrorStart = 0, mirrorEnd = 0, cursor = 0;

    bool frozen = false;
    std::int64_t frozenEnd = -1;

    std::uint64_t eventCursor = 0, eventSerial = 0;
    std::vector<dsp::EssEvent> eventChunk;
    Ring<Event> eventList { 512 };
    std::optional<Span> selection;

    std::uint64_t transportCursor = 0;
    Ring<dsp::TransportLog::Entry> anchors { dsp::TransportLog::capacity };
    std::vector<dsp::TransportLog::Entry> anchorChunk;

    std::vector<Row> rowList;
    Totals totalCounts;
    std::uint32_t rowGeneration = 0;
    std::array<std::uint64_t, 5> rowSource {};      // what the rows were built from: serials, counts, restarts

    dsp::LearnTuning tuning;
    dsp::EssCandidates candidates;
    Detection candidateMode = Detection::voice;
    Ring<Ess> essList { 2048 };
    Ess running;                                        // the candidate being tracked
    bool tracking = false;
    std::uint64_t essCount = 0;

    float frameCut[2] {}, cut[2] {}, cutHolds[2] {}, shownCut[2] {};
    bool frameHasRecords = false;

    dsp::LevelMeters levels;
    std::array<Meter, 2> meters {};

    dsp::SpectrumAnalyser analyser;
    dsp::SpectralView view;
    std::array<std::vector<float>, 2> curves;
    std::vector<float> target;
    bool targetShown = false;
    double sinceSpectrum = 1.0;
    std::uint32_t spectrumFrames = 0;
};

} // namespace halite::ui
