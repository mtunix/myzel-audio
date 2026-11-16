#pragma once

#include "dsp/Modes.h"
#include "dsp/common/EventRing.h"
#include "dsp/detector/Detector.h"
#include "dsp/feed/HopHistory.h"
#include "dsp/feed/SampleFifo.h"
#include "dsp/feed/SpectralSnapshot.h"
#include "dsp/feed/TransportLog.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace halite::dsp
{

class SpectralShape;

/**
    Everything the display reads from the audio thread besides the meters, events and Learn
    (src/dsp/feed/NOTES.md, the handoff): the sample streams for the spectrum and the In/Out
    meters, the Timeline's 60 s history, and Spectral's per-bin cut and target. HaliteChain owns
    and writes it; readers on any thread use only the const accessors (and acknowledge Spectral
    reads). Every buffer is allocated once, here, so prepare() never moves memory a reader holds.

    Its clock counts input samples since the feed started (`elapsed`), never going back: a host's
    reset, transport or prepare at the same rate leaves it running; a new sample rate restarts it
    at 0 and bumps epoch(). History record m holds the audio whose input sample numbers lie in
    [m · recordSamples(), (m + 1) · recordSamples()).
*/
class DisplayFeed
{
public:
    DisplayFeed();

    /** Not real-time safe (allocates nothing, but resets what readers see on a new rate). */
    void prepare (double sampleRate, int frameSize);

    //==============================================================================
    /** Audio thread: input samples since the feed started, at the next sample the chain takes in. */
    std::int64_t clock() const noexcept { return elapsedClock; }

    /** Audio thread: one detector hop, its S and reductions stored by the input they belong to. `offset`: clock() − the chain's timeline position. */
    void hop (const DetectorFrame&, std::int64_t offset) noexcept;

    /** Audio thread, once per block: `offset` as for hop(), `playing` as for stamp(); the transport log records changes only. */
    void noteTransport (std::int64_t offset, bool playing) noexcept
    {
        transportLog.note (elapsedClock, offset, playing, restarts.load (std::memory_order_relaxed));
    }

    /** Audio thread: stamps an event's elapsed time and song position (PLAN.md, Parts, Events sidebar). `playing`: the host's transport plays. */
    void stamp (EssEvent&, std::int64_t offset, bool playing) const noexcept;

    /**
        Audio thread: n samples leaving the chain, `dry` the latency-aligned input, `removed` what both
        bands remove from it, `out` what the plugin sends (1 or 2 channels each); `latency` the delay
        `dry` has. Writes the sample streams, closes history records, and advances clock() by n.
    */
    void audio (const float* const* dry, const float* const* removed, const float* const* out, int numChannels, int n, int latency) noexcept;

    /** Audio thread: Spectral's gains and target after each chunk; `running` is the lane's shape while it is Spectral, else null. */
    void spectral (const SpectralShape* running, Detection, int n) noexcept;

    //==============================================================================
    /** Any thread, as every accessor below: the rate of the last prepare(). */
    double sampleRate() const noexcept { return rate.load (std::memory_order_relaxed); }

    /** Samples of input per history record: 32 (one detector hop) up to 50 kHz, a whole number of hops above. */
    int recordSamples() const noexcept { return recordLength.load (std::memory_order_relaxed); }

    /** Bumped when the feed restarts at a new sample rate: history numbers and elapsed times start again from 0. */
    std::uint32_t epoch() const noexcept { return restarts.load (std::memory_order_acquire); }

    const SampleFifo& samples() const noexcept { return fifo; }

    /** Where the song position stood against the clock: any record's time, stamped as an event's would be. */
    const TransportLog& transport() const noexcept { return transportLog; }
    const HopHistory& history() const noexcept { return records; }

    /** Spectral's per-bin cut and target; a reader acknowledges what it read (SpectralSnapshot::acknowledge). */
    const SpectralSnapshot& spectralSnapshot() const noexcept { return snapshot; }

    /** The history record that holds input sample `elapsed` (an EssEvent's `elapsed`, say). */
    std::int64_t recordOf (std::int64_t elapsed) const noexcept;

    static constexpr int publishHz = 60;    // Spectral's snapshot
    static constexpr int carryLimit = 6;    // publications a cut waits for a reader (100 ms)

private:
    struct Staged
    {
        std::int64_t hop = -1;      // in 32-sample hops of input since the feed started
        float sibilanceDb = silentDb, essDb = 0.0f, harshDb = 0.0f;
    };

    void close() noexcept;
    HistoryRecord recordFor (std::int64_t record) noexcept;

    SampleFifo fifo;
    HopHistory records;
    SpectralSnapshot snapshot;
    TransportLog transportLog;

    std::atomic<double> rate { 0.0 };
    std::atomic<int> recordLength { Detector::hopSize };
    std::atomic<std::uint32_t> restarts { 0 };

    // The writer's own.
    double preparedRate = 0.0;
    std::int64_t elapsedClock = 0;
    int recordHops = 1;
    std::vector<Staged> stage;
    std::int64_t stageMask = 0;
    Staged held;                            // what a record reads when none of its hops was staged

    bool recordOpen = false;
    std::int64_t currentRecord = 0;
    float inputPeak = 0.0f, outputPeak = 0.0f, removedPeak = 0.0f;
    std::array<double, 2> outputSquares {};    // per channel, over the open record
    int recordFill = 0;                         // samples the open record holds

    int bins = 0, sincePublish = 0, publishInterval = 800, carried = 0;
    double binHz = 0.0;
    bool spectralShown = false;             // the last publication was active
    std::vector<float> essCut, harshCut, essShown, harshShown, target, reference;
    std::vector<double> scratch;
};

} // namespace halite::dsp
