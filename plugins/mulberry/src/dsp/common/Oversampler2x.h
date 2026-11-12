#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace mulberry::dsp
{

/**
    Per-sample 2x up- and downsampling with polyphase IIR half-band filters (two branches of
    first-order allpasses in z^-2; Valenzuela & Constantinides, with the elliptic-derived
    coefficient design of L. de Soras' HIIR, re-derived here). Each branch is an allpass, so the
    pair is power complementary and never gains: |H| <= 1 at every frequency, safe in a loop.

    For in-loop nonlinear stages: process one host sample, get two oversampled samples, process
    them, bring two back. The low-frequency group delay of the up/down pair is latencySamples()
    (host samples), which the tape machines take off their head distances.
*/
template <int numCoefficients>
    requires (numCoefficients % 2 == 0)
class HalfBandPair
{
public:
    /** transition: the normalised transition bandwidth (de Soras), the pass band ends at (0.5 - transition) fs. */
    explicit HalfBandPair (double transition) { design (transition); }

    void reset() noexcept
    {
        up = {};
        down = {};
    }

    /** One host sample in, two oversampled samples out. */
    void upsample (double x, double& y0, double& y1) noexcept
    {
        auto a = x, b = x;
        process (up, a, b);
        y0 = a;
        y1 = b;
    }

    /** Two oversampled samples in, one host sample out. */
    double downsample (double x0, double x1) noexcept
    {
        auto a = x1, b = x0;
        process (down, a, b);
        return 0.5 * (a + b);
    }

    /** Group delay of upsample() followed by downsample() at low frequencies, in host samples. */
    double latencySamples() const noexcept { return latency; }

    /** The pair's response at a frequency (as a fraction of the host rate), for tests. */
    std::complex<double> response (double normalisedFrequency) const noexcept
    {
        // Host-rate frequency w maps to w/2 at the oversampled rate; the half-band is
        // H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2 there, applied once going up and once going down.
        const auto w = std::numbers::pi * normalisedFrequency;
        const auto z1 = std::polar (1.0, -w);
        const auto z2 = z1 * z1;
        auto branch = [&] (int first)
        {
            std::complex<double> h { 1.0, 0.0 };
            for (int i = first; i < numCoefficients; i += 2)
                h *= (coefficients[(size_t) i] + z2) / (1.0 + coefficients[(size_t) i] * z2);
            return h;
        };
        const auto hb = 0.5 * (branch (0) + z1 * branch (1));
        return hb * hb;
    }

private:
    struct State
    {
        std::array<double, (size_t) numCoefficients> x {}, y {};
    };

    void process (State& s, double& even, double& odd) const noexcept
    {
        for (int i = 0; i < numCoefficients; i += 2)
        {
            const auto t0 = (even - s.y[(size_t) i]) * coefficients[(size_t) i] + s.x[(size_t) i];
            const auto t1 = (odd - s.y[(size_t) i + 1]) * coefficients[(size_t) i + 1] + s.x[(size_t) i + 1];
            s.x[(size_t) i] = even;
            s.x[(size_t) i + 1] = odd;
            s.y[(size_t) i] = t0;
            s.y[(size_t) i + 1] = t1;
            even = t0;
            odd = t1;
        }
    }

    /** Coefficients for a normalised transition bandwidth (de Soras' formulation). */
    void design (double transition)
    {
        const auto order = numCoefficients * 2 + 1;
        const auto kRaw = std::tan ((1.0 - 2.0 * transition) * std::numbers::pi / 4.0);
        const auto k = kRaw * kRaw;
        const auto kksqrt = std::pow (1.0 - k * k, 0.25);
        const auto e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const auto e4 = e * e * e * e;
        const auto q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

        for (int index = 0; index < numCoefficients; ++index)
        {
            const auto c = index + 1;
            double num = 0.0, den = 0.0;
            {
                int i = 0, j = 1;
                for (;;)
                {
                    const auto term = std::pow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * std::numbers::pi / order) * j;
                    num += term;
                    j = -j;
                    ++i;
                    if (std::abs (term) <= 1.0e-100)
                        break;
                }
            }
            {
                int i = 1, j = -1;
                for (;;)
                {
                    const auto term = std::pow (q, i * i) * std::cos (i * 2 * c * std::numbers::pi / order) * j;
                    den += term;
                    j = -j;
                    ++i;
                    if (std::abs (term) <= 1.0e-100)
                        break;
                }
            }
            num *= std::pow (q, 0.25);
            den += 0.5;
            const auto ww = num / den;
            const auto wwsq = ww * ww;
            const auto x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
            coefficients[(size_t) index] = (1.0 - x) / (1.0 + x);
        }

        // Low-frequency group delay of the pair (host samples), measured on the real structure:
        // tau = Re { DFT (n h) / DFT (h) } of its impulse response, at 1/1000 of the host rate.
        State u {}, d {};
        std::complex<double> sum {}, weighted {};
        const auto w = 2.0 * std::numbers::pi * 0.001;
        for (int n = 0; n < 512; ++n)
        {
            auto a = n == 0 ? 1.0 : 0.0, b = a;
            process (u, a, b);
            auto c = b, f = a;
            process (d, c, f);
            const auto y = 0.5 * (c + f);
            const auto rotation = std::polar (y, -w * n);
            sum += rotation;
            weighted += (double) n * rotation;
        }
        latency = (weighted / sum).real();
    }

    std::array<double, (size_t) numCoefficients> coefficients {};
    double latency = 0.0;
    State up, down;
};

/** The host-rate stage: 4 coefficients per branch, stop band below -100 dB above 0.54 fs. */
class Oversampler2x final : public HalfBandPair<8>
{
public:
    Oversampler2x() : HalfBandPair<8> (0.04) {}
};

/**
    4x or 8x as a cascade: the host-rate pair, then light pairs between 2x and 4x and between 4x
    and 8x. Each inner pass band holds everything the first stage passes; each inner stop band
    starts where products would fold under the first stage's pass band (-100 dB). Latency and
    response are of the whole cascade, at the host rate.
*/
template <int Factor>
class OversamplerCascade
{
public:
    static_assert (Factor == 4 || Factor == 8);
    static constexpr int factor = Factor;
    static constexpr int perDouble = Factor / 2;     // oversampled samples per 2x sample

    void reset() noexcept
    {
        outer.reset();
        mid.reset();
        inner.reset();
    }

    /** One 2x sample up to Factor / 2 samples at the full rate. */
    void upsampleFromDouble (double x, double* y) noexcept
    {
        if constexpr (Factor == 4)
        {
            mid.upsample (x, y[0], y[1]);
        }
        else
        {
            double a, b;
            mid.upsample (x, a, b);
            inner.upsample (a, y[0], y[1]);
            inner.upsample (b, y[2], y[3]);
        }
    }

    /** Factor / 2 samples at the full rate down to one 2x sample. */
    double downsampleToDouble (const double* y) noexcept
    {
        if constexpr (Factor == 4)
            return mid.downsample (y[0], y[1]);
        else
        {
            const auto a = inner.downsample (y[0], y[1]);   // in order: the stages keep state
            const auto b = inner.downsample (y[2], y[3]);
            return mid.downsample (a, b);
        }
    }

    double latencySamples() const noexcept
    {
        return outer.latencySamples() + 0.5 * mid.latencySamples() + (Factor == 8 ? 0.25 * inner.latencySamples() : 0.0);
    }

    std::complex<double> response (double normalisedFrequency) const noexcept
    {
        auto r = outer.response (normalisedFrequency) * mid.response (0.5 * normalisedFrequency);
        if constexpr (Factor == 8)
            r *= inner.response (0.25 * normalisedFrequency);
        return r;
    }

    Oversampler2x outer;
    HalfBandPair<4> mid { 0.22 };
    HalfBandPair<2> inner { 0.3 };
};

/**
    A linear-phase half-band FIR (Kaiser-windowed sinc, centre tap 1/2, every other tap 0), as a
    polyphase 2x interpolator and decimator. Its delay is the same at every frequency, so a path
    through it can be lined up exactly with an unfiltered one. K sets the length 4K + 3.
*/
template <int K>
class LinearPhaseHalfBand
{
public:
    static constexpr int taps = 2 * K + 2;          // non-zero taps of the odd phase

    explicit LinearPhaseHalfBand (double stopDb)
    {
        const auto beta = 0.1102 * (stopDb - 8.7);
        const auto c = 2 * K + 1;
        double sum = 0.0;
        for (int j = 0; j < taps; ++j)
        {
            const auto d = 2 * j - c;           // odd offsets from the centre
            const auto r = (double) d / (double) (c + 1);
            const auto w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            coefficients[(size_t) j] = std::sin (std::numbers::pi * d / 2.0) / (std::numbers::pi * d) * w;
            sum += coefficients[(size_t) j];
        }
        for (auto& v : coefficients)
            v *= 0.5 / sum;                     // with the centre's 1/2: unity at DC
    }

    void reset() noexcept
    {
        upHistory = {};
        downOdd = {};
        downEven = {};
        upIndex = downIndex = 0;
    }

    /** One sample in, two out (in time order). */
    void upsample (double x, double& y0, double& y1) noexcept
    {
        upIndex = (upIndex + taps - 1) % taps;
        upHistory[(size_t) upIndex] = x;
        upHistory[(size_t) (upIndex + taps)] = x;
        const auto* h = upHistory.data() + upIndex;    // h[j] = x[m - j]
        double acc = 0.0;
        for (int j = 0; j < taps; ++j)
            acc += coefficients[(size_t) j] * h[j];
        y0 = 2.0 * acc;
        y1 = h[K];
    }

    /** Two samples in (in time order), one out. */
    double downsample (double even, double odd) noexcept
    {
        downIndex = (downIndex + taps - 1) % taps;
        downOdd[(size_t) downIndex] = odd;
        downOdd[(size_t) (downIndex + taps)] = odd;
        downEven[(size_t) downIndex] = even;
        downEven[(size_t) (downIndex + taps)] = even;
        const auto* o = downOdd.data() + downIndex;
        double acc = 0.0;
        for (int j = 0; j < taps; ++j)
            acc += coefficients[(size_t) j] * o[j];
        return acc + 0.5 * downEven[(size_t) (downIndex + K)];
    }

private:
    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    std::array<double, (size_t) taps> coefficients {};
    std::array<double, (size_t) (2 * taps)> upHistory {}, downOdd {}, downEven {};
    int upIndex = 0, downIndex = 0;
};

} // namespace mulberry::dsp
