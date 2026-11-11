#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace umber::dsp::reel
{

/**
    Linear building blocks for the Reel record / playback networks (double precision).

    These mirror the shared TPT filters in dsp/common/Filters.h but additionally expose
    (a) their exact digital frequency response, used to normalise the model's small-signal
    gain analytically, and (b) a cheap denormal flush, so the model stays fast during long
    silences even when the host does not enable flush-to-zero.
*/

/** Flushes |x| < 1e-30 to zero (far above the denormal range, far below audibility). */
inline void flushTiny (double& x) noexcept
{
    if (std::abs (x) < 1.0e-30)
        x = 0.0;
}

/** Normalised TPT frequency variable: s/omega_c at frequency f for integrator gain g. */
inline std::complex<double> tptS (double freq, double g, double sampleRate) noexcept
{
    const auto f = std::min (freq, 0.4999 * sampleRate);
    return { 0.0, std::tan (std::numbers::pi * f / sampleRate) / g };
}

/**
    TPT integrator gain g for an analog cutoff fc, matched so that the digital magnitude
    equals the analog one at fMatch (instead of at fc). For cutoffs near or above Nyquist
    this keeps the in-band response consistent across sample rates.
*/
inline double matchedG (double fc, double fMatch, double sampleRate) noexcept
{
    const auto fm = std::min (fMatch, 0.45 * sampleRate);
    return std::tan (std::numbers::pi * fm / sampleRate) * fc / fm;
}

/** Plain bilinear prewarp at the cutoff itself. */
inline double prewarpedG (double fc, double sampleRate) noexcept
{
    return std::tan (std::numbers::pi * std::min (fc, 0.49 * sampleRate) / sampleRate);
}

/** 1-pole TPT section with LP / HP outputs. */
class OnePole
{
public:
    void setG (double g) noexcept
    {
        gain = g;
        G = g / (1.0 + g);
    }

    void reset() noexcept { s = 0.0; }
    void flush() noexcept { flushTiny (s); }

    double lowpass (double x) noexcept
    {
        const auto v = (x - s) * G;
        const auto lp = v + s;
        s = lp + v;
        return lp;
    }

    double highpass (double x) noexcept { return x - lowpass (x); }

    std::complex<double> lowpassResponse (double freq, double sampleRate) const noexcept
    {
        const auto sn = tptS (freq, gain, sampleRate);
        return 1.0 / (1.0 + sn);
    }

    std::complex<double> highpassResponse (double freq, double sampleRate) const noexcept
    {
        const auto sn = tptS (freq, gain, sampleRate);
        return sn / (1.0 + sn);
    }

private:
    double gain = 1.0, G = 0.5, s = 0.0;
};

/** 2-pole TPT state-variable filter (Simper). */
class Svf
{
public:
    struct Outputs { double lp, bp, hp; };

    void setG (double g, double q) noexcept
    {
        gain = g;
        k  = 1.0 / std::max (q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0; }
    void flush() noexcept { flushTiny (ic1); flushTiny (ic2); }

    Outputs process (double x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }

    double damping() const noexcept { return k; }

    /** High-pass output response. */
    std::complex<double> highpassResponse (double freq, double sampleRate) const noexcept
    {
        const auto sn = tptS (freq, gain, sampleRate);
        return sn * sn / (sn * sn + k * sn + 1.0);
    }

    /** Band-pass output response (peak gain 1/k at the cutoff). */
    std::complex<double> bandpassResponse (double freq, double sampleRate) const noexcept
    {
        const auto sn = tptS (freq, gain, sampleRate);
        return sn / (sn * sn + k * sn + 1.0);
    }

private:
    double gain = 1.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
};

/**
    First-order pole/zero section H(s) = (1 + s/wz) / (1 + s/wp), bilinear transform with each
    corner prewarped individually. `inverse()` gives the exact digital inverse (1/H(z)), so a
    pre-emphasis followed by its de-emphasis is bit-for-bit transparent at low level at every
    sample rate. Transposed direct form II.
*/
class ShelfSection
{
public:
    void design (double zeroHz, double poleHz, double sampleRate) noexcept
    {
        // s = K (1 - z^-1) / (1 + z^-1); prewarped corners: 1 + s/w = 1 + (1 - z^-1)/(g (1 + z^-1))
        const auto gz = prewarpedG (zeroHz, sampleRate);
        const auto gp = prewarpedG (poleHz, sampleRate);
        // numerator (gz + 1) + (gz - 1) z^-1, over gz ; denominator likewise with gp
        nb0 = (gz + 1.0) / gz;
        nb1 = (gz - 1.0) / gz;
        da0 = (gp + 1.0) / gp;
        da1 = (gp - 1.0) / gp;
        setFrom (nb0, nb1, da0, da1);
    }

    /** Turns this section into the exact inverse of `other`. */
    void designInverse (const ShelfSection& other) noexcept
    {
        nb0 = other.da0;
        nb1 = other.da1;
        da0 = other.nb0;
        da1 = other.nb1;
        setFrom (nb0, nb1, da0, da1);
    }

    void reset() noexcept { z1 = 0.0; }
    void flush() noexcept { flushTiny (z1); }

    double process (double x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y;
        return y;
    }

    std::complex<double> response (double freq, double sampleRate) const noexcept
    {
        const auto zi = std::polar (1.0, -2.0 * std::numbers::pi * freq / sampleRate);
        return (b0 + b1 * zi) / (1.0 + a1 * zi);
    }

private:
    void setFrom (double n0, double n1, double d0, double d1) noexcept
    {
        b0 = n0 / d0;
        b1 = n1 / d0;
        a1 = d1 / d0;
    }

    double nb0 = 1.0, nb1 = 0.0, da0 = 1.0, da1 = 0.0;
    double b0 = 1.0, b1 = 0.0, a1 = 0.0, z1 = 0.0;
};

} // namespace umber::dsp::reel
