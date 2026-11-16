#pragma once

#include "dsp/Modes.h"
#include "dsp/chain/DelayLine.h"
#include "dsp/chain/Fades.h"
#include "dsp/chain/ReductionTrack.h"
#include "dsp/common/EventRing.h"
#include "dsp/detector/Detector.h"
#include "dsp/detector/Learn.h"
#include "dsp/feed/DisplayFeed.h"
#include "dsp/shapes/ShapeLane.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace halite::dsp
{

/** Plain-value snapshot of every parameter the chain reads, taken once per block. */
struct ChainSettings
{
    DetectorSettings detector;      // detector.latency is the session's latency setting
    Shape shape = Shape::split;
    Listen listen = Listen::output;
    float outputDb = 0.0f;
    bool bypass = false;
};

/**
    What the detectors found, for the UI: written once per block by the audio thread, read
    anywhere. Levels and S are the last hop's, of the path the first reduction follows; the
    reductions are the largest of the block.
*/
struct DetectorMeters
{
    std::atomic<float> bandDb { silentDb }, bodyDb { silentDb }, sibilanceDb { silentDb }, noise { 1.0f };
    std::atomic<float> harshDb { silentDb }, harshBodyDb { silentDb };
    std::array<std::atomic<float>, 2> essReductionDb {}, harshReductionDb {};
    std::atomic<int> numReductions { 1 };
    std::atomic<bool> eventActive { false };
};

/** Why the shape that runs is not the one chosen. */
enum class ShapeFallback
{
    none = 0,
    live            // Spectral needs Normal latency: Split runs in Live
};

/** Where a block lies on the host's timeline, as its play head says (PLAN.md, Reduction, Warm start). */
struct Timeline
{
    std::int64_t position = 0;      // of the block's first sample
    bool known = false;             // the host gave a position
    bool playing = false;
};

/** What the chain is doing, for the UI: written once per block by the audio thread. */
struct ChainStatus
{
    std::atomic<int> shape { (int) Shape::split };          // the shape that runs (a fade ends in it)
    std::atomic<int> fallback { (int) ShapeFallback::none };
    std::atomic<int> latencySamples { 0 };                  // the latency the audio has now
};

/**
    The Halite signal path (PLAN.md, Signal flow; src/dsp/shapes/NOTES.md): the detectors on the
    key, the latency-aligned dry path, the shapes computing what to remove, out = dry − removed,
    Listen, Output, Bypass. With nothing to remove the output is the latency-aligned input bit for
    bit. Shape and stereo changes crossfade between two lanes, Listen between two signals, Bypass
    between the result and the aligned input; a change of latency fades out, switches, fades in.
*/
class HaliteChain
{
public:
    /** Not real-time safe. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels, LatencyMode);
    void reset() noexcept;

    /**
        Mono (one channel) or stereo (two), any block length, in place. `key` is the side chain
        (0, 1 or 2 channels; null when the host has it off). The first block after reset() anchors
        every analysis grid to the timeline (the detector's hops, Spectral's frames and its target's
        blocks); a transport that starts or jumps restarts the analysis there.
    */
    void process (float* const* channels, int numChannels, int numSamples, const ChainSettings&,
                  const float* const* key = nullptr, int numKeyChannels = 0, const Timeline& = {}) noexcept;

    /**
        One Spectral frame: 512 samples at 44.1 and 48 kHz, doubled or halved with each doubling
        of the rate so its length in ms stays about the same, and never shorter than the longest
        look-ahead and one hop (PLAN.md, Latency).
    */
    static int frameSize (double sampleRate) noexcept;

    /** Constant within a latency mode and the same for every shape: one frame in Normal, 0 in Live. */
    static int latencySamples (LatencyMode, double sampleRate) noexcept;

    const DetectorMeters& meters() const noexcept { return detectorMeters; }
    const ChainStatus& status() const noexcept { return chainStatus; }

    /** Every ess caught, for the UI's event list (one reader); positions on the timeline, stamped for the sidebar's Time. */
    const EventRing& events() const noexcept { return eventRing; }

    /** The display's samples, history and Spectral cut (src/dsp/feed/NOTES.md); read from any thread. */
    const DisplayFeed& feed() const noexcept { return displayFeed; }

    /**
        Learn (PLAN.md, Learn): request() a capture from any thread; its phase and histograms read
        lock-free. The thresholds are the message thread's to set from the captured histograms.
    */
    Learn& learn() noexcept { return learner; }
    const Learn& learn() const noexcept { return learner; }

    /** The lane that runs now (tests): a change waiting for its lane, or a fade, ends in it. */
    const ShapeLane& runningLane() const noexcept { return lanes[(size_t) (changing || laneFade.moving() ? 1 - activeLane : activeLane)]; }

    /** Either lane (tests). */
    const ShapeLane& lane (int index) const noexcept { return lanes[(size_t) index]; }

    static constexpr double bypassSeconds = 0.02;
    static constexpr double fadeSeconds = 0.03;         // Shape, Stereo, Listen (PLAN.md, Reduction)
    static constexpr double latencyFadeSeconds = 0.01;  // each way
    static constexpr double outputMs = 8.0;             // each of the Output smoother's two poles
    static constexpr std::int64_t jumpTolerance = 32;   // samples a host's position may stray by without counting as a jump

private:
    static constexpr int chunk = ShapeLane::maxSamples;

    struct Publisher final : DetectorObserver
    {
        EventRing* ring = nullptr;
        ReductionClock* reductions = nullptr;
        Learn* learn = nullptr;
        DisplayFeed* feed = nullptr;
        std::int64_t feedOffset = 0;                    // the feed's clock less the timeline position, for the chunk in flight
        bool playing = false;                           // the host's transport, for the block in flight
        const DetectorSettings* settings = nullptr;     // the detector's, for the block in flight
        DetectorFrame last;
        std::array<float, 2> essMax {}, harshMax {};
        bool any = false;

        void hop (const DetectorFrame&) noexcept override;
        void event (const EssEvent& e) noexcept override
        {
            auto stamped = e;
            feed->stamp (stamped, feedOffset, playing);
            ring->push (stamped);
        }

        StereoMode mode = StereoMode::linked;   // the one the detector runs in
    };

    void prime (const ChainSettings&, int numChannels, const Timeline&) noexcept;
    void restart (std::int64_t timelinePosition) noexcept;
    LaneConfig laneConfigFor (const ChainSettings&, int numChannels) const noexcept;
    void startLane (int index, const LaneConfig&, const ShapeLane* learned) noexcept;
    void processChunk (float* const* channels, int offset, int n, int numChannels, const ChainSettings&,
                       const float* const* key, int numKeyChannels) noexcept;
    void publish() noexcept;

    double sampleRate = 44100.0;
    bool primed = false;    // the first block after reset jumps to its settings
    std::int64_t samplePosition = 0;                    // the timeline position of the next input sample
    bool wasPlaying = false;
    std::int64_t expectedPosition = 0;

    Detector detector;
    Publisher publisher;
    EventRing eventRing;
    Learn learner;
    DisplayFeed displayFeed;
    DetectorMeters detectorMeters;
    ChainStatus chainStatus;
    ReductionClock reductions;

    LatencyMode latencyMode = LatencyMode::normal;      // what the audio has now
    int latency = 0;
    Crossfade latencyGain;                              // 1 except around a change of latency

    std::array<DelayLine, 2> dryDelay, essTriggerDelay, harshTriggerDelay;

    std::array<ShapeLane, 2> lanes;
    std::array<LaneReductions, 2> laneReductions;       // each lane reads its own stereo mode's
    int activeLane = 0;
    bool changing = false;                              // the other lane runs, unheard, until it can fade in
    Crossfade laneFade;                                 // towards the other lane
    ShapeBands bands;
    Shape shapeChosen = Shape::split;

    Listen listen = Listen::output, listenBefore = Listen::output;
    Crossfade listenFade;                               // from listenBefore to listen
    Crossfade bypassFade;
    GainSmoother outputGain;

    template <typename T>
    using Block = std::array<std::array<T, chunk>, 2>;
    Block<float> dry {}, essTap {}, harshTap {}, essTrigger {}, harshTrigger {};
    // Per lane, per reduction: a chunk's depths and those up to the end of Split's design segment after it.
    using Depths = std::array<std::array<float, chunk + SplitFilter::interval - 1>, 2>;
    std::array<Depths, 2> essDepth {}, harshDepth {};
    std::array<Block<float>, 2> laneEss {}, laneHarsh {};
    Block<float> removedMix {};                          // what both bands remove, for the display
    std::array<std::vector<float>, 2> history;          // the frame of input before a chunk, for Spectral's start
};

} // namespace halite::dsp
