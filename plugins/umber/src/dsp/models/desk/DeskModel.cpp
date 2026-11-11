#include "dsp/models/desk/DeskModel.h"

#include <cmath>

namespace umber::dsp
{

DeskCircuit DeskCircuit::standard()
{
    DeskCircuit c {};
    c.voltsPerUnit = 2.5;

    // Input: 10k:10k bridging transformer on an 80 % nickel (mu-metal class) core, fed from a
    // 600 ohm source, terminated by 10k at the amplifier input.
    c.input.core                  = { 5.5e5, 3.0, 1.0, 0.5, 3.0e-7 };
    c.input.R1                    = 600.0 + 350.0;
    c.input.Cc                    = 0.0;
    c.input.Lleak                 = 30.0e-3;
    c.input.R2                    = 350.0;
    c.input.Cw                    = 150.0e-12;
    c.input.RL                    = 10.0e3;
    c.input.saturationFlux        = 0.06;
    c.input.smallSignalInductance = 46.0;

    // Line amplifier: +18 dB, modest feedback, ~15 V rails.
    c.amp = { 8.0, 3.0, 15.0, 0.92, 5.0e6 };

    // Output: 600:600 transformer on a compact 50 % nickel core, driven through a 47 ohm
    // build-out resistor and a 2200 uF coupling capacitor, into a 10k bridging load.
    c.output.core                  = { 1.1e6, 20.0, 5.0, 0.7, 2.0e-6 };
    c.output.R1                    = 47.0 + 40.0;
    c.output.Cc                    = 2200.0e-6;
    c.output.Lleak                 = 10.0e-3;
    c.output.R2                    = 40.0;
    c.output.Cw                    = 250.0e-12;
    c.output.RL                    = 10.0e3;
    c.output.saturationFlux        = 0.04;
    c.output.smallSignalInductance = 2.5;

    return c;
}

DeskModel::DeskModel() : DeskModel (DeskCircuit::standard()) {}

DeskModel::DeskModel (const DeskCircuit& c) : circuit (c)
{
    inputStage.setParams (circuit.input);
    outputStage.setParams (circuit.output);
    amp.setParams (circuit.amp);
}

double DeskModel::smallSignalGain (const DeskCircuit& c, double frequency)
{
    const auto h = c.voltsPerUnit * c.input.smallSignalResponse (frequency) * c.amp.gain
                 * c.output.smallSignalResponse (frequency);
    return std::abs (h);
}

void DeskModel::prepare (double oversampledRate)
{
    inputStage.prepare (oversampledRate);
    outputStage.prepare (oversampledRate);
    amp.prepare (oversampledRate);
    makeup = 1.0 / smallSignalGain (circuit, 1000.0);
    reset();
}

void DeskModel::reset() noexcept
{
    inputStage.reset();
    outputStage.reset();
    amp.reset();
    bias.snap();
    amp.setInputOffset (bias.current);
}

void DeskModel::process (float* samples, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const double kin = circuit.voltsPerUnit;
    const auto source = [&] (int i) noexcept { return kin * (std::isfinite (samples[i]) ? (double) samples[i] : 0.0); };

    // Software-pipelined by one sample: each transformer's per-sample solve is one long serial
    // dependency chain, so the output stage of sample i-1 and the input stage of sample i run
    // phase by phase side by side (processPair), where the CPU overlaps the two independent
    // chains. Bit-identical to input -> amp -> output per sample.
    if (bias.isMoving())
        amp.setInputOffset (bias.next());

    double v2 = amp.process (inputStage.process (source (0)));
    for (int i = 1; i < numSamples; ++i)
    {
        double v3 = v2, v1 = source (i);
        desk::TransformerStage::processPair (outputStage, v3, inputStage, v1);
        samples[i - 1] = (float) (makeup * v3);

        if (bias.isMoving())
            amp.setInputOffset (bias.next());

        v2 = amp.process (v1);
    }
    samples[numSamples - 1] = (float) (makeup * outputStage.process (v2));

    inputStage.flushDenormals();
    outputStage.flushDenormals();
    amp.flushDenormals();
}

} // namespace umber::dsp
