#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace halite::dsp
{

/**
    The display's sample streams (PLAN.md, Signal flow, Display data): one writer (the audio
    thread) that never waits and overwrites the oldest samples, any number of readers that only
    read. Positions count every sample written since construction and never go back. A reader
    checks after copying that the writer had not reached the samples it copied (it announces each
    chunk before writing it), so a copy is either intact or reported as lost; every sample is an
    atomic, so no access is a data race.
*/
class SampleFifo
{
public:
    /** Input L, R (latency-aligned), output L, R (what the plugin sends). Mono writes its channel twice. */
    static constexpr int numStreams = 4;
    static constexpr int inputStream (int channel) noexcept { return channel; }
    static constexpr int outputStream (int channel) noexcept { return 2 + channel; }

    /** 1.4 s at 48 kHz, 0.34 s at 192 kHz: room for a 16384-point window and a stalled message thread. */
    static constexpr int capacity = 1 << 16;

    /** Allocates every stream, once. */
    SampleFifo()
    {
        for (auto& s : streams)
            s = std::make_unique<std::atomic<float>[]> ((size_t) capacity);
    }

    /** Writer: the next n samples (n ≤ capacity) are about to be written. */
    void begin (int n) noexcept
    {
        reserved.store (cursor + (std::uint64_t) n, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
    }

    /** Writer, between begin (n) and end (n): n samples of one stream from the write position. */
    void write (int stream, const float* source, int n) noexcept
    {
        auto* s = streams[(size_t) stream].get();
        for (int i = 0; i < n; ++i)
            s[(cursor + (std::uint64_t) i) & mask].store (source[i], std::memory_order_relaxed);
    }

    /** Writer: the n samples begun are in every stream. */
    void end (int n) noexcept
    {
        cursor += (std::uint64_t) n;
        written.store (cursor, std::memory_order_release);
    }

    /** Samples written so far; a reader's positions run up to it. */
    std::uint64_t count() const noexcept { return written.load (std::memory_order_acquire); }

    /**
        Reader: the n samples of each stream that end at `endPosition` (≤ count()), into out[stream]
        (null skips a stream); samples before position 0 read 0. False if the writer overwrote any of
        them while they were copied (or before): the copy is then not intact.
    */
    bool read (std::uint64_t endPosition, int n, float* const* out) const noexcept
    {
        n = std::min (n, capacity);
        const auto start = (std::int64_t) endPosition - n;
        for (int s = 0; s < numStreams; ++s)
        {
            if (out[s] == nullptr)
                continue;
            const auto* source = streams[(size_t) s].get();
            for (int i = 0; i < n; ++i)
            {
                const auto p = start + i;
                out[s][i] = p < 0 ? 0.0f : source[(std::uint64_t) p & mask].load (std::memory_order_relaxed);
            }
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        const auto ahead = reserved.load (std::memory_order_relaxed);
        return start < 0 || ahead <= (std::uint64_t) start + capacity;
    }

    /** The oldest position a reader can still copy from with room to spare (a quarter of the capacity). */
    std::uint64_t oldestSafe() const noexcept
    {
        const auto end = count();
        return end > (std::uint64_t) (capacity * 3 / 4) ? end - (std::uint64_t) (capacity * 3 / 4) : 0;
    }

private:
    static constexpr std::uint64_t mask = (std::uint64_t) capacity - 1;

    std::array<std::unique_ptr<std::atomic<float>[]>, numStreams> streams;
    std::uint64_t cursor = 0;                   // the writer's own
    std::atomic<std::uint64_t> written { 0 }, reserved { 0 };
};

} // namespace halite::dsp
