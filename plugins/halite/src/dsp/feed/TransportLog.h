#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace halite::dsp
{

/**
    Where the host's song position stood against the feed's clock, so the display can stamp any
    history record (an ess Learn's rule found, not only an event) the way EssEvent is stamped
    (src/dsp/feed/NOTES.md, Clocks). One entry per change: from input sample `from` on (the feed's
    clock), the chain's timeline lies `offset` samples behind it and the host's transport plays or
    not. The chain's timeline runs with the clock between jumps, so a host that plays straight on
    adds no entries; a loop or a start or stop adds one.

    One writer (the audio thread), one reader per copy of the cursor; the writer never waits and
    overwrites the oldest entry, and each slot carries a sequence number so a reader drops a slot
    rewritten while it copied it (as EventRing).
*/
class TransportLog
{
public:
    static constexpr int capacity = 256;

    struct Entry
    {
        std::int64_t from = 0;          // the feed's clock: input samples since it started
        std::int64_t offset = 0;        // the feed's clock less the chain's timeline position
        bool playing = false;           // the host's transport played (song positions), else elapsed time
        std::uint32_t epoch = 0;        // DisplayFeed::epoch() when written: a reader drops other epochs' entries
    };

    /** Writer only: the state from `from` on. Writes an entry only when offset, playing or epoch changed. */
    void note (std::int64_t from, std::int64_t offset, bool playing, std::uint32_t epoch) noexcept
    {
        if (any && offset == last.offset && playing == last.playing && epoch == last.epoch)
            return;

        last = { from, offset, playing, epoch };
        any = true;

        const auto n = written.load (std::memory_order_relaxed);
        auto& slot = slots[(size_t) (n & mask)];
        slot.sequence.store (2 * n + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        slot.from.store (from, std::memory_order_relaxed);
        slot.offset.store (offset, std::memory_order_relaxed);
        slot.playing.store (playing, std::memory_order_relaxed);
        slot.epoch.store (epoch, std::memory_order_relaxed);
        slot.sequence.store (2 * n + 2, std::memory_order_release);
        written.store (n + 1, std::memory_order_release);
    }

    /** Entries written so far. */
    std::uint64_t count() const noexcept { return written.load (std::memory_order_acquire); }

    /** Reader: copies the entries after `cursor` (oldest first, at most maxEntries) and moves the cursor past them. */
    int read (std::uint64_t& cursor, Entry* out, int maxEntries) const noexcept
    {
        const auto end = count();
        if (end > cursor + (std::uint64_t) capacity)
            cursor = end - (std::uint64_t) capacity;

        auto copied = 0;
        for (; cursor < end && copied < maxEntries; ++cursor)
        {
            const auto& slot = slots[(size_t) (cursor & mask)];
            const auto before = slot.sequence.load (std::memory_order_acquire);
            if (before != 2 * cursor + 2)
                continue;

            Entry e;
            e.from = slot.from.load (std::memory_order_relaxed);
            e.offset = slot.offset.load (std::memory_order_relaxed);
            e.playing = slot.playing.load (std::memory_order_relaxed);
            e.epoch = slot.epoch.load (std::memory_order_relaxed);
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
        std::atomic<std::uint64_t> sequence { 0 };
        std::atomic<std::int64_t> from { 0 }, offset { 0 };
        std::atomic<bool> playing { false };
        std::atomic<std::uint32_t> epoch { 0 };
    };

    std::array<Slot, (size_t) capacity> slots;
    std::atomic<std::uint64_t> written { 0 };

    // The writer's own.
    Entry last;
    bool any = false;
};

/** The time an input sample of the feed's clock shows as (an EssEvent's `seconds` and `songPosition`), given the entry in force there. */
inline double timeOf (std::int64_t elapsed, const TransportLog::Entry& in, double sampleRate, bool& songPosition) noexcept
{
    songPosition = in.playing;
    return sampleRate > 0.0 ? (double) (in.playing ? elapsed - in.offset : elapsed) / sampleRate : 0.0;
}

} // namespace halite::dsp
