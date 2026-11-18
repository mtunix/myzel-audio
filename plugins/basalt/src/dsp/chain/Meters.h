#pragma once

#include "dsp/Modes.h"

#include <array>
#include <atomic>

// What the chain publishes for the display (M4's meters and status line), as Halite's chain
// publishes its detector meters: plain atomics, written by the audio thread, read anywhere.

namespace basalt::dsp
{

/**
    A peak the audio thread raises and one reader takes: take() returns the largest value noted
    since the last take() and starts again from 0, so a display that reads at its own rate misses
    no peak between two reads, whatever the block size.
*/
class PeakMeter
{
public:
    static_assert (std::atomic<float>::is_always_lock_free);

    /** Audio thread. */
    void note (float value) noexcept
    {
        auto current = peak.load (std::memory_order_relaxed);
        while (value > current && ! peak.compare_exchange_weak (current, value, std::memory_order_relaxed))
        {
        }
    }

    /** The one reader. */
    float take() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }

    /** Any thread, without taking (tests). */
    float read() const noexcept { return peak.load (std::memory_order_relaxed); }

private:
    std::atomic<float> peak { 0.0f };
};

/**
    In, Out and Reduction (PLAN.md, UI: Meters). In is the latency-aligned input, so the three line
    up in time; Reduction is per detector channel of the stereo mode heard (L and R, or M and S).
*/
struct ChainMeters
{
    std::array<PeakMeter, 2> input, output;     // linear peak magnitude per audio channel
    std::array<PeakMeter, 2> reductionDb;       // the largest reduction applied, dB
    std::array<PeakMeter, 2> liftDb;            // the largest lift applied (Upward), dB
    std::atomic<int> detectorChannels { 2 };
    std::atomic<int> stereo { static_cast<int> (StereoMode::linked) };
};

/** What the chain runs, for the status line: written once per block. */
struct ChainStatus
{
    std::atomic<int> model { static_cast<int> (Model::clean) };    // the model heard: Clean for one not built yet
    std::atomic<int> vcaDetector { static_cast<int> (VcaDetector::peak) };     // the heard engine's (`VCA RMS` on the status line)
    std::atomic<int> requestedModel { static_cast<int> (Model::clean) };       // the Model parameter: a 1.1 model's note when it differs
    std::atomic<int> latencySamples { 0 };                          // the latency the audio has now
    std::atomic<float> autoGainDb { 0.0f };                         // the make-up Auto Gain adds now (0 when off)
};

} // namespace basalt::dsp
