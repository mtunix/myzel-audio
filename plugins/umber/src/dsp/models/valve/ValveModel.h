#pragma once

#include "common/dsp/TriodeStage.h"
#include "dsp/common/BiasRamp.h"
#include "dsp/common/CircuitModel.h"

#include <algorithm>

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

    The stage is the shared common-cathode triode (myzel::dsp::tubes::TriodeStage) with the
    values above: Koren's triode model plus a grid-cathode conduction law, the circuit as a
    discrete nonlinear state-space system (the nodal DK method, equivalent to a WDF with an
    R-type root) and the two tube ports (Vgk, Vpk) solved with Newton-Raphson every sample.
    Everything the stage is known for falls out of the circuit:

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

    /** The voiced circuit drawn above (values and reasons: valve/NOTES.md). */
    static myzel::dsp::tubes::TriodeStageComponents standardComponents();

    /** Diagnostics for tests: Newton evaluations used by the last sample, and port voltages. */
    int lastIterations() const noexcept { return stage.lastIterations(); }
    double gridCathodeVolts() const noexcept { return stage.gridCathodeVolts(); }
    double quiescentGridCathodeVolts() const noexcept { return stage.quiescentGridCathodeVolts(); }
    double quiescentPlateCathodeVolts() const noexcept { return stage.quiescentPlateCathodeVolts(); }

private:
    myzel::dsp::tubes::TriodeStage stage;
    double makeup = 1.0;
    BiasRamp bias;   // volts on the bias supply; kept across prepare()
};

} // namespace umber::dsp
