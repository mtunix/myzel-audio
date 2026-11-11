#include "dsp/models/valve/ValveModel.h"

#include <cmath>

namespace umber::dsp
{

namespace
{
    /** The circuit drawn in ValveModel.h. Component values and their reasons: valve/NOTES.md. */
    tubes::Netlist makeNetlist()
    {
        tubes::Netlist n;

        const int in      = n.node();
        const int leak    = n.node();
        const int grid    = n.node();
        const int cathode = n.node();
        const int plate   = n.node();
        const int bplus   = n.node();
        const int supply  = n.node();
        const int out     = n.node();
        const int biasRet = n.node();

        n.input = { in, 0 };
        n.supply (supply, 0, 262.0);

        // input coupling, grid leak, grid stopper
        n.capacitor (in, leak, 100.0e-9);
        n.resistor  (leak, biasRet, 220.0e3);   // grid leak returns to the bias supply (0 V nominally)
        n.biasSupply (biasRet, 0);
        n.resistor  (leak, grid, 47.0e3);

        // supply decoupling: sags with the stage's average current
        n.resistor  (supply, bplus, 10.0e3);
        n.capacitor (bplus, 0, 22.0e-6);

        // plate load, Miller (grid-plate) capacitance, plate stray + next-stage input capacitance
        n.resistor  (bplus, plate, 100.0e3);
        n.capacitor (grid, plate, 1.7e-12);
        n.capacitor (plate, 0, 10.0e-12);

        // cathode bias, bypassed
        n.resistor  (cathode, 0, 1.5e3);
        n.capacitor (cathode, 0, 47.0e-6);

        // output coupling into the next stage's grid leak
        n.capacitor (plate, out, 47.0e-9);
        n.resistor  (out, 0, 1.0e6);
        n.output = { out, 0 };

        // tube ports: grid-cathode (grid current), plate-cathode (plate current)
        n.port (grid, cathode);
        n.port (plate, cathode);
        return n;
    }
} // namespace

void ValveModel::prepare (double oversampledRate)
{
    // Newton step tolerances (grid, plate). The accepted solution's error is second order in
    // the last step: < -120 dB against a fully converged solve (see NOTES.md).
    circuit.tolerance = { 1.0e-3, 1.0e-2 };
    circuit.maxIterations = 12;
    circuit.setBiasRange (kBiasRangeVolts);
    circuit.prepare (makeNetlist(), oversampledRate);

    const auto h = circuit.smallSignalGain (1000.0, oversampledRate);
    const double sign = h.real() < 0.0 ? -1.0 : 1.0;
    makeup = sign / (std::abs (h) * kInputVolts);
    reset();
}

void ValveModel::reset() noexcept
{
    bias.snap();
    circuit.setBiasVolts (bias.current);
    circuit.reset();
}

void ValveModel::process (float* samples, int numSamples) noexcept
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
