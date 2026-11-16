#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace halite::dsp
{

/**
    One caught ess (PLAN.md, Detection 6). Positions are timeline positions (HaliteChain anchors them
    to the host). The detector fills the first four fields; HaliteChain stamps the rest as it pushes
    the event, for the events sidebar's Time column (PLAN.md, Parts; src/dsp/feed/NOTES.md).
*/
struct EssEvent
{
    std::int64_t start = 0;         // the first hop whose reduction exceeded the start level
    std::int64_t end = 0;           // the end of the hangover after the reduction fell back
    float peakReductionDb = 0.0f;
    float peakSibilanceDb = 0.0f;   // the largest S during the event

    std::int64_t elapsed = 0;       // `start` in samples of input since processing started (DisplayFeed::elapsed)
    double seconds = 0.0;           // the host's song position of `start` while it played, else elapsed in seconds
    bool songPosition = false;      // `seconds` is the song position (shown as m:ss.cc), not elapsed (~m:ss.cc)
};

/**
    The most recent events for the UI: one writer (the audio thread), one reader (the message
    thread). The writer never waits and overwrites the oldest entry when the reader falls behind;
    each slot carries a sequence number, so the reader drops a slot that was rewritten while it
    copied it (a seqlock with atomic fields, so no access is a data race). The count never goes
    back, not even on a reset: event positions restart at 0 then, the ring does not.
*/
template <int capacity = 256>
class EventRingOf
{
    static_assert (capacity > 0 && (capacity & (capacity - 1)) == 0, "capacity must be a power of two");

public:
    static constexpr int size = capacity;

    /** Writer only. Real-time safe. */
    void push (const EssEvent& e) noexcept
    {
        const auto n = written.load (std::memory_order_relaxed);
        auto& slot = slots[(size_t) (n & mask)];

        slot.sequence.store (2 * n + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        slot.start.store (e.start, std::memory_order_relaxed);
        slot.end.store (e.end, std::memory_order_relaxed);
        slot.peakReductionDb.store (e.peakReductionDb, std::memory_order_relaxed);
        slot.peakSibilanceDb.store (e.peakSibilanceDb, std::memory_order_relaxed);
        slot.elapsed.store (e.elapsed, std::memory_order_relaxed);
        slot.seconds.store (e.seconds, std::memory_order_relaxed);
        slot.songPosition.store (e.songPosition, std::memory_order_relaxed);
        slot.sequence.store (2 * n + 2, std::memory_order_release);
        written.store (n + 1, std::memory_order_release);
    }

    /** Events pushed so far: a reader's cursor runs up to it. */
    std::uint64_t count() const noexcept { return written.load (std::memory_order_acquire); }

    /**
        Reader only. Copies the events after `cursor` (oldest first, at most maxEvents) and moves
        the cursor past them. Events overwritten before they were read are skipped.
    */
    int read (std::uint64_t& cursor, EssEvent* out, int maxEvents) const noexcept
    {
        const auto end = count();
        if (end > cursor + (std::uint64_t) capacity)
            cursor = end - (std::uint64_t) capacity;

        auto copied = 0;
        for (; cursor < end && copied < maxEvents; ++cursor)
        {
            const auto& slot = slots[(size_t) (cursor & mask)];
            const auto before = slot.sequence.load (std::memory_order_acquire);

            if (before != 2 * cursor + 2)
                continue;

            EssEvent e;
            e.start = slot.start.load (std::memory_order_relaxed);
            e.end = slot.end.load (std::memory_order_relaxed);
            e.peakReductionDb = slot.peakReductionDb.load (std::memory_order_relaxed);
            e.peakSibilanceDb = slot.peakSibilanceDb.load (std::memory_order_relaxed);
            e.elapsed = slot.elapsed.load (std::memory_order_relaxed);
            e.seconds = slot.seconds.load (std::memory_order_relaxed);
            e.songPosition = slot.songPosition.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);

            if (slot.sequence.load (std::memory_order_relaxed) == before)
                out[copied++] = e;
        }
        return copied;
    }

private:
    static constexpr std::uint64_t mask = (std::uint64_t) capacity - 1;

    struct Slot
    {
        std::atomic<std::uint64_t> sequence { 0 };   // odd while written, 2n + 2 once event n is in
        std::atomic<std::int64_t> start { 0 }, end { 0 };
        std::atomic<float> peakReductionDb { 0.0f }, peakSibilanceDb { 0.0f };
        std::atomic<std::int64_t> elapsed { 0 };
        std::atomic<double> seconds { 0.0 };
        std::atomic<bool> songPosition { false };
    };

    static_assert (std::atomic<std::int64_t>::is_always_lock_free && std::atomic<double>::is_always_lock_free);

    std::array<Slot, (size_t) capacity> slots;
    std::atomic<std::uint64_t> written { 0 };
};

using EventRing = EventRingOf<256>;

} // namespace halite::dsp
