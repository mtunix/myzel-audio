#pragma once

#include "dsp/dynamics/GainComputer.h"
#include "dsp/dynamics/Levels.h"

#include <array>
#include <cmath>

// The interface every model implements in its own folder (src/dsp/models/<m>/), so the chain
// runs, switches and crossfades models without knowing them (NOTES.md, Model interface).

namespace basalt::dynamics
{

/** The parameters a model reads, as one plain snapshot per block, in the units of the knobs. */
struct ModelParams
{
    Curve curve;                    // Threshold, 1/Ratio (0 at ∞), Knee, Range: targets; the model fades to them (CurveFade)
    double attackMs = 10.0;
    double releaseMs = 150.0;
    bool autoRelease = true;
    double floorDb = -70.0;         // Upward only; the model glides it as the time knobs
    bool vcaRms = false;            // VCA only: the RMS detector
    bool linked = false;            // Linked stereo with two detector channels: Link applies
    double link = 1.0;              // λ = Link/100, read only when linked; it fades with the curve (CurveFade)
};

/** A block of key for the detectors: after the key EQ, the stereo matrix and the look-ahead tap. */
struct KeyBlock
{
    std::array<const double*, maxDetectorChannels> channels {};
    int numChannels = 1;
    int numSamples = 0;

    /** Per sample, the look-ahead the tap gives now, in samples (it glides); nullptr reads as 0. */
    const double* lookahead = nullptr;

    int lookaheadAt (int i) const noexcept { return lookahead != nullptr ? (int) (lookahead[i] + 0.5) : 0; }
};

/**
    A model turns the key into a reduction in dB per detector channel and sample: positive
    reduces, negative lifts (Upward), exactly 0 is idle and becomes a gain of exactly 1
    (gainFor()). The chain applies it to the delayed audio through the stereo matrix.

    Rules for an implementation: no latency of its own (the look-ahead is the tap's and arrives in
    KeyBlock); nothing allocated, locked or logged outside prepare(); coefficients follow the
    snapshot without moving the reduction; Threshold, Ratio, Knee, Range and Link fade (CurveFade,
    the old and the new setting's ballistics side by side), the time knobs glide (SmoothGlide,
    timeGlideMs), and so does any other level the model reads, as Upward's Floor, so no knob
    reaches the gain as a step; every state that depends on the key alone runs in follow() too.

    M3's Learn replays a model over a captured envelope (per millisecond, the peak and the mean
    square per channel; PLAN.md, Learn), which no key can stand for: M3 adds that entry point
    here, for every model (NOTES.md, Model interface).
*/
class DynamicsModel
{
public:
    virtual ~DynamicsModel() = default;

    /** Allocates for this rate (look-ahead windows, tables). Not real-time safe. Ends in reset(). */
    virtual void prepare (double sampleRate) = 0;

    /** Silence: released, level states empty, the curve taken from the next snapshot at once. */
    virtual void reset() noexcept = 0;

    /**
        Starts the ballistics at the reduction applied, one value per detector channel (a model
        or VCA detector switch, Settled 33). Key-only states keep running; memories (Auto release
        networks, the Opto's light history) start empty. A model whose state is not in dB seeds it
        through its own law.
    */
    virtual void seed (const double* reductionDb, int numChannels) noexcept = 0;

    /** The reduction for each sample and detector channel of the block, into reductionDb[c][i]. */
    virtual void process (const ModelParams&, const KeyBlock&, double* const* reductionDb) noexcept = 0;

    /**
        Runs only the states that depend on the key alone, from reset(), while the model warms up
        unheard before a switch (only the model heard runs otherwise). The one-pole level states
        divide out their start from rest (WarmUpWeight), so after settleMs() they read the key as
        if they had always run; the first process() after follow() keeps that.
    */
    virtual void follow (const ModelParams&, const KeyBlock&) noexcept = 0;

    /**
        The settled reduction for a steady sine at levelDb on the detector scale, from the targets
        (no fade): the transfer curve, Auto Gain and Learn's display read it. Negative for a lift.
    */
    virtual double staticReduction (const ModelParams&, double levelDb) const noexcept = 0;

    /** The reduction the ballistics hold now for a detector channel. */
    virtual double reduction (int channel) const noexcept = 0;

    /**
        How long follow() needs from reset() before the key-only states read the key: a model or
        a lane switched to follows this long (plus the look-ahead window), unheard, before it is
        seeded and faded in (NOTES.md, Model interface; The chain).
    */
    virtual double settleMs (const ModelParams&) const noexcept { return 0.0; }
};

/** Auto Gain's make-up in dB (NOTES.md, Make-up and Auto Gain): the static reduction at the reference level. */
inline double autoGainDb (const DynamicsModel& m, const ModelParams& p) noexcept
{
    return m.staticReduction (p, autoGainReferenceDb);
}

} // namespace basalt::dynamics
