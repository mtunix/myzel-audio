#pragma once

// FET's specification as pure functions, transcribed from src/dsp/models/fet/NOTES.md (Gain
// computer, All, Ballistics, Gate table) and Settled 38, never from the engine's code: what the
// unit tests and BasaltMeasure compare the engine with. Ratios are plain values, ∞ (All) as
// std::numeric_limits<double>::infinity(); levels in dB on the peak scale, times in ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace basalt::reference::fet
{

inline constexpr double infinity = std::numeric_limits<double>::infinity();

/** The loop's ratio cap; All runs at it too. */
inline constexpr double ratioCap = 20.0;

/** The pre-hold's longest h (NOTES.md, Detector, 1). */
inline constexpr double maxHoldMs = 1.0;

/** All's key lag L (NOTES.md, All; M3 may voice it within 0.25 … 2 ms). */
inline constexpr double allLagMs = 1.0;

/** Gate tolerances (NOTES.md, Gate table). */
inline constexpr double staticToleranceDb = 0.25;
inline constexpr double timeTolerance = 0.10;           // ±10 % of the knob, or ±0.25 sample where wider
inline constexpr double kneeToleranceDb = 0.05;         // threshold coupling
inline constexpr double lagQuietDb = 0.1;               // All's lag: the reduction stays below this for L
inline constexpr double lagToleranceSamples = 1.0;
inline constexpr double rangeToleranceDb = 0.05;
inline constexpr double autoGainToleranceDb = 0.01;

/** The Static curve gate's grid. */
inline constexpr std::array staticRatios { 1.5, 2.0, 4.0, 8.0, 12.0, 20.0, infinity };
inline constexpr std::array staticKnees { 0.0, 6.0, 24.0 };
inline constexpr std::array staticRanges { 40.0, 10.0 };
inline constexpr std::array staticThresholds { -60.0, -30.0, -10.0 };
inline constexpr double staticBelowDb = -20.0, staticAboveDb = 30.0;    // T − 20 … T + 30 in 1 dB steps
inline constexpr double staticAttackMs = 10.0, staticReleaseMs = 1000.0;

/** The Ballistics gate's grid (Knee 0, 10 dB of reduction on the carrier). */
inline constexpr std::array ballisticsAttacksMs { 0.02, 0.05, 0.2, 0.8, 10.0, 100.0, 300.0 };
inline constexpr std::array ballisticsReleasesMs { 5.0, 50.0, 300.0, 1100.0, 5000.0 };
inline constexpr std::array ballisticsRatios { 4.0, 20.0, infinity };
inline constexpr double ballisticsDepthDb = 10.0;

/** The Threshold coupling gate's rows: Ratio and where the hard knee is found, relative to Threshold. */
inline constexpr std::array<std::pair<double, double>, 6> couplingRows { { { 4.0, 0.0 }, { 8.0, 4.0 }, { 12.0, 5.0 },
                                                                             { 20.0, 6.0 }, { 5.66, 2.0 }, { infinity, 0.0 } } };

inline bool isAll (double ratio) { return std::isinf (ratio); }

/** The ratio the loop settles on: Ratio capped at 20; All is 20. */
inline double loopRatio (double ratio) { return std::min (ratio, ratioCap); }

/**
    ΔT(R): 0 up to 4:1, then piecewise linear in log2 R through (4, 0), (8, +4), (12, +5),
    (20, +6) dB, the reference's table (NOTES.md, Gain computer); 20:1 above the cap. All: 0, the 4:1
    threshold.
*/
inline double thresholdOffsetDb (double ratio)
{
    if (isAll (ratio))
        return 0.0;

    const auto octaves = std::log2 (loopRatio (ratio));
    constexpr std::array<std::pair<double, double>, 4> table { { { 4.0, 0.0 }, { 8.0, 4.0 }, { 12.0, 5.0 }, { 20.0, 6.0 } } };

    if (octaves <= std::log2 (table.front().first))
        return 0.0;

    for (size_t i = 1; i < table.size(); ++i)
    {
        const auto x0 = std::log2 (table[i - 1].first), x1 = std::log2 (table[i].first);
        if (octaves <= x1)
            return table[i - 1].second + (table[i].second - table[i - 1].second) * (octaves - x0) / (x1 - x0);
    }

    return table.back().second;
}

/** T_eff = T + ΔT(R). */
inline double effectiveThresholdDb (double thresholdDb, double ratio) { return thresholdDb + thresholdOffsetDb (ratio); }

/** Giannoulis et al. 2012 eq. 4 as the reduction x − G(x), written from the paper's three cases. */
inline double eq4ReductionDb (double levelDb, double thresholdDb, double ratio, double kneeDb)
{
    const auto over = levelDb - thresholdDb;
    const auto slope = 1.0 - 1.0 / ratio;

    if (2.0 * over < -kneeDb)
        return 0.0;
    if (2.0 * std::abs (over) <= kneeDb && kneeDb > 0.0)
        return slope * (over + 0.5 * kneeDb) * (over + 0.5 * kneeDb) / (2.0 * kneeDb);
    return slope * over;
}

/** The specified static curve: min(x − G(x; T_eff, R capped), Range). */
inline double staticReductionDb (double levelDb, double thresholdDb, double ratio, double kneeDb, double rangeDb)
{
    return std::min (eq4ReductionDb (levelDb, effectiveThresholdDb (thresholdDb, ratio), loopRatio (ratio), kneeDb), rangeDb);
}

/** Auto Gain's make-up: the static reduction at the reference, a sine peaking at −18 dBFS. */
inline double autoGainDb (double thresholdDb, double ratio, double kneeDb, double rangeDb)
{
    return staticReductionDb (-18.0, thresholdDb, ratio, kneeDb, rangeDb);
}

/** The level whose settled reduction is `depthDb` on the hard knee (below Range). */
inline double levelForReductionDb (double thresholdDb, double ratio, double depthDb)
{
    return effectiveThresholdDb (thresholdDb, ratio) + depthDb / (1.0 - 1.0 / loopRatio (ratio));
}

/** Where the Threshold coupling gate finds the hard knee: T_eff. */
inline double kneeFoundAtDb (double thresholdDb, double ratio) { return effectiveThresholdDb (thresholdDb, ratio); }

/** The measured attack and release (the core's definition: 63.2 % up, 36.8 % down) are the knobs, 1:1 over their ranges. */
inline double attackMs (double attackKnobMs) { return attackKnobMs; }
inline double releaseMs (double releaseKnobMs) { return releaseKnobMs; }

/** The key's lag: L at All, else 0. Attack and release are measured from the end of it. */
inline double lagMs (double ratio) { return isAll (ratio) ? allLagMs : 0.0; }

/** The pre-hold h = min(Release/10, 1 ms). */
inline double holdMs (double releaseKnobMs) { return std::min (0.1 * releaseKnobMs, maxHoldMs); }

/** Auto release is dimmed in FET: no source documents a program-dependent release, so it changes nothing. */
inline constexpr bool autoReleaseChangesAnything = false;

/** The Ballistics gate's band around an expected time: ±10 %, or ±0.25 sample where that is wider. */
inline double timeToleranceMs (double expectedMs, double sampleRate)
{
    return std::max (timeTolerance * expectedMs, 0.25 * 1000.0 / sampleRate);
}

/**
    For the Knee region row (reported, not gated): the closed loop's attack time constant at the
    level it settles on, linearised, Attack·R·G'(x), with G' the curve's local slope there. Above
    the knee G' = 1/R and it is Attack; inside it the loop is slower. Inference from the loop's
    equations (dynamics/NOTES.md, Results: Knee region), not a published figure.
*/
inline double kneeRegionAttackMs (double attackKnobMs, double levelDb, double thresholdDb, double ratio, double kneeDb)
{
    const auto t = effectiveThresholdDb (thresholdDb, ratio);
    const auto r = loopRatio (ratio);
    const auto over = levelDb - t;
    auto slope = 1.0 / r;
    if (kneeDb > 0.0 && 2.0 * std::abs (over) <= kneeDb)
        slope = 1.0 + (1.0 / r - 1.0) * (over + 0.5 * kneeDb) / kneeDb;
    return attackKnobMs * r * slope;
}

} // namespace basalt::reference::fet
