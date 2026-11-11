#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cerulean::dsp
{

/**
    Topology-preserving-transform filters (Zavalishin / Simper), double precision.

    These stay numerically well-behaved at very high oversampled rates with very low cutoffs
    (e.g. 20 Hz at 768 kHz), where direct-form biquads in float fall apart.
*/

inline double prewarp (double cutoffHz, double sampleRate) noexcept
{
    const auto fc = std::clamp (cutoffHz, 1.0, 0.49 * sampleRate);
    return std::tan (std::numbers::pi * fc / sampleRate);
}

/** 2-pole state-variable filter with simultaneous LP / BP / HP outputs. */
class TptSvf
{
public:
    struct Outputs { double lp, bp, hp; };

    void setCoefficients (double cutoffHz, double q, double sampleRate) noexcept
    {
        g  = prewarp (cutoffHz, sampleRate);
        k  = 1.0 / std::max (q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0; }

    Outputs process (double x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }

    double processLowpass  (double x) noexcept { return process (x).lp; }
    double processHighpass (double x) noexcept { return process (x).hp; }

private:
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
};

/** 1-pole TPT filter with LP and HP outputs (lp + hp == input). */
class TptOnePole
{
public:
    struct Outputs { double lp, hp; };

    void setCutoff (double cutoffHz, double sampleRate) noexcept
    {
        const auto g = prewarp (cutoffHz, sampleRate);
        G = g / (1.0 + g);
    }

    void reset() noexcept { s = 0.0; }

    Outputs process (double x) noexcept
    {
        const auto v  = (x - s) * G;
        const auto lp = v + s;
        s = lp + v;
        return { lp, x - lp };
    }

    double processLowpass  (double x) noexcept { return process (x).lp; }
    double processHighpass (double x) noexcept { return process (x).hp; }

private:
    double G = 0.0, s = 0.0;
};

/** Gentle DC blocker (1-pole highpass, ~5 Hz by default). */
class DcBlocker
{
public:
    void prepare (double sampleRate, double cutoffHz = 5.0) noexcept { filter.setCutoff (cutoffHz, sampleRate); }
    void reset() noexcept { filter.reset(); }
    double process (double x) noexcept { return filter.processHighpass (x); }

private:
    TptOnePole filter;
};

/**
    First-order tilt EQ: splits at the pivot and scales lows/highs in opposite directions.
    tilt in [-1, 1]: -1 = dark, +1 = bright. maxDb is the boost/cut per side at the extremes.
*/
class TiltEq
{
public:
    void setCoefficients (double pivotHz, double tilt, double maxDb, double sampleRate) noexcept
    {
        split.setCutoff (pivotHz, sampleRate);
        const auto db = std::clamp (tilt, -1.0, 1.0) * maxDb;
        highGain = std::pow (10.0, db / 20.0);
        lowGain  = 1.0 / highGain;
    }

    void reset() noexcept { split.reset(); }

    double process (double x) noexcept
    {
        const auto [lp, hp] = split.process (x);
        return lowGain * lp + highGain * hp;
    }

private:
    TptOnePole split;
    double lowGain = 1.0, highGain = 1.0;
};

/**
    First-order shelf for pre-/de-emphasis around a nonlinearity.

    H(s) = (g s + w0) / (s + g w0): high-frequency gain g, low-frequency gain 1/g, pole and zero
    placed symmetrically around the pivot w0. The filter with gain 1/g is the exact inverse
    (pole and zero swap), and because both use the same prewarped pivot their bilinear
    transforms are exact inverses too. So pre-emphasis -> (linear circuit) -> de-emphasis is
    transparent, and only the nonlinearity "sees" the tilt.
*/
class EmphasisShelf
{
public:
    void setCoefficients (double pivotHz, double hfGain, double sampleRate) noexcept
    {
        hfGain = std::clamp (hfGain, 0.05, 20.0);
        const auto g = prewarp (pivotHz, sampleRate) * hfGain;   // pole, in prewarped units
        G = g / (1.0 + g);
        high = hfGain;
        low = 1.0 / hfGain;
    }

    void reset() noexcept { s = 0.0; }

    double process (double x) noexcept
    {
        const auto v  = (x - s) * G;
        const auto lp = v + s;
        s = lp + v;
        return high * (x - lp) + low * lp;
    }

private:
    double G = 0.0, s = 0.0, high = 1.0, low = 1.0;
};

} // namespace cerulean::dsp
