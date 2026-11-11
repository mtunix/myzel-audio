#pragma once

namespace umber::dsp
{

/**
    Per-sample linear ramp for a model's bias control. The chain updates the bias at most once
    per 32 samples; a component value that jumped that coarsely would tick, so models glide to
    each new target over `length` samples. The target is reached exactly (bit-identical nominal
    behaviour once the ramp ends at 0).
*/
struct BiasRamp
{
    double current = 0.0, target = 0.0;

    void setTarget (double newTarget, int length = 64) noexcept
    {
        if (newTarget == target)
            return;

        target = newTarget;
        remaining = length;
        step = (target - current) / (double) length;
    }

    void snap() noexcept
    {
        current = target;
        remaining = 0;
    }

    bool isMoving() const noexcept { return remaining > 0; }

    double next() noexcept
    {
        if (remaining > 0 && --remaining > 0)
            current += step;
        else
            current = target;

        return current;
    }

private:
    double step = 0.0;
    int remaining = 0;
};

} // namespace umber::dsp
