#pragma once

#include "dsp/detector/Detector.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace halite::dsp
{

/**
    Learn's constants (PLAN.md, Learn; chosen on the tune split, NOTES.md, M3: Learn). Not
    parameters: the plugin runs these defaults, tools/measure sweeps them.
*/
struct LearnTuning
{
    double captureSeconds = 8.0;            // plan value
    double liveSeconds = 60.0;              // the live histogram's window (plan value)

    // An ess candidate is a run of S over the floor (a dB value of S, Voice) or of B (Allround,
    // dBFS), ending once S has stayed under it for the hangover; its value is the run's peak. In
    // Allround a run also ends where B falls splitAbsDb under its peak, and the next one starts
    // once B has risen as much from its lowest since: an instrument's level need not fall under a
    // floor between hits.
    float floorDb = -24.0f, floorAbsDb = -60.0f;
    float splitAbsDb = 6.0f;                // 0: off
    double hangoverMs = 20.0;               // the event rule's
    double minLengthMs = 25.0;              // shorter runs are not counted: a stop's burst, a click

    // The rule: the median ess reduced by medianReductionDb, the quietest quietShare by none.
    float medianReductionDb = 4.0f;
    double quietShare = 0.25;
    int minEsses = 3;                       // with fewer, Threshold stays

    // Harsh: the loudest harshShare of voiced frames over the threshold. A frame is voiced when
    // both bands read harmonic (N under voicedNoise), the body is over its floor and, in Voice, S
    // is under the ess floor. Voiced frames include the voiced consonants, whose Harsh band is
    // weak: their loudest 7 % match the loudest tenth of the vowels in speech (the plan's tenth).
    double harshShare = 0.07;
    float voicedNoise = 0.5f;
    double minVoicedSeconds = 1.0;          // with less, Harsh Threshold stays
};

/**
    Counts of levels in 0.1 dB bins from −96 to +24 dB, the edges collecting everything beyond.
    One writer; each bin is an atomic, so a reader on any thread may see a histogram between two
    counts, never a torn one.
*/
class LevelHistogram
{
public:
    static constexpr float lowDb = -96.0f, binDb = 0.1f;
    static constexpr int numBins = 1200;
    using Counts = std::array<std::uint32_t, (size_t) numBins>;

    static int binOf (float db) noexcept;
    static float lowEdgeOf (int bin) noexcept { return lowDb + binDb * (float) bin; }

    /** Writer only. */
    void add (int bin) noexcept;
    void remove (int bin) noexcept;
    void clear() noexcept;

    /** Any thread. */
    std::uint32_t total() const noexcept { return count.load (std::memory_order_acquire); }
    void read (Counts&) const noexcept;

private:
    std::array<std::atomic<std::uint32_t>, (size_t) numBins> bins {};
    std::atomic<std::uint32_t> count { 0 };
};

/**
    The level below which `share` (0 … 1) of the counts lie, each bin's counts spread evenly across
    it. NaN when the histogram is empty.
*/
float histogramLevelDb (const LevelHistogram::Counts&, std::uint32_t total, double share) noexcept;

/** What Learn concluded from one capture. A threshold that is not set stays as it was. */
struct LearnResult
{
    bool essSet = false;
    float essDb = 0.0f;             // Threshold: relative dB in Voice, dBFS in Allround
    int esses = 0;                  // ess candidates in the capture
    bool harshSet = false;
    float harshDb = 0.0f;           // Harsh Threshold, in the same unit
    double voicedSeconds = 0.0;     // voiced frames in the capture (Harsh on)
};

/** Learn's rule (PLAN.md, Learn) on a capture's histograms. */
LearnResult learnThresholds (const LevelHistogram::Counts& esses, std::uint32_t numEsses, const LevelHistogram::Counts& voiced,
                             std::uint32_t numVoiced, bool harshOn, double hopSeconds, const LearnTuning& = {}) noexcept;

/**
    Splits a stream of S (or B) into ess candidates, independently of the threshold: a run over the
    floor, ended by the hangover, at least minLength long.
*/
class EssCandidates
{
public:
    void prepare (double hopSeconds, const LearnTuning&) noexcept;
    void reset() noexcept;

    /** One hop. True when a candidate ended with this hop; its peak in `peakDb`. `splitDb` 0: runs end at the floor only. */
    bool push (float levelDb, float floorDb, float splitDb, float& peakDb) noexcept;

    /** The open candidate, if long enough, ended now (a capture that stops). */
    bool flush (float& peakDb) noexcept;

    bool open() const noexcept { return active; }

    /** The last candidate's length in hops, from its first hop over the floor to its last. */
    int lengthHops() const noexcept { return length; }

private:
    int hangoverHops = 1, minLengthHops = 1;
    bool active = false;
    float peak = 0.0f, lowest = 0.0f;       // lowest: since the last run ended (a split asks for a rise from it)
    int length = 0, below = 0;
};

/**
    Learn on the audio thread (PLAN.md, Learn): ess candidates from every detector hop into the live
    histogram of the last 60 s; on request, an 8 s capture of the candidates and, with Harsh on, of
    the voiced frames' Harsh level, into histograms that stay as they are until the next request.
    A capture counts the candidates that begin inside it, so a run open at the press (its peak
    reached before it) or one counted when the last capture stopped is never counted again.
    The threshold is the message thread's to set (learnThresholds on the captured histograms). No
    allocation after prepare(); the histograms and the phase are read lock-free.
*/
class Learn
{
public:
    enum class Phase
    {
        idle = 0,
        capturing,
        captured        // the capture histograms hold the last capture
    };

    /**
        Allocates on the first call. A host prepares again for a new block size or latency too: at
        the same rate a capture runs on, and a finished one stays until the next request. At a new
        rate a capture in flight starts again (its hops were counted at the old rate) and the live
        histogram empties; a finished capture stays (its levels are in dB, its hop length kept).
    */
    void prepare (double sampleRate, const LearnTuning& = {});

    /**
        The detector restarts: a candidate in flight is dropped. A capture runs on and the live
        histogram keeps its last 60 s, since a host resets on a transport start or a loop.
    */
    void reset() noexcept;

    /** Any thread: capture the next captureSeconds. A request during a capture starts it again. Returns the request's id (captureId() once it runs). */
    std::uint32_t request() noexcept { return requests.fetch_add (1, std::memory_order_acq_rel) + 1; }

    /** Audio thread: every detector hop. */
    void hop (const DetectorFrame&, const DetectorSettings&) noexcept;

    Phase phase() const noexcept { return static_cast<Phase> (state.load (std::memory_order_acquire)); }

    /** The request the capture histograms answer (request() counts from 1). Read it before phase(): once it is a request's, the phase is its capture's. */
    std::uint32_t captureId() const noexcept { return served.load (std::memory_order_acquire); }

    /** 0 … 1 of the capture done. */
    float progress() const noexcept;

    const LevelHistogram& live() const noexcept { return liveEsses; }
    const LevelHistogram& capturedEsses() const noexcept { return captureEsses; }
    const LevelHistogram& capturedVoiced() const noexcept { return captureVoiced; }

    /** What the capture ran in: Detection, set when it starts, and whether Harsh was on when it ended. */
    Detection capturedDetection() const noexcept { return static_cast<Detection> (captureDetection.load (std::memory_order_acquire)); }
    bool capturedHarshOn() const noexcept { return captureHarsh.load (std::memory_order_acquire); }

    /** The hop length the capture counted its voiced frames in. */
    double capturedHopSeconds() const noexcept { return captureHop.load (std::memory_order_acquire); }

    double hopSeconds() const noexcept { return hopLength; }
    const LearnTuning& getTuning() const noexcept { return tuning; }

    /** One hop's ess level: S (the largest of the reductions'), or B in Allround, which S is there. */
    static float essLevelDb (const DetectorFrame&) noexcept;

    /** One hop's Harsh level if the hop is voiced (Harsh on): H − body in Voice, H in Allround; NaN otherwise. */
    static float voicedHarshDb (const DetectorFrame&, const DetectorSettings&, const LearnTuning&) noexcept;

private:
    void startCapture (const DetectorSettings&) noexcept;
    void addLive (int bin) noexcept;
    void expireLive() noexcept;

    LearnTuning tuning;
    double preparedRate = 0.0;
    double hopLength = Detector::hopSize / 48000.0;
    int captureHopsTotal = 1;
    std::int64_t liveHops = 1;

    EssCandidates candidates;
    bool openInCapture = false;             // the open candidate began during the capture: only those count in it
    LevelHistogram liveEsses, captureEsses, captureVoiced;

    struct Entry
    {
        std::int64_t hop = 0;
        int bin = 0;
    };
    std::vector<Entry> liveQueue;           // the live histogram's candidates, oldest first (a ring)
    size_t liveHead = 0, liveSize = 0;
    std::int64_t hopCount = 0;
    int detection = -1;

    std::atomic<std::uint32_t> requests { 0 }, served { 0 };
    std::atomic<int> state { (int) Phase::idle };
    std::atomic<int> captureHops { 0 };
    std::atomic<int> captureDetection { 0 };
    std::atomic<bool> captureHarsh { false };
    std::atomic<double> captureHop { Detector::hopSize / 48000.0 };
};

} // namespace halite::dsp
