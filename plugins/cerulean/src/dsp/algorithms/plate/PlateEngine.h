#pragma once

#include "dsp/Engine.h"
#include "dsp/common/ReverbBlocks.h"

namespace cerulean::dsp
{

/**
    Plate: bright, dense, instantly diffuse. A Dattorro figure-of-eight plate tank (JAES 1997),
    lengths scaled from Dattorro's 29761 Hz to the host rate and by Size, extended with a third
    (Density) allpass per half, modulated decay-diffusion allpasses, 3-band decay shelves and an
    exact exponential decay window (every delay element carries its share of the RT60).

    in L/R -> bandwidth LP -> 6 allpasses per side, L/R rotated between stages
           -> each half's start (own side) and its middle (other side)
    half:  AP1 (mod, -dd1) -> D1 -> shelves -> + -> AP2 (mod, dd2) -> AP3 (-dd3) -> D2 -> shelves -> other half
    out:   10 signed taps per side (Dattorro's 7 + 3 early ones), read across both halves.

    Param A (Density): grainy <-> smooth. Below the middle, input and decay diffusion rise from
                       sparse (0.3 / 0.35) to Dattorro's (0.75 / 0.70); above it the input
                       diffusers stretch up to 1.8x and the extra in-tank allpass AP3 fades in.
    Param B (Metal):   the ring of a steel plate. Less decay diffusion (AP2 0.6 -> 0.25) and less
                       high-frequency damping as it rises: more ring and sheen.
    See NOTES.md for the tuning rationale and measurements.
*/
class PlateEngine final : public Engine
{
public:
    explicit PlateEngine (uint32_t randomSeed = 1) : seed (randomSeed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

    //==============================================================================
    // Local building blocks (the same as Room's; candidates for src/dsp/common).

    /**
        A per-sample ramp that restarts exactly at its previous target each sub-block, so a
        constant target is held bit-exactly (a frozen loop gain of 1 stays exactly 1).
    */
    class Ramp
    {
    public:
        void reset() noexcept { primed = false; }

        void set (float newTarget, int numSamples) noexcept
        {
            if (! primed)
            {
                value = target = newTarget;
                step = 0.0f;
                primed = true;
                return;
            }
            value = target;
            target = newTarget;
            step = (newTarget - value) / (float) std::max (numSamples, 1);
        }

        float next() noexcept { value += step; return value; }
        float get() const noexcept { return value; }

    private:
        float value = 0.0f, target = 0.0f, step = 0.0f;
        bool primed = false;
    };

    /**
        A delay length (samples) gliding to round (nominal x scale): integer at rest, so static
        reads are exact (no interpolation loss in the loop), smooth per-sample while Size moves.
    */
    class Length
    {
    public:
        void prepare (double sampleRate, float nominalSamples, float minimumSamples) noexcept
        {
            nominal = nominalSamples;
            minimum = minimumSamples;
            glide.prepare (sampleRate);
            reset();
        }

        void reset() noexcept
        {
            glide.snap (std::max (minimum, std::round (nominal)));
            value = glide.get();
            step = 0.0f;
        }

        void update (float scale, int numSamples, bool snap) noexcept
        {
            const auto target = std::max (minimum, std::round (nominal * scale));
            if (snap)
                glide.snap (target);
            else
                glide.setTarget (target);
            const auto start = glide.get();
            const auto end = glide.advance (numSamples);
            value = start;
            step = (end - start) / (float) std::max (numSamples, 1);
        }

        float next() noexcept { value += step; return value; }
        float end() const noexcept { return glide.get(); }

    private:
        float nominal = 0.0f, minimum = 2.0f;
        GlideValue glide;
        float value = 0.0f, step = 0.0f;
    };

    /**
        Jot 3-band decay shelves with every coefficient ramped per sample (the crossover
        coefficients are passed in, already ramped). The mid gain lives in the delay elements
        (gamma^D), so this only applies the low/high band ratios.
    */
    class BandDecay
    {
    public:
        void reset() noexcept
        {
            sLow = sHigh = 0.0f;
            lowShelf.reset();
            highShelf.reset();
        }

        void setRatios (float lowRatio, float highRatio, int numSamples) noexcept
        {
            lowShelf.set (lowRatio - 1.0f, numSamples);
            highShelf.set (highRatio - 1.0f, numSamples);
        }

        float process (float x, float lowG, float highG) noexcept
        {
            const auto v1 = (x - sLow) * lowG;
            const auto lp = v1 + sLow;
            sLow = flushTiny (lp + v1);
            x += lowShelf.next() * lp;

            const auto v2 = (x - sHigh) * highG;
            const auto lp2 = v2 + sHigh;
            sHigh = flushTiny (lp2 + v2);
            return x + highShelf.next() * (x - lp2);
        }

    private:
        float sLow = 0.0f, sHigh = 0.0f;
        Ramp lowShelf, highShelf;
    };

private:
    static constexpr int numDiffusers = 6;

    static constexpr int numTank = 10;   // per half: AP1, D1, AP2, AP3, D2
    static constexpr int numTaps = 10;
    static constexpr int numMod = 4;

    struct Modulator
    {
        SineLfo sine;
        SmoothRandom random;
        Ramp offset;
    };

    uint32_t seed;
    double fs = 48000.0;

    std::array<DelayLine, (size_t) numDiffusers> diffuseL, diffuseR;
    std::array<Length, (size_t) numDiffusers> diffuseLenL, diffuseLenR;
    std::array<DelayLine, (size_t) numTank> tank;
    std::array<Length, (size_t) numTank> tankLen;
    std::array<Length, (size_t) (2 * numTaps)> tapLen;
    std::array<Modulator, (size_t) numMod> mods;
    std::array<Ramp, (size_t) numDiffusers> diffuseGammaL, diffuseGammaR;
    std::array<Ramp, (size_t) numTank> tankGamma;
    std::array<Ramp, (size_t) (2 * numTaps)> tapGain;
    BandDecay decayL, decayR, decayL2, decayR2;
    OnePole bandwidthL, bandwidthR;

    Ramp bandwidthG, lowCrossG, highCrossG;
    Ramp inDiff1, inDiff2, decayDiff1, decayDiff2, decayDiff3;

    bool snapNext = true;
};

} // namespace cerulean::dsp
