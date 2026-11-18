#pragma once

#include "dsp/dynamics/Levels.h"

#include <algorithm>
#include <cmath>

// The VCA's two sets of ballistics (NOTES.md here, 3. Ballistics): the Peak detector's smooth
// decoupled cascade with its first stage replaced by a two-capacitor network for Auto release,
// and the RMS detector's release-rate limiter.

namespace basalt::models
{

/**
    The two-capacitor network of Peak's Auto release, advanced exactly over one sample. In the dB
    domain, between samples:

        d/dt v1 = −(v1 − v2) / τ1
        d/dt v2 =  (v1 − v2) / (m·τ1) − v2 / τ2

    The step is the matrix exponential of that system, e^(A/fs) = e^p·(cosh q·I + sinh q / q·(A/fs − p·I)),
    p half its trace and q² = ((a11 − a22)/2)² + a12·a21 > 0, so the eigenvalues are real.
*/
struct TwoStageNetwork
{
    double fastFromFast = 1.0, fastFromSlow = 0.0, slowFromFast = 0.0, slowFromSlow = 1.0;

    static TwoStageNetwork forTimes (double fastMs, double slowMs, double capacitorRatio, double sampleRate) noexcept
    {
        const auto dt = 1000.0 / sampleRate;
        const auto a11 = -dt / fastMs, a12 = dt / fastMs;
        const auto a21 = dt / (capacitorRatio * fastMs), a22 = -a21 - dt / slowMs;
        const auto p = 0.5 * (a11 + a22);
        const auto half = 0.5 * (a11 - a22);
        const auto q = std::sqrt (half * half + a12 * a21);
        const auto scale = std::exp (p);
        const auto c = std::cosh (q);
        const auto s = q > 1.0e-12 ? std::sinh (q) / q : 1.0;

        return { scale * (c + s * (a11 - p)), scale * s * a12, scale * s * a21, scale * (c + s * (a22 - p)) };
    }
};

/**
    Peak's ballistics on a static reduction r in dB. The first stage is eq. 17's hold relaxing
    towards r with τ1 (Auto off), or the network's small capacitor relaxing into the large one
    and topped up to r by the rectifier (Auto on); the second stage is the attack one-pole. While
    Auto release glides, the first stage blends the two updates by its amount, so the reduction's
    slope never steps; the large capacitor starts empty when Auto comes on and is dropped when it
    goes off. Each state snaps to exactly 0 with its target.
*/
class PeakBallistics
{
public:
    struct Coefficients
    {
        double attack = 0.0, release = 0.0;     // α_A, and α_R of τ1 for eq. 17
        TwoStageNetwork network;
    };

    /** A seed (Settled 33) or a reset: both stages at r, the large capacitor empty. */
    void reset (double reductionDb = 0.0) noexcept
    {
        held = smooth = reductionDb;
        slow = 0.0;
    }

    /** autoAmount: 0 is Auto off, 1 Auto on, between while it glides. */
    double process (double r, const Coefficients& k, double autoAmount) noexcept
    {
        double next;

        if (autoAmount > 0.0)
        {
            const auto& n = k.network;
            const auto fast = n.fastFromFast * held + n.fastFromSlow * slow;
            slow = n.slowFromFast * held + n.slowFromSlow * slow;
            next = autoAmount >= 1.0 ? fast : fast + (1.0 - autoAmount) * (k.release * held + (1.0 - k.release) * r - fast);
        }
        else
        {
            next = k.release * held + (1.0 - k.release) * r;
            slow = 0.0;
        }

        held = dynamics::snapState (std::max (r, next), r);
        slow = dynamics::snapState (slow, held);
        smooth = dynamics::snapState (k.attack * smooth + (1.0 - k.attack) * held, held);
        return smooth;
    }

    double reduction() const noexcept { return smooth; }
    double fastStage() const noexcept { return held; }
    double slowStage() const noexcept { return slow; }

private:
    double held = 0.0, slow = 0.0, smooth = 0.0;
};

/**
    RMS's ballistics: the detector alone attacks (the reduction follows a rising target at once),
    and the reduction recovers at most `step` dB per sample, 10 dB per Release (the 160A's
    constant rate). A target below the knee is exactly 0, so the release lands on exactly 0.
*/
class RateLimitedRelease
{
public:
    void reset (double reductionDb = 0.0) noexcept { y = reductionDb; }

    double process (double r, double step) noexcept
    {
        y = dynamics::snapState (r >= y ? r : std::max (r, y - step), r);
        return y;
    }

    double reduction() const noexcept { return y; }

private:
    double y = 0.0;
};

} // namespace basalt::models
