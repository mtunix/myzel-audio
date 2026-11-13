#pragma once

#include "dsp/ModuleInfo.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace inkcap::dsp
{

/**
    The two macros (PLAN.md, Controls). Damage takes every module from clean to its Amount,
    Chaos from steady, periodic wear to irregular wear.

    Each module has its own Damage curve, d^exponent: voiced in M3 so that the middle of the pad
    is already musical for every module, each in the unit it is heard in (the targets and the
    measured ladder are in each module's NOTES.md; `InkcapCalibrate --voicing` prints it). Each
    module maps its intensity onto its own controls so that equal steps sound like equal steps;
    the curve only places those steps on the pad. Whatever the curve, Damage 0 gives exactly 0:
    the null gate rests on it.
*/
inline constexpr std::array<float, numModules> damageExponents {
    1.0f,       // Glitch: its density (0.5 i^2 of the slots) doubles the events per bar every sixth of the pad
    1.0f,       // Warble: its depth law (i^1.3) already spaces the cents
    0.6f,       // Crush: drive in dB; the warmth comes in well before the middle
    0.75f,      // Bits: clock and depth log-linear from where a converter is first heard
    1.0f,       // Hiss: the bed's level law is in dB
    1.0f,       // Room: the wet in dB follows its amplitude law
};

inline float damageCurve (Module m, float damage) noexcept
{
    const auto d = std::clamp (damage, 0.0f, 1.0f);
    return d <= 0.0f ? 0.0f : std::pow (d, damageExponents[(size_t) m]);
}

/** A module's Damage-scaled Amount, 0 .. 1: how far the module is from clean right now. */
inline float moduleIntensity (Module m, float amount, float damage) noexcept
{
    return std::clamp (amount, 0.0f, 1.0f) * damageCurve (m, damage);
}

/** The pad's own wear, for the display (its contour rings): the modules' curves on average. */
inline float padWear (float damage) noexcept
{
    float sum = 0.0f;
    for (int m = 0; m < numModules; ++m)
        sum += damageCurve ((Module) m, damage);
    return sum / (float) numModules;
}

/** Chaos as the modules read it, 0 (periodic) .. 1 (random). */
inline float chaosAmount (float chaos) noexcept
{
    return std::clamp (chaos, 0.0f, 1.0f);
}

} // namespace inkcap::dsp
