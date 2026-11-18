#pragma once

// Vari-mu's expectations for BasaltMeasure and the model tests: a double-precision transcription of
// src/dsp/models/varimu/NOTES.md, written from the NOTES, never from the engine's code. Pure
// functions: the static curve r_s, the local ratio, the times the knobs mean, the gate table's
// program-dependent limits, and the Auto release rows' expected release, from a simulation of the
// specified loop and memories on the same stimulus. Header-only, standard library only.

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>

namespace basalt::measure::reference::varimu
{

// NOTES.md, Gain computer: eq. 4 plus the rise, cap 20.
inline constexpr double ratioCap = 20.0;
inline constexpr double riseCentreDb = 16.0;    // U_r
inline constexpr double riseWidthDb = 2.0;      // w_r

// NOTES.md, Ballistics.
inline constexpr double attackFloorMs = 0.1;
inline constexpr double referenceReductionDb = 10.0;
inline constexpr double maxHoldMs = 5.0;

// NOTES.md, Auto release.
inline constexpr double fastTier = 5.0, slowTier = 12.5, tierCapS = 25.0;
inline constexpr double fastChargeS = 0.3, slowChargeS = 5.0, callingS = 0.025;

inline double inverseRatioOf (double ratio) { return std::isinf (ratio) ? 0.0 : 1.0 / ratio; }

/** The ratio the curve uses: Ratio capped at 20, ∞ included. */
inline double cappedRatio (double inverseRatio) { return std::min (1.0 / std::max (inverseRatio, 1.0e-12), ratioCap); }

/** A = φ(R)·(1/R − 1/20), φ(R) = min(1, (R − 1)/0.5); 0 from Ratio 20 on. */
inline double riseScale (double inverseRatio)
{
    const auto r = cappedRatio (inverseRatio);
    return std::min (1.0, (r - 1.0) / 0.5) * (1.0 / r - 1.0 / ratioCap);
}

/** eq. 4's x − G(x) at the capped ratio, before Range. */
inline double baseReduction (double x, double t, double inverseRatio, double w)
{
    const auto slope = 1.0 - 1.0 / cappedRatio (inverseRatio);
    const auto over = x - t;
    if (2.0 * over > w)
        return slope * over;
    if (w > 0.0 && 2.0 * over >= -w)
        return slope * (over + 0.5 * w) * (over + 0.5 * w) / (2.0 * w);
    return 0.0;
}

inline double baseReductionSlope (double x, double t, double inverseRatio, double w)
{
    const auto slope = 1.0 - 1.0 / cappedRatio (inverseRatio);
    const auto over = x - t;
    if (2.0 * over > w)
        return slope;
    if (w > 0.0 && 2.0 * over >= -w)
        return slope * (over + 0.5 * w) / w;
    return 0.0;
}

inline double softplus (double u) { return u > 0.0 ? u + std::log1p (std::exp (-u)) : std::log1p (std::exp (u)); }
inline double logistic (double u) { return 1.0 / (1.0 + std::exp (-u)); }

/** d(x): the reduction before Range. */
inline double reduction (double x, double t, double inverseRatio, double w)
{
    return baseReduction (x, t, inverseRatio, w) + riseScale (inverseRatio) * riseWidthDb * softplus ((x - t - riseCentreDb) / riseWidthDb);
}

/** d'(x). */
inline double reductionSlope (double x, double t, double inverseRatio, double w)
{
    return baseReductionSlope (x, t, inverseRatio, w) + riseScale (inverseRatio) * logistic ((x - t - riseCentreDb) / riseWidthDb);
}

/** r_s(x) = min(d(x), Range): the static curve the Static curve gate compares with, on the detector's own reading. */
inline double staticReduction (double x, double t, double inverseRatio, double w, double range)
{
    return std::min (reduction (x, t, inverseRatio, w), range);
}

/** The local ratio 1/G'(x) before Range. */
inline double localRatio (double x, double t, double inverseRatio, double w)
{
    return 1.0 / (1.0 - reductionSlope (x, t, inverseRatio, w));
}

/** The input level where d(x) = depth, by bisection. */
inline double levelForReduction (double depth, double t, double inverseRatio, double w)
{
    auto lo = t - 0.5 * w, hi = t + 10.0;
    while (reduction (hi, t, inverseRatio, w) < depth && hi < t + 1.0e6)
        hi = t + 2.0 * (hi - t);
    for (int i = 0; i < 200; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        (reduction (mid, t, inverseRatio, w) < depth ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

/** The local ratio where the static reduction is `depth` (Threshold does not enter). */
inline double localRatioAtReduction (double depth, double inverseRatio, double w)
{
    return localRatio (levelForReduction (depth, 0.0, inverseRatio, w), 0.0, inverseRatio, w);
}

/**
    The gate's secant: the local ratio from 1 dB input steps, 1 / (G(x) − G(x − 1)), read at the
    reduction `depth` by interpolating linearly between the 1 dB grid points (levels Threshold + n dB)
    whose reductions bracket it. The engine's staircase gives the same reading from its settled steps.
*/
inline double secantRatioAtReduction (double depth, double inverseRatio, double w)
{
    const auto g = [&] (double x) { return x - reduction (x, 0.0, inverseRatio, w); };
    for (int n = -19; n <= 60; ++n)
    {
        const auto d0 = reduction (n - 1.0, 0.0, inverseRatio, w), d1 = reduction ((double) n, 0.0, inverseRatio, w);
        if (d0 <= depth && d1 >= depth)
        {
            const auto rPrev = 1.0 / (g (n - 1.0) - g (n - 2.0)), rHere = 1.0 / (g ((double) n) - g (n - 1.0));
            return rPrev + (depth - d0) / (d1 - d0) * (rHere - rPrev);
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

/** The rising-ratio gate at Ratio 4, Knee 6 (NOTES.md, Gate table): the secant's limits at three depths. */
struct RatioLimit
{
    double reductionDb;
    double atMost, atLeast;
};

inline constexpr std::array<RatioLimit, 3> risingRatioLimits { { { 8.0, 5.0, 0.0 }, { 16.0, 1.0e9, 12.0 }, { 20.0, 1.0e9, 17.0 } } };

/** At Ratio 20 every secant above the knee is 20 ± this. */
inline constexpr double capRatioTolerance = 1.0;

/** Static gate: ±0.25 dB of r_s. */
inline constexpr double staticToleranceDb = 0.25;

/** The 63 % attack the knob means: 1:1, floored at 0.1 ms. */
inline double expectedAttackMs (double attackKnobMs) { return std::max (attackKnobMs, attackFloorMs); }

/** ±10 %, and ±15 % at the floor (0.1 ms and below). */
inline double attackTolerance (double attackKnobMs) { return attackKnobMs <= attackFloorMs ? 0.15 : 0.10; }

/** The 63 % release with Auto off: the knob, 5 ms … 5 s, ±10 %. */
inline double expectedReleaseMs (double releaseKnobMs) { return releaseKnobMs; }

inline constexpr double timeTolerance = 0.10;

/** Overshoot over the settled reduction, steps to 10, 20 and 30 dB, every Attack. */
inline constexpr double overshootLimitDb = 0.05;

/** Auto Gain's make-up: r_s at −18 dB on the detector scale. */
inline double autoGainDb (double t, double inverseRatio, double w, double range) { return staticReduction (-18.0, t, inverseRatio, w, range); }

/** R_∞ = 1/(1/R − A): the curve's asymptotic local ratio, 20 from Ratio 1.5 on. */
inline double asymptoticRatio (double inverseRatio)
{
    return 1.0 / (1.0 / cappedRatio (inverseRatio) - riseScale (inverseRatio));
}

/** κ(Ratio) as NOTES.md (Measured in M2) tabulates it: Ratio 1 … 20 in 20 log steps, linear in log Ratio between them. */
inline constexpr double kappaTable[20] {
    1.0, 1.0, 0.9956, 0.9755, 0.9560, 0.9319, 0.9089, 0.8931, 0.8869, 0.8892,
    0.8974, 0.9090, 0.9222, 0.9357, 0.9489, 0.9613, 0.9726, 0.9828, 0.9920, 1.0,
};

inline double kappa (double inverseRatio)
{
    const auto x = std::log (cappedRatio (std::min (inverseRatio, 1.0))) / std::log (ratioCap) * 19.0;
    const auto i = std::min ((int) std::floor (x), 18);
    return kappaTable[i] + (x - i) * (kappaTable[i + 1] - kappaTable[i]);
}

/** Φ = R_ref / (κ·R_∞), R_ref the local ratio where the reduction is 10 dB (NOTES.md, Attack). */
inline double attackCalibration (double inverseRatio, double w)
{
    if (inverseRatio >= 1.0)
        return 1.0;
    return localRatioAtReduction (referenceReductionDb, inverseRatio, w) / (kappa (inverseRatio) * asymptoticRatio (inverseRatio));
}

/** The settings the transcription reads. */
struct Settings
{
    double thresholdDb = -30.0, inverseRatio = 0.25, kneeDb = 6.0, rangeDb = 40.0;
    double attackMs = 0.4, releaseMs = 2000.0;
    bool autoRelease = true;
};

/** The loop's demand c(y) = G⁻¹(y) − y, by regula falsi (Illinois) on [y, y + 20·d(y)]. */
inline double demand (double y, const Settings& s)
{
    const auto t = s.thresholdDb, q = s.inverseRatio, w = s.kneeDb;
    const auto d = reduction (y, t, q, w);
    if (d <= 0.0)
        return 0.0;
    auto lo = y, hi = y + 20.0 * d + 1.0e-9;
    auto flo = -d, fhi = hi - reduction (hi, t, q, w) - y;
    int side = 0;
    for (int i = 0; i < 400 && hi - lo > 1.0e-13 * std::max (1.0, std::abs (hi)); ++i)
    {
        const auto x = (lo * fhi - hi * flo) / (fhi - flo);
        const auto f = x - reduction (x, t, q, w) - y;
        if (std::abs (f) < 1.0e-13)
            return x - y;
        if (f < 0.0)
        {
            lo = x;
            flo = f;
            if (side == -1)
                fhi *= 0.5;
            side = -1;
        }
        else
        {
            hi = x;
            fhi = f;
            if (side == 1)
                flo *= 0.5;
            side = 1;
        }
    }
    return 0.5 * (lo + hi) - y;
}

/**
    NOTES.md's loop on one channel of key magnitudes |k|, from rest: the peak held over
    h = min(Release/10, 5 ms) plus the sample; the output estimate as that level less the previous
    reduction r = max(s, m, ℓ); the demand c = G⁻¹(y) − y by regula falsi on [y, y + 20·d(y)]; smooth
    branching with the attack step (1 − e^(−Φ/(Attack·fs)))/R_ref and the release pole
    e^(−1/((Release − h)·fs)); Range on s; the memories charging on 0.3 s and 5 s while c ≥ s
    (within 10⁻⁶ dB) at some sample of the last 25 ms, else releasing at min(5·Release, 25 s) and
    min(12.5·Release, 25 s). The reduction in dB per sample.
*/
inline std::vector<double> simulate (const std::vector<double>& magnitude, const Settings& s, double rate)
{
    const auto t = s.thresholdDb, q = s.inverseRatio, w = s.kneeDb;
    const auto holdMs = std::min (s.releaseMs / 10.0, maxHoldMs);
    const auto window = (size_t) std::lround (holdMs * 0.001 * rate) + 1;

    const auto rRef = localRatio (levelForReduction (referenceReductionDb, t, q, w), t, q, w);
    const auto phi = attackCalibration (q, w);
    const auto attackStep = (1.0 - std::exp (-phi / (std::max (s.attackMs, attackFloorMs) * 0.001 * rate))) / rRef;
    const auto releasePole = std::exp (-1.0 / ((s.releaseMs - holdMs) * 0.001 * rate));
    const auto fastCharge = 1.0 - std::exp (-1.0 / (fastChargeS * rate));
    const auto slowCharge = 1.0 - std::exp (-1.0 / (slowChargeS * rate));
    const auto fastRelease = std::exp (-1.0 / (std::min (fastTier * s.releaseMs * 0.001, tierCapS) * rate));
    const auto slowRelease = std::exp (-1.0 / (std::min (slowTier * s.releaseMs * 0.001, tierCapS) * rate));
    const auto callingSamples = (int) std::lround (callingS * rate);

    std::vector<double> out (magnitude.size());
    std::deque<std::pair<size_t, double>> wedge;
    double state = 0.0, fast = 0.0, slow = 0.0, applied = 0.0;
    int calling = 0;

    for (size_t n = 0; n < magnitude.size(); ++n)
    {
        while (! wedge.empty() && wedge.back().second <= magnitude[n])
            wedge.pop_back();
        wedge.emplace_back (n, magnitude[n]);
        while (wedge.front().first + window <= n)
            wedge.pop_front();
        const auto held = wedge.front().second;
        const auto level = held > 1.0e-10 ? 20.0 * std::log10 (held) : -200.0;

        const auto c = demand (level - applied, s);
        const auto before = state;
        state = c > state ? state + attackStep * (c - state) : releasePole * state + (1.0 - releasePole) * c;
        state = std::min (state, s.rangeDb);
        if (std::abs (state) < 1.0e-6 && std::abs (c) < 1.0e-6)
            state = 0.0;

        calling = c >= before - 1.0e-6 ? callingSamples : std::max (calling - 1, 0);
        const auto charging = s.autoRelease && calling > 0;
        fast = charging && state > fast ? fast + fastCharge * (state - fast) : fast * fastRelease;
        slow = charging && state > slow ? slow + slowCharge * (state - slow) : slow * slowRelease;
        fast = fast < 1.0e-6 ? 0.0 : fast;
        slow = slow < 1.0e-6 ? 0.0 : slow;

        applied = std::max ({ state, fast, slow });
        out[n] = applied;
    }

    return out;
}

/** A history before the release: bursts of the carrier at one level, then silence from `end`. */
struct History
{
    const char* name;
    int bursts;
    double burstS, spacingS;     // burst length, and start-to-start spacing

    double endS() const { return (bursts - 1) * spacingS + burstS; }
};

/** The Auto release rows (NOTES.md, Gate table), with M0's expected release in multiples of Release at Release 2 s. */
struct AutoRow
{
    History history;
    double m0Expected;      // NaN: the 25 s row, min(12.5 × Release, 25 s)
};

inline const std::array<AutoRow, 6>& autoRows()
{
    static const std::array<AutoRow, 6> rows { {
        { { "one 50 ms peak", 1, 0.05, 0.4 }, 1.0 },
        { { "3 peaks of 50 ms, 0.4 s apart", 3, 0.05, 0.4 }, 1.6 },
        { { "5 peaks", 5, 0.05, 0.4 }, 3.0 },
        { { "8 peaks", 8, 0.05, 0.4 }, 3.9 },
        { { "2 s sustained", 1, 2.0, 2.0 }, 5.0 },
        { { "30 s sustained", 1, 30.0, 30.0 }, std::numeric_limits<double>::quiet_NaN() },
    } };
    return rows;
}

/** M0's expectation for a row at a Release, in seconds. */
inline double m0ExpectedReleaseS (const AutoRow& row, double releaseMs)
{
    const auto r = releaseMs * 0.001;
    return std::isnan (row.m0Expected) ? std::min (slowTier * r, tierCapS) : row.m0Expected * r;
}

/** The carrier's magnitude per sample for a history: `pre` of silence, the bursts at `amplitude`, then `tailS` of silence. */
inline std::vector<double> magnitudes (const History& h, double amplitude, double preS, double tailS, double rate)
{
    const auto at = [rate] (double s) { return (size_t) std::lround (s * rate); };
    std::vector<double> m (at (preS + h.endS() + tailS), 0.0);
    for (int b = 0; b < h.bursts; ++b)
        std::fill (m.begin() + (std::ptrdiff_t) at (preS + b * h.spacingS), m.begin() + (std::ptrdiff_t) at (preS + b * h.spacingS + h.burstS), amplitude);
    return m;
}

/**
    The release time after a history: from the step to silence until the reduction first falls to
    36.8 % of what it was at the step (the output sample from input j after the step at (j + 1)/fs,
    interpolated). In seconds; NaN if it never gets there.
*/
inline double releaseSeconds (const std::vector<double>& r, size_t step, double rate)
{
    const auto from = r[step - 1], target = std::exp (-1.0) * from;
    auto prevT = 0.0, prevV = from;
    for (size_t j = 0; step + j < r.size(); ++j)
    {
        const auto tt = (double) (j + 1) / rate, v = r[step + j];
        if (v <= target)
            return prevT + (prevV - target) / (prevV - v) * (tt - prevT);
        prevT = tt;
        prevV = v;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

/** The Auto release rows' expected release: the transcription on the carrier at the level of `depth` dB static reduction. */
inline double expectedAutoReleaseS (const History& h, const Settings& s, double depth, double rate)
{
    constexpr double preS = 0.01;
    const auto level = levelForReduction (depth, s.thresholdDb, s.inverseRatio, s.kneeDb);
    const auto tailS = std::min (slowTier * s.releaseMs * 0.001, tierCapS) * 2.0 + 1.0;
    const auto m = magnitudes (h, std::pow (10.0, level / 20.0), preS, tailS, rate);
    const auto r = simulate (m, s, rate);
    return releaseSeconds (r, (size_t) std::lround ((preS + h.endS()) * rate), rate);
}

} // namespace basalt::measure::reference::varimu
