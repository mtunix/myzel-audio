#pragma once

#include "dsp/Engine.h"
#include "dsp/common/ReverbBlocks.h"

#include <vector>

namespace cerulean::dsp
{

/**
    Room: small to medium rooms, early-reflection heavy, natural.

    in L/R -> bandwidth LP
           -> image-source early reflections (shoebox, orders 1-2, two sources = the two inputs,
              two spaced subcardioid mics); each bounce costs the wall reflection coefficient and
              one absorption low-pass ------------------------------------------- x early gain --+
              |                                                                                 |
              +-> 16-line Hadamard FDN, an allpass inside every line, 3-band decay -> x late gain -+-> out

    The late field is fed by the reflections themselves (not by the dry input), so it grows out
    of the reflection pattern like a real room's diffuse field.

    Param A (Early/Late): equal-power balance, 0 = reflections dominate, 0.5 = even (equal energy
                          at Decay 1 s, the RT of a typical medium room), 1 = late field dominates.
    Param B (Walls):      soft <-> hard. Reflection coefficient 0.5 -> 0.92 and absorption
                          low-pass 2.4 -> 18 kHz per bounce, the tank's high-band decay and the
                          in-loop diffusion (soft: darker, a little sparser; hard: bright, dense).
    Size: 1x = a 7.3 x 5.5 x 3.1 m room, 0.3x = a vocal booth, 2x = a large live room. The
          reflections scale fully; below 1x the tank scales as sqrt (Size) (modal density).
    See NOTES.md for the tuning rationale and measurements.
*/
class RoomEngine final : public Engine
{
public:
    explicit RoomEngine (uint32_t randomSeed = 1) : seed (randomSeed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

    //==============================================================================
    // Local building blocks (the same as Plate's; candidates for src/dsp/common).

    /** Per-sample ramp that restarts exactly at its previous target each sub-block. */
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

    /** A delay length gliding to round (nominal x scale): integer (exact reads) at rest. */
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
        bool isStatic() const noexcept { return ! (std::abs (step) > 0.0f); }

    private:
        float nominal = 0.0f, minimum = 2.0f;
        GlideValue glide;
        float value = 0.0f, step = 0.0f;
    };

    /** Jot 3-band decay shelves with every coefficient ramped per sample. */
    class BandDecay
    {
    public:
        void reset() noexcept
        {
            sLow = sHigh = 0.0f;
            lowShelf.reset();
            highShelf.reset();
        }

        /** Band gains relative to the mid band (the mid gain lives in the delay elements). */
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
    static constexpr int lines = 16;

    /** Multi-tap line for the reflections: taps summed straight from the buffer. */
    class TapLine
    {
    public:
        void setMaxDelay (int maxDelaySamples)
        {
            int size = 4;
            while (size < maxDelaySamples + 4)
                size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            writeIndex = 0;
        }

        void reset() noexcept
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
        }

        void write (float x) noexcept
        {
            buffer[(size_t) writeIndex] = x;
            writeIndex = (writeIndex + 1) & mask;
        }

        /** Integer delays (>= 1): exact, used whenever Size is at rest. */
        float sum (const int* delay, const float* gain, size_t count) const noexcept
        {
            float acc = 0.0f;
            for (size_t k = 0; k < count; ++k)
                acc += gain[k] * buffer[(size_t) ((writeIndex - delay[k]) & mask)];
            return acc;
        }

        /** Fractional delays (>= 2), centred cubic Lagrange: used while Size glides. */
        float sumCubic (const float* delay, const float* gain, size_t count) const noexcept
        {
            float acc = 0.0f;
            for (size_t k = 0; k < count; ++k)
            {
                const auto d = std::max (delay[k], 2.0f);
                const auto i = (int) d;
                const auto f = d - (float) i;
                const auto xm1 = buffer[(size_t) ((writeIndex - i + 1) & mask)];
                const auto x0 = buffer[(size_t) ((writeIndex - i) & mask)];
                const auto x1 = buffer[(size_t) ((writeIndex - i - 1) & mask)];
                const auto x2 = buffer[(size_t) ((writeIndex - i - 2) & mask)];
                const auto fm1 = f - 1.0f, fm2 = f - 2.0f, fp1 = f + 1.0f;
                acc += gain[k] * (-f * fm1 * fm2 * (1.0f / 6.0f) * xm1 + fp1 * fm1 * fm2 * 0.5f * x0
                                  - fp1 * f * fm2 * 0.5f * x1 + fp1 * f * fm1 * (1.0f / 6.0f) * x2);
            }
            return acc;
        }

    private:
        std::vector<float> buffer;
        int mask = 0, writeIndex = 0;
    };

    /** The reflections of one order from one source into one mic (struct of arrays). */
    struct TapGroup
    {
        std::vector<float> gain, position;
        std::vector<int> delay;
        std::vector<Length> length;
    };

    uint32_t seed;
    double fs = 48000.0;

    // Early reflections.
    std::array<TapLine, 2> erInput;                                       // per source (input)
    std::array<std::array<std::array<TapGroup, 2>, 2>, 2> erGroups;       // [mic][order - 1][source]
    std::array<float, 2> erEnergy1 {}, erEnergy2 {};
    std::array<OnePole, 4> bounce1, bounce2;      // per (mic, source): order 2 passes both
    OnePole bandwidthL, bandwidthR;

    // Late tank.
    std::array<DelayLine, (size_t) lines> tankLine, tankAp;
    std::array<Length, (size_t) lines> lineLen, apLen;
    std::array<int, (size_t) lines> apDelay {};
    std::array<std::array<float, (size_t) lines>, 4> inSign {};
    std::array<std::array<float, (size_t) lines>, 2> outSign {};
    std::array<Ramp, (size_t) lines> lineGamma, apGamma, modOffset;
    std::array<BandDecay, (size_t) lines> shelves;
    std::array<SineLfo, (size_t) lines> sine;
    std::array<SmoothRandom, (size_t) lines> random;

    Ramp bandwidthG, bounceG, reflection, erNorm, earlyGain, lateGain, apCoefficient, lowCrossG, highCrossG;

    bool snapNext = true, erStatic = false, apStatic = false;
};

} // namespace cerulean::dsp
