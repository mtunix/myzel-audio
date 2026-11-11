#pragma once

#include "dsp/Mappings.h"
#include "dsp/common/ReverbBlocks.h"

namespace cerulean::dsp::nimbus
{

/**
    The Nimbus "cloud" tank: an N-line feedback delay network in which every line carries its
    own chain of modulated Schroeder allpasses *inside* the loop (after CloudSeed's idea of
    diffusers within the feedback path), so each pass round the loop smears the sound further.

    Per sample, for line i:
        x   = line_i read at its (slowly, randomly) modulated length
        x   = AP_i,k (x)   for k = 0 .. Aps-1   (modulated lengths, shared coefficient)
        y_i = DecayFilter_i (x)
        line_i <- Hadamard(y)_i + in_i

    - The allpasses are lossless, so the loop gain is set by the decay filters alone, computed
      from the *total* loop length (line + allpasses): the RT60 stays exact for any Density.
    - Every delay is modulated by its own seeded smooth random walk, computed at control rate and
      interpolated per sample (Lagrange reads). No two run at the same rate.
    - Freeze: the random walks stop where they are (rate x (1 - freeze)) instead of springing
      back, so engaging Freeze never bends the pitch; above freeze 0.5 every length is rounded to
      an integer, so at freeze 1 the Lagrange reads are exact and the loop is lossless.
    - Size: one glide for the whole tank (slew-limited so the longest line bends <= 12 %).

    A local variant of dsp::Fdn (which has no in-loop diffusers).
*/
template <int N, int Aps>
class CloudTank
{
public:
    struct Setup
    {
        std::array<float, (size_t) N> lineMs {};
        std::array<std::array<float, (size_t) Aps>, (size_t) N> apMs {};
        float maxScale = 2.0f;
        float maxLineModMs = 8.0f;
        float maxApModMs = 1.5f;
        float lineModHz = 0.2f;     // nominal random-walk rates (Mod Rate multiplies them)
        float apModHz = 0.4f;
        uint32_t seed = 1;
    };

    struct Controls
    {
        float scale = 1.0f;
        float rt60 = 8.0f;
        float lowEnd = 1.0f;
        maps::HighDecay high { 0.45f, 4500.0f };
        float freeze = 0.0f;
        float lineModMs = 2.0f;
        float apModMs = 0.4f;
        float modRate = 1.0f;
        float apGain = 0.4f;        // in-loop diffusion coefficient
    };

    void prepare (double sampleRate, const Setup& s)
    {
        fs = sampleRate;
        setup = s;

        float longest = 1.0f;
        for (int i = 0; i < N; ++i)
        {
            const auto lineMax = msToSamples (s.lineMs[(size_t) i] * s.maxScale + s.maxLineModMs, sampleRate) + 8.0f;
            lines[(size_t) i].setMaxDelay ((int) std::ceil (lineMax));
            longest = std::max (longest, msToSamples (s.lineMs[(size_t) i], sampleRate));

            for (int k = 0; k < Aps; ++k)
            {
                const auto apMax = msToSamples (s.apMs[(size_t) i][(size_t) k] * s.maxScale + s.maxApModMs, sampleRate) + 8.0f;
                aps[(size_t) i][(size_t) k].setMaxDelay ((int) std::ceil (apMax));
            }
        }

        lo = ShelfCoefficients::at (maps::lowCrossoverHz, sampleRate);

        // The longest line bends its pitch by at most 12 % while Size glides.
        scale.prepare (sampleRate, 220.0f, 0.12f / longest);

        // Seeded random walks, one per delay, each at its own rate.
        for (int j = 0; j < numDelays; ++j)
            walks[(size_t) j].setSeed (s.seed * 7919u + (uint32_t) j * 104729u + 11u);

        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            lines[(size_t) i].reset();
            filters[(size_t) i].reset();
            for (auto& ap : aps[(size_t) i])
                ap.reset();
        }
        for (auto& w : walks)
            w.reset();

        scale.snap (1.0f);
        delay.fill (0.0f);
        step.fill (0.0f);
        last.fill (0.0f);
        apGain.reset();
        hiA1.reset();
        hiA2.reset();
        hiA3.reset();
        snapNext = true;
    }

    /** Once per sub-block, before its samples. */
    void update (int numSamples, const Controls& c) noexcept
    {
        const auto n = std::max (numSamples, 1);
        const auto freeze = std::clamp (c.freeze, 0.0f, 1.0f);
        const auto still = 1.0f - freeze;

        if (snapNext)
            scale.snap (c.scale);
        else
            scale.setTarget (c.scale);
        const auto sc = scale.advance (n);

        const auto lineDepth = msToSamples (std::min (c.lineModMs, setup.maxLineModMs), fs);
        const auto apDepth = msToSamples (std::min (c.apModMs, setup.maxApModMs), fs);
        const auto hi = ShelfCoefficients::at (c.high.crossoverHz, fs);
        hiA1.set (hi.a1, n);
        hiA2.set (hi.a2, n);
        hiA3.set (hi.a3, n);

        for (int i = 0; i < N; ++i)
        {
            // Rates spread 0.7 .. 1.3x around the nominal one, interleaved so neighbours differ.
            const auto spread = 0.7f + 0.6f * (float) ((i * 7) % N) / (float) (N - 1);
            float loop = 0.0f;

            {
                const auto j = index (i, -1);
                const auto base = msToSamples (setup.lineMs[(size_t) i] * sc, fs);
                loop += base;
                walks[(size_t) j].setRate (setup.lineModHz * c.modRate * spread * still, fs);
                setTarget (j, base + lineDepth * walks[(size_t) j].advance (n), freeze, n, 4.0f);
            }

            for (int k = 0; k < Aps; ++k)
            {
                const auto j = index (i, k);
                const auto base = msToSamples (setup.apMs[(size_t) i][(size_t) k] * sc, fs);
                loop += base;
                const auto apSpread = 0.7f + 0.6f * (float) ((i * 5 + k * 11) % N) / (float) (N - 1);
                walks[(size_t) j].setRate (setup.apModHz * c.modRate * apSpread * still, fs);
                setTarget (j, base + apDepth * walks[(size_t) j].advance (n), freeze, n, 2.0f);
            }

            const auto g = computeBandGains (loop, c.rt60, c.lowEnd, c.high.ratio, freeze, (float) fs);
            filters[(size_t) i].setGains (g.low, g.mid, g.high, n);
        }

        apGain.set (c.apGain, n);
        snapNext = false;
    }

    /** One sample. in: N values injected into the lines; out: N line outputs (post-filter). */
    void process (const float* in, float* out) noexcept
    {
        std::array<float, (size_t) N> y;
        const auto g = apGain.next();
        const ShelfCoefficients hi { hiA1.next(), hiA2.next(), hiA3.next() };

        for (int i = 0; i < N; ++i)
        {
            auto j = index (i, -1);
            delay[(size_t) j] += step[(size_t) j];
            auto x = lines[(size_t) i].readCubic (delay[(size_t) j]);

            for (int k = 0; k < Aps; ++k)
            {
                j = index (i, k);
                delay[(size_t) j] += step[(size_t) j];
                x = aps[(size_t) i][(size_t) k].process (x, delay[(size_t) j], g);
            }

            y[(size_t) i] = filters[(size_t) i].process (x, lo, hi);
            out[i] = y[(size_t) i];
        }

        hadamardInPlace<N> (y.data());

        for (int i = 0; i < N; ++i)
            lines[(size_t) i].write (flushTiny (y[(size_t) i] + in[i]));
    }

private:
    static constexpr int numDelays = N * (Aps + 1);

    /** Delay slot of line i (k = -1) or of its k-th allpass. */
    static constexpr int index (int i, int k) noexcept { return i * (Aps + 1) + k + 1; }

    void setTarget (int j, float target, float freeze, int n, float minimum) noexcept
    {
        target = std::max (target, minimum);
        if (freeze >= 0.5f)
            target = std::round (target);   // integer lengths: exactly lossless when frozen
        // Restart each block from the exact previous target (accumulated float steps can miss
        // it by an ulp, which would leave a frozen delay a hair off-integer: slightly lossy).
        delay[(size_t) j] = snapNext ? target : last[(size_t) j];
        last[(size_t) j] = target;
        step[(size_t) j] = (target - delay[(size_t) j]) / (float) n;
    }

    double fs = 48000.0;
    Setup setup;
    std::array<DelayLine, (size_t) N> lines;
    std::array<std::array<Allpass, (size_t) Aps>, (size_t) N> aps;
    std::array<DecayFilter, (size_t) N> filters;
    ShelfCoefficients lo;
    LinearRamp hiA1, hiA2, hiA3;   // the high crossover moves with Tone: ramped per sample
    std::array<SmoothRandom, (size_t) numDelays> walks;
    std::array<float, (size_t) numDelays> delay {}, step {}, last {};
    GlideValue scale;
    LinearRamp apGain;
    bool snapNext = true;
};

} // namespace cerulean::dsp::nimbus
