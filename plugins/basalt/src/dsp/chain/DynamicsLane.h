#pragma once

#include "dsp/Modes.h"
#include "dsp/chain/Engines.h"
#include "dsp/chain/Fades.h"
#include "dsp/dynamics/Model.h"
#include "dsp/dynamics/Stereo.h"

#include <array>
#include <memory>

namespace basalt::dsp
{

/** What a lane hears: one engine on one key, in one stereo mode, on one bus width. */
struct LaneConfig
{
    int engine = static_cast<int> (Engine::clean);
    StereoMode stereo = StereoMode::linked;     // Linked on a mono bus, where the mode does not apply
    KeySource key = KeySource::internal;        // the source heard: External falls back to the input with the side chain off
    int numChannels = 2;                        // of the audio

    bool operator== (const LaneConfig&) const = default;

    dynamics::StereoLayout layout() const noexcept { return dynamics::layoutFor (stereo, numChannels); }
};

/**
    How long a model follows the key from rest before it is heard: its settleMs() plus the
    look-ahead window, in samples.
*/
int warmUpSamples (const dynamics::DynamicsModel&, const dynamics::ModelParams&, double lookahead, double sampleRate) noexcept;

/**
    One key configuration's dynamics: a model per engine, each run on the same detector channels.
    Only the heard engine runs. A new engine first follow()s the key unheard from rest for its
    settleMs() plus the look-ahead window, then starts at the reduction applied and the two
    reductions crossfade in dB over 30 ms with both processing (Settled 33; models/vca/NOTES.md,
    Switching detectors; dynamics/NOTES.md, The chain). A different key or stereo mode needs
    another lane (BasaltChain).
*/
class DynamicsLane
{
public:
    static constexpr int maxSamples = 256;
    static constexpr double fadeSeconds = 0.03;

    /** Not real-time safe. The lane owns the model from here; prepare() comes after. */
    void install (int engine, std::unique_ptr<dynamics::DynamicsModel>);

    /** Not real-time safe: prepares every installed model. */
    void prepare (double sampleRate);

    /** Releases every model and takes the configuration; the lane is unheard until seed(). */
    void start (const LaneConfig&) noexcept;

    /** Starts the configuration's engine at these reductions, one per detector channel. */
    void seed (const double* reductionDb) noexcept;

    /** Detector channel c's key for the next block, after the key EQ, the stereo matrix and the look-ahead tap. */
    double* key (int channel) noexcept { return keys[(size_t) channel].data(); }

    /**
        Asks for an engine before a block: a different one starts its warm-up, unless a crossfade
        is running (it waits for the end); a change during a warm-up restarts it, or cancels it
        when the engine heard is asked for again. `lookahead` is the tap's look-ahead now.
    */
    void request (int engine, const dynamics::ModelParams&, double lookahead) noexcept;

    /** The reductions and gains for n ≤ maxSamples samples of key; `lookahead` is the tap's per sample. */
    void process (const dynamics::ModelParams&, const double* lookahead, int n) noexcept;

    /** request(), then process(). */
    void process (const dynamics::ModelParams&, const double* lookahead, int n, int engine) noexcept;

    /** Unheard: the engine runs only its key-only states (a lane warming up for a key or stereo switch). */
    void follow (const dynamics::ModelParams&, const double* lookahead, int n) noexcept;

    /**
        Samples until the lane's next decision (a warm-up's end, a crossfade's end); a block that
        ends there keeps the output independent of the block size.
    */
    int samplesToDecision() const noexcept;

    const LaneConfig& config() const noexcept { return lane; }
    int detectorChannels() const noexcept { return numDetectors; }
    bool switching() const noexcept { return incoming >= 0 || warming >= 0; }

    /** The engine heard now, and the one it heads to (the same outside a switch). */
    int heardEngine() const noexcept { return heard; }
    int targetEngine() const noexcept { return incoming >= 0 ? incoming : (warming >= 0 ? warming : heard); }
    const dynamics::DynamicsModel& targetModel() const noexcept { return *models[(size_t) targetEngine()]; }

    /** The last block's, per detector channel and sample. */
    const double* reductions (int channel) const noexcept { return applied[(size_t) channel].data(); }
    const float* gains (int channel) const noexcept { return gain[(size_t) channel].data(); }

    /** The reduction applied at the last sample processed, per detector channel. */
    const double* lastApplied() const noexcept { return last.data(); }

    /** How long this lane's engine must follow() before it is seeded and heard. */
    double settleMs (const dynamics::ModelParams& p) const noexcept { return models[(size_t) heard]->settleMs (p); }

private:
    dynamics::ModelParams paramsFor (const dynamics::ModelParams&) const noexcept;
    dynamics::KeyBlock keyBlock (const double* lookahead, int n) const noexcept;

    using Doubles = std::array<std::array<double, maxSamples>, dynamics::maxDetectorChannels>;

    std::array<std::unique_ptr<dynamics::DynamicsModel>, maxEngines> models;
    LaneConfig lane;
    dynamics::StereoLayout stereoLayout;
    double rate = 48000.0;
    int numDetectors = 2;
    int heard = 0, incoming = -1, warming = -1, warmUpLeft = 0;
    Crossfade fade;

    Doubles keys {}, applied {}, incomingReductions {};
    std::array<std::array<float, maxSamples>, dynamics::maxDetectorChannels> gain {};
    std::array<double, dynamics::maxDetectorChannels> last {};
};

} // namespace basalt::dsp
