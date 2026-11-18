#pragma once

#include "dsp/dynamics/Levels.h"

#include <algorithm>

// Feed-forward ballistics: Giannoulis 2012's smooth decoupled detector (eq. 17) in the log domain
// after the gain computer (eq. 23), with the release solved so that the measured release is the
// knob (NOTES.md, Ballistics, Settled 28).

namespace basalt::dynamics
{

/** The cascade cannot release faster than its attack stage; the floor is 1.01 × τ_A (NOTES.md, Minimum release). */
inline constexpr double minimumReleaseRatio = 1.01;

/** Attack and Release glide in log time over this long, so a jump never completes a pending attack at once (NOTES.md, Clicks). */
inline constexpr double timeGlideMs = 20.0;

/**
    h(ρ) = τ_R/τ_A: the first stage's constant for which the two-pole cascade (τ_R, then τ_A)
    falls to 1/e of a step at ρ·τ_A. Tabulated over log ρ from 1 to 10⁴ (256 points) and
    interpolated linearly; ρ below the floor, or NaN, reads the floor, above 10⁴ h = ρ − 1. Real-time safe.
*/
double releaseRatio (double rho) noexcept;

/** h(ρ) solved directly by bisection, for the table, the tests and the measure tool. Not for the audio thread. */
double solveReleaseRatio (double rho) noexcept;

/** τ_R for a release (the measured time) with an attack stage of τ_A, both in ms. */
inline double releaseConstantMs (double releaseMs, double attackConstantMs) noexcept
{
    return attackConstantMs * releaseRatio (std::max (releaseMs / attackConstantMs, minimumReleaseRatio));
}

/**
    eq. 17 on a reduction r in dB: an instant-attack peak hold that releases towards r with τ_R,
    then a one-pole of τ_A. Each state snaps to 0 once it and its target are within 10⁻⁶ dB, so
    a released detector reads exactly 0.
*/
class SmoothDecoupled
{
public:
    /** The two poles: α_A for the smoothing stage, α_R for the hold's release. */
    struct Poles
    {
        double attack = 0.0, release = 0.0;
    };

    /** Poles for an attack constant τ_A and a release time measured as NOTES.md defines it. */
    static Poles polesFor (double attackConstantMs, double releaseMs, double sampleRate) noexcept
    {
        return { poleFor (attackConstantMs, sampleRate),
                 poleFor (releaseConstantMs (releaseMs, attackConstantMs), sampleRate) };
    }

    /** Both stages at r: a seed (Settled 33) or a reset. */
    void reset (double reductionDb = 0.0) noexcept { held = smooth = reductionDb; }

    double process (double r, const Poles& p) noexcept
    {
        held = snapState (std::max (r, p.release * held + (1.0 - p.release) * r), r);
        smooth = snapState (p.attack * smooth + (1.0 - p.attack) * held, held);
        return smooth;
    }

    double reduction() const noexcept { return smooth; }
    double hold() const noexcept { return held; }

private:
    double held = 0.0, smooth = 0.0;
};

} // namespace basalt::dynamics
