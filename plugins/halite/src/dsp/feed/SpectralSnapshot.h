#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace halite::dsp
{

/** What the reader gets from SpectralSnapshot: its own copy, per bin of the Spectral shape's frame. */
struct SpectralView
{
    bool active = false;            // the running shape is Spectral; else every gain reads 1 and there is no target
    int bins = 0;                   // frame / 2 + 1
    double binHz = 0.0;             // bin k sits at k · binHz
    std::uint32_t sequence = 0;     // the publication this is (ack it with SpectralSnapshot::acknowledge)
    std::vector<float> essGain, harshGain;  // amplitude gains, the deepest since the reader's last read (at most 100 ms); 1 where a band does nothing
    std::vector<float> target;      // the Ess band's reference (the thin line), as mean-square shares per bin; 0 where unknown

    /** Allocates room for every frame size, so read() never does. */
    void reserve()
    {
        for (auto* v : { &essGain, &harshGain, &target })
            v->assign ((size_t) 8193, 0.0f);
    }
};

/**
    Spectral's per-bin gains and its target for the display (src/dsp/feed/NOTES.md): one writer
    (the audio thread, about 60 times a second), any number of readers, behind a sequence lock
    with atomic fields. A reader that wants the deepest cut since it last looked acknowledges what
    it read; the writer then starts the next publication afresh, else it carries the last one over
    (for at most a few publications, so a closed editor never holds an old cut).
*/
class SpectralSnapshot
{
public:
    static constexpr int maxBins = 8193;    // a 16384-point frame (HaliteChain::frameSize at 768 kHz is 8192)

    /** Allocates, once. */
    SpectralSnapshot() : fields (std::make_unique<std::atomic<float>[]> ((size_t) maxBins * 3)) {}

    /** Writer: one publication; `target` may be null (none known). */
    void publish (bool active, int bins, double binHz, const float* essGain, const float* harshGain, const float* target) noexcept
    {
        bins = std::clamp (bins, 0, maxBins);
        const auto s = sequence.load (std::memory_order_relaxed);
        sequence.store (s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);

        isActive.store (active, std::memory_order_relaxed);
        numBins.store (bins, std::memory_order_relaxed);
        width.store (binHz, std::memory_order_relaxed);
        for (int k = 0; k < bins; ++k)
        {
            fields[(size_t) k].store (essGain[k], std::memory_order_relaxed);
            fields[(size_t) (maxBins + k)].store (harshGain[k], std::memory_order_relaxed);
            fields[(size_t) (2 * maxBins + k)].store (target != nullptr ? target[k] : 0.0f, std::memory_order_relaxed);
        }
        sequence.store (s + 2, std::memory_order_release);
    }

    /** Writer: whether a reader acknowledged the publication numbered `published`. */
    bool acknowledged (std::uint32_t published) const noexcept { return acknowledgedSequence.load (std::memory_order_relaxed) == published; }

    /** Writer: the number the last publication carries. */
    std::uint32_t lastSequence() const noexcept { return sequence.load (std::memory_order_relaxed); }

    /**
        Reader: copies the newest publication into `out` (reserve() it first, or it allocates).
        False when none has been published or the writer kept rewriting it while it was copied.
    */
    bool read (SpectralView& out) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = sequence.load (std::memory_order_acquire);
            if (before == 0 || (before & 1u) != 0)
                continue;

            const auto bins = numBins.load (std::memory_order_relaxed);
            out.active = isActive.load (std::memory_order_relaxed);
            out.binHz = width.load (std::memory_order_relaxed);
            for (auto* v : { &out.essGain, &out.harshGain, &out.target })
                if ((int) v->size() < bins)
                    v->resize ((size_t) bins);
            for (int k = 0; k < bins; ++k)
            {
                out.essGain[(size_t) k] = fields[(size_t) k].load (std::memory_order_relaxed);
                out.harshGain[(size_t) k] = fields[(size_t) (maxBins + k)].load (std::memory_order_relaxed);
                out.target[(size_t) k] = fields[(size_t) (2 * maxBins + k)].load (std::memory_order_relaxed);
            }
            std::atomic_thread_fence (std::memory_order_acquire);

            if (sequence.load (std::memory_order_relaxed) == before)
            {
                out.bins = bins;
                out.sequence = before;
                return true;
            }
        }
        return false;
    }

    /** Reader: it has seen publication `sequence` (SpectralView::sequence); the next one holds only what came after. */
    void acknowledge (std::uint32_t seen) const noexcept { acknowledgedSequence.store (seen, std::memory_order_relaxed); }

private:
    std::unique_ptr<std::atomic<float>[]> fields;   // Ess gains, Harsh gains, target: maxBins each
    std::atomic<std::uint32_t> sequence { 0 };
    mutable std::atomic<std::uint32_t> acknowledgedSequence { 0 };   // the readers' side, written from const reads
    std::atomic<bool> isActive { false };
    std::atomic<int> numBins { 0 };
    std::atomic<double> width { 0.0 };
};

} // namespace halite::dsp
