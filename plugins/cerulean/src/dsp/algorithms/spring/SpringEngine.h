#pragma once

#include "dsp/Engine.h"
#include "dsp/algorithms/spring/DispersionCascade.h"
#include "dsp/algorithms/spring/PreciseGlide.h"
#include "dsp/algorithms/spring/SoftDrive.h"
#include "dsp/common/ReverbBlocks.h"

namespace cerulean::dsp
{

/**
    Spring: the boingy guitar-amp tank. Drips, chirps, splash.

    in -> low cut -> Drip pre-emphasis -> bandwidth LP -> soft drive (dwell, anti-aliased)
       -> 1-3 springs, each a round trip:
              +-> dispersion -> transit --(far pickup)--> dispersion -> transit -> decay -> smear -+
              ^                                                                                    |
              +---------------------------------------------------------------------(near pickup)-+
       -> a weak, diffuse high-frequency "sizzle" loop (the modes above the chirp cutoff)
    pickups -> stereo (far/near ends, alternating per spring) -> image-removing LP -> out

    Param A (Springs): 1-3 springs (maps::springCount), each with its own length and dispersion.
    Param B (Drip):    dispersion strength + treble emphasis + drive into the tank:
                       0 = smooth, subtle; 1 = full surf drip.
    See NOTES.md for the topology, tuning and measurements.
*/
class SpringEngine final : public Engine
{
public:
    explicit SpringEngine (uint32_t seed = 1) : rngSeed (seed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

    static constexpr int maxSprings = 3;
    static constexpr int halfStages = 72;        // dispersion stages per transit (144 per round trip)
    static constexpr int firstGroupStages = 40;  // the first 40 disperse from Drip 0; the rest join above 0.5
    using Cascade = DispersionCascade<halfStages, firstGroupStages>;

    /** The stretch K in use (fc = fs / 2K), for tests. */
    int getStretch() const noexcept { return stretch; }

private:
    /** A Schroeder allpass whose length glides with Size and snaps to integers when frozen. */
    struct GlidingAllpass
    {
        Allpass ap;
        PreciseGlide length;
        float nominal = 1.0f, current = 1.0f, step = 0.0f, blockEnd = 1.0f;

        void prepare (double fs, float ms, float maxScale);
        void reset() noexcept;
        void update (int numSamples, float scale, bool frozen, bool snap, double fs) noexcept;
        float process (float x, float g) noexcept
        {
            current += step;
            return ap.process (x, current, g);
        }
    };

    /** A delay line with a gliding, modulated read position. */
    struct GlidingLine
    {
        DelayLine line;
        PreciseGlide length;
        SmoothRandom wobble;
        float nominal = 1.0f, current = 1.0f, step = 0.0f, blockEnd = 1.0f;
        float minLength = 3.0f;   // shortest read (samples), incl. modulation

        void prepare (double fs, float ms, float maxScale, float maxModMs, uint32_t seed);
        void reset() noexcept;
        /** offsetSamples is added to the glided length (already integer when frozen). */
        void update (int numSamples, float scale, float offsetSamples, float modSamples, float rateHz,
                     bool frozen, bool snap, double fs) noexcept;
        float read() noexcept
        {
            current += step;
            return line.readCubic (current);
        }
        /** For a line whose writes for this block are deferred: i samples into the block. */
        float readDeferred (int i) noexcept
        {
            current += step;
            return line.readCubic (current - (float) i);
        }
        float base() const noexcept { return length.get(); }
        float lastEnd() const noexcept { return blockEnd; }
    };

    /** A level that follows a target with a one-pole (computed per block), ramped per sample. */
    struct Fader
    {
        float value = 0.0f;
        LinearRamp ramp;

        void reset() noexcept { ramp.reset(); }
        void update (float target, float coefficient, bool snap, int numSamples) noexcept
        {
            value = snap ? target : value + (target - value) * coefficient;
            if (std::abs (target - value) < 1.0e-5f)
                value = target;
            ramp.set (value, numSamples);
        }
        float next() noexcept { return ramp.next(); }
    };

    struct Spring
    {
        GlidingLine out, back;          // the two transits of one round trip
        DecayFilter decay;
        std::array<GlidingAllpass, 2> smear;
        Fader active;                   // fades the spring in/out when Springs changes
        LinearRamp injectL, injectR;    // input weights (incl. the fade)
        LinearRamp farL, farR, nearL, nearR;   // pickup -> output weights
    };

    uint32_t rngSeed;
    double fs = 48000.0;
    int stretch = 5;

    // Input stage, per channel.
    std::array<OnePole, 2> emphasisSplit;
    std::array<Svf, 2> lowCut;
    std::array<Svf, 2> bandwidth;
    std::array<SoftDrive, 2> drive;
    LinearRamp emphasis, driveGain, driveMix;

    // Springs.
    std::array<Spring, (size_t) maxSprings> springs;
    Cascade outward, backward;
    std::array<LinearRamp, (size_t) maxSprings> dispersionFirst, dispersionRest;

    // High-frequency sizzle loop.
    Svf sizzleSplit1, sizzleSplit2;
    GlidingLine sizzleLine;
    DecayFilter sizzleDecay;
    std::array<GlidingAllpass, 3> sizzleSmear;
    std::array<Svf, 2> sizzleTop;   // the tank's top end: nothing above ~7 kHz
    LinearRamp sizzleLevel, sizzleInput;

    // Output: removes the stretched allpass images above fc, then level.
    std::array<Svf, 2> imageCut1, imageCut2;
    LinearRamp smearGain, outputGain;

    // Per-block scratch (the springs run in passes over the block, see process()).
    std::array<Cascade::Lanes, (size_t) maxSubBlock> blockX {}, blockFirst {}, blockRest {};
    std::array<std::array<float, 2>, (size_t) maxSubBlock> blockDriven {}, blockLf {};
    std::array<std::array<float, (size_t) maxSprings>, (size_t) maxSubBlock> blockNear {};

    bool snapNext = true;
};

} // namespace cerulean::dsp
