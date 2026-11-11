#include "dsp/models/fuzz/FuzzModel.h"

#include <cmath>

namespace umber::dsp
{

void FuzzModel::prepare (double oversampledRate)
{
    circuit.tolerance = { 3.0e-3, 3.0e-2, 3.0e-3, 5.0e-2, 5.0e-2 };
    circuit.maxIterations = 16;
    circuit.setBiasRange (kBiasRangeVolts);
    circuit.prepare (fuzz::makeNetlist(), oversampledRate);

    const auto h = circuit.smallSignalGain (1000.0, oversampledRate);
    const double sign = h.real() < 0.0 ? -1.0 : 1.0;
    makeup = sign / (std::abs (h) * kInputVolts);
    reset();
}

void FuzzModel::reset() noexcept
{
    bias.snap();
    circuit.setBiasVolts (bias.current);
    circuit.reset();
}

void FuzzModel::process (float* samples, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        if (bias.isMoving())
            circuit.setBiasVolts (bias.next());

        const double u = tubes::softLimit (kInputVolts * (double) samples[n], kSourceKneeVolts, kSourceMaxVolts);
        samples[n] = (float) (makeup * circuit.process (u));
    }

    circuit.flushTinyStates();
}

} // namespace umber::dsp
