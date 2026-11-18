#pragma once

#include "dsp/dynamics/Levels.h"

#include <algorithm>

// Level detectors on the key (NOTES.md, What Basalt computes; models/clean/NOTES.md, Auto
// release). These states depend on the key alone, so a model runs them whether it is heard or not
// and an incoming model starts settled (Settled 33).

namespace basalt::dynamics
{

/**
    A one-pole started from rest on a running key reads low until its whole weight has built up:
    after n samples its state is (1 − α^n) of the level. A warm-up (a model about to be heard,
    NOTES.md, Model interface) divides that out, so it reads the weighted mean of the key it has
    seen, as a detector that had always run would read a steady key.
*/
class WarmUpWeight
{
public:
    void reset() noexcept { missing = 1.0; }

    /** One sample of a one-pole with this pole, then the factor that undoes its start from rest. */
    double next (double pole) noexcept
    {
        missing *= pole;
        return missing < 1.0 ? 1.0 / (1.0 - missing) : 1.0;
    }

    double factor() const noexcept { return missing < 1.0 ? 1.0 / (1.0 - missing) : 1.0; }

private:
    double missing = 1.0;
};

/** Mean square m[n] = β·m[n−1] + (1 − β)·k²[n] (Giannoulis 2012 eq. 10); read it with meanSquareDb(). */
class RmsDetector
{
public:
    void setTimeConstant (double ms, double sampleRate) noexcept { pole = poleFor (ms, sampleRate); }

    void reset() noexcept
    {
        m = 0.0;
        warmUp.reset();
    }

    double process (double squared) noexcept
    {
        m = flushLevel (pole * m + (1.0 - pole) * squared);
        return m;
    }

    /** process() during a warm-up from reset(): the mean square the samples so far give. */
    double processWarming (double squared) noexcept
    {
        process (squared);
        return m * warmUp.next (pole);
    }

    /** Ends a warm-up: the state becomes the mean square it read, and runs on as process() from there. */
    void endWarmUp() noexcept
    {
        m *= warmUp.factor();
        warmUp.reset();
    }

    double meanSquare() const noexcept { return m; }

private:
    double pole = 0.0, m = 0.0;
    WarmUpWeight warmUp;
};

/**
    The crest factor C² = p/q of Giannoulis et al. 2013 eq. 7: a peak detector p and a mean square
    q with the same time constant, so they release together and C² ≥ 1.

    The guard (models/clean/NOTES.md, Auto release): C² is 0/0 at reset and in silence, and a
    flush of q alone would send it to infinity in the middle of a release. So C² holds its last
    value while q < 10⁻²⁰, p and q flush together, and C² starts at 2, the sine's value.
*/
class CrestFactor
{
public:
    static constexpr double defaultMs = 200.0;
    static constexpr double resetCrestSquared = 2.0;
    static constexpr double holdBelow = 1.0e-20;

    void setTimeConstant (double ms, double sampleRate) noexcept { pole = poleFor (ms, sampleRate); }

    void reset() noexcept
    {
        p = q = 0.0;
        crest = resetCrestSquared;
        warmUp.reset();
    }

    void process (double squared) noexcept
    {
        p = std::max (squared, pole * p + (1.0 - pole) * squared);
        q = pole * q + (1.0 - pole) * squared;

        if (q < flushBelow)
            p = q = 0.0;
    }

    /**
        process() during a warm-up from reset(): q counts as the mean of the samples so far (see
        WarmUpWeight) once endWarmUp() takes it. p needs nothing: it holds the peaks it has seen.
    */
    void processWarming (double squared) noexcept
    {
        process (squared);
        warmUp.next (pole);
    }

    void endWarmUp() noexcept
    {
        q *= warmUp.factor();
        warmUp.reset();
    }

    /** C² now, held through silence. Cheap enough per sample, but meant for a slower update. */
    double crestSquared() noexcept
    {
        if (q >= holdBelow)
            crest = std::max (p / q, 1.0);
        return crest;
    }

    double peak() const noexcept { return p; }
    double meanSquare() const noexcept { return q; }

private:
    double pole = 0.0, p = 0.0, q = 0.0, crest = resetCrestSquared;
    WarmUpWeight warmUp;
};

} // namespace basalt::dynamics
