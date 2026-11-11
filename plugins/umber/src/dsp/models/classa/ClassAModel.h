#pragma once

#include "dsp/common/BiasRamp.h"
#include "dsp/common/CircuitModel.h"
#include "dsp/models/classa/ClassACircuit.h"

#include <algorithm>

namespace umber::dsp
{

/**
    Class A: single-ended discrete transistor console stage.

    A degenerated common-emitter BJT voltage amplifier (bypassed bias resistor, coupling caps)
    DC-coupled to a class-A emitter follower that drives an output transformer through a
    coupling cap. Solved per sample at circuit level (Ebers–Moll transistors, nonlinear
    transformer core, nodal DK method with Newton–Raphson) — see classa/NOTES.md.

    Character: H2-dominant soft onset from the transistor's exponential, asymmetric clipping
    (follower cut-off against Q1 saturation/cut-off) bringing in a strong H3, bias shift and
    compression on sustained level as the coupling and emitter caps charge, and mild LF core
    saturation in the output transformer.
*/
class ClassAModel final : public CircuitModel
{
public:
    ClassAModel() = default;

    /** For validation tools: a component variation and a different input calibration. */
    ClassAModel (const classa::Components& c, double inputVoltsPerUnit);

    void prepare (double oversampledRate) override;
    void reset() noexcept override;
    void process (float* samples, int numSamples) noexcept override;
    void setBias (double amount) noexcept override { bias.setTarget (std::clamp (amount, -1.0, 1.0)); }

    const classa::ClassACircuit& getCircuit() const noexcept { return circuit; }
    double getMakeupGain() const noexcept { return makeup; }

    /** Source EMF in volts for a full-scale (1.0) input sample. */
    static constexpr double defaultInputVolts = 0.035;

    /** Peak swing of the driving line stage; the source soft-limits towards it. */
    static constexpr double sourceRailVolts = 12.0;

private:
    classa::Components components;
    classa::ClassACircuit circuit;
    double inputVolts = defaultInputVolts;
    double makeup = 1.0;
    BiasRamp bias;   // kept across prepare()
};

} // namespace umber::dsp
