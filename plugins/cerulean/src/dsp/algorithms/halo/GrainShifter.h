#pragma once

#include "dsp/common/ReverbBlocks.h"

namespace cerulean::dsp::halo
{

/**
    One voice of a granular (delay-line) pitch shifter: overlapping windowed grains, each reading
    a shared history buffer with a read head that moves at `ratio` relative to the write head.

    - Grains are ~85 ms long (+-10 %) and start every ~28 ms (+-25 %), so about three overlap.
      The jitter is seeded per voice, so grains of different voices and lines never line up.
    - Each new grain's start is aligned (WSOLA-style) to the grain it fades in over: within
      +-5 ms of its nominal position it picks the lag where the input best matches what the
      previous grain is reading. On tonal material consecutive grains then add in phase instead
      of beating against each other (the classic grain-rate flutter); on diffuse material there is
      no clear match and the choice is effectively random, like plain jitter.
    - The window is (1 - x^2)^2 (a smooth Hann-like bell, zero slope at the edges). The output is
      normalised by the expected power of the grain sum, sum w_i w_j rho_ij, using the measured
      correlation rho between consecutive grains: unit power gain for tones (coherent grains)
      and diffuse input (incoherent grains) alike, so the loop energy does not depend on the
      material.
    - Each grain keeps the ratio it was started with. Changing the ratio therefore crossfades
      at grain level: old grains finish at the old interval while new ones fade in at the new
      one, over one grain length. No jump, no click.
*/
class GrainVoice
{
public:
    static constexpr int maxGrains = 6;

    static constexpr float grainSeconds = 0.085f;
    static constexpr float minDelaySeconds = 0.002f;
    static constexpr float searchSeconds = 0.005f;     // alignment range, +- around nominal
    static constexpr float spreadSeconds = 0.004f;     // random part of the nominal start delay
    static constexpr float matchSeconds = 0.006f;      // window compared when aligning
    static constexpr float rhoSeconds = 0.020f;        // window for the correlation estimate

    void prepare (double sampleRate, uint32_t seed) noexcept
    {
        fs = (float) sampleRate;
        startSeed = seed;
        grainSamples = grainSeconds * fs;
        hopSamples = grainSamples / 3.0f;
        minDelay = std::max (minDelaySeconds * fs, 3.0f);
        search = searchSeconds * fs;
        spread = spreadSeconds * fs;
        decimation = std::max (1, (int) std::lround (fs / 12000.0f));
        matchLength = (int) (matchSeconds * fs);
        rhoLength = (int) (rhoSeconds * fs);
        reset();
    }

    void reset() noexcept
    {
        rng.setSeed (startSeed);
        for (auto& g : grains)
            g = {};
        head = count = 0;
        countdown = hopSamples * rng.uniform();
    }

    /** Ratio used by grains started from now on (> 0). */
    void setRatio (float r) noexcept { ratio = r; }

    /** History (samples) a voice needs for ratios up to maxRatio. */
    static float historyFor (double sampleRate, float maxRatio) noexcept
    {
        const auto longest = minDelaySeconds + 2.0f * searchSeconds + spreadSeconds
                           + grainSeconds * 1.1f * std::max (maxRatio - 1.0f, 0.5f);
        return (float) sampleRate * (longest + 2.0f * searchSeconds + rhoSeconds) + 16.0f;
    }

    /** Window-weighted mean read delay (s) for a ratio: the latency this voice adds. */
    static float meanLatencySeconds (float r) noexcept
    {
        return minDelaySeconds + searchSeconds + 0.5f * spreadSeconds + grainSeconds * std::abs (r - 1.0f) * 0.5f;
    }

    /**
        Advances one sample. When `render` is false the grains only advance (cheap), so a voice
        that is faded out keeps its timing and comes back seamlessly.
    */
    float process (const DelayLine& history, bool render) noexcept
    {
        if ((countdown -= 1.0f) <= 0.0f)
        {
            startGrain (history, render);
            countdown += hopSamples * (1.0f + 0.25f * rng.bipolar());
        }

        float acc = 0.0f, power = 0.0f, chain = 0.0f, previousW = 0.0f;
        for (int m = 0; m < count; ++m)
        {
            auto& g = grains[(size_t) ((head + m) % maxGrains)];

            if (render)
            {
                const auto x = 2.0f * g.phase - 1.0f;
                const auto u = 1.0f - x * x;
                const auto w = u * u;
                acc += w * history.readCubic (g.delay);

                // sum_i w_i^2 + 2 sum_{i<j} w_i w_j rho_ij, with rho along the chain of grains.
                chain = g.rho * (chain + previousW);
                power += w * w + 2.0f * w * chain;
                previousW = w;
            }

            g.phase += g.increment;
            g.delay += g.step;
        }

        // Grains finish in start order (lengths vary less than the hop), so retire from the head.
        while (count > 0 && grains[(size_t) head].phase >= 1.0f)
        {
            head = (head + 1) % maxGrains;
            --count;
        }

        return render ? acc / std::sqrt (power + 0.02f) : 0.0f;
    }

private:
    struct Grain
    {
        float phase = 0.0f, increment = 0.0f, delay = 0.0f, step = 0.0f;
        float rho = 0.0f;   // correlation with the previous grain
    };

    /** Normalised cross-correlation of the history at delays a.. and b.. (n samples, stride s). */
    static float correlate (const DelayLine& h, int a, int b, int n, int stride, float& energyB) noexcept
    {
        float ab = 0.0f, bb = 0.0f;
        for (int k = 0; k < n; k += stride)
        {
            const auto x = h.read (a + k), y = h.read (b + k);
            ab += x * y;
            bb += y * y;
        }
        energyB = bb;
        return ab;
    }

    void startGrain (const DelayLine& history, bool align) noexcept
    {
        if (count >= maxGrains)
            return;

        const auto length = grainSamples * (1.0f + 0.1f * rng.bipolar());
        const auto sweep = length * std::max (ratio - 1.0f, 0.0f);   // up-shifts start further back
        const auto lowest = minDelay + sweep;
        auto start = lowest + search + spread * rng.uniform();
        float rho = 0.0f;

        if (align && count > 0)
        {
            const auto& previous = grains[(size_t) ((head + count - 1) % maxGrains)];
            const auto ref = (int) previous.delay;
            const auto lo = std::max ((int) std::ceil (lowest), (int) (start - search));
            const auto hi = (int) (start + search);

            // Coarse search on a decimated grid, then refine at full rate.
            float best = -1.0e30f;
            int bestLag = (int) start;
            for (int d = lo; d <= hi; d += decimation)
            {
                float e;
                const auto c = correlate (history, ref, d, matchLength, decimation, e);
                const auto score = c / std::sqrt (e + 1.0e-12f);
                if (score > best) { best = score; bestLag = d; }
            }
            best = -1.0e30f;
            const auto coarse = bestLag;
            for (int d = std::max (lo, coarse - decimation + 1); d <= std::min (hi, coarse + decimation - 1); ++d)
            {
                float e;
                const auto c = correlate (history, ref, d, matchLength, 1, e);
                const auto score = c / std::sqrt (e + 1.0e-12f);
                if (score > best) { best = score; bestLag = d; }
            }

            // Correlation the two grains will actually have (longer window, at the chosen lag).
            float eb, ea;
            const auto c = correlate (history, ref, bestLag, rhoLength, 1, eb);
            correlate (history, bestLag, ref, rhoLength, 1, ea);
            rho = std::clamp (c / std::sqrt (ea * eb + 1.0e-24f), 0.0f, 0.95f);

            // Keep the fractional part of the previous grain's delay, so the two read heads sit
            // exactly the chosen whole number of samples apart.
            start = (float) bestLag + (previous.delay - (float) ref);
        }

        auto& g = grains[(size_t) ((head + count) % maxGrains)];
        g.phase = 0.0f;
        g.increment = 1.0f / length;
        g.step = 1.0f - ratio;   // the read head moves at `ratio`: the delay changes by 1 - ratio per sample
        g.delay = std::max (start, lowest);
        g.rho = rho;
        ++count;
    }

    float fs = 48000.0f, grainSamples = 4080.0f, hopSamples = 1360.0f;
    float minDelay = 96.0f, search = 240.0f, spread = 192.0f;
    int decimation = 4, matchLength = 288, rhoLength = 960;
    float ratio = 1.0f, countdown = 0.0f;
    std::array<Grain, (size_t) maxGrains> grains {};
    int head = 0, count = 0;
    Rng rng;
    uint32_t startSeed = 1;
};

/**
    In-loop shifter for one tank line: a history buffer shared by two grain voices (the second
    only runs for the +12/+19 interval), summed, then band-limited (high cut against whistling
    build-up, low cut against sub-octave rumble).
*/
class GrainShifter
{
public:
    void prepare (double sampleRate, float maxRatio, uint32_t seed)
    {
        history.setMaxDelay ((int) std::ceil (GrainVoice::historyFor (sampleRate, maxRatio)));
        voices[0].prepare (sampleRate, seed * 2654435761u + 11u);
        voices[1].prepare (sampleRate, seed * 2246822519u + 29u);
        highCut.setCoefficients (5200.0f, 0.6f, sampleRate);
        lowCut.setCoefficients (80.0f, 0.7071f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        history.reset();
        voices[0].reset();
        voices[1].reset();
        highCut.reset();
        lowCut.reset();
    }

    /**
        The shifted path's high cut. Dark Tone settings pull it down (a darker shimmer that also
        keeps the high band's decay dark); flat/bright stay at 5.2 kHz, the anti-whistle limit.
    */
    void setHighCut (float hz, double sampleRate) noexcept { highCut.setCoefficients (hz, 0.6f, sampleRate); }

    void setRatios (float r1, float r2) noexcept
    {
        voices[0].setRatio (r1);
        voices[1].setRatio (r2);
    }

    /** gain1/gain2: per-voice gains (>= 0; 0 = that voice is not rendered). */
    float process (float x, float gain1, float gain2, bool render) noexcept
    {
        history.write (x);
        const auto a = voices[0].process (history, render && gain1 > 0.0f);
        const auto b = voices[1].process (history, render && gain2 > 0.0f);
        if (! render)
            return 0.0f;
        return highCut.lowpass (lowCut.highpass (gain1 * a + gain2 * b));
    }

private:
    DelayLine history;
    std::array<GrainVoice, 2> voices;
    Svf highCut;
    Svf lowCut;
};

} // namespace cerulean::dsp::halo
