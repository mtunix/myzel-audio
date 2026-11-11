#pragma once

#include "dsp/Mappings.h"
#include "dsp/common/ReverbBlocks.h"

namespace cerulean::dsp
{

/**
    N-line feedback delay network: the late-reverb tank behind Hall, Rise, Choir, Halo, Echo and
    Billow.

    Per sample:  y_i = DecayFilter_i (line_i read at its modulated length)
                 line_i <- Mix(y)_i + in_i
                 out_i  = y_i

    - Mixing is orthonormal (Hadamard or Householder), so with the decay filters at unity
      (freeze) the loop is lossless and with any gain < 1 it strictly decays.
    - Lengths glide (GlideValue, double precision) when Size changes; modulation is a sine +
      smooth random blend per line, computed at control rate and interpolated per sample, each
      block restarting from the exact previous target.
    - Line gains are recomputed from the current lengths every update(), so the RT60 is exact
      whatever Size is doing. Second-order shelves (DecayFilter) keep the mid-band RT60 within a
      few % at any Decay; the high crossover's coefficients ramp per sample (they move with Tone).
    - Freeze stops the modulation where it is (rate -> 0: no pitch bend while freezing), rounds
      the lengths to whole samples (exact reads) and takes every band gain to 1: lossless.

    History: consolidated from the Billow agent's ShelvedFdn and the Halo agent's read/write split.
*/
template <int N>
class Fdn
{
public:
    enum class Mixing { hadamard, householder };

    struct Setup
    {
        std::array<float, (size_t) N> delayMs {};   // nominal lengths (Size = 1x)
        float maxScale = 2.0f;             // largest Size scale that will be requested
        float maxModMs = 2.0f;             // largest modulation depth (ms, peak)
        float nominalModHz = 0.5f;         // nominal modulation rate (Mod Rate multiplies it)
        uint32_t seed = 1;
        Mixing mixing = Mixing::hadamard;
    };

    void prepare (double sampleRate, const Setup& s)
    {
        fs = sampleRate;
        setup = s;
        lo = ShelfCoefficients::at (maps::lowCrossoverHz, sampleRate);

        for (int i = 0; i < N; ++i)
        {
            const auto maxLen = msToSamples (s.delayMs[(size_t) i] * s.maxScale + s.maxModMs, sampleRate) + 8.0f;
            lines[(size_t) i].setMaxDelay ((int) std::ceil (maxLen));
            lengths[(size_t) i].prepare (sampleRate);

            sine[(size_t) i].setStartPhase ((float) i / (float) N);
            random[(size_t) i].setSeed (s.seed * 7919u + (uint32_t) i * 104729u + 1u);
        }

        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            lines[(size_t) i].reset();
            filters[(size_t) i].reset();
            sine[(size_t) i].reset();
            random[(size_t) i].reset();
            lengths[(size_t) i].snap (std::round (msToSamples (setup.delayMs[(size_t) i], fs)));
            delay[(size_t) i] = lengths[(size_t) i].get();
            delayStep[(size_t) i] = 0.0f;
            lastEnd[(size_t) i] = delay[(size_t) i];
        }
        hiA1.reset();
        hiA2.reset();
        hiA3.reset();
        snapNext = true;   // the first update() after a reset starts at its targets (no glide)
    }

    struct Controls
    {
        float scale = 1.0f;         // Size scale
        float rt60 = 3.0f;          // mid-band RT60 (s)
        float lowEnd = 1.0f;        // RT60(low) / RT60(mid)
        maps::HighDecay high { 0.45f, 4500.0f };
        float freeze = 0.0f;        // 0..1
        float modDepthMs = 0.0f;    // peak modulation (already scaled by Mod)
        float modRate = 1.0f;       // multiplier of Setup::nominalModHz
        float randomBlend = 0.5f;   // 0 = pure sine, 1 = pure smooth random
    };

    /** Once per sub-block, before processing its samples. */
    void update (int numSamples, const Controls& c) noexcept
    {
        const auto freeze = std::clamp (c.freeze, 0.0f, 1.0f);
        const auto hi = ShelfCoefficients::at (c.high.crossoverHz, fs);
        hiA1.set (hi.a1, numSamples);
        hiA2.set (hi.a2, numSamples);
        hiA3.set (hi.a3, numSamples);
        // Freeze stops the modulation where it is (rate -> 0) instead of pulling the delays back
        // to their base lengths, which would bend the pitch during the freeze ramp.
        const auto depth = msToSamples (std::min (c.modDepthMs, setup.maxModMs), fs);
        const auto rateHz = setup.nominalModHz * c.modRate * (1.0f - freeze);
        const auto n = std::max (numSamples, 1);

        for (int i = 0; i < N; ++i)
        {
            auto& len = lengths[(size_t) i];
            const auto target = std::max (4.0f, msToSamples (setup.delayMs[(size_t) i] * c.scale, fs));
            if (snapNext)
                len.snap (target);
            else
                len.setTarget (target);
            const auto base = len.advance (n);

            // Each line gets its own rate around the nominal one, so they never beat in sync.
            const auto lineRate = rateHz * (0.75f + 0.5f * (float) i / (float) N);
            sine[(size_t) i].setRate (lineRate, fs);
            random[(size_t) i].setRate (lineRate * 1.7f, fs);
            const auto m = (1.0f - c.randomBlend) * sine[(size_t) i].advance (n)
                         + c.randomBlend * random[(size_t) i].advance (n);

            auto end = std::max (base + depth * m, 3.0f);
            if (freeze >= 0.5f)
                end = std::round (end);   // integer lengths: exactly lossless when frozen

            // Restart each block from the exact previous target (accumulated float steps can miss
            // it by an ulp, which would leave a frozen delay a hair off-integer: slightly lossy).
            delay[(size_t) i] = snapNext ? end : lastEnd[(size_t) i];
            lastEnd[(size_t) i] = end;
            delayStep[(size_t) i] = (end - delay[(size_t) i]) / (float) n;

            const auto g = computeBandGains (base, c.rt60, c.lowEnd, c.high.ratio, freeze, (float) fs);
            filters[(size_t) i].setGains (g.low, g.mid, g.high, n);
        }
        snapNext = false;
    }

    /** One sample. in: N values injected into the lines; out: N line outputs (post-filter). */
    void process (const float* in, float* out) noexcept
    {
        std::array<float, (size_t) N> y;
        read (y.data());
        for (int i = 0; i < N; ++i)
            out[i] = y[(size_t) i];
        write (y.data(), in);
    }

    /**
        The same sample in two halves, for algorithms that process the line outputs inside the
        loop (Halo's pitch shifters): read() gives the N filtered line outputs; write() mixes v
        (those outputs, possibly modified, in place) and writes it back with `in`.
    */
    void read (float* y) noexcept
    {
        const ShelfCoefficients hi { hiA1.next(), hiA2.next(), hiA3.next() };
        for (int i = 0; i < N; ++i)
        {
            delay[(size_t) i] += delayStep[(size_t) i];
            y[i] = filters[(size_t) i].process (lines[(size_t) i].readCubic (delay[(size_t) i]), lo, hi);
        }
    }

    void write (float* v, const float* in) noexcept
    {
        if (setup.mixing == Mixing::hadamard)
            hadamardInPlace<N> (v);
        else
            householderInPlace<N> (v);

        for (int i = 0; i < N; ++i)
            lines[(size_t) i].write (flushTiny (v[i] + in[i]));
    }

    /** Reads inside line i (for extra output taps); d in samples, >= 1. */
    float tap (int line, float d) const noexcept { return lines[(size_t) line].readLinear (d); }

    float currentLength (int line) const noexcept { return lengths[(size_t) line].get(); }

private:
    double fs = 48000.0;
    Setup setup;
    std::array<DelayLine, (size_t) N> lines;
    std::array<DecayFilter, (size_t) N> filters;
    ShelfCoefficients lo;
    LinearRamp hiA1, hiA2, hiA3;
    std::array<GlideValue, (size_t) N> lengths;
    std::array<SineLfo, (size_t) N> sine;
    std::array<SmoothRandom, (size_t) N> random;
    std::array<float, (size_t) N> delay {}, delayStep {}, lastEnd {};
    bool snapNext = true;
};

} // namespace cerulean::dsp
