#pragma once

// The Opto's specification as pure functions in double precision, written from
// src/dsp/models/opto/NOTES.md and never from the engine's code: the static curve, what the
// Attack and Release knobs measure, the cell itself (light, conductance, divider, memory) per
// sample, and the program-dependent figures of the NOTES' gate table. The unit tests, the τ_on
// table's generator and BasaltMeasure compare the engine against it.

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace basalt::measure::reference::opto
{

inline constexpr double epsilon = 0.0024787521766663585;    // e^(−6), the panel's onset
inline constexpr double memoryShare = 0.1;                   // σ
inline constexpr double chargeMs = 3000.0;                   // τ_sc
inline constexpr double dischargePerRelease = 16.0;          // τ_sd = 16 × Release …
inline constexpr double dischargeMaxMs = 25000.0;            // … at most 25 s
inline constexpr double lightMaxMs = 5.0;                    // τ_m
inline constexpr double lightPerAttack = 1.5;                // τ_m = min(5 ms, 1.5 × Attack) (NOTES, Implementation (M2))
inline constexpr double attackFloorMs = 2.0;
inline constexpr double slopeShareFloor = 1.0 / 3.0;         // the time solves take Ratio 1.5's q below it
inline constexpr double referenceDepthDb = 10.0;
inline constexpr double dbPerNeper = 10.0 / std::numbers::ln10;

/** w = (Knee + 12 dB)/5.5. */
inline double kneeWidthDb (double kneeDb) { return (kneeDb + 12.0) / 5.5; }

/** q = 1 − 1/R; inverseRatio 0 is ∞. */
inline double slopeShare (double inverseRatio) { return 1.0 - inverseRatio; }

/** r_s(u) = min(q·w·ln(1 + max(e^((u − T)/w) − ε, 0)), Range), u on the detector scale. */
inline double staticReduction (double levelDb, double thresholdDb, double inverseRatio, double kneeDb, double rangeDb)
{
    const auto w = kneeWidthDb (kneeDb);
    const auto g = std::max (std::exp ((levelDb - thresholdDb) / w) - epsilon, 0.0);
    return std::min (slopeShare (inverseRatio) * w * std::log1p (g), rangeDb);
}

/** The level on the detector scale whose static reduction is `depthDb` (below Range). */
inline double levelForReduction (double depthDb, double thresholdDb, double inverseRatio, double kneeDb)
{
    const auto w = kneeWidthDb (kneeDb);
    return thresholdDb + w * std::log (std::expm1 (depthDb / (slopeShare (inverseRatio) * w)) + epsilon);
}

/** T − 6w: at and below it the reduction is exactly 0. */
inline double onsetDb (double thresholdDb, double kneeDb) { return thresholdDb - 6.0 * kneeWidthDb (kneeDb); }

/** The light's mean-square constant τ_m for an Attack setting, in ms. */
inline double lightMs (double attackMs) { return std::min (lightMaxMs, lightPerAttack * std::max (attackMs, attackFloorMs)); }

/** Δ = τ_m·w/4.343: how long the light's level takes to fall by w once the key stops. */
inline double lightDelayMs (double attackMs, double kneeDb) { return lightMs (attackMs) * kneeWidthDb (kneeDb) / dbPerNeper; }

/** What a measured attack (silence → 10 dB, 63.2 %) reads for an Attack setting. */
inline double expectedAttackMs (double attackMs) { return std::max (attackMs, attackFloorMs); }

/** What a measured release (10 dB → 5 dB, Auto off) reads for a Release setting: the knob, floored at Δ + 1 ms. */
inline double expectedReleaseMs (double releaseMs, double attackMs, double kneeDb)
{
    return std::max (releaseMs, lightDelayMs (attackMs, kneeDb) + 1.0);
}

/** The first release stage τ_ff = (Release − Δ)/ln(G₀/G₁), G at 10 and 5 dB; q floored at Ratio 1.5's. */
inline double releaseConstantMs (double releaseMs, double attackMs, double inverseRatio, double kneeDb)
{
    const auto qw = std::max (slopeShare (inverseRatio), slopeShareFloor) * kneeWidthDb (kneeDb);
    const auto spread = std::log (std::expm1 (referenceDepthDb / qw) / std::expm1 (0.5 * referenceDepthDb / qw));
    return (expectedReleaseMs (releaseMs, attackMs, kneeDb) - lightDelayMs (attackMs, kneeDb)) / spread;
}

inline double dischargeMs (double releaseMs) { return std::min (dischargePerRelease * releaseMs, dischargeMaxMs); }

inline double pole (double ms, double rate) { return ms > 0.0 ? std::exp (-1000.0 / (ms * rate)) : 0.0; }

struct Settings
{
    double thresholdDb = -30.0, inverseRatio = 0.25, kneeDb = 6.0, rangeDb = 40.0;
    double attackMs = 10.0, releaseMs = 60.0;
    bool autoRelease = false;
    double rate = 48000.0;
    double attackConstantMs = 0.0;     // τ_on, from solveAttackConstantMs() or a table
};

/** One detector channel of NOTES.md's *Detector*, per sample, in double. */
class Cell
{
public:
    explicit Cell (const Settings& settings)
        : set (settings), w (kneeWidthDb (set.kneeDb)), qw (slopeShare (set.inverseRatio) * w),
          light (pole (lightMs (set.attackMs), set.rate)), rise (pole (set.attackConstantMs, set.rate)),
          fall (pole (releaseConstantMs (set.releaseMs, set.attackMs, set.inverseRatio, set.kneeDb), set.rate)),
          charge (pole (chargeMs, set.rate)), discharge (pole (dischargeMs (set.releaseMs), set.rate)),
          conductanceMax (set.rangeDb <= 0.0 ? 0.0 : (qw > 0.0 ? std::expm1 (std::min (set.rangeDb / qw, 700.0)) : std::numeric_limits<double>::max()))
    {
    }

    /** One sample of key, as its square; the reduction in dB. */
    double process (double squared)
    {
        m = light * m + (1.0 - light) * squared;
        const auto u = m > 0.0 ? dbPerNeper * std::log (2.0 * m) : -std::numeric_limits<double>::infinity();
        const auto g = std::min (std::max (std::exp ((u - set.thresholdDb) / w) - epsilon, 0.0), conductanceMax);

        if (set.autoRelease)
        {
            const auto target = memoryShare * g;
            s += (1.0 - (target > s ? charge : discharge)) * (target - s);
        }

        const auto target = std::max (g - s, 0.0);
        f += (1.0 - (target > f ? rise : fall)) * (target - f);
        s = std::min (s, memoryShare * conductanceMax);
        f = std::min (f, conductanceMax - s);
        return qw * std::log1p (f + s);
    }

    double fast() const { return f; }
    double memory() const { return s; }

private:
    Settings set;
    double w, qw, light, rise, fall, charge, discharge, conductanceMax;
    double m = 0.0, f = 0.0, s = 0.0;
};

/** The carrier's square for a static reduction of `depthDb`: its mean square reads 3.01 dB above its amplitude. */
inline double carrierSquareFor (const Settings& s, double depthDb)
{
    return 0.5 * std::pow (10.0, levelForReduction (depthDb, s.thresholdDb, s.inverseRatio, s.kneeDb) / 10.0);
}

/**
    The time, in s, from a step at the cell's input until its reduction first crosses `target`
    (from `from` at t = 0): the output from the j-th input after the step sits at (j + 1)/fs,
    crossings interpolated linearly (dynamics/NOTES.md, What the knobs mean). NaN past `limitS`.
*/
inline double crossingS (Cell& cell, double squared, double from, double target, double rate, double limitS)
{
    const auto rising = target > from;
    auto prevT = 0.0, prevR = from;

    for (long j = 0; j < (long) (limitS * rate); ++j)
    {
        const auto t = (double) (j + 1) / rate;
        const auto r = cell.process (squared);
        if (rising ? r >= target : r <= target)
            return prevT + (target - prevR) / (r - prevR) * (t - prevT);
        prevT = t;
        prevR = r;
    }

    return std::numeric_limits<double>::quiet_NaN();
}

/** The measured attack in ms for a given τ_on: carrier from silence to 10 dB, Auto off, 63.2 %. */
inline double measuredAttackMs (Settings s)
{
    s.autoRelease = false;
    s.rangeDb = std::max (s.rangeDb, 40.0);
    Cell cell (s);
    const auto target = (1.0 - std::exp (-1.0)) * referenceDepthDb;
    return 1000.0 * crossingS (cell, carrierSquareFor (s, referenceDepthDb), 0.0, target, s.rate, 1000.0);
}

/**
    τ_on for an Attack: the reference step integrated per sample at `rate` (192 kHz for the table),
    bisection on τ_on to 10⁻⁴ of Attack. 0 where the light alone is slower than the Attack.
*/
inline double solveAttackConstantMs (double attackMs, double inverseRatio, double kneeDb, double rate = 192000.0)
{
    Settings s;
    s.inverseRatio = std::min (inverseRatio, 1.0 - slopeShareFloor);
    s.kneeDb = kneeDb;
    s.attackMs = std::max (attackMs, attackFloorMs);
    s.rate = rate;
    const auto wanted = s.attackMs;

    auto attackWith = [&] (double tau)
    {
        s.attackConstantMs = tau;
        return measuredAttackMs (s);
    };

    if (attackWith (0.0) >= wanted)
        return 0.0;

    auto lo = 0.0, hi = wanted;
    while (attackWith (hi) < wanted)
    {
        lo = hi;
        hi *= 2.0;
    }

    while (hi - lo > 1.0e-4 * wanted)
    {
        const auto mid = 0.5 * (lo + hi);
        (attackWith (mid) < wanted ? lo : hi) = mid;
    }

    return 0.5 * (lo + hi);
}

struct Release
{
    double heldDb = 0.0, seconds = 0.0;
};

/**
    Holds the carrier at a static reduction of `depthDb` for `holdS`, then silence: the reduction
    held when the key stops and the time until it falls to `fraction` of it (0.5: t50, the
    Release knob's measure; 0.1: the full release of the history gate). τ_on from the solve.
*/
inline Release releaseAfterHold (Settings s, double depthDb, double holdS, double fraction)
{
    if (s.attackConstantMs <= 0.0)
        s.attackConstantMs = solveAttackConstantMs (s.attackMs, s.inverseRatio, s.kneeDb, s.rate);

    Cell cell (s);
    const auto squared = carrierSquareFor (s, depthDb);
    auto held = 0.0;
    for (long i = 0; i < std::lround (holdS * s.rate); ++i)
        held = cell.process (squared);

    return { held, crossingS (cell, 0.0, held, fraction * held, s.rate, 120.0) };
}

/** NOTES' history row: full release (to 10 %) after `holdS` at 10 dB, Auto on. */
inline double fullReleaseS (Settings s, double holdS)
{
    s.autoRelease = true;
    return releaseAfterHold (s, referenceDepthDb, holdS, 0.1).seconds;
}

/** The history gate's floor: full release after 10 s at least this many times that after 0.5 s. */
inline constexpr double historyRatioFloor = 2.0;

/** M0's expected values at Release 60 ms, Ratio 4, Knee 6 (a variant without Δ, levels at a 2 kHz rate): reported beside the gate. */
inline constexpr double m0FullReleaseAfterHalfSecondS = 0.55;
inline constexpr double m0FullReleaseAfterTenSecondsS = 2.31;

/** NOTES' depth row: t50 after 20 dB over t50 after 10 dB, Auto off (M0 expected about 1.84). */
inline double depthRatio (Settings s, double holdS = 1.0)
{
    s.autoRelease = false;
    return releaseAfterHold (s, 20.0, holdS, 0.5).seconds / releaseAfterHold (s, referenceDepthDb, holdS, 0.5).seconds;
}

/**
    How long a static-curve measurement must settle after a 1 dB step up for the cell to sit within
    0.005 dB of r_s: τ_on·ln(q·w·(e^(1/w) − 1)/0.005), plus five light constants, in s. Low ratios
    with a hard knee calibrate τ_on far above Attack (the log law), so they settle slowly.
*/
inline double stepSettleS (double attackConstantMs, double attackMs, double inverseRatio, double kneeDb)
{
    const auto w = kneeWidthDb (kneeDb);
    const auto gap = slopeShare (inverseRatio) * w * std::expm1 (1.0 / w) / 0.005;
    return 0.001 * (attackConstantMs * std::log (std::max (gap, 1.0)) + 5.0 * lightMs (attackMs));
}

} // namespace basalt::measure::reference::opto
