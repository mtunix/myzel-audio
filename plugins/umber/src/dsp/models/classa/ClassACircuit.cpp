#include "dsp/models/classa/ClassACircuit.h"

#include <algorithm>
#include <numbers>

namespace umber::dsp::classa
{

void ClassACircuit::stamp (Solver::Netlist& n, const Components& c)
{
    constexpr int gnd = Solver::ground;
    constexpr int rail = -2; // Q2's collector sits on the ideal supply

    // input: source EMF + impedance, coupling cap, divider bias, base spreading resistance
    n.source (nodeA, gnd, c.rs);
    n.capacitor (nodeA, nodeB, c.cin, stateCin);
    n.resistorToSupply (nodeB, c.r1);
    n.resistor (nodeB, gnd, c.r2);
    n.resistor (nodeB, nodeBi, c.rbb);

    // Q1: degenerated common emitter, bypassed bias resistor, collector load, Miller cap
    n.npn (nodeBi, nodeC1, nodeE1, c.betaF1, c.betaR1, portQ1Be, portQ1Bc);
    n.resistor (nodeE1, nodeE2, c.re1);
    n.resistor (nodeE2, gnd, c.re2);
    n.capacitor (nodeE2, gnd, c.ce, stateCe);
    n.biasCurrentInto (nodeE2);  // bias trim (0 A nominally)
    n.resistorToSupply (nodeC1, c.rc);
    n.capacitor (nodeC1, nodeB, c.ccb, stateCcb);

    // Q2: class-A emitter follower with resistive pull-down
    n.npn (nodeC1, rail, nodeE3, c.betaF2, 0.0, portQ2Be, -1);
    n.resistor (nodeE3, gnd, c.re3);

    // output transformer: coupling cap, winding resistance, magnetising inductance || load
    n.capacitor (nodeE3, nodeT0, c.cout, stateCout);
    n.resistor (nodeT0, nodeT, c.rw);
    n.inductor (nodeT, gnd, c.lm, stateLm, portCore);
    n.inject (portCore, nodeT, -1.0);
    n.resistor (nodeT, gnd, c.rl);

    n.output[nodeT] = 1.0;
}

void ClassACircuit::prepare (double newSampleRate, const Components& c)
{
    using PortModel = Solver::PortModel;
    std::array<PortModel, numPorts> ports;
    ports[portQ1Be] = { PortModel::junction, c.is1, 0.0, 0.0, 0.0 };
    ports[portQ1Bc] = { PortModel::junction, c.is1, 0.0, 0.0, 0.0 };
    ports[portQ2Be] = { PortModel::junction, c.is2, 0.0, 0.0, 0.0 };
    ports[portCore] = { PortModel::core, 1.0, 1.0 / c.lm, 1.0 / c.phiSat, c.coreKnee * c.coreKnee };

    biasTrimAmps = c.biasTrimAmps;
    sampleRate = newSampleRate;
    solver.setBiasRange (c.biasTrimAmps);
    solver.prepare (sampleRate, c.vcc, c.vt, ports, [&c] (Solver::Netlist& n) { stamp (n, c); });

    // The fine solver only ever runs from states handed over by the main one, so it needs no
    // operating-point grid of its own.
    fine.setBiasRange (0.0);
    fine.prepare (sampleRate * substeps, c.vcc, c.vt, ports, [&c] (Solver::Netlist& n) { stamp (n, c); });
    switchScale = solver.switchScale();   // one switching criterion for both

    // Cubic (Lagrange) interpolation of the input at the sub-steps, through the inputs at
    // n-3, n-2, n-1 and n (nodes -2, -1, 0, 1 relative to the start of the period).
    for (int j = 0; j < substeps; ++j)
    {
        const auto t = (double) (j + 1) / substeps;
        interpolation[(size_t) j] = { -(t + 1.0) * t * (t - 1.0) / 6.0,
                                       (t + 2.0) * t * (t - 1.0) / 2.0,
                                      -(t + 2.0) * (t + 1.0) * (t - 1.0) / 2.0,
                                       (t + 2.0) * (t + 1.0) * t / 6.0 };
    }

    // Quadratic through t = -1 - h, -1 and 0 (h = 1 / substeps), integrated over [-1, 0]
    constexpr auto h = 1.0 / substeps;
    afterWeights[0] = -1.0 / (6.0 * h * (1.0 + h));
    afterWeights[2] = (1.0 / 3.0 + 0.5 * h) / (1.0 + h);
    afterWeights[1] = 1.0 - afterWeights[0] - afterWeights[2];

    reset();
}

void ClassACircuit::reset() noexcept
{
    solver.reset();
    fine.reset();
    average = y1 = y2 = a1 = a2 = output = solver.restOutput();
    vin1 = vin2 = vin3 = 0.0;
    mask = solver.conductingMask (0u, switchRatio, switchRatio, switchScale);
    yLastSub = 0.0;
    eventPending = afterEvent = lastWasFine = false;
    iterations = solves = 0;
}

double ClassACircuit::process (double vin) noexcept
{
    double y = 0.0;

    if (eventPending)
    {
        y = fineSteps (vin);
    }
    else
    {
        solver.save (before);
        y = solver.process (vin);

        if (solver.conductingMask (mask, switchOn, switchOff, switchScale) == mask)
        {
            // Smooth period: integrate the quadratic through the last three samples over it, or,
            // right after a switching event, through the last sub-step before this period instead
            // of the sample before the bend.
            average = afterEvent ? afterWeights[0] * yLastSub + afterWeights[1] * y1 + afterWeights[2] * y
                                 : (5.0 * y + 8.0 * y1 - y2) * (1.0 / 12.0);
            afterEvent = false;
            iterations = solver.lastIterations();
            solves = 1;
            lastWasFine = false;
        }
        else
        {
            // A junction switched inside this period: redo it in sub-steps.
            const auto first = solver.lastIterations();
            solver.restore (before);
            y = fineSteps (vin);
            iterations = std::max (iterations, first);
            solves += 1;
        }
    }

    output = eq0 * average + eq1 * a1 + eq2 * a2;
    a2 = a1;
    a1 = average;

    y2 = y1;
    y1 = y;
    vin3 = vin2;
    vin2 = vin1;
    vin1 = vin;
    return y;
}

double ClassACircuit::fineSteps (double vin) noexcept
{
    fine.transferFrom (solver);

    auto sum = 0.5 * y1;
    double y = 0.0;
    int lastSwitch = 0;   // sub-step in which a junction last switched (1-based)
    iterations = 0;

    for (int j = 0; j < substeps; ++j)
    {
        const auto& w = interpolation[(size_t) j];
        const auto u = j == substeps - 1 ? vin : w[0] * vin3 + w[1] * vin2 + w[2] * vin1 + w[3] * vin;
        if (j == substeps - 1)
            yLastSub = y;
        y = fine.process (u);
        sum += j == substeps - 1 ? 0.5 * y : y;
        iterations = std::max (iterations, fine.lastIterations());

        const auto now = fine.conductingMask (mask, switchOn, switchOff, switchScale);
        if (now != mask)
            lastSwitch = j + 1;
        mask = now;
    }

    solver.transferFrom (fine);
    average = sum * (1.0 / substeps);
    solves = substeps;
    lastWasFine = true;

    // The next period's averaging rule must not reach back across the bend: if a junction
    // switched in the last sub-step, sub-step the next period too; otherwise its rule starts
    // from the last sub-step of this one.
    eventPending = lastSwitch == substeps;
    afterEvent = lastSwitch > 0 && ! eventPending;
    return y;
}

std::complex<double> ClassACircuit::outputResponse (double freqHz) const noexcept
{
    const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * freqHz / sampleRate);
    return solver.smallSignalResponse (freqHz) * (5.0 + 8.0 * z1 - z1 * z1) / 12.0
         * (eq0 + eq1 * z1 + eq2 * z1 * z1);
}

} // namespace umber::dsp::classa
