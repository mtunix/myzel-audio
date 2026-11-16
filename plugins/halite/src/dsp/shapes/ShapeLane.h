#pragma once

#include "dsp/Modes.h"
#include "dsp/shapes/Shapes.h"
#include "dsp/shapes/spectral/SpectralShape.h"

#include <array>
#include <cstdint>

namespace halite::dsp
{

/** What a lane is set up to do; any change of it is a crossfade to a second lane (NOTES.md). */
struct LaneConfig
{
    Shape shape = Shape::split;
    StereoMode stereo = StereoMode::linked; // ignored on one channel
    int numChannels = 2;
    SplitFilter::Kind essKind = SplitFilter::Kind::bell;

    bool operator== (const LaneConfig&) const = default;
};

/** Everything a lane reads for one call, besides its configuration. */
struct LaneInput
{
    const float* const* input = nullptr;    // the input as it arrives (Spectral analyses it and answers a frame later)
    const float* const* dry = nullptr;      // the input delayed by the latency: what the time-domain shapes act on
    std::array<const float*, 2> essDepth {}, harshDepth {};     // per reduction, per dry sample and SplitFilter::lookahead more
    DesignGrid grid;                        // where Split's design segments lie for the dry samples
    bool harsh = false;                     // the Harsh band runs
    ShapeBands bands;
    std::int64_t position = 0;              // the timeline position of input[0]
    SpectralSettings spectral;
    const ReductionReader* reductions = nullptr;
};

/**
    One shape in one stereo mode: it turns the latency-aligned input and the depths of each
    reduction into what each band removes, per output channel (PLAN.md, Reduction, Stereo).
    Linked applies reduction 0 to both channels, Unlinked reduction c to channel c; Mid and Side
    reduce only that component, (L ± R) / 2, and decode what it removed back into the channels,
    so nothing at all is removed where the depth is 0. Spectral reads the input as it arrives and
    removes from the dry samples a frame later; Linked shares its gains between the channels.
*/
class ShapeLane
{
public:
    static constexpr int maxSamples = 256;  // per call

    /** Allocates. `frameSize`: Spectral's frame (the Normal latency). */
    void prepare (double sampleRate, int frameSize);

    /**
        Sets the lane up from rest: state to zero, the bands without a glide, ready at once (as if
        it had heard silence). Spectral's target starts from what `learned` (another lane running
        Spectral) has learned, else from its warm start.
    */
    void start (const LaneConfig&, const ShapeBands&, const ShapeLane* learned = nullptr) noexcept;

    /**
        After start(), Spectral only: the frame of input before the next call (`history`,
        numChannels, frameSize samples each, ending where the next call's input begins) goes into
        its first analysis, so it analyses what a lane that ran all along would. No frame runs here;
        its removed part is complete, and the lane ready(), one frame later (NOTES.md, Crossfades).
    */
    void fill (const float* const* history) noexcept;

    /** Whether what it removes is complete: always, but for a Spectral lane in its first frame after fill(). */
    bool ready() const noexcept { return laneConfig.shape != Shape::spectral || heard >= frame; }

    /** The host's timeline restarted: Spectral finishes the frame in flight on its old grid and starts on the new one (SpectralShape::restart). */
    void restart (std::int64_t shift, std::int64_t boundary) noexcept;

    const LaneConfig& config() const noexcept { return laneConfig; }
    const SpectralShape& spectralShape() const noexcept { return spectral; }

    /** Writes each channel's removed part per band, for the n dry samples. n ≤ maxSamples. */
    void process (const LaneInput&, float* const* essRemoved, float* const* harshRemoved, int n) noexcept;

private:
    int components (const float* const* in, int n, std::array<const float*, 2>& out) noexcept;
    std::array<int, 2> reductionsFor() const noexcept;

    LaneConfig laneConfig;
    int frame = 512, heard = 512;   // Spectral: samples run since fill(), up to a frame
    TimeDomainShape shape;
    SpectralShape spectral;
    std::array<std::array<float, maxSamples>, 2> component {}, componentEss {}, componentHarsh {};
};

} // namespace halite::dsp
