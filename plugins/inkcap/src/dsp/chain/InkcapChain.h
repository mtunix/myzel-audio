#pragma once

#include "dsp/chain/ChainSettings.h"
#include "dsp/chain/Match.h"
#include "dsp/chain/MatchedDry.h"
#include "dsp/chain/Telemetry.h"
#include "dsp/common/Crossfade.h"
#include "dsp/common/GridClock.h"
#include "dsp/modules/bits/BitsModule.h"
#include "dsp/modules/crush/CrushModule.h"
#include "dsp/modules/glitch/GlitchModule.h"
#include "dsp/modules/hiss/HissModule.h"
#include "dsp/modules/room/RoomModule.h"
#include "dsp/modules/warble/WarbleModule.h"

#include <array>
#include <vector>

namespace inkcap::dsp
{

/**
    The Inkcap signal path (PLAN.md, Signal flow):

        in -> Glitch -> Warble -> Crush (2x) -> Bits -> (+ Hiss) -> Room
           -> Match -> Mix (vs the latency-aligned dry) -> Output -> Bypass -> out

    The six modules share one interface (ChainModule in dsp/modules/Module.h) and run in this
    fixed order. The clock (GridClock) turns the host's position into the song position of every
    sample before the modules run. The latency is the modules' sum, Crush's 2x pair and Warble's
    rest, and never changes; the dry path is delayed by exactly that, and Mix's dry also takes the
    phase of Crush's pair and Bits' converter (MatchedDry). Mix is linear (PLAN.md, Settled 20);
    Mix, Output and Match follow their targets through two-pole glides that track automation,
    Bypass fades along the C2 fade shape; Match compensates each module's level from calibrated
    tables (Match.h). A host loop that wraps inside a block splits it there, so the modules see
    the jump where it happens.

    Mono (one channel) or stereo (two), any block length; no allocation after prepare(). The
    display reads telemetry() (atomics, see Telemetry.h).
*/
class InkcapChain
{
public:
    /** Not real-time safe. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset() noexcept;

    void process (float* const* channels, int numChannels, int numSamples, const ChainSettings&,
                  const HostPosition& = {}) noexcept;

    int getLatencySamples() const noexcept { return latencySamples; }

    /** The 2x polyphase IIR pair's low-frequency group delay (3.07 samples at any rate), rounded. */
    static int pairLatencySamples() { return CrushModule::pairLatencySamples(); }

    /** The chain's latency at any rate: Crush's pair and Warble's rest, on or off. */
    static int fixedLatencySamples() { return pairLatencySamples() + WarbleModule::restSamples; }

    /** How long the chain can sound after its input stops: the modules' longest tails, in series. */
    static constexpr double maxTailSeconds = GlitchModule::maxTailSeconds + WarbleModule::maxTailSeconds + CrushModule::maxTailSeconds
                                             + BitsModule::maxTailSeconds + HissModule::maxTailSeconds + RoomModule::maxTailSeconds;

    const Telemetry& telemetry() const noexcept { return meters; }
    const GridClock& clock() const noexcept { return grid; }

    GlitchModule& glitchModule() noexcept { return glitch; }
    WarbleModule& warbleModule() noexcept { return warble; }

    static constexpr int maxChannels = 2;
    static constexpr double bypassSeconds = 0.02;
    static constexpr double outputSeconds = 0.03;   // the glides' time in all (two poles): automation lags by this
    static constexpr double mixSeconds = 0.03;
    static constexpr double matchSeconds = 0.05;

    /** Where a host loop wraps inside a block of this length: 0 if it does not. */
    static int loopWrapOffset (const HostPosition&, double sampleRate, double fallbackBpm, int numSamples) noexcept;

    /** The host's position at the wrap: the loop's start (plus the part of a sample past its end). */
    static HostPosition afterLoopWrap (const HostPosition&, double sampleRate, double fallbackBpm, int wrapOffset) noexcept;

private:
    /**
        Two one-poles in series that land exactly: a value whose slope never steps and that follows a
        moving target (automation) at a constant lag, whatever the host's block size.
    */
    struct Glide
    {
        double first = 0.0, value = 0.0, coefficient = 1.0, landing = 1.0e-6;

        void setTime (double sampleRate, double seconds) noexcept
        {
            coefficient = 1.0 - std::exp (-1.0 / (0.5 * seconds * sampleRate));
        }

        void jumpTo (double v) noexcept { first = value = v; }

        double next (double target) noexcept
        {
            first += (target - first) * coefficient;
            value += (first - value) * coefficient;
            if (std::abs (target - value) < landing && std::abs (target - first) < landing)
                first = value = target;
            return value;
        }
    };

    /** A gain gliding in dB that lands exactly, so 0 dB is exactly unity; exp only while it moves. */
    struct DbGlide
    {
        Glide db { .landing = 1.0e-4 };     // 1e-4 dB: a step of 1e-5 of the gain, 100 dB down
        double appliedDb = 0.0, gain = 1.0;

        void jumpTo (double v) noexcept
        {
            db.jumpTo (v);
            apply();
        }

        float next (double targetDb) noexcept
        {
            if (! myzel::dsp::isExactly (db.next (targetDb), appliedDb))
                apply();
            return (float) gain;
        }

        void apply() noexcept
        {
            appliedDb = db.value;
            gain = std::exp (appliedDb * 0.11512925464970229);     // ln 10 / 20
        }
    };

    void processSegment (float* const* channels, int numChannels, int offset, int numSamples, const ChainSettings&,
                         const HostPosition&) noexcept;
    void processChunk (float* const* channels, int numChannels, int numSamples, int64_t firstSample, const ChainSettings&,
                       const BlockContext&) noexcept;
    void publish (const ChainSettings&, const HostPosition&, int numSamples) noexcept;

    GridClock grid;
    GlitchModule glitch;
    WarbleModule warble;
    CrushModule crush;
    BitsModule bits;
    HissModule hiss;
    RoomModule room;

    double fs = 48000.0;
    int latencySamples = 0;
    int chunkSize = 0;
    std::array<std::vector<float>, maxChannels> dryLine;    // Bypass's dry: a ring of latencySamples
    std::array<std::vector<float>, maxChannels> dryChunk;   // the input of the chunk being processed
    std::array<std::vector<float>, maxChannels> mixChunk;   // Mix's dry for the chunk (MatchedDry)
    int dryIndex = 0;
    MatchedDry matchedDry;
    Fade bypassAmount;
    Glide mixAmount;
    DbGlide matchGain, outputGain;
    double matchTargetDb = 0.0, outputTargetDb = 0.0, mixTarget = 1.0;
    bool primed = false;    // the first block after reset jumps to its settings

    Telemetry meters;
};

} // namespace inkcap::dsp
