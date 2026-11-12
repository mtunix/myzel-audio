#pragma once

#include "common/dsp/KorenTriode.h"
#include "common/dsp/NodalCircuit.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <iterator>
#include <limits>

namespace myzel::dsp::tubes
{

/**
    Component values of a common-cathode triode stage (SI units). Every value must be set: the
    voicing belongs to the plugin (Umber's Valve: ValveModel::standardComponents()). A value
    left out stays NaN, which prepare() asserts against and the output would show.

    @code
                                  B+ supplyVolts
                                        |
                                  supplyResistor
                                        +------ supplyCap --- gnd   (supply sag)
                                    plateLoad
                                        |                    outputCap
                    +-- gridPlateCap ---+----------+------------||----+---- out
                    |                   | plate    |                  |
    in --||--+-- gridStopper --+ grid  triode    plateCap            load  (next stage)
     inputCap|                          |          |                  |
          gridLeak                      | cathode gnd                gnd
             |                          |
        bias supply           cathodeResistor || cathodeCap
        (0 V nominal)                   |
             |                         gnd
            gnd
    @endcode
*/
struct TriodeStageComponents
{
    static constexpr double unset = std::numeric_limits<double>::quiet_NaN();

    double supplyVolts = unset, supplyResistor = unset, supplyCap = unset;
    double inputCap = unset, gridLeak = unset, gridStopper = unset;
    double plateLoad = unset, gridPlateCap = unset, plateCap = unset;
    double cathodeResistor = unset, cathodeCap = unset;
    double outputCap = unset, load = unset;
    KorenTriode tube;   // Koren's 12AX7 unless set

    /** True when every value is set (finite). */
    bool complete() const noexcept
    {
        const double values[] { supplyVolts, supplyResistor, supplyCap, inputCap, gridLeak, gridStopper,
                                plateLoad, gridPlateCap, plateCap, cathodeResistor, cathodeCap, outputCap, load };
        static_assert (sizeof (values) + sizeof (KorenTriode) == sizeof (TriodeStageComponents), "complete() lists every field");
        return std::all_of (std::begin (values), std::end (values), [] (double v) { return std::isfinite (v); });
    }
};

/**
    One common-cathode triode gain stage, solved at circuit level every sample: the netlist of
    TriodeStageComponents in a NodalCircuit (nodal DK method), Newton-Raphson on the two tube
    ports (Vgk, Vpk). Grid conduction and blocking, cathode and supply sag, the Miller
    capacitance and the asymmetric clip all come out of the circuit. The grid leak returns to
    an adjustable bias supply: 0 V nominally, and back at 0 the operating point is exactly the
    nominal one again.

    Input and output are volts: process() takes the source EMF and returns the output's swing
    around its quiescent value. The stage inverts. Method and validation against an independent
    reference: plugins/umber/src/dsp/models/valve/NOTES.md.
*/
class TriodeStage
{
public:
    /** Newton step tolerances (grid, plate), volts. The accepted solution's error is second
        order in the last step: < -120 dB against a fully converged solve (valve/NOTES.md). */
    std::array<double, 2> tolerance { 1.0e-3, 1.0e-2 };
    int maxIterations = 12;

    /** Bias supply range (volts, +-) its operating points are pre-solved over; before prepare(). */
    void setBiasRange (double maxVolts) noexcept { circuit.setBiasRange (maxVolts); }

    /** Builds the circuit and solves its operating point (not real-time). */
    void prepare (const TriodeStageComponents& c, double newSampleRate)
    {
        assert (c.complete());
        sampleRate = newSampleRate;
        circuit.tolerance = tolerance;
        circuit.maxIterations = maxIterations;
        circuit.device.tube = c.tube;
        circuit.device.tube.bindTables();   // c.tube.ex may have been set field by field
        circuit.prepare (netlist (c), sampleRate);
    }

    void reset() noexcept { circuit.reset(); }

    /** Bias supply in volts (0 = the nominal circuit, exactly); real-time safe. */
    void setBiasVolts (double volts) noexcept { circuit.setBiasVolts (volts); }

    /** One sample: source EMF in volts -> output volts around the quiescent point. */
    double process (double volts) noexcept { return circuit.process (volts); }

    /** Denormal insurance, once per block. */
    void flushTinyStates() noexcept { circuit.flushTinyStates(); }

    /** Small-signal gain of the linearised discrete circuit (complex; the stage inverts). */
    std::complex<double> smallSignalGain (double hz) const { return circuit.smallSignalGain (hz, sampleRate); }

    /** Diagnostics: Newton evaluations used by the last sample, and port voltages. */
    int lastIterations() const noexcept { return circuit.lastIterations(); }
    double gridCathodeVolts() const noexcept { return circuit.portVoltages()[0]; }
    double quiescentGridCathodeVolts() const noexcept { return circuit.operatingPointVoltages()[0]; }
    double quiescentPlateCathodeVolts() const noexcept { return circuit.operatingPointVoltages()[1]; }

    /** The circuit drawn at TriodeStageComponents. */
    static Netlist netlist (const TriodeStageComponents& c)
    {
        Netlist n;

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
        n.supply (supply, 0, c.supplyVolts);

        // input coupling, grid leak (returning to the bias supply), grid stopper
        n.capacitor (in, leak, c.inputCap);
        n.resistor  (leak, biasRet, c.gridLeak);
        n.biasSupply (biasRet, 0);
        n.resistor  (leak, grid, c.gridStopper);

        // supply decoupling: sags with the stage's average current
        n.resistor  (supply, bplus, c.supplyResistor);
        n.capacitor (bplus, 0, c.supplyCap);

        // plate load, Miller (grid-plate) capacitance, plate stray + next-stage input capacitance
        n.resistor  (bplus, plate, c.plateLoad);
        n.capacitor (grid, plate, c.gridPlateCap);
        n.capacitor (plate, 0, c.plateCap);

        // cathode bias, bypassed
        n.resistor  (cathode, 0, c.cathodeResistor);
        n.capacitor (cathode, 0, c.cathodeCap);

        // output coupling into the next stage's grid leak
        n.capacitor (plate, out, c.outputCap);
        n.resistor  (out, 0, c.load);
        n.output = { out, 0 };

        // tube ports: grid-cathode (grid current), plate-cathode (plate current)
        n.port (grid, cathode);
        n.port (plate, cathode);
        return n;
    }

private:
    struct Device
    {
        KorenTriode tube;

        /** Non-zero pattern of dI/dV: grid current depends on Vgk only. */
        static constexpr std::array<std::array<bool, 2>, 2> jacobianPattern { { { true, false }, { true, true } } };

        void operator() (const std::array<double, 2>& v, std::array<double, 2>& i,
                         std::array<std::array<double, 2>, 2>& j) const noexcept
        {
            const auto r = tube.evaluate (v[0], v[1]);
            i = { r.ig, r.ip };
            j = { { { r.dIgDvgk, 0.0 }, { r.dIpDvgk, r.dIpDvpk } } };
        }

        void limit (const std::array<double, 2>& previous, std::array<double, 2>& next) const noexcept
        {
            next[1] = KorenTriode::limitPlate (previous[1], next[1]);
        }
    };

    NodalCircuit<6, 2, Device> circuit;
    double sampleRate = 48000.0;
};

} // namespace myzel::dsp::tubes
