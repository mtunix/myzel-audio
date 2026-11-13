#pragma once

#include "dsp/ModuleInfo.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace inkcap::dsp
{

/**
    What the audio thread publishes for the display, through atomics only (relaxed: each value
    stands alone). Written once per block by InkcapChain; read by the editor on the message
    thread. See src/dsp/chain/NOTES.md for what each value means and how the editor uses it.

    Glitch's coming events are not here: they are a pure function of the seed, the settings and
    the slot (glitch::eventAt), so the editor computes the next bars itself from `ppq`, `bpm`
    and the time signature below, exactly as the audio thread will play them.
*/
struct Telemetry
{
    struct ModuleState
    {
        std::atomic<float> intensity { 0.0f };  // Damage-scaled Amount, 0 .. 1 (0 when off): the module bar
        std::atomic<float> activity { 0.0f };   // what it does right now, 0 .. 1: the module dot
        std::atomic<bool> on { false };         // the On key as the audio thread last saw it
    };

    std::array<ModuleState, numModules> modules;

    /** Overall wear, 0 .. 1: the mean intensity of the modules that are on (the wear matrix). */
    std::atomic<float> wear { 0.0f };

    /** Peak levels (linear) since the editor's last read: read them with exchange (0.0f). */
    std::array<std::atomic<float>, 2> inputPeak { 0.0f, 0.0f };
    std::array<std::atomic<float>, 2> outputPeak { 0.0f, 0.0f };

    /** The song position at the end of the last block, and where it comes from. */
    std::atomic<double> ppq { 0.0 };
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> followingHost { false };     // false: free-running at the host's or the fallback tempo
    std::atomic<int> timeSigNumerator { 4 };
    std::atomic<int> timeSigDenominator { 4 };
    std::atomic<double> barStartPpq { 0.0 };       // the host's last bar start (ppq), when hasBarStart
    std::atomic<bool> hasBarStart { false };

    /** Glitch: the seed its events use now (a new seed waits for the next slot), and the event playing. */
    std::atomic<uint32_t> glitchSeed { 0 };
    std::atomic<int> glitchEventType { -1 };       // GlitchType of the event playing, -1: none
    std::atomic<int64_t> glitchEventSlot { 0 };    // the slot it started on
    std::atomic<float> glitchEventProgress { 0.0f };

    /** Blocks processed since prepare: the editor sees whether audio runs at all. */
    std::atomic<uint32_t> blocks { 0 };

    static void storeMax (std::atomic<float>& target, float value) noexcept
    {
        auto current = target.load (std::memory_order_relaxed);
        while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed)) {}
    }
};

static_assert (std::atomic<double>::is_always_lock_free);
static_assert (std::atomic<int64_t>::is_always_lock_free);

} // namespace inkcap::dsp
