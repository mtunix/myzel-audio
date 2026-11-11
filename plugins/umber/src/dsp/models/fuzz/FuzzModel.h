#pragma once

#include "dsp/common/BiasRamp.h"
#include "dsp/common/CircuitModel.h"
#include "dsp/common/tubes/KorenPentode.h"
#include "dsp/common/tubes/KorenTriode.h"
#include "dsp/common/tubes/NodalCircuit.h"

#include <algorithm>
#include <array>

namespace umber::dsp
{

namespace fuzz
{
    /** The circuit drawn in FuzzModel.h. Component values and their reasons: fuzz/NOTES.md. */
    constexpr tubes::Netlist makeNetlist()
    {
        tubes::Netlist n;

        const int in       = n.node();
        const int leak1    = n.node();
        const int grid1    = n.node();
        const int cathode1 = n.node();
        const int plate1   = n.node();
        const int leak2    = n.node();
        const int grid2    = n.node();
        const int cathode2 = n.node();
        const int screen   = n.node();
        const int bplus    = n.node();
        const int plate2   = n.node();
        const int winding  = n.node();
        const int biasRet  = n.node();

        n.input = { in, 0 };
        n.supply (bplus, 0, 285.0);

        // screen / preamp supply node: drops and sags with the screen current
        n.resistor  (bplus, screen, 1.0e3);
        n.capacitor (screen, 0, 22.0e-6);

        // driver: input coupling, grid leak, grid stopper, plate load from the screen node
        n.capacitor (in, leak1, 22.0e-9);
        n.resistor  (leak1, 0, 1.0e6);
        n.resistor  (leak1, grid1, 33.0e3);
        n.resistor  (screen, plate1, 100.0e3);

        // driver cathode: partially bypassed (full gain only above ~30 Hz) for a tight low end
        n.resistor  (cathode1, 0, 1.5e3);
        n.capacitor (cathode1, 0, 6.8e-6);

        // interstage coupling, EL84 grid leak and grid stopper
        n.capacitor (plate1, leak2, 47.0e-9);
        n.resistor  (leak2, biasRet, 220.0e3);   // returns to the bias supply (0 V nominally)
        n.biasSupply (biasRet, 0);
        n.resistor  (leak2, grid2, 10.0e3);

        // EL84 cathode bias, bypassed, a little cold
        n.resistor  (cathode2, 0, 180.0);
        n.capacitor (cathode2, 0, 220.0e-6);

        // output transformer primary: winding resistance + magnetising inductance (DC path),
        // reflected load and winding capacitance across it
        n.resistor  (bplus, winding, 250.0);
        n.inductor  (winding, plate2, 40.0);
        n.resistor  (bplus, plate2, 5.0e3);
        n.capacitor (bplus, plate2, 1.0e-9);
        n.output = { plate2, bplus };

        // tube ports
        n.port (grid1, cathode1);
        n.port (plate1, cathode1);
        n.port (grid2, cathode2);
        n.port (screen, cathode2);
        n.port (plate2, cathode2);
        return n;
    }
} // namespace fuzz

/**
    Fuzz: a 12AX7 driver into a single-ended EL84 output stage with its output transformer,
    slammed. Solved at circuit level every sample, both tubes coupled in one Newton solve.

    @code
                                           B+ 285 V
                                              |
                      +------- Rscr 1k -------+-----------------------+
                      |                       |                       |
               screen node -- Cscr 22u        |      OT primary:      |
               (= EL84 g2)                    |   Rw 250 + Lp 40H     |
                      |                       |   || RL' 5k || Cw 1n  |
                   Ra 100k                    |                       |
                      |       Cc 47n          +-----------+-----------+
                      +--------||-----+                   | plate2
                      | plate1        |                   |
    in -||-+- Rs1 33k-+ grid1         +-- Rs2 10k --- g1 EL84 (Koren pentode + division)
    Cin 22n|        12AX7           Rg2 220k              | cathode2
         Rg1 1M       | cathode1      |            Rk2 180 || Ck2 220u
           |    Rk1 1.5k || Ck1 6.8u  gnd                 |
          gnd         |                                  gnd
                     gnd
    @endcode
    Output = the primary voltage (i.e. what the secondary delivers).

    What makes it a fuzz, all emergent from the circuit:
      - the EL84 is driven well past both cutoff and plate saturation, and the pentode's flat
        plate curves square the wave off on both sides: odd-rich, with the triode driver's
        2nd harmonic and the asymmetric bias giving the ragged, uneven edge,
      - interstage grid conduction charges Cc, so the EL84 bias is pushed towards cutoff
        (bias starvation): on heavy material only the tops of the wave get through and decays
        break up into a gated sputter while Cc discharges through Rg2,
      - screen current rises steeply when the plate bottoms out (current division), sagging
        the screen supply: compression and rasp,
      - the output transformer's primary inductance and winding capacitance band-limit the
        result (tight lows, softened fizz), and its flyback adds bite on cutoff edges,
      - Ck1 only bypasses the driver's cathode above ~30 Hz, which keeps the low end tight.

    Input samples become volts via kInputVolts (source swing soft-limited as in ValveModel); the
    make-up gain from the linearised circuit at 1 kHz restores unity small-signal gain. Values,
    the EL84 fit, validation and measurements: fuzz/NOTES.md.
*/
class FuzzModel final : public CircuitModel
{
public:
    /** Volts at the input for an input sample of 1.0. */
    static constexpr double kInputVolts = 0.12;

    /** Swing limit of the driving source (volts): linear below the knee, tanh shoulder above. */
    static constexpr double kSourceKneeVolts = 10.0;
    static constexpr double kSourceMaxVolts  = 20.0;

    void prepare (double oversampledRate) override;
    void reset() noexcept override;
    void process (float* samples, int numSamples) noexcept override;

    /** Bias: a fixed-bias supply on the EL84's grid leak, +-kBiasRangeVolts at the extremes. */
    void setBias (double amount) noexcept override { bias.setTarget (std::clamp (amount, -1.0, 1.0) * kBiasRangeVolts); }
    static constexpr double kBiasRangeVolts = 6.5;

    /** Diagnostics for tests. Port order: Vgk1, Vpk1, Vg1k2, Vg2k2, Vpk2. */
    int lastIterations() const noexcept { return circuit.lastIterations(); }
    double portVolts (int port) const noexcept { return circuit.portVoltages()[(size_t) port]; }
    double quiescentPortVolts (int port) const noexcept { return circuit.operatingPointVoltages()[(size_t) port]; }

    /** The solver skips the netlist's structural zeros (Tubes::structure); true when the
        matrices built at this rate have no non-zero outside that pattern. */
    bool solverStructureHolds() const noexcept { return circuit.structureHolds(); }

private:
    struct Tubes
    {
        tubes::KorenTriode driver;
        tubes::KorenPentode output;

        /** Structural zeros of the netlist's matrices, skipped in the per-sample products. */
        static constexpr auto structure = tubes::analyseStructure<7, 5> (fuzz::makeNetlist());

        /** Non-zero pattern of dI/dV: two independent tubes, grid currents depend on Vgk only. */
        static constexpr std::array<std::array<bool, 5>, 5> jacobianPattern { { { true,  false, false, false, false },
                                                                                   { true,  true,  false, false, false },
                                                                                   { false, false, true,  false, false },
                                                                                   { false, false, true,  true,  true  },
                                                                                   { false, false, true,  true,  true  } } };

        void operator() (const std::array<double, 5>& v, std::array<double, 5>& i,
                         std::array<std::array<double, 5>, 5>& j) const noexcept
        {
            const auto t = driver.evaluate (v[0], v[1]);
            const auto p = output.evaluate (v[2], v[3], v[4]);

            i = { t.ig, t.ip, p.ig1, p.ig2, p.ip };
            j = { { { t.dIgDvgk, 0.0,       0.0,        0.0,        0.0        },
                    { t.dIpDvgk, t.dIpDvpk, 0.0,        0.0,        0.0        },
                    { 0.0,       0.0,       p.dIg1[0],  0.0,        0.0        },
                    { 0.0,       0.0,       p.dIg2[0],  p.dIg2[1],  p.dIg2[2]  },
                    { 0.0,       0.0,       p.dIp[0],   p.dIp[1],   p.dIp[2]   } } };
        }

        void limit (const std::array<double, 5>& previous, std::array<double, 5>& next) const noexcept
        {
            next[1] = tubes::KorenTriode::limitPlate (previous[1], next[1]);
            next[4] = tubes::KorenPentode::limitPlate (previous[4], next[4]);
        }
    };

    tubes::NodalCircuit<7, 5, Tubes> circuit;
    double makeup = 1.0;
    BiasRamp bias;   // volts on the bias supply; kept across prepare()
};

} // namespace umber::dsp
