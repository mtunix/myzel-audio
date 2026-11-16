#pragma once

#include "common/dsp/Filters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace halite::dsp
{

/** What a level of nothing reads, in dB. */
inline constexpr float silentDb = -300.0f;

/** Power (mean square) in dB; 0 power reads silentDb, so a ratio of two levels stays finite. */
inline float powerToDb (double power) noexcept
{
    return (float) (10.0 * std::log10 (std::max (power, 1.0e-30)));
}

/**
    A band between two edges: a 2nd-order Butterworth high-pass at the low edge into a 2nd-order
    Butterworth low-pass at the high edge (4th order in all, 12 dB per octave on each side).
    powerScale() undoes the cascade's loss at the geometric centre, so a sine there reads its
    own power whatever the width (1 octave loses 1.9 dB, 0.3 octave 4.4 dB).
*/
class BandFilter
{
public:
    void setBand (double lowHz, double highHz, double sampleRate) noexcept
    {
        hp.setCoefficients (lowHz, std::numbers::sqrt2 / 2.0, sampleRate);
        lp.setCoefficients (highHz, std::numbers::sqrt2 / 2.0, sampleRate);

        // The TPT filters are bilinear, so the digital response at f is the analogue one at the
        // prewarped frequency.
        const auto warp = [sampleRate] (double hz) { return std::tan (std::numbers::pi * std::clamp (hz, 1.0, 0.49 * sampleRate) / sampleRate); };
        const auto lo = warp (lowHz), hi = warp (highHz), centre = warp (std::sqrt (lowHz * highHz));
        const auto hpPower = 1.0 / (1.0 + std::pow (lo / centre, 4.0));
        const auto lpPower = 1.0 / (1.0 + std::pow (centre / hi, 4.0));
        scale = 1.0 / (hpPower * lpPower);
    }

    void reset() noexcept
    {
        hp.reset();
        lp.reset();
    }

    double process (double x) noexcept { return lp.processLowpass (hp.processHighpass (x)); }

    /** As process(), and the low-pass stage's band-pass output around its edge: Q 0.707, so × √2 is unity at its peak. */
    double process (double x, double& upperBand) noexcept
    {
        const auto out = lp.process (hp.processHighpass (x));
        upperBand = std::numbers::sqrt2 * out.bp;
        return out.lp;
    }

    double powerScale() const noexcept { return scale; }

private:
    myzel::dsp::TptSvf hp, lp;
    double scale = 1.0;
};

/**
    The level detector every band uses (B, V, H and the Harsh body), so differences of two levels
    in dB do not depend on crest factors (PLAN.md, Detection 1): the power is averaged by a one-pole,
    then follows attack/release ballistics. It is linear in the power, so the dB difference of two
    levels does not change with the input gain. The times are one-pole time constants of the power:
    a release of τ falls by 4.34 dB per τ. For B (4 ms averaging, 0.5 / 5 ms ballistics) on
    4.5–12 kHz band noise: 0.45 dB ripple (std of the per-hop dB) at 44.1–96 kHz (NOTES.md,
    Measured (M1)); the attack alone, with a 20 ms release, gives 0.54 dB.
*/
class PowerEnvelope
{
public:
    void setTimes (double averagingMs, double attackMs, double releaseMs, double sampleRate) noexcept
    {
        averaging = coefficient (averagingMs, sampleRate);
        attack = coefficient (attackMs, sampleRate);
        release = coefficient (releaseMs, sampleRate);
    }

    void reset() noexcept { average = level = 0.0; }

    /** While held, the level can rise but not fall. */
    void setHeld (bool shouldHold) noexcept { held = shouldHold; }
    bool isHeld() const noexcept { return held; }

    void process (double power) noexcept
    {
        average += (power - average) * averaging;
        const auto rising = average > level;
        level += (average - level) * (rising ? attack : (held ? 0.0 : release));
    }

    /** The ballistic level (mean square). */
    double get() const noexcept { return level; }

    /** The averaged power before the ballistics: what is there now. */
    double present() const noexcept { return average; }

    /** A one-pole's coefficient for a time constant; 0 ms follows at once. */
    static double coefficient (double ms, double sampleRate) noexcept
    {
        return ms > 0.0 ? 1.0 - std::exp (-1.0 / (ms * 0.001 * sampleRate)) : 1.0;
    }

private:
    double averaging = 1.0, attack = 1.0, release = 1.0;
    double average = 0.0, level = 0.0;
    bool held = false;
};

/**
    Whether a level in dB rose by more than a margin over the last few hops, fed once per hop. A
    difference of dB, so it does not depend on the input gain.
*/
class RiseDetector
{
public:
    static constexpr int maxHops = 64;

    void setSpan (int hops) noexcept { span = std::clamp (hops, 1, maxHops - 1); }

    void reset() noexcept
    {
        history.fill (silentDb);
        newest = 0;
    }

    bool push (float db, float marginDb) noexcept
    {
        newest = (newest + 1) % maxHops;
        history[(size_t) newest] = db;
        return db > history[(size_t) ((newest - span + maxHops) % maxHops)] + marginDb;
    }

private:
    std::array<float, maxHops> history {};
    int span = 1, newest = 0;
};

/**
    The largest (or smallest) of the last `span` values fed, fed once per hop: a monotone queue,
    so each value is compared a couple of times however long the span. Before span values have
    been fed, the missing ones count as `initial`. Until prepare() it holds nothing, reads
    `initial` and ignores what it is fed, so a reset before prepare() is safe.
*/
template <bool largest>
class SlidingExtreme
{
public:
    /** Allocates. */
    void prepare (int newSpan, float newInitial)
    {
        span = std::max (1, newSpan);
        initial = newInitial;
        auto capacity = 1;
        while (capacity < span + 1)
            capacity <<= 1;
        entries.assign ((size_t) capacity, {});
        mask = capacity - 1;
        reset();
    }

    void reset() noexcept
    {
        head = tail = 0;
        count = 0;
        for (int i = 0; i < span; ++i)
            push (initial);
    }

    /** The extreme of the last span values fed. */
    float get() const noexcept { return head != tail ? entries[(size_t) (head & mask)].value : initial; }

    void push (float value) noexcept
    {
        if (entries.empty())
            return;
        while (head != tail && ! beats (entries[(size_t) ((tail - 1) & mask)].value, value))
            --tail;
        entries[(size_t) (tail++ & mask)] = { count++, value };
        if (entries[(size_t) (head & mask)].index <= count - 1 - span)
            ++head;
    }

private:
    struct Entry
    {
        std::int64_t index = 0;
        float value = 0.0f;
    };

    static bool beats (float a, float b) noexcept { return largest ? a > b : a < b; }

    std::vector<Entry> entries;
    int span = 1, mask = 0;
    std::int64_t head = 0, tail = 0, count = 0;
    float initial = 0.0f;
};

} // namespace halite::dsp
