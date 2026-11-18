#pragma once

#include "dsp/Modes.h"
#include "dsp/chain/DelayLine.h"
#include "dsp/chain/DynamicsLane.h"
#include "dsp/chain/Fades.h"
#include "dsp/chain/Meters.h"
#include "dsp/dynamics/KeyPath.h"

#include <array>

namespace basalt::dsp
{

/** Plain-value snapshot of every control the chain reads, taken once per block. */
struct ChainSettings
{
    Model model = Model::clean;
    VcaDetector vcaDetector = VcaDetector::peak;
    double thresholdDb = -18.0;
    double inverseRatio = 0.25;     // exactly 0 at ∞ (Parameters.h)
    double kneeDb = 6.0;
    double attackMs = 10.0;
    double releaseMs = 150.0;
    bool autoRelease = true;
    double rangeDb = 40.0;
    double floorDb = -70.0;
    float gainDb = 0.0f;
    bool autoGain = false;
    double mix = 1.0;               // 0 … 1
    double lookaheadMs = 0.0;
    StereoMode stereo = StereoMode::linked;
    double link = 1.0;              // 0 … 1
    KeySource key = KeySource::internal;
    dynamics::KeyEqSettings keyEq;  // Off already decided from the read-outs (Parameters.h)
    Listen listen = Listen::output;
    float outputDb = 0.0f;
    bool bypass = false;
    LatencyMode latency = LatencyMode::normal;
};

/**
    The Basalt signal path (PLAN.md, Signal flow; dynamics/NOTES.md, The side chain, in order):
    the key, internal or the side chain, through the key EQ into a delay line the look-ahead
    reads; the stereo matrix and the dynamics in a lane; the reductions as gains on the audio
    delayed by the constant latency; Gain and Auto Gain on the wet path; Mix against the
    latency-aligned dry path; Listen; Output; Bypass.

    With a gain of exactly 1 everywhere (nothing over Threshold, Ratio 1 or Range 0; Gain 0 or
    Auto Gain's make-up 0; Mix 100 %; Output 0 dB; Bypass settled) the output is the input delayed
    by the latency, bit for bit: the samples pass as copies, never through arithmetic.

    Switches crossfade over 30 ms with both paths running: a model in its lane (the reductions in
    dB), a key source or stereo mode between two lanes (the audio). The incoming model or lane
    first follows the key unheard until its level states have settled, then starts at the
    reduction applied (Settled 33, 56). A change of latency fades out, switches and fades in, 10 ms
    each way. Every decision falls on a chunk's first sample and a chunk ends where the next is
    due, so the output does not depend on the host's block size.
*/
class BasaltChain
{
public:
    BasaltChain();

    /** Not real-time safe. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels, LatencyMode);

    /**
        As prepare(), with Character's integer delay in Normal given instead of
        characterDelaySamples(): the tests stand in for M3's Character with it.
    */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels, LatencyMode, int characterDelay);

    void reset() noexcept;

    /**
        Mono (one channel) or stereo (two), any block length, in place. `key` is the side chain
        (0, 1 or 2 channels; null when the host has it off): External with no key channels hears
        the input, as Halite (its Settled 143).
    */
    void process (float* const* channels, int numChannels, int numSamples, const ChainSettings&,
                  const float* const* key = nullptr, int numKeyChannels = 0) noexcept;

    /** What the host is told: Normal is the look-ahead's budget plus Character's delay, Live 0 (PLAN.md, Latency). */
    static int latencySamples (LatencyMode, double sampleRate) noexcept;

    /** The audio's delay that gives the detector its look-ahead: 10 ms in Normal, 0 in Live. */
    static int lookaheadDelaySamples (LatencyMode, double sampleRate) noexcept;

    /** Character's integer delay (character/NOTES.md, The dry path): 0 until M3 builds it. */
    static int characterDelaySamples (LatencyMode, double sampleRate) noexcept;

    /** Output's linear gain: exactly 1 at 0 dB, never 10^(0/20) computed (PLAN.md, Signal flow). */
    static float gainFor (float db) noexcept;

    const ChainMeters& meters() const noexcept { return chainMeters; }
    ChainMeters& meters() noexcept { return chainMeters; }
    const ChainStatus& status() const noexcept { return chainStatus; }

    /** The lane heard now, or heading in (tests). */
    const DynamicsLane& heardLane() const noexcept { return lanes[(size_t) (changing || laneFade.moving() ? 1 - activeLane : activeLane)]; }

    static constexpr double bypassSeconds = 0.02;
    static constexpr double fadeSeconds = 0.03;         // Listen, and a lane's key or stereo change
    static constexpr double latencyFadeSeconds = 0.01;  // each way
    static constexpr double outputMs = 8.0;             // each pole of the Output, Gain and Mix smoothers

private:
    static constexpr int chunk = DynamicsLane::maxSamples;

    /**
        One key source's path: the key EQ, then the delay line the look-ahead taps. It runs only
        while a lane hears it; one that starts again starts from rest, and the lane that needs it
        waits until it is filled.
    */
    struct KeyLine
    {
        dynamics::KeyEq eq;
        dynamics::KeyDelay delay;
        std::array<std::array<double, chunk>, 2> block {};     // this chunk's key, sane, then through the EQ
        bool running = false;

        void start() noexcept
        {
            eq.reset();
            delay.reset();
            running = true;
        }
    };

    void prime (const ChainSettings&, int numChannels, bool keyAvailable) noexcept;

    /** Up to `available` samples from `offset`, ending early at the next switch decision; returns how many. */
    int processChunk (float* const* channels, int offset, int available, int numChannels, const ChainSettings&,
                      const float* const* key, int numKeyChannels) noexcept;

    /** The latency of a mode with this chain's Character delay. */
    int latencyFor (LatencyMode) const noexcept;

    LaneConfig configFor (const ChainSettings&, int numChannels, bool keyAvailable) const noexcept;
    dynamics::ModelParams paramsFor (const ChainSettings&) const noexcept;
    double lookaheadTarget (const ChainSettings&) const noexcept;
    float makeupFor (const ChainSettings&, const dynamics::ModelParams&) noexcept;
    void tapKey (const DynamicsLane&, double delay, std::array<double, 2>& out) const noexcept;

    double sampleRate = 48000.0;
    int characterDelay = 0;                             // Normal's; Live reports none (PLAN.md, Latency)
    bool primed = false;

    LatencyMode latencyMode = LatencyMode::normal;      // what the audio has now
    int latency = 0;                                    // the audio's delay; the key is read at latency − look-ahead
    Crossfade latencyGain;                              // 1 except around a change of latency
    dynamics::LinearGlide lookahead;                    // the key tap's look-ahead, in samples

    std::array<KeyLine, 2> keyLines;                    // internal, external
    std::array<DelayLine, 2> dryDelay;

    std::array<DynamicsLane, 2> lanes;
    int activeLane = 0;
    bool changing = false;                              // the other lane follows, unheard, until it has settled
    int warmUpLeft = 0;
    Crossfade laneFade;                                 // towards the other lane

    Listen listen = Listen::output, listenBefore = Listen::output;
    Crossfade listenFade;                               // from listenBefore to listen
    Crossfade bypassFade;
    GainSmoother makeupGain, mixAmount, outputGain;
    float autoGainDb = 0.0f;

    ChainMeters chainMeters;
    ChainStatus chainStatus;

    template <typename T>
    using Block = std::array<std::array<T, chunk>, 2>;
    Block<float> dry {}, wetOther {}, cell {}, keyHeard {};
    std::array<double, chunk> tap {};                   // the look-ahead per sample
};

} // namespace basalt::dsp
