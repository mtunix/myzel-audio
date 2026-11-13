#pragma once

#include "dsp/DamageCurves.h"
#include "dsp/chain/ChainSettings.h"
#include "dsp/common/Crossfade.h"
#include "dsp/common/GridClock.h"

#include <concepts>
#include <cstdint>

namespace inkcap::dsp
{

/** What a module is prepared for. */
struct ModuleSpec
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    int numChannels = 2;    // 1 or 2
};

/** Per-sample records a module keeps for the chain hold at least this many samples (the chain's largest chunk). */
inline constexpr int maxShareBlock = 8192;

/** Audio processed in place: one or two channels. firstSample is the GridClock index of sample 0. */
struct AudioBlock
{
    float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;
    int64_t firstSample = 0;
};

/** What every module reads besides its own settings, once per block. */
struct BlockContext
{
    const GridClock& clock;     // the song position of every sample (ppqAt, secondsAt, jumpedAt)
    float damage = 0.0f;        // 0 .. 1
    float chaos = 0.0f;         // 0 .. 1
    uint32_t seed = 0;          // the instance's seed now; Glitch takes it on at the next slot
};

/**
    The shape all six modules share, so the chain treats them alike (PLAN.md, Signal flow).

    - prepare() allocates; reset() forgets the past (a new render starts).
    - process() works in place, any block length; the same input, settings and positions give the
      same output to the bit for any block size. No allocation, locks or I/O.
    - latencySamples(): constant. 0 for every module but Crush, which carries the 2x pair's delay
      (InkcapChain::pairLatencySamples()) on its clean path too, and Warble, whose head rests
      where the windowed sinc reads (WarbleModule::restSamples).
    - intensity(): the Damage-scaled Amount of the last block (0 .. 1), activity(): what it does
      right now (0 .. 1), for the display. Read on the audio thread; the chain publishes them.
    - maxTailSeconds: how long the module can sound after its input stops.
    - Damage 0 is clean: the output is the input (Crush, Warble: delayed by their latency) to the bit.
*/
template <typename M, typename Settings>
concept ChainModule = requires (M m, const M cm, const ModuleSpec& spec, const AudioBlock& block, const Settings& s,
                                const BlockContext& context) {
    m.prepare (spec);
    m.reset();
    m.process (block, s, context);
    { cm.latencySamples() } -> std::convertible_to<int>;
    { cm.intensity() } -> std::convertible_to<float>;
    { cm.activity() } -> std::convertible_to<float>;
    { M::maxTailSeconds } -> std::convertible_to<double>;
};

/**
    A module's On key: 0 (off) .. 1 (on) over 20 ms along the fade shape (PLAN.md: module On/Off
    crossfades). The first block after reset() jumps. Idle (exactly 0, at rest) means the module
    may skip its work and pass the input through untouched.
*/
class OnOffFade
{
public:
    static constexpr double seconds = 0.02;

    void prepare (double sampleRate) noexcept
    {
        fade.setDuration (sampleRate, seconds);
        reset();
    }

    void reset() noexcept { primed = false; }

    void set (bool on) noexcept
    {
        const auto target = on ? 1.0f : 0.0f;
        if (! primed)
            fade.jumpTo (target);
        else
            fade.moveTo (target);
        primed = true;
    }

    float next() noexcept { return fade.next(); }
    float current() const noexcept { return fade.current(); }
    bool isIdle() const noexcept { return fade.isAt (0.0f); }
    bool isFull() const noexcept { return fade.isAt (1.0f); }

    /** The output for a fade value: the input exactly at 0, the module exactly at 1. */
    static float blend (float input, float processed, float amount) noexcept
    {
        if (amount >= 1.0f)
            return processed;
        if (amount <= 0.0f)
            return input;
        return input + amount * (processed - input);     // the fade already has its shape: equal gain
    }

private:
    Fade fade;
    bool primed = false;
};

} // namespace inkcap::dsp
