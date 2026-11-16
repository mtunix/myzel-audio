#pragma once

#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace halite::dsp
{

/**
    FFT of a real frame of 2^order points, forward and inverse: a radix-2 complex FFT of half the
    length on the even and odd samples, then the split into the real frame's spectrum. No lock, no
    stack scratch and no allocation after prepare(), unlike JUCE's fallback engine, which locks a
    spin lock and copies the frame into a full-length complex transform on the stack.
*/
class RealFft
{
public:
    /** Allocates. */
    void prepare (int order)
    {
        n = 1 << order;
        half = n / 2;

        reverse.assign ((size_t) half, 0);
        for (int i = 0, bits = order - 1; i < half; ++i)
            for (int b = 0; b < bits; ++b)
                reverse[(size_t) i] |= ((i >> b) & 1) << (bits - 1 - b);

        twiddleRe.resize ((size_t) half);
        twiddleIm.resize ((size_t) half);
        for (int k = 0; k < half; ++k)
        {
            twiddleRe[(size_t) k] = (float) std::cos (2.0 * std::numbers::pi * k / half);
            twiddleIm[(size_t) k] = (float) -std::sin (2.0 * std::numbers::pi * k / half);
        }

        splitRe.resize ((size_t) half + 1);
        splitIm.resize ((size_t) half + 1);
        for (int k = 0; k <= half; ++k)
        {
            splitRe[(size_t) k] = (float) std::cos (2.0 * std::numbers::pi * k / n);
            splitIm[(size_t) k] = (float) -std::sin (2.0 * std::numbers::pi * k / n);
        }

        re.assign ((size_t) half, 0.0f);
        im.assign ((size_t) half, 0.0f);
    }

    int size() const noexcept { return n; }

    /** `in`: size() samples. Writes bins 0 … size()/2 to outRe and outIm (size()/2 + 1 each). Real-time safe. */
    void forward (const float* in, float* outRe, float* outIm) noexcept
    {
        for (int i = 0; i < half; ++i)
        {
            const auto j = (size_t) reverse[(size_t) i];
            re[j] = in[2 * i];
            im[j] = in[2 * i + 1];
        }

        transform();

        // X[k] = E[k] + W^k O[k], with E and O recovered from Z[k] and conj (Z[half − k]).
        for (int k = 0; k <= half; ++k)
        {
            const auto p = (size_t) (k % half), q = (size_t) ((half - k) % half);
            const auto evenRe = 0.5f * (re[p] + re[q]), evenIm = 0.5f * (im[p] - im[q]);
            const auto oddRe = 0.5f * (im[p] + im[q]), oddIm = -0.5f * (re[p] - re[q]);
            const auto wr = splitRe[(size_t) k], wi = splitIm[(size_t) k];
            outRe[k] = evenRe + oddRe * wr - oddIm * wi;
            outIm[k] = evenIm + oddRe * wi + oddIm * wr;
        }
    }

    /**
        Inverse of forward(): bins 0 … size()/2 of a real frame's spectrum (the imaginary parts of
        bins 0 and size()/2 are ignored) to size() samples in `out`, scaled so that
        inverse (forward (x)) = x. Real-time safe.
    */
    void inverse (const float* inRe, const float* inIm, float* out) noexcept
    {
        // Z[k] = E[k] + i O[k], the spectrum of z[n] = x[2n] + i x[2n + 1], with
        // E = (X[k] + conj X[half − k]) / 2 and O = (X[k] − conj X[half − k]) / (2 W^k); its inverse
        // runs on the forward transform as conj (FFT (conj Z)) / half.
        for (int k = 0; k < half; ++k)
        {
            const auto q = (size_t) (half - k);
            const auto evenRe = 0.5f * (inRe[k] + inRe[q]), evenIm = 0.5f * (inIm[k] - inIm[q]);
            const auto diffRe = 0.5f * (inRe[k] - inRe[q]), diffIm = 0.5f * (inIm[k] + inIm[q]);
            const auto wr = splitRe[(size_t) k], wi = splitIm[(size_t) k];
            const auto oddRe = diffRe * wr + diffIm * wi, oddIm = diffIm * wr - diffRe * wi;
            const auto j = (size_t) reverse[(size_t) k];
            re[j] = evenRe - oddIm;
            im[j] = -(evenIm + oddRe);
        }

        transform();

        const auto scale = 1.0f / (float) half;
        for (int i = 0; i < half; ++i)
        {
            out[2 * i] = re[(size_t) i] * scale;
            out[2 * i + 1] = -im[(size_t) i] * scale;
        }
    }

private:
    /** In place on re and im, the input in bit-reversed order: the radix-2 butterflies. */
    void transform() noexcept
    {
        for (int span = 1, stride = half / 2; span < half; span *= 2, stride /= 2)
            for (int start = 0; start < half; start += 2 * span)
                for (int k = 0; k < span; ++k)
                {
                    const auto a = (size_t) (start + k), b = a + (size_t) span;
                    const auto wr = twiddleRe[(size_t) (k * stride)], wi = twiddleIm[(size_t) (k * stride)];
                    const auto tr = re[b] * wr - im[b] * wi;
                    const auto ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
    }

    int n = 0, half = 0;
    std::vector<int> reverse;
    std::vector<float> twiddleRe, twiddleIm, splitRe, splitIm, re, im;
};

} // namespace halite::dsp
