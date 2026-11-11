#pragma once

#include <algorithm>

#include "dsp/common/CircuitModel.h"
#include "dsp/common/BiasRamp.h"
#include "dsp/models/desk/LineAmp.h"
#include "dsp/models/desk/TransformerStage.h"

namespace umber::dsp
{

/**
    The complete Desk circuit: input transformer -> line amplifier -> output transformer.
    Component values and core materials live in DeskModel.cpp (see NOTES.md).
*/
struct DeskCircuit
{
    double voltsPerUnit;              // input sample 1.0 (0 dBFS) -> source EMF peak volts
    desk::TransformerParams input;    // 10k:10k bridging input transformer, high-nickel core
    desk::LineAmpParams amp;
    desk::TransformerParams output;   // 600:600 output transformer, 50 % nickel / steel core

    static DeskCircuit standard();
};

/**
    Desk: transformer-coupled console line amplifier; low frequencies saturate the iron first.

    Each transformer is a wave digital filter whose root is a nonlinear magnetising inductance
    with Jiles–Atherton hysteresis (see TransformerStage). Because flux is the integral of
    voltage, the cores saturate first at low frequencies; their hysteresis adds odd harmonics and
    a level-dependent low-frequency phase shift. The line amplifier between them provides the
    1 kHz saturation onset, a touch of asymmetry near its rails and slew limiting at extremes.
*/
class DeskModel final : public CircuitModel
{
public:
    DeskModel();
    explicit DeskModel (const DeskCircuit& circuit);

    void prepare (double oversampledRate) override;
    void reset() noexcept override;
    void process (float* samples, int numSamples) noexcept override;

    /** Bias: the line amp's input offset trim, +-biasRangeVolts at the extremes. */
    void setBias (double amount) noexcept override { bias.setTarget (std::clamp (amount, -1.0, 1.0) * biasRangeVolts); }
    static constexpr double biasRangeVolts = 0.6;

    /** Small-signal 1 kHz gain from input samples to output volts (used for make-up). */
    static double smallSignalGain (const DeskCircuit& circuit, double frequency);

    const desk::TransformerStage& inputTransformer() const noexcept { return inputStage; }
    const desk::TransformerStage& outputTransformer() const noexcept { return outputStage; }

private:
    DeskCircuit circuit;
    desk::TransformerStage inputStage, outputStage;
    desk::LineAmp amp;
    double makeup = 1.0;
    BiasRamp bias;   // volts of input offset; kept across prepare()
};

} // namespace umber::dsp
