#pragma once

#include "common/dsp/DkSolver.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <iterator>
#include <limits>
#include <numbers>

namespace myzel::dsp
{

/**
    Component values of the class-A stage drawn in ClassACircuit (SI units). Every field must be
    set: the voicing belongs to the plugin (Umber's is ClassAModel::standardComponents(); the
    schematic and its validation are in plugins/umber/src/dsp/models/classa/NOTES.md). A field
    left out stays NaN, which prepare() asserts against and the output would show.
*/
struct ClassAComponents
{
    static constexpr double unset = std::numeric_limits<double>::quiet_NaN();

    double vcc = unset;        // single supply rail

    // Input: line source (impedance rs) AC-coupled into the base, divider bias
    double rs = unset;
    double cin = unset;
    double r1 = unset;         // Vcc -> base
    double r2 = unset;         // base -> ground
    double rbb = unset;        // base spreading resistance (intrinsic base node)

    // Q1: common-emitter voltage amplifier
    double re1 = unset;        // unbypassed emitter degeneration
    double re2 = unset;        // bias resistor, bypassed by ce
    double ce = unset;
    double rc = unset;         // collector load
    double ccb = unset;        // collector-base (Miller) compensation incl. Cμ

    // Q2: class-A emitter follower, resistive pull-down, cap-coupled to the output transformer
    double re3 = unset;
    double cout = unset;
    double rw = unset;         // primary winding resistance
    double lm = unset;         // magnetising inductance (small-signal)
    double rl = unset;         // reflected load across the primary
    double phiSat = unset;     // core flux (V·s) scale of saturation
    double coreKnee = unset;   // fraction of phiSat below which the core is linear

    // Ebers–Moll parameters: Q1 small-signal NPN, Q2 medium-power NPN (forward-active only)
    double is1 = unset, betaF1 = unset, betaR1 = unset;
    double is2 = unset, betaF2 = unset;
    double vt = unset;         // thermal voltage

    // Bias trim: a DC current drawn from Q1's emitter node (a trim pot through a large resistor
    // to the rails, in parallel with Re2), +- this at the Bias extremes. Drawing current lowers
    // the emitter voltage, so Q1 runs hotter. Ce bypasses the node, so the operating point
    // glides (tens of ms) instead of stepping into the audio path when the trim moves.
    double biasTrimAmps = unset;

    /** True when every value is set (finite). */
    bool complete() const noexcept
    {
        const double values[] { vcc, rs, cin, r1, r2, rbb, re1, re2, ce, rc, ccb, re3, cout, rw, lm, rl,
                                phiSat, coreKnee, is1, betaF1, betaR1, is2, betaF2, vt, biasTrimAmps };
        static_assert (sizeof (values) == sizeof (ClassAComponents), "complete() lists every field");
        return std::all_of (std::begin (values), std::end (values), [] (double v) { return std::isfinite (v); });
    }
};

/**
    A single-ended class-A transistor stage on top of the generic DK solver: a degenerated
    common-emitter amplifier (Q1, divider bias, bypassed emitter resistor, Miller cap)
    DC-coupled to a class-A emitter follower (Q2) that drives an output transformer with a
    saturating core through a coupling cap. Component values: ClassAComponents.

        port 0: Q1 base-emitter junction  (forward Ebers–Moll diode)
        port 1: Q1 base-collector junction (reverse Ebers–Moll diode, saturation)
        port 2: Q2 base-emitter junction  (collector on the rail: never saturates)
        port 3: output-transformer core flux (excess magnetising current)

    States: Cin, Ccb, Ce, Cout, Lm.  Output: the transformer primary voltage, in volts.

    Switching events and the sampled output (Umber's classa/NOTES.md, "Aliasing"): a transistor
    that cuts off, turns on or saturates bends the waveform within a fraction of a microsecond,
    so the circuit's output really contains harmonics far above any sample rate. Sample periods
    in which a junction switches are therefore solved again in `substeps` sub-steps
    by a second solver for the same netlist at the higher rate, and the output delivered to the
    model is the circuit's output averaged over each sample period (integrate-and-dump):
    trapezoidal over the sub-steps where the waveform bends, a third-order rule over the plain
    samples elsewhere. The average's spectrum has nulls at every multiple of the sample rate,
    where the strong high harmonics would otherwise fold back into the audio band. Averaging
    delays by half a sample and droops (sinc); a three-tap equaliser on the averages
    (lastOutput) takes the delay back to a quarter sample and keeps the response flat to second
    order, so the model's audio-band response and its phase against the dry signal stay close
    to the sampled circuit's.
*/
class ClassACircuit
{
public:
    enum Node { nodeA, nodeB, nodeBi, nodeE1, nodeE2, nodeC1, nodeE3, nodeT0, nodeT, numNodes };
    enum State { stateCin, stateCcb, stateCe, stateCout, stateLm, numStates };
    enum Port { portQ1Be, portQ1Bc, portQ2Be, portCore, numPorts };

    using Solver = DkSolver<numNodes, numStates, numPorts>;

    /** Sub-steps per sample period across a switching event. */
    static constexpr int substeps = 3;

    void prepare (double sampleRate, const ClassAComponents& components);
    void reset() noexcept;

    /**
        One sample. vin is the source EMF in volts; returns the primary voltage at the sampling
        instant (rest = 0). The value over the whole period is lastAverage().
    */
    double process (double vin) noexcept;

    /** The output averaged over the sample period just processed (half a sample later). */
    double lastAverage() const noexcept { return average; }

    /** The period averages, equalised (see class comment): what the model plays. */
    double lastOutput() const noexcept { return output; }

    /** Bias trim, -1 (cold) .. +1 (hot); 0 is the nominal circuit. */
    void setBias (double amount) noexcept
    {
        solver.setBiasCurrent (-amount * biasTrimAmps);
        fine.setBiasCurrent (-amount * biasTrimAmps);
    }

    /** Small-signal response of the sampled circuit (vin -> process()). */
    std::complex<double> smallSignalResponse (double freqHz) const noexcept { return solver.smallSignalResponse (freqHz); }

    /** Small-signal response of the equalised period averages (vin -> lastOutput()). */
    std::complex<double> outputResponse (double freqHz) const noexcept;

    const Solver::NodeVector& dcNodeVoltages() const noexcept { return solver.restNodeVoltages(); }
    Solver::NodeVector lastNodeVoltages() const noexcept     { return lastWasFine ? fine.lastNodeVoltages() : solver.lastNodeVoltages(); }
    bool dcConverged() const noexcept                         { return solver.dcConverged(); }

    /** Most Newton iterations any single solve of the last sample needed. */
    int lastIterations() const noexcept                      { return iterations; }

    /** Circuit solves the last sample took (1, or 1 + substeps across a switching event). */
    int lastSolves() const noexcept                          { return solves; }

private:
    /** Junction conductance relative to its surroundings that counts as "conducting", and the
        hysteresis band around it (a junction hovering at the threshold is not switching). */
    static constexpr double switchRatio = 0.25, switchOn = 2.0 * switchRatio, switchOff = 0.5 * switchRatio;

    double biasTrimAmps = 0.0, sampleRate = 48000.0;

    static void stamp (Solver::Netlist& n, const ClassAComponents& c);

    double fineSteps (double vin) noexcept;

    Solver solver, fine;   // the circuit at the sample rate and at substeps times it
    Solver::Snapshot before;
    Solver::PortVector switchScale {};
    std::array<std::array<double, 4>, substeps> interpolation {};

    std::array<double, 3> afterWeights {};
    double average = 0.0, y1 = 0.0, y2 = 0.0;        // average, last two samples
    double yLastSub = 0.0;                           // last sub-step before the last sample
    double output = 0.0, a1 = 0.0, a2 = 0.0;         // equalised output, last two averages

    /** Equaliser taps: with the smooth-period rule (5, 8, -1) / 12 they make a filter of unit
        gain, a quarter-sample delay and second moment 1/16 (flat to second order). */
    static constexpr double eq0 = 131.0 / 96.0, eq1 = -46.0 / 96.0, eq2 = 11.0 / 96.0;
    double vin1 = 0.0, vin2 = 0.0, vin3 = 0.0;       // last three inputs
    unsigned mask = 0;                               // conducting junctions at the last sample
    bool eventPending = false, afterEvent = false, lastWasFine = false;
    int iterations = 0, solves = 0;
};

inline void ClassACircuit::stamp (Solver::Netlist& n, const ClassAComponents& c)
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

inline void ClassACircuit::prepare (double newSampleRate, const ClassAComponents& c)
{
    assert (c.complete());

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

inline void ClassACircuit::reset() noexcept
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

inline double ClassACircuit::process (double vin) noexcept
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

inline double ClassACircuit::fineSteps (double vin) noexcept
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

inline std::complex<double> ClassACircuit::outputResponse (double freqHz) const noexcept
{
    const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * freqHz / sampleRate);
    return solver.smallSignalResponse (freqHz) * (5.0 + 8.0 * z1 - z1 * z1) / 12.0
         * (eq0 + eq1 * z1 + eq2 * z1 * z1);
}

} // namespace myzel::dsp
