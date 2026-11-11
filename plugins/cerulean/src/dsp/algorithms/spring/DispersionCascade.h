#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace cerulean::dsp
{

/**
    A cascade of "stretched" first-order allpasses, A(z^K) with A(z) = (a + z^-1) / (1 + a z^-1),
    run for four independent lanes (springs) at once.

    This is the dispersive core of the Välimäki/Parker/Abel spring model: replacing z^-1 by z^-K
    squeezes the allpass's whole phase response into 0 .. fs/(2K), so with a > 0 the group delay
    rises towards fc = fs/(2K) (high frequencies arrive later: the spring "drip" chirp). Above fc
    the response repeats as mirrored images, which the engine filters out after the loop.

    The stages come in two groups with their own coefficient: the first FirstGroup stages use
    aFirst, the rest aRest (a = 0 makes a stage a plain K-sample delay, so a group can be faded
    in and out of the dispersion smoothly).

    Because A(z^K) is K interleaved copies of A(z), each running at fs/K, sample n only touches
    the states of phase n mod K. The states are stored phase-major ([phase][stage][lane]), so one
    sample walks a contiguous block. Transposed direct form II:  y = a x + s,  s <- x - a y.
    Per stage that is one FMA on the critical path, four lanes wide.

    Lossless for any fixed a in (-1, 1) (|A| = 1 exactly, also with the coefficient rounded to
    float), so a frozen loop built around it holds its energy. No allocation after prepare().
*/
template <int Stages, int FirstGroup>
class DispersionCascade
{
public:
    static_assert (FirstGroup >= 0 && FirstGroup <= Stages);

    static constexpr int lanes = 4;
    using Lanes = std::array<float, (size_t) lanes>;

    /** Allocates. stretch = K >= 1. */
    void prepare (int stretch)
    {
        K = std::max (stretch, 1);
        state.assign ((size_t) (K * Stages * lanes), 0.0f);
        phase = 0;
    }

    void reset() noexcept
    {
        std::fill (state.begin(), state.end(), 0.0f);
        phase = 0;
    }

    int getStretch() const noexcept { return K; }

    /** One sample for all lanes, in place. aFirst / aRest: per-lane coefficients of each group. */
    void process (Lanes& x, const Lanes& aFirst, const Lanes& aRest) noexcept
    {
        float* __restrict s = state.data() + (size_t) (phase * Stages * lanes);
        alignas (16) float v[lanes] = { x[0], x[1], x[2], x[3] };

        auto run = [&v, &s] (const Lanes& a, int count)
        {
            alignas (16) const float c[lanes] = { a[0], a[1], a[2], a[3] };
            for (int m = 0; m < count; ++m, s += lanes)
                for (int l = 0; l < lanes; ++l)
                {
                    const auto y = c[l] * v[l] + s[l];
                    s[l] = v[l] - c[l] * y;
                    v[l] = y;
                }
        };
        run (aFirst, FirstGroup);
        run (aRest, Stages - FirstGroup);

        for (int l = 0; l < lanes; ++l)
            x[(size_t) l] = v[l];

        if (++phase == K)
            phase = 0;
    }

    /**
        A block of samples, in place (x, aFirst, aRest: numSamples entries each). Same result as
        calling process() per sample: consecutive samples touch different phase rows, so their
        cascades are independent and four of them run interleaved (four times the instruction-
        level parallelism of one latency-bound chain).
    */
    void processBlock (Lanes* x, const Lanes* aFirst, const Lanes* aRest, int numSamples) noexcept
    {
        int i = 0;
        if (K >= interleave)
            for (; i + interleave <= numSamples; i += interleave)
                processInterleaved (x + i, aFirst + i, aRest + i);
        for (; i < numSamples; ++i)
            process (x[i], aFirst[i], aRest[i]);
    }

    /** Group delay (samples) of one stretched stage at omega (rad/sample). */
    static float stageGroupDelay (float a, int stretch, float omega) noexcept
    {
        const auto c = std::cos ((float) stretch * omega);
        return (float) stretch * (1.0f - a * a) / std::max (1.0f + 2.0f * a * c + a * a, 1.0e-6f);
    }

    /** Group delay (samples) of the whole cascade. */
    static float groupDelay (float aFirst, float aRest, int stretch, float omega) noexcept
    {
        return (float) FirstGroup * stageGroupDelay (aFirst, stretch, omega)
             + (float) (Stages - FirstGroup) * stageGroupDelay (aRest, stretch, omega);
    }

private:
    static constexpr int interleave = 4;

    void processInterleaved (Lanes* x, const Lanes* aFirst, const Lanes* aRest) noexcept
    {
        constexpr int G = interleave;
        float* __restrict s[G];
        alignas (16) float v[G][lanes], c1[G][lanes], c2[G][lanes];
        for (int g = 0; g < G; ++g)
        {
            auto row = phase + g;
            if (row >= K)
                row -= K;
            s[g] = state.data() + (size_t) (row * Stages * lanes);
            for (int l = 0; l < lanes; ++l)
            {
                v[g][l] = x[g][(size_t) l];
                c1[g][l] = aFirst[g][(size_t) l];
                c2[g][l] = aRest[g][(size_t) l];
            }
        }

        auto run = [&] (const float (&c)[G][lanes], int begin, int end)
        {
            for (int m = begin; m < end; ++m)
                for (int g = 0; g < G; ++g)
                {
                    float* __restrict st = s[g] + m * lanes;
                    for (int l = 0; l < lanes; ++l)
                    {
                        const auto y = c[g][l] * v[g][l] + st[l];
                        st[l] = v[g][l] - c[g][l] * y;
                        v[g][l] = y;
                    }
                }
        };
        run (c1, 0, FirstGroup);
        run (c2, FirstGroup, Stages);

        for (int g = 0; g < G; ++g)
            for (int l = 0; l < lanes; ++l)
                x[g][(size_t) l] = v[g][l];

        phase += G;
        while (phase >= K)
            phase -= K;
    }

    std::vector<float> state;
    int K = 1, phase = 0;
};

} // namespace cerulean::dsp
