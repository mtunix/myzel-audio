#pragma once

#include "dsp/common/CircuitModel.h"

#include <cmath>

namespace umber::dsp
{

/**
    Temporary stand-in used by model stubs until the circuit models land.
    Asymmetric tanh with first-order antiderivative anti-aliasing (ADAA).
    Unity small-signal gain, non-inverting, DC removed by the chain.
*/
class PlaceholderSaturator : public CircuitModel
{
public:
    /** knee: input level where saturation starts. bias: asymmetry (adds even harmonics). */
    PlaceholderSaturator (double kneeIn, double biasIn) : knee (kneeIn), bias (biasIn) {}

    void prepare (double) override { reset(); }

    void reset() noexcept override
    {
        xPrev = 0.0;
        fPrev = antiderivative (0.0);
    }

    void process (float* samples, int numSamples) noexcept override
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const double x = samples[i] / knee;
            const double F = antiderivative (x);
            const double dx = x - xPrev;

            const double y = std::abs (dx) > 1.0e-5 ? (F - fPrev) / dx
                                                    : shape (0.5 * (x + xPrev));
            xPrev = x;
            fPrev = F;
            samples[i] = static_cast<float> ((y - shape (0.0)) * knee * gainNorm());
        }
    }

private:
    double shape (double x) const noexcept { return std::tanh (x + bias); }

    // d/dx log(cosh(x + b)) = tanh(x + b). Overflow-safe log-cosh.
    double antiderivative (double x) const noexcept
    {
        const double u = std::abs (x + bias);
        return u + std::log1p (std::exp (-2.0 * u)) - std::log (2.0);
    }

    double gainNorm() const noexcept
    {
        const double t = std::tanh (bias);
        return 1.0 / (1.0 - t * t); // 1 / slope at origin
    }

    double knee, bias;
    double xPrev = 0.0, fPrev = 0.0;
};

} // namespace umber::dsp
