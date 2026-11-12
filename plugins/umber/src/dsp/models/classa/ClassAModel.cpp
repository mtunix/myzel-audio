#include "dsp/models/classa/ClassAModel.h"

#include <cmath>

namespace umber::dsp
{

myzel::dsp::ClassAComponents ClassAModel::standardComponents()
{
    myzel::dsp::ClassAComponents c {};
    c.vcc      = 24.0;

    c.rs       = 600.0;
    c.cin      = 4.7e-6;
    c.r1       = 47.0e3;
    c.r2       = 6.8e3;
    c.rbb      = 120.0;

    c.re1      = 22.0;
    c.re2      = 1.0e3;
    c.ce       = 1000.0e-6;
    c.rc       = 5.6e3;
    c.ccb      = 33.0e-12;

    c.re3      = 1.5e3;
    c.cout     = 100.0e-6;
    c.rw       = 40.0;
    c.lm       = 4.0;
    c.rl       = 600.0;
    c.phiSat   = 0.016;
    c.coreKnee = 0.3;

    c.is1      = 1.0e-14;
    c.betaF1   = 250.0;
    c.betaR1   = 4.0;
    c.is2      = 4.0e-14;
    c.betaF2   = 120.0;
    c.vt       = 0.025852;   // 300 K

    c.biasTrimAmps = 600.0e-6;   // +-600 uA trim at the Bias extremes
    return c;
}

ClassAModel::ClassAModel (const myzel::dsp::ClassAComponents& c, double inputVoltsPerUnit)
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
