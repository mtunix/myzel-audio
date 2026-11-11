#pragma once

#include "dsp/common/BiasRamp.h"
#include "dsp/common/CircuitModel.h"
#include "dsp/common/tubes/KorenTriode.h"
#include "dsp/common/tubes/NodalCircuit.h"

#include <algorithm>
#include <array>

namespace umber::dsp
{

/**
    Valve: one 12AX7 common-cathode preamp stage, solved at circuit level every sample.

    @code
                                      B+ supply 262 V
                                            |
                                        Rsup 10k
                                            +------ Csup 22u --- gnd   (supply sag)
                                         Ra 100k
                                            |                Cout 47n
                          +---- Cgp 1.7p ---+------------+-----||-----+---- out
                          |                 | plate      |            |
    in --||--+-- Rs 47k --+ grid         12AX7         Cs 10p       RL 1M  (next stage)
     Cin 100n|                         (Koren)          |            |
            Rg 220k                         | cathode  gnd          gnd
              |                             |
             gnd                    Rk 1.5k || Ck 47u
                                            |
                                           gnd
    @endcode

    The triode is Koren's model plus a grid-cathode conduction law (tubes::KorenTriode). The
    circuit becomes a discrete nonlinear state-space system (tubes::NodalCircuit: the DK method,
    equivalent to a WDF with an R-type root) and the two tube ports (Vgk, Vpk) are solved with
    Newton-Raphson every sample. Everything the stage is known for falls out of the circuit:

      - 2nd-harmonic-dominant curvature of the triode at moderate level,
      - grid conduction on positive peaks charges Cin through Rs, so the bias walks negative
        ("blocking"): soft squash and bloom on hits, recovering over Cin * Rg (~22 ms),
      - the cathode bias (Rk || Ck) and the supply (Rsup / Csup) follow the programme:
        compression and sag with release times of ~40 ms and ~0.2 s,
      - Miller capacitance Cgp * (1 + gain) against Rs rolls off the top, less so when the stage
        saturates and its gain collapses,
      - asymmetric clipping (cutoff vs. grid-conduction clamp) at heavy drive.

    Input samples become source volts via kInputVolts. The driving source has a finite swing
    (soft-limited above kSourceKneeVolts), as any real preceding stage does; it only acts far
    beyond the +18 dBFS contract level and keeps the bias shift after abuse bounded. The make-up
    gain, computed from the linearised circuit at 1 kHz in prepare(), restores unity
    small-signal gain and undoes the stage's polarity inversion. Values, validation against an
    independent reference simulation, and measurements: valve/NOTES.md.
*/
class ValveModel final : public CircuitModel
{
public:
    /** Volts at the input for an input sample of 1.0 (sets where the stage starts to bite). */
    static constexpr double kInputVolts = 0.6;

    /** Swing limit of the driving source (volts): linear below the knee, tanh shoulder above. */
    static constexpr double kSourceKneeVolts = 10.0;
    static constexpr double kSourceMaxVolts  = 20.0;

    void prepare (double oversampledRate) override;
    void reset() noexcept override;
    void process (float* samples, int numSamples) noexcept override;

    /** Bias: a supply on the grid leak's return, +-kBiasRangeVolts at the extremes. */
    void setBias (double amount) noexcept override { bias.setTarget (std::clamp (amount, -1.0, 1.0) * kBiasRangeVolts); }
    static constexpr double kBiasRangeVolts = 1.3;

    /** Diagnostics for tests: Newton evaluations used by the last sample, and port voltages. */
    int lastIterations() const noexcept { return circuit.lastIterations(); }
    double gridCathodeVolts() const noexcept { return circuit.portVoltages()[0]; }
    double quiescentGridCathodeVolts() const noexcept { return circuit.operatingPointVoltages()[0]; }
    double quiescentPlateCathodeVolts() const noexcept { return circuit.operatingPointVoltages()[1]; }

private:
    struct Triode
    {
        tubes::KorenTriode tube;

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
            next[1] = tubes::KorenTriode::limitPlate (previous[1], next[1]);
        }
    };

    tubes::NodalCircuit<6, 2, Triode> circuit;
    double makeup = 1.0;
    BiasRamp bias;   // volts on the bias supply; kept across prepare()
};

} // namespace umber::dsp
