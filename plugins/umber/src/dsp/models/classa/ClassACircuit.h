#pragma once

#include "dsp/models/classa/DkSolver.h"

#include <array>
#include <complex>

namespace umber::dsp::classa
{

/**
    Component values of the Class A circuit (SI units). See NOTES.md for the schematic.

    The defaults are the voiced circuit; the struct exists so validation tools can probe
    variations without touching the solver.
*/
struct Components
{
    double vcc    = 24.0;      // single supply rail

    // Input: line source (impedance rs) AC-coupled into the base, divider bias
    double rs     = 600.0;
    double cin    = 4.7e-6;
    double r1     = 47.0e3;    // Vcc -> base
    double r2     = 6.8e3;     // base -> ground
    double rbb    = 120.0;     // base spreading resistance (intrinsic base node)

    // Q1: common-emitter voltage amplifier
    double re1    = 22.0;      // unbypassed emitter degeneration
    double re2    = 1.0e3;     // bias resistor, bypassed by ce
    double ce     = 1000.0e-6;
    double rc     = 5.6e3;     // collector load
    double ccb    = 33.0e-12;  // collector-base (Miller) compensation incl. Cμ

    // Q2: class-A emitter follower, resistive pull-down, cap-coupled to the output transformer
    double re3    = 1.5e3;
    double cout   = 100.0e-6;
    double rw     = 40.0;      // primary winding resistance
    double lm     = 4.0;       // magnetising inductance (small-signal)
    double rl     = 600.0;     // reflected load across the primary
    double phiSat = 0.016;     // core flux (V·s) scale of saturation
    double coreKnee = 0.3;     // fraction of phiSat below which the core is linear

    // Ebers–Moll parameters
    double is1 = 1.0e-14, betaF1 = 250.0, betaR1 = 4.0;   // small-signal NPN
    double is2 = 4.0e-14, betaF2 = 120.0;                  // medium-power NPN (forward-active only)
    double vt  = 0.025852;                                 // thermal voltage at 300 K

    // Bias trim: a DC current drawn from Q1's emitter node (a trim pot through a large resistor
    // to the rails, in parallel with Re2), +- this at the Bias extremes. Drawing current lowers
    // the emitter voltage, so Q1 runs hotter. Ce bypasses the node, so the operating point
    // glides (tens of ms) instead of stepping into the audio path when the trim moves.
    double biasTrimAmps = 600.0e-6;
};

/**
    The Class A netlist on top of the generic DK solver.

        port 0: Q1 base-emitter junction  (forward Ebers–Moll diode)
        port 1: Q1 base-collector junction (reverse Ebers–Moll diode, saturation)
        port 2: Q2 base-emitter junction  (collector on the rail: never saturates)
        port 3: output-transformer core flux (excess magnetising current)

    States: Cin, Ccb, Ce, Cout, Lm.  Output: the transformer primary voltage, in volts.

    Switching events and the sampled output (see NOTES.md, "Aliasing"): a transistor that cuts
    off, turns on or saturates bends the waveform within a fraction of a microsecond, so the
    circuit's output really contains harmonics far above any sample rate. Sample periods in
    which a junction switches are therefore solved again in `substeps` sub-steps
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

    void prepare (double sampleRate, const Components& components);
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

    static void stamp (Solver::Netlist& n, const Components& c);

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

} // namespace umber::dsp::classa
