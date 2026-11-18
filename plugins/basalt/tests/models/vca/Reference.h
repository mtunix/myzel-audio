#pragma once

// VCA's specification as pure functions in double precision, written from
// src/dsp/models/vca/NOTES.md and never from the engine's code: what the unit tests and
// BasaltMeasure compare the VCA engine with (the static curve with the moving threshold, the times
// the knobs mean, the program-dependent rows of the NOTES' gate table, and a transcription of the
// Auto network). Header-only, no JUCE, no engine includes.

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace basalt::reference::vca
{

/** S in T_eff = T − S/R. */
inline constexpr double thresholdShiftDb = 6.0;

/** 10·log10(2): the carrier reads this much higher on the RMS detector than on the peak one. */
inline constexpr double carrierRmsOffsetDb = 3.0102999566398120;

inline double inverseRatioOf (double ratio) { return std::isinf (ratio) ? 0.0 : 1.0 / ratio; }

/** T_eff = T − S/R, 1/R = 0 at ∞. */
inline double effectiveThresholdDb (double thresholdDb, double inverseRatio)
{
    return thresholdDb - thresholdShiftDb * inverseRatio;
}

/**
    The static reduction both detectors settle on for a level on their own reading:
    min(x − G(x; T_eff, R, W), Range), G Giannoulis 2012 eq. 4 centred on T_eff.
*/
inline double staticReductionDb (double levelDb, double thresholdDb, double inverseRatio, double kneeDb, double rangeDb)
{
    const auto t = effectiveThresholdDb (thresholdDb, inverseRatio);
    const auto x = levelDb;
    double g;

    if (2.0 * (x - t) < -kneeDb)
        g = x;
    else if (kneeDb > 0.0 && 2.0 * std::abs (x - t) <= kneeDb)
        g = x + (inverseRatio - 1.0) * (x - t + 0.5 * kneeDb) * (x - t + 0.5 * kneeDb) / (2.0 * kneeDb);
    else
        g = t + (x - t) * inverseRatio;

    return std::min (std::max (x - g, 0.0), rangeDb);
}

/** The level on the detector's own reading that gives `depthDb` of static reduction at the hard knee (below Range). */
inline double levelForReductionDb (double depthDb, double thresholdDb, double inverseRatio)
{
    return effectiveThresholdDb (thresholdDb, inverseRatio) + depthDb / (1.0 - inverseRatio);
}

/** Where the coupling gate expects the hard knee, found by extrapolating the reduction to 0: T − 3, −1.5, −0.6, −0.3, 0 dB at 2, 4, 10, 20, ∞. */
inline double couplingKneeDb (double thresholdDb, double inverseRatio)
{
    return effectiveThresholdDb (thresholdDb, inverseRatio);
}

/** What is left of a unit step at t (units of τ_A) after the cascade of h·τ_A, then τ_A. */
inline double cascadeRemaining (double h, double t)
{
    if (std::abs (h - 1.0) < 1.0e-9)
        return (1.0 + t) * std::exp (-t);
    return (h * std::exp (-t / h) - std::exp (-t)) / (h - 1.0);
}

/** h(ρ): τ_R/τ_A for which the cascade falls to 1/e at ρ·τ_A, by bisection; ρ floored at 1.01. */
inline double releaseRatio (double rho)
{
    rho = std::max (rho, 1.01);
    auto lo = 0.0, hi = rho;
    for (int i = 0; i < 200; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        (cascadeRemaining (mid, rho) < std::exp (-1.0) ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

/** Peak: the measured attack is the knob (the core's definition). */
inline double peakAttackMs (double attackMs) { return attackMs; }

/** Peak, Auto off: the measured release is the knob, or the cascade's floor 1.01 × Attack below it. */
inline double peakReleaseMs (double attackMs, double releaseMs) { return std::max (releaseMs, 1.01 * attackMs); }

/** Peak Auto: τ1 = Attack·h(Release/Attack), τ2 = min(120 × Release, 20 s), C₂/C₁ = 10. */
inline double autoFastMs (double attackMs, double releaseMs) { return attackMs * releaseRatio (releaseMs / attackMs); }
inline double autoSlowMs (double releaseMs) { return std::min (120.0 * releaseMs, 20000.0); }
inline constexpr double autoCapacitorRatio = 10.0;

/**
    The Auto network's per-sample step, e^(A/fs), by its eigendecomposition: A has two real,
    negative eigenvalues, so E = V·diag(e^λ)·V⁻¹.
*/
inline std::array<double, 4> autoStep (double attackMs, double releaseMs, double rate)
{
    const auto t1 = autoFastMs (attackMs, releaseMs) * 0.001, t2 = autoSlowMs (releaseMs) * 0.001, m = autoCapacitorRatio;
    const auto a = -1.0 / t1, b = 1.0 / t1, c = 1.0 / (m * t1), d = -1.0 / (m * t1) - 1.0 / t2;
    const auto mean = 0.5 * (a + d), radius = std::sqrt (0.25 * (a - d) * (a - d) + b * c);
    const auto l1 = mean + radius, l2 = mean - radius;

    // Eigenvectors (b, λ − a); V⁻¹ by the 2 × 2 inverse.
    const auto v11 = b, v21 = l1 - a, v12 = b, v22 = l2 - a;
    const auto det = v11 * v22 - v12 * v21;
    const auto e1 = std::exp (l1 / rate), e2 = std::exp (l2 / rate);

    const auto i11 = v22 / det, i12 = -v12 / det, i21 = -v21 / det, i22 = v11 / det;
    return { v11 * e1 * i11 + v12 * e2 * i21, v11 * e1 * i12 + v12 * e2 * i22,
             v21 * e1 * i11 + v22 * e2 * i21, v21 * e1 * i12 + v22 * e2 * i22 };
}

/**
    Peak's ballistics with Auto on, over a static reduction per sample (dB): the network between
    samples, the rectifier topping v1 up to r, then the attack one-pole (NOTES.md here, Peak:
    Auto). The reduction per sample.
*/
inline std::vector<double> autoReleaseTrace (const std::vector<double>& staticReduction, double attackMs, double releaseMs, double rate)
{
    const auto e = autoStep (attackMs, releaseMs, rate);
    const auto alpha = std::exp (-1000.0 / (attackMs * rate));
    double v1 = 0.0, v2 = 0.0, y = 0.0;
    std::vector<double> out (staticReduction.size());

    for (size_t n = 0; n < staticReduction.size(); ++n)
    {
        const auto f = e[0] * v1 + e[1] * v2;
        v2 = e[2] * v1 + e[3] * v2;
        v1 = std::max (f, staticReduction[n]);
        y = alpha * y + (1.0 - alpha) * v1;
        out[n] = y;
    }

    return out;
}

/** What the Peak Auto gate reads after `heldS` of a 10 dB static reduction from silence, then silence. */
struct AutoRelease
{
    double applied = 0.0;               // the reduction when the signal stops
    double slowStage = 0.0;             // C₂ (v2) at that moment
    std::array<double, 6> after {};     // the reduction at +0.1, +0.3, +1, +3, +10, +30 s
    double releaseS = 0.0;              // to 36.8 % of `applied`
};

inline constexpr std::array<double, 6> autoReadAtS { 0.1, 0.3, 1.0, 3.0, 10.0, 30.0 };

inline AutoRelease autoReleaseAfter (double attackMs, double releaseMs, double heldS, double rate, double depthDb = 10.0)
{
    const auto held = (size_t) std::lround (heldS * rate);
    const auto total = held + (size_t) std::lround (31.0 * rate);
    std::vector<double> r (total, 0.0);
    std::fill (r.begin(), r.begin() + (std::ptrdiff_t) held, depthDb);
    const auto y = autoReleaseTrace (r, attackMs, releaseMs, rate);

    AutoRelease out;
    out.applied = y[held - 1];

    // v2 at the step, by running the network alone over the hold.
    {
        const auto e = autoStep (attackMs, releaseMs, rate);
        double v1 = 0.0, v2 = 0.0;
        for (size_t n = 0; n < held; ++n)
        {
            const auto f = e[0] * v1 + e[1] * v2;
            v2 = e[2] * v1 + e[3] * v2;
            v1 = std::max (f, depthDb);
        }
        out.slowStage = v2;
    }

    for (size_t k = 0; k < autoReadAtS.size(); ++k)
        out.after[k] = y[held - 1 + (size_t) std::lround (autoReadAtS[k] * rate)];

    // The output from input held + j sits at (j + 1)/fs; crossings interpolated linearly.
    const auto target = std::exp (-1.0) * out.applied;
    auto prevT = 0.0, prevV = out.applied;
    out.releaseS = std::numeric_limits<double>::quiet_NaN();
    for (size_t j = 0; held + j < y.size(); ++j)
    {
        const auto t = (double) (j + 1) / rate, v = y[held + j];
        if (v <= target)
        {
            out.releaseS = prevT + (prevV - target) / (prevV - v) * (t - prevT);
            break;
        }
        prevT = t;
        prevV = v;
    }

    return out;
}

/** M0's table (NOTES.md here, Peak: Auto): a setting, the hold, and what it read. */
struct AutoRow
{
    double attackMs, releaseMs, heldS;
    double after01, after1, after10;    // NaN where the NOTES give no figure
    double releaseS;
    double slowAtStep;                  // C₂ when the signal stops
};

/** The rows the NOTES print; the Attack 10 ms and Release 50 ms rows give release times only. */
inline constexpr double notGiven = std::numeric_limits<double>::quiet_NaN();
inline constexpr std::array<AutoRow, 9> autoRows { {
    { 1.0, 100.0, 0.02, 4.05, 1.02, 0.52, 0.112, 0.20 },
    { 1.0, 100.0, 1.0, 7.64, 6.06, 3.07, 7.6, 6.14 },
    { 1.0, 100.0, 5.0, 9.49, 8.66, 4.38, 12.3, 9.20 },
    { 10.0, 150.0, 0.02, notGiven, notGiven, notGiven, 0.192, notGiven },
    { 10.0, 150.0, 1.0, notGiven, notGiven, notGiven, 7.9, notGiven },
    { 10.0, 150.0, 5.0, notGiven, notGiven, notGiven, 18.2, notGiven },
    { 0.3, 50.0, 0.02, notGiven, notGiven, notGiven, 0.058, notGiven },
    { 0.3, 50.0, 1.0, notGiven, notGiven, notGiven, 5.5, notGiven },
    { 0.3, 50.0, 5.0, notGiven, notGiven, notGiven, 6.2, notGiven },
} };

/** τ_RMS = Attack / 0.45442. */
inline double rmsTimeConstantMs (double attackMs) { return attackMs / 0.45442; }

/**
    t/τ for a one-pole on the square to take the level from Threshold to 63.2 % of a `depthDb`
    rise: e^(−t/τ) = (10^(D/10) − 10^(0.632·D/10)) / (10^(D/10) − 1). 0.45442, 0.19299, 0.08105 at
    10, 20, 30 dB.
*/
inline double rmsAttackPerTimeConstant (double depthDb)
{
    const auto final = std::pow (10.0, depthDb / 10.0);
    const auto mark = std::pow (10.0, (1.0 - std::exp (-1.0)) * depthDb / 10.0);
    return -std::log ((final - mark) / (final - 1.0));
}

/** RMS, Ratio ∞: the measured attack of a `depthDb` step from Threshold, in ms. Attack itself at 10 dB. */
inline double rmsAttackMs (double attackMs, double depthDb)
{
    return rmsAttackPerTimeConstant (depthDb) * rmsTimeConstantMs (attackMs);
}

/** The 160A's sheet: 15, 5, 3 ms for 10, 20, 30 dB (reported beside the single pole). */
inline double datasheetAttackMs (double depthDb) { return depthDb <= 10.0 ? 15.0 : (depthDb <= 20.0 ? 5.0 : 3.0); }

/** Attack 15.79 ms and Release 80 ms: τ = 34.74 ms, the 160A's 125 dB/s. */
inline constexpr double markAttackMs = 15.79;
inline constexpr double markReleaseMs = 80.0;

/** How long the RMS detector alone takes to let the reduction recover 10 dB: τ·ln 10 / (1 − 1/R). */
inline double rmsReleaseFloorMs (double attackMs, double inverseRatio)
{
    return inverseRatio < 1.0 ? rmsTimeConstantMs (attackMs) * std::numbers::ln10 / (1.0 - inverseRatio)
                              : std::numeric_limits<double>::infinity();
}

/** RMS: the measured time per 10 dB of recovery, the knob or the detector's floor, whichever is slower. */
inline double rmsReleasePer10DbMs (double attackMs, double releaseMs, double inverseRatio)
{
    return std::max (releaseMs, rmsReleaseFloorMs (attackMs, inverseRatio));
}

/**
    The third harmonic a one-pole RMS's 2f ripple puts on a sine of `hz` through the gain cell:
    20·log10[(1 − 1/R) / (4·√(1 + (4π·f·τ)²))] dBc (NOTES.md here, Gates, Distortion).
*/
inline double rmsThirdHarmonicDbc (double attackMs, double inverseRatio, double hz)
{
    const auto tau = rmsTimeConstantMs (attackMs) * 0.001;
    const auto w = 4.0 * std::numbers::pi * hz * tau;
    return 20.0 * std::log10 ((1.0 - inverseRatio) / (4.0 * std::sqrt (1.0 + w * w)));
}

/** RMS, linked by power sum: the level both channels are reduced for at Link 100 %, from the two channels' mean squares. */
inline double powerSumLevelDb (double meanSquareLeft, double meanSquareRight)
{
    return 10.0 * std::log10 (meanSquareLeft + meanSquareRight);
}

} // namespace basalt::reference::vca
