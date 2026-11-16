#pragma once

// Measuring Split's filter: its response at a frozen depth, in double precision, against the
// analog prototype computed from the elliptic low-pass's poles and zeros; and the band's own
// power, which Split and Spectral are gated on (Settled 39, 41, 44).

#include "dsp/shapes/split/SplitFilter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace halite::testing
{

using Complex = std::complex<double>;

/** In-place radix-2 FFT of a power-of-two length, double precision. */
class DoubleFft
{
public:
    void transform (std::vector<Complex>& a)
    {
        const auto n = a.size();
        if (twiddles.size() != n / 2)
        {
            twiddles.resize (n / 2);
            for (size_t k = 0; k < n / 2; ++k)
                twiddles[k] = std::polar (1.0, -2.0 * std::numbers::pi * (double) k / (double) n);
        }

        for (size_t i = 1, j = 0; i < n; ++i)
        {
            auto bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1)
                j ^= bit;
            j |= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }

        for (size_t len = 2; len <= n; len <<= 1)
            for (size_t i = 0; i < n; i += len)
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto w = twiddles[k * (n / len)] * a[i + k + len / 2];
                    a[i + k + len / 2] = a[i + k] - w;
                    a[i + k] += w;
                }
    }

private:
    std::vector<Complex> twiddles;
};

/**
    What the band's own power lost (Settled 39): the power in the bins from `low` to `high`, each
    weighted by the detector's band filter there (as Spectral weighs them), output against input
    over [from, to); 4096-point Hann frames at 48 kHz (scaled with the rate), a quarter apart.
*/
inline double ownBandLostDb (const std::vector<float>& in, const std::vector<float>& out, int from, int to, double low, double high,
                             double fs)
{
    auto n = 1;
    while (n < (int) std::lround (4096.0 * fs / 48000.0))
        n <<= 1;
    DoubleFft fft;
    std::vector<Complex> a ((size_t) n), b ((size_t) n);
    const auto warp = [fs] (double hz) { return std::tan (std::numbers::pi * std::min (hz, 0.49 * fs) / fs); };
    auto had = 0.0, kept = 0.0;
    for (auto start = from; start + n <= to; start += n / 4)
    {
        for (int j = 0; j < n; ++j)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * j / n);
            a[(size_t) j] = w * in[(size_t) (start + j)];
            b[(size_t) j] = w * out[(size_t) (start + j)];
        }
        fft.transform (a);
        fft.transform (b);
        for (int k = 1; k < n / 2; ++k)
        {
            const auto hz = k * fs / n;
            if (hz < low || hz > high)
                continue;
            const auto weight = 1.0 / (1.0 + std::pow (warp (low) / warp (hz), 4.0)) / (1.0 + std::pow (warp (hz) / warp (high), 4.0));
            had += weight * std::norm (a[(size_t) k]);
            kept += weight * std::norm (b[(size_t) k]);
        }
    }
    return 10.0 * std::log10 (had / kept);
}

/**
    The analog prototype in dB from the elliptic low-pass's poles and zeros (not from the design's
    polynomials or roots): a shelf from `lowHz`, or the bell over [lowHz, highHz], the cut shelf
    at its low edge over a boost shelf a third of an octave past its high edge.
*/
inline double analogDb (const dsp::splitDesign::Prototype& p, dsp::SplitKind kind, double lowHz, double highHz, double hz, double gain)
{
    using namespace dsp::splitDesign;
    const auto cut = analogShelfFromPoles (p, lowHz, hz, gain);
    const auto power = kind == dsp::SplitKind::bell ? cut / analogShelfFromPoles (p, highHz * guard, hz, gain) : cut;
    return 10.0 * std::log10 (power);
}

/**
    The real filter's response x − removed to `x` at a frozen depth, from rest: one silent design
    segment first, so the signal meets the depth's own design rather than the ramp to it.
*/
inline std::vector<double> frozenResponse (dsp::SplitFilter& filter, float depth, const std::vector<double>& x)
{
    constexpr int lead = dsp::SplitFilter::interval;
    filter.reset();
    std::vector<double> in (x.size() + lead, 0.0), removed (in.size());
    std::copy (x.begin(), x.end(), in.begin() + lead);
    const std::vector<float> depths (in.size() + lead, depth);
    filter.process (in.data(), depths.data(), removed.data(), (int) in.size(), 0);

    std::vector<double> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = x[i] - removed[i + lead];
    return y;
}

/** The real filter's impulse response, x − removed, at a frozen depth, from rest. */
inline std::vector<double> impulseResponse (dsp::SplitFilter& filter, float depth, int n)
{
    std::vector<double> x ((size_t) n, 0.0);
    x[0] = 1.0;
    return frozenResponse (filter, depth, x);
}

/** |H| in dB at one frequency, by a direct DFT of an impulse response. */
inline double responseDbAt (const std::vector<double>& h, double hz, double rate)
{
    Complex sum {};
    for (size_t i = 0; i < h.size(); ++i)
        sum += h[i] * std::polar (1.0, -2.0 * std::numbers::pi * hz * (double) i / rate);
    return 20.0 * std::log10 (std::abs (sum));
}

/** A section's response: the bilinear image of the analog SVF (or one-pole) and its mix. */
inline Complex sectionResponse (const dsp::splitDesign::Section& c, bool onePole, double hz, double rate)
{
    const Complex s (0.0, std::tan (std::numbers::pi * hz / rate));
    if (onePole)
    {
        const auto g = c.a1 / (1.0 - c.a1);
        return 1.0 - c.r0 * s / (s + g);
    }
    const auto g = c.a2 / c.a1, k = (1.0 / c.a1 - 1.0) / g - g;
    const auto d = s * s + g * k * s + g * g;
    const auto bp = g * s / d, lp = g * g / d;
    return 1.0 - (c.r0 * (1.0 - lp) + c.r1 * bp);
}

/** A design's digital response in dB: its sections in cascade and its gain. */
inline double designDb (const dsp::splitDesign::Prototype& p, dsp::SplitKind kind, const dsp::splitDesign::Design& d, double hz, double rate)
{
    const auto perShelf = p.pairs + p.reals, count = perShelf * (kind == dsp::SplitKind::bell ? 2 : 1);
    Complex h = d.gain;
    for (int k = 0; k < count; ++k)
        h *= sectionResponse (d.sections[(size_t) k], k % perShelf >= p.pairs, hz, rate);
    return 20.0 * std::log10 (std::abs (h));
}

/** The design for one band at a depth, as the filter makes it once the band has settled. */
inline dsp::splitDesign::Design designFor (const dsp::splitDesign::Prototype& p, dsp::SplitKind kind, double lowHz, double highHz,
                                           double rate, double gain)
{
    using namespace dsp::splitDesign;
    Design d;
    design (p, kind, baseFor (p, lowHz, rate), baseFor (p, highHz * guard, rate), gain, d);
    return d;
}

/** The depth the chain hands the filter for a cut of `db`, and the gain the filter then applies. */
inline float depthFor (double db) { return (float) (1.0 - std::pow (10.0, -db / 20.0)); }
inline double gainOf (float depth) { return 1.0 - (double) depth; }

} // namespace halite::testing
