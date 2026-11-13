#pragma once

#include "dsp/DamageCurves.h"
#include "dsp/ModuleInfo.h"
#include "dsp/chain/ChainSettings.h"
#include "dsp/modules/glitch/GlitchPattern.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace inkcap::dsp::match
{

/**
    Match (PLAN.md, Match): output compensation so that Damage changes character, not loudness.

    Each module that changes the level (Glitch, Crush, Bits, Hiss, Room) has a table of the gain (dB) that
    restores the K-weighted loudness of pink noise at -18 dBFS RMS, over its type, its two
    level-relevant controls and its intensity (Amount x its Damage curve). tools/calibrate
    measures it through the real chain and writes MatchTables.h; the chain sums the modules that
    are on. Warble moves pitch, not level: no table (it stays within the gate without one,
    InkcapCalibrate --report). Glitch's table holds the mean over its events (Chew and Stall dip).

    Intensity 0 reads exactly 0 dB, whatever the table holds near it, so Damage 0 is exactly unity.
    A boost stops at maxBoostDb, per module and in all: where a module takes away most of a quiet
    mix (a 2-bit converter rounding it to zero, a transport stalled most of the time) the boost
    that matches -18 dBFS pink noise would blow up a louder programme. Cuts are never limited.
*/

inline constexpr float maxBoostDb = 12.0f;

/** One module's table, [type][a][b][intensity], dB; a, b and the intensity at even steps over their range. */
template <int Types, int APoints, int BPoints, int IPoints>
struct Table
{
    static constexpr int types = Types, aPoints = APoints, bPoints = BPoints, intensityPoints = IPoints;
    static constexpr int size = Types * APoints * BPoints * IPoints;

    static constexpr int indexOf (int type, int a, int b, int i) noexcept
    {
        return ((type * APoints + a) * BPoints + b) * IPoints + i;
    }

    std::array<float, (size_t) size> db {};
};

/**
    Glitch: Length at 7 points, one row per Rate (a choice: interpolating between grids means
    nothing). Its level follows the share of slots that fire, which at Chaos 0 steps with the
    steady pattern's period: the last axis is that share, none and then each period's 1 / N
    (glitch::steadyPeriods), exact at Chaos 0 and interpolated as Chaos blends in a probability.
*/
using GlitchTable = Table<5, 7, numGlitchRates, 1 + (int) glitch::steadyPeriods.size()>;

/** The mean share of slots that fire at a point of Glitch's last axis: 0, then 1 / 128 .. 1 / 2. */
inline float glitchDensityAt (int k) noexcept
{
    return k <= 0 ? 0.0f : 1.0f / (float) glitch::steadyPeriods[glitch::steadyPeriods.size() - (size_t) k];
}
/** Crush, Bits, Hiss, Room: their two controls at 7 points each, intensity at 9 (Bits and Hiss leave the fourth type unused). */
using ModuleTable = Table<4, 7, 7, 9>;

/** The two controls a module's table runs over, as positions 0 .. 1. */
struct Controls
{
    float a = 0.5f, b = 0.5f;
};

inline float bipolar (float v) noexcept { return 0.5f * (std::clamp (v, -1.0f, 1.0f) + 1.0f); }

inline Controls controlsOf (const GlitchSettings& s) noexcept
{
    return { s.length, (float) std::clamp (s.rate, 0, numGlitchRates - 1) / (float) (numGlitchRates - 1) };
}
inline Controls controlsOf (const CrushSettings& s) noexcept { return { s.drive, bipolar (s.tone) }; }
inline Controls controlsOf (const BitsSettings& s) noexcept { return { s.rate, s.depth }; }
inline Controls controlsOf (const HissSettings& s) noexcept { return { bipolar (s.tone), bipolar (s.follow) }; }
inline Controls controlsOf (const RoomSettings& s) noexcept { return { s.size, s.duck }; }

/** A point between two grid points of an axis: the lower one and the share of the way to the next. */
struct AxisPoint
{
    int lo = 0;
    float f = 0.0f;
};

inline AxisPoint evenAxis (float position, int points) noexcept
{
    const auto x = std::clamp (position, 0.0f, 1.0f) * (float) (points - 1);
    const auto lo = std::min ((int) x, points - 2);
    return { lo, x - (float) lo };
}

/** Multilinear interpolation in the table at a type, the controls and a point on the last axis. */
template <typename T>
float lookupAt (const T& table, int type, Controls c, AxisPoint i) noexcept
{
    const auto t = std::clamp (type, 0, T::types - 1);
    const auto a = evenAxis (c.a, T::aPoints), b = evenAxis (c.b, T::bPoints);

    float sum = 0.0f;
    for (int da = 0; da < 2; ++da)
        for (int db = 0; db < 2; ++db)
            for (int di = 0; di < 2; ++di)
            {
                const auto w = (da == 0 ? 1.0f - a.f : a.f) * (db == 0 ? 1.0f - b.f : b.f) * (di == 0 ? 1.0f - i.f : i.f);
                if (w > 0.0f)
                    sum += w * table.db[(size_t) T::indexOf (t, a.lo + da, b.lo + db, i.lo + di)];
            }
    return sum;
}

/** The table at an intensity (even steps); exactly 0 at intensity 0. */
template <typename T>
float lookup (const T& table, int type, Controls c, float intensity) noexcept
{
    return intensity > 0.0f ? lookupAt (table, type, c, evenAxis (intensity, T::intensityPoints)) : 0.0f;
}

/** Glitch's table at the mean share of slots that fire; exactly 0 when none do. */
inline float lookupGlitch (const GlitchTable& table, int type, Controls c, float meanDensity) noexcept
{
    if (! (meanDensity > 0.0f))
        return 0.0f;

    AxisPoint p { GlitchTable::intensityPoints - 2, 1.0f };
    for (int k = 1; k < GlitchTable::intensityPoints; ++k)
        if (meanDensity <= glitchDensityAt (k))
        {
            const auto lo = glitchDensityAt (k - 1), hi = glitchDensityAt (k);
            p = { k - 1, (meanDensity - lo) / (hi - lo) };
            break;
        }
    return lookupAt (table, type, c, p);
}

/** The compensation (dB) for the chain's settings: the sum over the modules that are on; 0 with Match off. */
float compensationDb (const ChainSettings&) noexcept;

/** A grid point's position (0 .. 1) on an axis of `points`. */
inline float positionAt (int k, int points) noexcept { return (float) k / (float) (points - 1); }

} // namespace inkcap::dsp::match
