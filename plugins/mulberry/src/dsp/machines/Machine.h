#pragma once

#include "common/dsp/Blocks.h"
#include "common/dsp/Filters.h"
#include "dsp/TapPlan.h"

#include <array>
#include <cstdint>
#include <vector>

namespace mulberry::dsp
{

using myzel::dsp::dbToGain;
using myzel::dsp::flushTiny;
using myzel::dsp::msToSamples;

/**
    The delay memory of one engine slot, allocated once in prepare(). Whichever machine is live
    in the slot borrows it: audio and (for the moving media) the medium position written with each
    sample, two lines each. Nothing allocates on the audio thread.
*/
struct SlotMemory
{
    static constexpr double seconds = 13.0;     // 12 s of reads (Loop's third head at 4 s) + glides
    static constexpr int guard = 16;

    static constexpr int positionChunk = 16;

    void allocate (double sampleRate)
    {
        // The rack converter runs its own clock (up to 48 kHz) in this memory too.
        capacity = (int) std::ceil (seconds * std::max (sampleRate, 48000.0));
        capacity = (capacity + positionChunk - 1) / positionChunk * positionChunk;
        for (auto& a : audio)
            a.assign ((size_t) (capacity + guard), 0.0f);
        for (auto& p : positionOffset)
            p.assign ((size_t) capacity, 0.0f);
        for (auto& b : positionBase)
            b.assign ((size_t) (capacity / positionChunk), 0.0);
    }

    int capacity = 0;
    std::array<std::vector<float>, 2> audio;

    // Medium position at each written sample: a double base per chunk plus a float offset, so a
    // position costs 4 bytes and keeps sub-nanometre precision over hours.
    std::array<std::vector<float>, 2> positionOffset;
    std::array<std::vector<double>, 2> positionBase;
};

/** Per-block targets for a machine. The machine glides, jumps or slews them its own way. */
struct MachineSettings
{
    std::array<float, 2> timeMs { 375.0f, 375.0f };     // per line
    float paramA = 0.5f;
    float paramB = 0.5f;
    float wobble = 0.5f;            // the knob, 0 .. 1 (maps::wobbleScale)
    float wobbleRate = 1.0f;        // x the machine's own rates
    float wobbleShape = 0.5f;       // 0 periodic only .. 0.5 the machine's own mix .. 1 random only
    float noise = 0.0f;             // 0 .. 1, constant noise floor (0 = none)
    bool twoTransports = false;
    double feedbackOffsetSamples = 0.0;     // added to every feedback read (the chain's in-loop delays)
};

/** The plan's reads for one sub-block, with a gain ramp when two plans crossfade. */
struct ReadList
{
    static constexpr int maxReads = 2 * maxReadsPerLine;

    struct Entry
    {
        Read read;
        float fadeStart = 1.0f, fadeStep = 0.0f;
        bool feedback = false;
        bool ramp = false;                          // weights move per sample (weight-only plan change)
        bool steady = true;                         // no plan crossfade: the gain is 1 all sub-block
        std::array<float, numBuses> weightStep {};
    };

    std::array<Entry, maxReads> entries {};
    int count = 0;
};

/** A read's weights at sample k of the sub-block (they ramp when only weights change). */
inline std::array<float, numBuses> weightsAt (const ReadList::Entry& e, int k) noexcept
{
    if (! e.ramp)
        return e.read.weights;
    auto w = e.read.weights;
    for (size_t b = 0; b < (size_t) numBuses; ++b)
        w[b] += e.weightStep[b] * (float) (k + 1);
    return w;
}

/** A read's plan-crossfade gain at sample k of the sub-block, eased so the fade has no kink. */
inline float planGain (const ReadList::Entry& e, int k) noexcept
{
    if (e.steady)
        return 1.0f;
    const auto f = std::clamp (e.fadeStart + e.fadeStep * (float) (k + 1), 0.0f, 1.0f);
    return f * f * (3.0f - 2.0f * f);
}

/**
    The chain's part of a slot's feedback loop, per sample: the machine computes its buses, this
    turns the feedback buses into the record inputs (DC block, diffuser, Feedback, + input, Low/High
    Cut), collects the wet outputs, and the machine records.
*/
class LoopContext
{
public:
    static constexpr int maxSubBlock = 32;
    static constexpr int diffuserStages = 4;
    static constexpr std::array<float, diffuserStages> diffuserMs { 0.43f, 0.71f, 1.07f, 1.49f };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // Whole samples: at zero diffusion the chain is an exact delay, which the feedback reads
        // take back (feedbackOffsetSamples), so Diffuse never moves the echo time.
        diffuserDelay = 0;
        for (size_t i = 0; i < diffuserStages; ++i)
        {
            diffuserLength[i] = std::max (1, (int) std::lround (msToSamples (diffuserMs[i], fs)));
            diffuserDelay += diffuserLength[i];
            for (auto& d : diffuser)
                d[i].setMaxDelay (diffuserLength[i] + 2);
        }
        for (auto& dc : dcBlock)
            dc.prepare (sampleRate, 10.0);
        reset();
    }

    /** The diffuser's delay at zero diffusion (samples); feedback reads are moved earlier by it. */
    int diffuserDelaySamples() const noexcept { return diffuserDelay; }

    void reset() noexcept
    {
        for (auto& d : diffuser)
            for (auto& stage : d)
                stage.reset();
        for (auto& dc : dcBlock)
            dc.reset();
        for (auto& c : lowCut)
            c.reset();
        for (auto& c : highCut)
            c.reset();
    }

    /** Sub-block setup by the chain. */
    void begin (const float* in0, const float* in1, float* outL, float* outR,
                float feedbackFrom, float feedbackTo, float diffusion,
                float lowHz, float lowMix, float highHz, float highMix, int numSamples) noexcept
    {
        input[0] = in0;
        input[1] = in1;
        wet[0] = outL;
        wet[1] = outR;
        fbGain = feedbackFrom;
        fbStep = (feedbackTo - feedbackFrom) / (float) std::max (numSamples, 1);
        diffusionGain = std::clamp (diffusion, 0.0f, 0.75f);
        for (int l = 0; l < 2; ++l)
        {
            lowCut[(size_t) l].setCoefficients (lowHz, 0.7071f, fs);
            highCut[(size_t) l].setCoefficients (highHz, 0.7071f, fs);
        }
        lowMixStart = lowMixNow;
        lowMixStep = (lowMix - lowMixNow) / (float) std::max (numSamples, 1);
        highMixStart = highMixNow;
        highMixStep = (highMix - highMixNow) / (float) std::max (numSamples, 1);
        lowMixNow = lowMix;
        highMixNow = highMix;
    }

    /** Snap the filter blends (first block). */
    void snapCuts (float lowMix, float highMix) noexcept { lowMixNow = lowMix; highMixNow = highMix; }

    /**
        One sample: feedback buses in (sum of the feedback reads into each line), wet out, record
        inputs back. Feedback runs at record level; the chain applies the make-up after the slots.
    */
    void step (int k, float fb0, float fb1, float outL, float outR, float& rec0, float& rec1) noexcept
    {
        wet[0][k] += outL;
        wet[1][k] += outR;

        fbGain += fbStep;
        const auto lm = lowMixStart + lowMixStep * (float) (k + 1);
        const auto hm = highMixStart + highMixStep * (float) (k + 1);
        const std::array<float, 2> fb { fb0, fb1 };
        std::array<float, 2> rec {};

        for (size_t l = 0; l < 2; ++l)
        {
            auto x = (float) dcBlock[l].process ((double) fb[l]);
            for (size_t i = 0; i < diffuserStages; ++i)
                x = diffuser[l][i].processFixed (x, diffuserLength[i], diffusionGain);
            x *= fbGain;
            auto y = input[l][k] + x;

            // Off = bypassed exactly (no phase shift on the echo time); the blend ramps in and out.
            if (lm > 0.0f)
                y += lm * (lowCut[l].highpass (y) - y);
            if (hm > 0.0f)
                y += hm * (highCut[l].lowpass (y) - y);
            rec[l] = flushTiny (y);
        }

        rec0 = rec[0];
        rec1 = rec[1];
    }

private:
    double fs = 48000.0;
    std::array<const float*, 2> input {};
    std::array<float*, 2> wet {};
    float fbGain = 0.0f, fbStep = 0.0f, diffusionGain = 0.0f;
    float lowMixNow = 0.0f, highMixNow = 0.0f, lowMixStart = 0.0f, lowMixStep = 0.0f, highMixStart = 0.0f, highMixStep = 0.0f;
    std::array<std::array<myzel::dsp::Allpass, diffuserStages>, 2> diffuser;
    std::array<int, diffuserStages> diffuserLength {};
    int diffuserDelay = 0;
    std::array<myzel::dsp::DcBlocker, 2> dcBlock;
    std::array<myzel::dsp::Svf, 2> lowCut, highCut;
};

/**
    One delay machine: its medium (memory, transport, clock or converter), its record stage and
    its heads. It runs the per-sample loop itself and calls LoopContext::step() between reading
    and recording.

    Contract (checked for every machine by tests/support/MachineGates.h):
    - prepare() may allocate; everything else must not.
    - Small-signal gain from record input to a head is 1 at 1 kHz, apart from the medium's own
      physical per-pass losses, which predictedPassGain() reports.
    - At large signals the record stage limits, so Feedback above 100 % self-oscillates at a
      bounded level.
    - Wobble 0 means no modulation at all; Noise 0 means silence in gives exact silence out.
    - Deterministic: seeded randomness, reset() restores the initial state exactly.
*/
class EchoMachine
{
public:
    virtual ~EchoMachine() = default;

    virtual void prepare (double sampleRate, SlotMemory& memory) = 0;
    virtual void reset() noexcept = 0;

    /** Runs numSamples (<= LoopContext::maxSubBlock) samples of the loop. */
    virtual void process (LoopContext& loop, const std::array<ReadList, 2>& reads,
                          const MachineSettings& settings, int numSamples) noexcept = 0;

    /**
        The predicted small-signal gain of one pass (record -> medium -> head), at frequency hz,
        for the current settings, at the nominal speed or clock: the "per-repeat loss" the
        measurement checks. The chain's loop filters are not included.
    */
    virtual double predictedPassGain (double hz, const MachineSettings& settings) const noexcept = 0;

    /** Current time of a line in ms (gliding), for tests and the UI. */
    virtual float currentTimeMs (int line) const noexcept = 0;
};

} // namespace mulberry::dsp
