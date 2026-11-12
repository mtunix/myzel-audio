#include "dsp/models/valve/ValveModel.h"

#include <cmath>

namespace umber::dsp
{

myzel::dsp::tubes::TriodeStageComponents ValveModel::standardComponents()
{
    myzel::dsp::tubes::TriodeStageComponents c {};
    c.supplyVolts     = 262.0;
    c.supplyResistor  = 10.0e3;
    c.supplyCap       = 22.0e-6;
    c.inputCap        = 100.0e-9;
    c.gridLeak        = 220.0e3;
    c.gridStopper     = 47.0e3;
    c.plateLoad       = 100.0e3;
    c.gridPlateCap    = 1.7e-12;
    c.plateCap        = 10.0e-12;
    c.cathodeResistor = 1.5e3;
    c.cathodeCap      = 47.0e-6;
    c.outputCap       = 47.0e-9;
    c.load            = 1.0e6;
    return c;
}

void ValveModel::prepare (double oversampledRate)
{
    stage.setBiasRange (kBiasRangeVolts);
    stage.prepare (standardComponents(), oversampledRate);

    const auto h = stage.smallSignalGain (1000.0);
    const double sign = h.real() < 0.0 ? -1.0 : 1.0;
    makeup = sign / (std::abs (h) * kInputVolts);
    reset();
}

void ValveModel::reset() noexcept
{
    bias.snap();
    stage.setBiasVolts (bias.current);
    stage.reset();
}

void ValveModel::process (float* samples, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        if (bias.isMoving())
            stage.setBiasVolts (bias.next());

        const double u = myzel::dsp::tubes::softLimit (kInputVolts * (double) samples[n], kSourceKneeVolts, kSourceMaxVolts);
        samples[n] = (float) (makeup * stage.process (u));
    }

    stage.flushTinyStates();
}

} // namespace umber::dsp
