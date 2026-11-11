#include "dsp/models/classa/ClassAModel.h"

#include <cmath>

namespace umber::dsp
{

ClassAModel::ClassAModel (const classa::Components& c, double inputVoltsPerUnit)
    : components (c), inputVolts (inputVoltsPerUnit)
{
}

void ClassAModel::prepare (double oversampledRate)
{
    circuit.prepare (oversampledRate, components);

    // Unity small-signal gain at 1 kHz, measured on the discrete linearised system itself
    // (the equalised period averages the model plays). The common emitter inverts and the
    // follower/transformer do not, so the sign flips.
    const auto h = circuit.outputResponse (1000.0) * inputVolts;
    makeup = (h.real() < 0.0 ? -1.0 : 1.0) / std::abs (h);

    reset();
}

void ClassAModel::reset() noexcept
{
    bias.snap();
    circuit.setBias (bias.current);
    circuit.reset();
}

void ClassAModel::process (float* samples, int numSamples) noexcept
{
    constexpr double invRail2 = 1.0 / (sourceRailVolts * sourceRailVolts);

    for (int i = 0; i < numSamples; ++i)
    {
        if (bias.isMoving())
            circuit.setBias (bias.next());

        // The driving line stage cannot swing past its rails: x / sqrt (1 + (x / rail)^2) is
        // transparent in the musical range (-0.2 dB at +38 dBFS) and bounds absurd inputs.
        const auto v = inputVolts * (double) samples[i];
        const auto source = v / std::sqrt (1.0 + v * v * invRail2);
        circuit.process (source);
        samples[i] = (float) (makeup * circuit.lastOutput());
    }
}

} // namespace umber::dsp
