#pragma once

#include "dsp/detector/Levels.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace halite::dsp
{

/**
    One record of the Timeline's history (src/dsp/feed/NOTES.md): DisplayFeed::recordSamples() of
    audio as it leaves the plugin, the detector's values for the same audio beside it. Where two
    reductions run (Unlinked) each value is the larger; the peaks and the RMS are the largest over the channels.
*/
struct HistoryRecord
{
    float sibilanceDb = silentDb;   // S as the reduction saw it: Voice, dB over the voice; Allround, B in dBFS RMS
    float essDb = 0.0f;             // Ess reduction, dB ≥ 0
    float harshDb = 0.0f;           // Harsh reduction, dB ≥ 0
    float inputPeak = 0.0f;         // linear sample peak of the latency-aligned input
    float outputPeak = 0.0f;        // of the de-essed signal (input − removed), before Listen, Output and Bypass
    float removedPeak = 0.0f;       // of what both bands remove (Listen Removed's signal)
    float outputRms = 0.0f;         // linear RMS of the de-essed signal over the record: the Timeline's core
};

/**
    The history ring: one writer (the audio thread), any number of readers. As SampleFifo, the
    writer never waits and announces each record before writing it, and a reader drops what was
    overwritten while it copied. Record numbers count from the feed's start (DisplayFeed::recordOf)
    and go back to 0 only when the feed restarts at a new sample rate (DisplayFeed::epoch).
*/
class HopHistory
{
public:
    /** 84 s or more at any rate (DisplayFeed keeps a record under 1563 per second). */
    static constexpr int capacity = 1 << 17;
    static constexpr int numFields = 7;

    /** Allocates, once. */
    HopHistory() : fields (std::make_unique<std::atomic<float>[]> ((size_t) capacity * numFields)) {}

    /** Writer only: back to record 0. */
    void restart() noexcept
    {
        next = 0;
        reserved.store (0, std::memory_order_relaxed);
        written.store (0, std::memory_order_release);
    }

    /** Writer only: appends record number count(). */
    void push (const HistoryRecord& r) noexcept
    {
        reserved.store (next + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        auto* f = fields.get() + (size_t) (next & mask) * numFields;
        f[0].store (r.sibilanceDb, std::memory_order_relaxed);
        f[1].store (r.essDb, std::memory_order_relaxed);
        f[2].store (r.harshDb, std::memory_order_relaxed);
        f[3].store (r.inputPeak, std::memory_order_relaxed);
        f[4].store (r.outputPeak, std::memory_order_relaxed);
        f[5].store (r.removedPeak, std::memory_order_relaxed);
        f[6].store (r.outputRms, std::memory_order_relaxed);
        written.store (++next, std::memory_order_release);
    }

    /** Records written so far: the next record's number. */
    std::int64_t count() const noexcept { return written.load (std::memory_order_acquire); }

    /**
        Reader: copies records from `cursor` on (oldest first, at most maxRecords) and moves the
        cursor past them; the n records returned are the n before the new cursor. A cursor older
        than what the ring holds moves up to the oldest record still held, and records the writer
        reached during the copy are dropped from the front; a cursor past count() reads nothing.
    */
    int read (std::int64_t& cursor, HistoryRecord* out, int maxRecords) const noexcept
    {
        const auto end = count();
        cursor = std::clamp (cursor, std::max<std::int64_t> (0, end - keep), end);
        const auto n = (int) std::min<std::int64_t> (end - cursor, maxRecords);

        for (int i = 0; i < n; ++i)
        {
            const auto* f = fields.get() + (size_t) ((cursor + i) & mask) * numFields;
            auto& r = out[i];
            r.sibilanceDb = f[0].load (std::memory_order_relaxed);
            r.essDb = f[1].load (std::memory_order_relaxed);
            r.harshDb = f[2].load (std::memory_order_relaxed);
            r.inputPeak = f[3].load (std::memory_order_relaxed);
            r.outputPeak = f[4].load (std::memory_order_relaxed);
            r.removedPeak = f[5].load (std::memory_order_relaxed);
            r.outputRms = f[6].load (std::memory_order_relaxed);
        }
        std::atomic_thread_fence (std::memory_order_acquire);

        // Records the writer reached while they were copied are dropped from the front.
        const auto lost = (int) std::clamp<std::int64_t> (reserved.load (std::memory_order_relaxed) - capacity - cursor, 0, n);
        if (lost > 0)
            std::copy (out + lost, out + n, out);
        cursor += n;
        return n - lost;
    }

    /** The oldest record a reader can count on: the ring holds a little more, as margin against the writer. */
    std::int64_t oldest() const noexcept { return std::max<std::int64_t> (0, count() - keep); }

private:
    static constexpr std::int64_t mask = capacity - 1;
    static constexpr std::int64_t keep = capacity - 4096;

    std::unique_ptr<std::atomic<float>[]> fields;
    std::int64_t next = 0;                      // the writer's own
    std::atomic<std::int64_t> written { 0 }, reserved { 0 };
};

} // namespace halite::dsp
