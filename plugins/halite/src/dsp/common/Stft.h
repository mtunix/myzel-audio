#pragma once

#include "dsp/common/RealFft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace halite::dsp
{

/**
    Short-time Fourier analysis and weighted overlap-add synthesis of what to remove (PLAN.md,
    Reduction, Spectral; src/dsp/shapes/spectral/NOTES.md): periodic Hann analysis and synthesis
    windows at 75 % overlap, whose product sums to 1.5 over the four overlapping frames, so the
    synthesis is scaled by 1 / 1.5.

    It runs one sample behind its own frame grid: before sample t enters, a caller at a frame
    boundary analyses the frame of the last frameSize() samples [t − N, t) and synthesises the
    spectra each output removes from it; read() then gives what was removed from sample t − N,
    whose frames are all done. The latency is exactly one frame, wherever the grid lies. An output
    no frame wrote to reads exactly 0: a spectrum of zeros is never synthesised, so a frame whose
    gains are all 1 adds nothing, not rounding.
*/
class Stft
{
public:
    static constexpr int maxOutputs = 2;

    /** Allocates. `frameSize` a power of two, at least 16. */
    void prepare (int frameSize, int numOutputs)
    {
        auto order = 0;
        while ((1 << order) < frameSize)
            ++order;
        n = 1 << order;
        mask = n - 1;
        outputs = std::clamp (numOutputs, 1, maxOutputs);
        fft.prepare (order);

        window.resize ((size_t) n);
        for (int j = 0; j < n; ++j)
            window[(size_t) j] = (float) (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * j / n));

        input.assign ((size_t) n, 0.0f);
        for (auto& o : overlap)
            o.assign ((size_t) n, 0.0f);
        frame.assign ((size_t) n, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (input.begin(), input.end(), 0.0f);
        for (auto& o : overlap)
            std::fill (o.begin(), o.end(), 0.0f);
        write = 0;
    }

    /** Takes the other's last frameSize() samples of input, nothing it has synthesised. Same prepare(). Real-time safe. */
    void continueFrom (const Stft& other) noexcept
    {
        std::copy (other.input.begin(), other.input.end(), input.begin());
        for (auto& o : overlap)
            std::fill (o.begin(), o.end(), 0.0f);
        write = other.write;
    }

    int frameSize() const noexcept { return n; }
    int hopSize() const noexcept { return n / 4; }
    int numBins() const noexcept { return n / 2 + 1; }

    /** The analysis-windowed frame of the last frameSize() samples, forward: numBins() bins each. Real-time safe. */
    void analyse (float* re, float* im) noexcept
    {
        for (int j = 0; j < n; ++j)
            frame[(size_t) j] = input[(size_t) ((write + j) & mask)] * window[(size_t) j];
        fft.forward (frame.data(), re, im);
    }

    /** Adds the frame whose spectrum is (re, im) to an output: inverse, synthesis window, 1 / 1.5. Real-time safe. */
    void synthesise (const float* re, const float* im, int output) noexcept
    {
        fft.inverse (re, im, frame.data());
        auto& o = overlap[(size_t) output];
        constexpr auto scale = 1.0f / 1.5f;
        for (int j = 0; j < n; ++j)
        {
            auto& slot = o[(size_t) ((write + j) & mask)];
            slot += frame[(size_t) j] * (window[(size_t) j] * scale);
        }
    }

    /**
        Moves m samples on: each output's samples for [t − N, t − N + m) into removed[output]
        (null skips one), x in. Between two frames only.
    */
    void step (const float* x, float* const* removed, int m) noexcept
    {
        for (int o = 0; o < outputs; ++o)
        {
            auto& ring = overlap[(size_t) o];
            auto* out = removed[o];
            for (int i = 0; i < m; ++i)
            {
                auto& slot = ring[(size_t) ((write + i) & mask)];
                if (out != nullptr)
                    out[i] = slot;
                slot = 0.0f;
            }
        }
        for (int i = 0; i < m; ++i)
            input[(size_t) ((write + i) & mask)] = x[i];
        write = (write + m) & mask;
    }

private:
    int n = 0, mask = 0, outputs = 1, write = 0;
    RealFft fft;
    std::vector<float> window, input, frame;
    std::array<std::vector<float>, maxOutputs> overlap;
};

} // namespace halite::dsp
