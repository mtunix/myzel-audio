#pragma once

// Upward's specification as pure functions (src/dsp/models/upward/NOTES.md), written from the
// NOTES, never from the engine's code: what the unit tests and BasaltMeasure compare the engine
// against. Header-only and free of the engine, so the measure tool can include it as it is.
// Levels are on the detector scale (a sine reads its peak; the constant-magnitude carrier reads
// 3.01 dB above its amplitude on this RMS detector). Lifts are in dB, ≥ 0; the engine hands the
// chain the reduction −ℓ.

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace basalt::measure::reference::upward
{

inline constexpr double rmsTimeConstantMs = 10.0;     // NOTES.md, 1. Detector
inline constexpr double floorFadeDb = 12.0;           // 2. Gain computer: the fade above Floor
inline constexpr double liftPerReleaseDb = 10.0;      // 3. Ballistics: Release is the time for this much lift to come in
inline constexpr double autoMemoryMs = 2000.0;        // Auto: τ_h
inline constexpr double autoSlowdown = 4.0;           // Auto: k = 1 + 4·h, so at most 5 × slower
inline constexpr double autoGainReferenceDb = -18.0;  // the core's Auto Gain reference
inline constexpr double snapDb = 1.0e-6;              // the core's snap to exactly 0

/** How much higher the carrier reads on an RMS detector than its amplitude: 10·log10(2). */
inline constexpr double carrierReadingOffsetDb = 3.0102999566398120;

/** What the gain computer reads: Threshold, 1/Ratio (0 at ∞), Knee, Range and Floor, in dB. */
struct Curve
{
    double thresholdDb = -18.0;
    double inverseRatio = 0.25;
    double kneeDb = 6.0;
    double rangeDb = 40.0;
    double floorDb = -70.0;
};

/** The mirrored quadratic knee q(u), u = T − x the depth below Threshold. */
inline double kneeDepth (double u, double kneeDb)
{
    if (u < -0.5 * kneeDb)
        return 0.0;
    if (kneeDb > 0.0 && std::abs (u) <= 0.5 * kneeDb)
        return (u + 0.5 * kneeDb) * (u + 0.5 * kneeDb) / (2.0 * kneeDb);
    return u;
}

/** ℓ₁ = min((1 − 1/R)·q(T − x), Range): the lift before the Floor fade. */
inline double liftBeforeFloor (const Curve& c, double x)
{
    return std::min ((1.0 - c.inverseRatio) * kneeDepth (c.thresholdDb - x, c.kneeDb), c.rangeDb);
}

/** w = s²(3 − 2s), s = clamp((x − F)/12, 0, 1): 0 at and below Floor, 1 from Floor + 12 dB. */
inline double floorWeight (double x, double floorDb)
{
    const auto s = std::clamp ((x - floorDb) / floorFadeDb, 0.0, 1.0);
    return s * s * (3.0 - 2.0 * s);
}

/** ℓ(x) = w·ℓ₁, the static lift. */
inline double lift (const Curve& c, double x)
{
    return floorWeight (x, c.floorDb) * liftBeforeFloor (c, x);
}

/** The engine's static reduction: −ℓ (0 where there is no lift). */
inline double reduction (const Curve& c, double x)
{
    return 0.0 - lift (c, x);
}

/** The transfer curve: output level x + ℓ(x). */
inline double outputLevel (const Curve& c, double x)
{
    return x + lift (c, x);
}

/**
    d(output)/dx, analytically: 1 + w'·ℓ₁ + w·ℓ₁'. At least 1/R wherever ℓ₁ is differentiable, so
    the output never folds back; above 1 inside the Floor fade (an expander there).
*/
inline double outputSlope (const Curve& c, double x)
{
    const auto s = std::clamp ((x - c.floorDb) / floorFadeDb, 0.0, 1.0);
    const auto w = s * s * (3.0 - 2.0 * s);
    const auto dw = (s > 0.0 && s < 1.0) ? 6.0 * s * (1.0 - s) / floorFadeDb : 0.0;
    const auto u = c.thresholdDb - x;
    const auto uncapped = (1.0 - c.inverseRatio) * kneeDepth (u, c.kneeDb);
    const auto capped = std::min (uncapped, c.rangeDb);

    auto dq = 0.0;      // dq/du
    if (u >= -0.5 * c.kneeDb)
        dq = c.kneeDb > 0.0 && std::abs (u) <= 0.5 * c.kneeDb ? (u + 0.5 * c.kneeDb) / c.kneeDb : 1.0;
    const auto dl = uncapped < c.rangeDb ? -(1.0 - c.inverseRatio) * dq : 0.0;

    return 1.0 + dw * capped + w * dl;
}

/**
    Auto Gain for Upward (NOTES.md, 4. Make-up): M = −ℓ(L_ref), L_ref = −18 dB on the detector
    scale, so material at the reference keeps its level. A cut, or 0 when Threshold sits at or
    below −18 − W/2 (or the reference is at or under Floor).
*/
inline double autoGainDb (const Curve& c)
{
    return 0.0 - lift (c, autoGainReferenceDb);
}

/** The static curve with Auto Gain on: x + ℓ(x) + M. */
inline double outputLevelWithAutoGain (const Curve& c, double x)
{
    return outputLevel (c, x) + autoGainDb (c);
}

/** The carrier's amplitude in dBFS for a reading on this detector. */
inline double carrierAmplitudeDbFor (double readingDb)
{
    return readingDb - carrierReadingOffsetDb;
}

/**
    The detector's floor on the lift-in (NOTES.md, Minimum release): the RMS level falls at
    10·log10(e)/τ dB/s, so the lift cannot come in faster than (1 − 1/R) of that. The time per
    10 dB of lift, ln 10 · τ / (1 − 1/R): 23.03 ms at ∞, 30.7 ms at 4:1. Infinite at Ratio 1.
*/
inline double detectorReleaseFloorMs (double inverseRatio)
{
    const auto c = 1.0 - inverseRatio;
    return c > 0.0 ? std::numbers::ln10 * rmsTimeConstantMs / c : std::numeric_limits<double>::infinity();
}

/** Time per 10 dB of lift with Auto off: Release, or the detector's floor where Release is shorter. */
inline double releasePer10DbMs (double releaseMs, double inverseRatio)
{
    return std::max (releaseMs, detectorReleaseFloorMs (inverseRatio));
}

/** Auto's memory after `seconds` above Threshold from `start`: 1 − (1 − start)·e^(−t/τ_h). */
inline double autoMemoryAfter (double seconds, double start = 0.0)
{
    return 1.0 - (1.0 - start) * std::exp (-seconds * 1000.0 / autoMemoryMs);
}

/** Auto's slowdown of the lift-in for a memory h: k = 1 + 4h, 1 … 5. */
inline double autoFactor (double memory)
{
    return 1.0 + autoSlowdown * memory;
}

/** The knobs the ballistics read. */
struct Settings
{
    Curve curve;
    double attackMs = 10.0;
    double releaseMs = 150.0;
    bool autoRelease = false;
};

/**
    The specified equations sample by sample (NOTES.md, 1–3), one detector channel, look-ahead 0,
    from rest: m = β·m + (1 − β)·k², L = 10·log10(2m), ℓ(L), h = α_h·h + (1 − α_h)·[L > T],
    y = α_A·y + (1 − α_A)·ℓ when ℓ < y, else min(ℓ, y + 10 dB / (Release·fs·k)), the snap. The
    lift y per sample, in dB.
*/
inline std::vector<double> liftTrace (const Settings& s, const std::vector<double>& key, double rate)
{
    const auto beta = std::exp (-1.0 / (rmsTimeConstantMs * 0.001 * rate));
    const auto alphaA = std::exp (-1.0 / (s.attackMs * 0.001 * rate));
    const auto alphaH = std::exp (-1.0 / (autoMemoryMs * 0.001 * rate));
    const auto step = liftPerReleaseDb / (s.releaseMs * 0.001 * rate);

    std::vector<double> out;
    out.reserve (key.size());
    double m = 0.0, y = 0.0, h = 0.0;

    for (const auto k : key)
    {
        m = beta * m + (1.0 - beta) * k * k;
        const auto level = m > 0.0 ? 10.0 * std::log10 (2.0 * m) : -std::numeric_limits<double>::infinity();
        const auto target = std::isfinite (level) ? lift (s.curve, level) : 0.0;
        h = alphaH * h + (1.0 - alphaH) * (level > s.curve.thresholdDb ? 1.0 : 0.0);
        const auto k2 = s.autoRelease ? autoFactor (h) : 1.0;

        y = target < y ? alphaA * y + (1.0 - alphaA) * target : std::min (target, y + step / k2);
        if (std::abs (y) < snapDb && std::abs (target) < snapDb)
            y = 0.0;
        out.push_back (y);
    }

    return out;
}

/** The carrier at a reading on this detector, n samples. */
inline std::vector<double> carrierAt (double readingDb, int n)
{
    const auto a = std::pow (10.0, carrierAmplitudeDbFor (readingDb) / 20.0);
    std::vector<double> s ((size_t) n);
    for (int i = 0; i < n; ++i)
        s[(size_t) i] = i % 2 == 0 ? a : -a;
    return s;
}

/**
    The first time r crosses `level` going the way `rising` says, from sample `from` on: sample
    from + j sits at (j + 1)/fs, the value before at t = 0 is `startValue`, crossings
    interpolated linearly (the core's measurement definition). Seconds; NaN if never.
*/
inline double crossingSeconds (const std::vector<double>& r, size_t from, double rate, double level, bool rising, double startValue)
{
    double prevT = 0.0, prevV = startValue;
    for (auto j = (size_t) 0; from + j < r.size(); ++j)
    {
        const auto t = (double) (j + 1) / rate;
        const auto v = r[from + j];
        if (rising ? v >= level : v <= level)
            return std::abs (v - prevV) > 0.0 ? prevT + (level - prevV) / (v - prevV) * (t - prevT) : t;
        prevT = t;
        prevV = v;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

/** The attack gate's set-up (NOTES.md, Gates): the carrier from T − 20 to T + 10 on this detector's reading. */
struct AttackGate
{
    static constexpr double thresholdDb = -30.0, floorDb = -90.0, rangeDb = 40.0, kneeDb = 0.0;
    static constexpr double belowDb = 20.0, aboveDb = 10.0;
    static constexpr double releaseMs = 50.0;       // brings the starting lift in quickly; the attack does not read it

    /** Long enough for the starting lift to settle: the RMS's onset leaves it on an Attack one-pole. */
    static double holdSecondsFor (double attackMs) { return std::max (1.0, 0.02 * attackMs); }
};

/**
    The specified attack: the time from the step until the lift falls to 36.8 % of the lift at
    the step, on AttackGate's set-up (the carrier from T − belowDb to T + aboveDb), in seconds.
    It does not depend on Ratio (the target's fraction of its start is (T − L)/belowDb at every
    ratio); the detector's rise adds to Attack, the more the smaller the step.
*/
inline double attackSeconds (double attackMs, double rate, double inverseRatio = 0.25, double belowDb = AttackGate::belowDb,
                             double aboveDb = AttackGate::aboveDb)
{
    Settings s;
    s.curve = { AttackGate::thresholdDb, inverseRatio, AttackGate::kneeDb, AttackGate::rangeDb, AttackGate::floorDb };
    s.attackMs = attackMs;
    s.releaseMs = AttackGate::releaseMs;

    const auto hold = (int) std::lround (AttackGate::holdSecondsFor (attackMs) * rate);
    auto key = carrierAt (AttackGate::thresholdDb - belowDb, hold);
    const auto loud = carrierAt (AttackGate::thresholdDb + aboveDb, (int) std::lround ((10.0 * attackMs * 0.001 + 0.1) * rate));
    key.insert (key.end(), loud.begin(), loud.end());

    const auto y = liftTrace (s, key, rate);
    const auto start = y[(size_t) hold - 1];
    return crossingSeconds (y, (size_t) hold, rate, std::exp (-1.0) * start, false, start);
}

/**
    What the attack gate compares the engine with: Attack itself from 3 ms (NOTES.md, Gates), and
    below 3 ms the specified attack above, where the detector's rise is a large share. In seconds.
*/
inline double gatedAttackSeconds (double attackMs, double rate)
{
    return attackMs >= 3.0 ? attackMs * 0.001 : attackSeconds (attackMs, rate);
}

/** The release gate's set-up: the carrier from T + 10 to T − 60, Range 20, Floor −90, the lift's slope from 2 to 12 dB. */
struct ReleaseGate
{
    static constexpr double thresholdDb = -10.0, floorDb = -90.0, rangeDb = 20.0, kneeDb = 0.0;
    static constexpr double aboveDb = 10.0, belowDb = 60.0;
    static constexpr double fromDb = 2.0, toDb = 12.0;
};

/**
    The specified lift-in on ReleaseGate's set-up: the carrier held at T + 10 for `heldSeconds`
    from rest (silence before, so Auto's memory starts empty), then at T − 60; the time the lift
    takes from 2 to 12 dB, in seconds per 10 dB.
*/
inline double liftInSeconds (double releaseMs, double inverseRatio, double rate, bool autoRelease = false,
                             double heldSeconds = 0.5, double attackMs = 5.0)
{
    Settings s;
    s.curve = { ReleaseGate::thresholdDb, inverseRatio, ReleaseGate::kneeDb, ReleaseGate::rangeDb, ReleaseGate::floorDb };
    s.attackMs = attackMs;
    s.releaseMs = releaseMs;
    s.autoRelease = autoRelease;

    const auto held = (int) std::lround (heldSeconds * rate);
    auto key = carrierAt (ReleaseGate::thresholdDb + ReleaseGate::aboveDb, held);
    const auto slowest = (autoRelease ? autoFactor (1.0) : 1.0) * releasePer10DbMs (releaseMs, inverseRatio);
    const auto quiet = carrierAt (ReleaseGate::thresholdDb - ReleaseGate::belowDb,
                                  (int) std::lround ((1.3 * slowest + 250.0) * 0.001 * rate));
    key.insert (key.end(), quiet.begin(), quiet.end());

    const auto y = liftTrace (s, key, rate);
    const auto from = crossingSeconds (y, (size_t) held, rate, ReleaseGate::fromDb, true, y[(size_t) held - 1]);
    const auto to = crossingSeconds (y, (size_t) held, rate, ReleaseGate::toDb, true, y[(size_t) held - 1]);
    return (to - from) * liftPerReleaseDb / (ReleaseGate::toDb - ReleaseGate::fromDb);
}

/**
    Auto release's gate (NOTES.md, Gates): Release 150 ms, Attack 5 ms, Ratio 4, the lift-in per
    10 dB after `secondsAbove` at T + 10 from rest. M0's simulation read 209 ms after 0.2 s and
    606 ms after 6 s.
*/
inline double autoLiftInSeconds (double secondsAbove, double rate, double releaseMs = 150.0)
{
    return liftInSeconds (releaseMs, 0.25, rate, true, secondsAbove, 5.0);
}

} // namespace basalt::measure::reference::upward
