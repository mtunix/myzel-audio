#pragma once

#include <algorithm>
#include <cmath>

namespace cerulean::dsp
{

/**
    Local variant of GlideValue (ReverbBlocks.h) that keeps its state in double.

    GlideValue steps in float: for a delay of ~1400 samples the float spacing is 1.2e-4, so once
    the one-pole step (distance x 9.5e-5 per sample at tau = 220 ms) falls below half of that,
    value + step rounds back to value and the glide stalls up to ~0.6 samples short of its target
    (~1.3 samples at Size 2x). For Freeze that matters: the target is rounded to an integer so
    the Lagrange read is exact and the loop lossless, but a stalled glide leaves a fractional
    read that loses ~1 % per round trip at 6 kHz (measured: -7 dB at 6 kHz over 36 s frozen).
    In double the glide converges and then snaps exactly onto the target.

    Same law as GlideValue: one-pole approach (tau) with a slew limit, so Size still glides
    like tape.
*/
class PreciseGlide
{
public:
    void prepare (double sampleRate, double tauMs = 220.0, double maxSlewPerSample = 0.12) noexcept
    {
        coeff = 1.0 - std::exp (-1.0 / (tauMs * 0.001 * sampleRate));
        maxSlew = maxSlewPerSample;
    }

    void snap (double v) noexcept { value = target = v; }
    void setTarget (double v) noexcept { target = v; }
    double getTarget() const noexcept { return target; }
    float get() const noexcept { return (float) value; }

    /** Advances n samples; returns the new value. */
    float advance (int n) noexcept
    {
        if (value >= target && value <= target)
            return (float) value;

        for (int i = 0; i < n; ++i)
            value += std::clamp ((target - value) * coeff, -maxSlew, maxSlew);

        if (std::abs (target - value) < 1.0e-6)
            value = target;
        return (float) value;
    }

private:
    double value = 0.0, target = 0.0, coeff = 1.0, maxSlew = 1.0;
};

} // namespace cerulean::dsp
