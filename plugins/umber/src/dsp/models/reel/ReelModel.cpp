#include "dsp/models/reel/ReelModel.h"
#include "dsp/models/reel/ReelRecordAmp.h"

#include <algorithm>
#include <cmath>

namespace umber::dsp
{

ReelModel::ReelModel()
{
    prepare (sampleRate);
}

void ReelModel::prepare (double oversampledRate)
{
    sampleRate = oversampledRate;
    amp = &reel::RecordAmpCurve::instance(); // built once, shared by all instances

    coupling.setG (reel::prewarpedG (couplingHz, sampleRate));
    preEmphasis.design (emphasisZeroHz, emphasisPoleHz, sampleRate);
    deEmphasis.designInverse (preEmphasis);
    rolloff.setG (reel::prewarpedG (rolloffHz, sampleRate), rolloffQ);
    bump.setG (reel::prewarpedG (bumpHz, sampleRate), bumpQ);
    bumpGain = (std::pow (10.0, bumpDb / 20.0) - 1.0) * bump.damping();
    gapLoss.setG (reel::matchedG (gapLossHz, gapLossMatchHz, sampleRate));

    // analytic small-signal gain at 1 kHz -> make-up gain for exact unity
    constexpr double f = 1000.0;
    const auto linear = coupling.highpassResponse (f, sampleRate)
                      * preEmphasis.response (f, sampleRate) * deEmphasis.response (f, sampleRate)
                      * rolloff.highpassResponse (f, sampleRate)
                      * (1.0 + bumpGain * bump.bandpassResponse (f, sampleRate))
                      * gapLoss.lowpassResponse (f, sampleRate);

    const auto smallSignal = inputVolts * std::abs (amp->smallSignalGain()) * fieldPerVolt
                           * tape.initialSusceptibility() * std::abs (linear);
    outputGain = 1.0 / smallSignal;

    reset();
}

void ReelModel::reset() noexcept
{
    bias.snap();
    applyBiasVolts (bias.current);
    coupling.reset();
    preEmphasis.reset();
    deEmphasis.reset();
    tape.reset();
    rolloff.reset();
    bump.reset();
    gapLoss.reset();
}

void ReelModel::setBias (double amount) noexcept
{
    bias.setTarget (std::clamp (amount, -1.0, 1.0) * biasRangeVolts);
}

void ReelModel::applyBiasVolts (double volts) noexcept
{
    // The grid sits `volts` away from its nominal DC. The coupling cap Cc holds the new
    // quiescent plate voltage, so only the swing around it reaches the next stage.
    biasVolts = volts;
    biasPlateOffset = volts != 0.0 ? amp->process (volts) : 0.0;
}

void ReelModel::process (float* samples, int numSamples) noexcept
{
    const auto& curve = *amp;

    for (int i = 0; i < numSamples; ++i)
    {
        if (bias.isMoving())
            applyBiasVolts (bias.next());

        const bool biased = biasVolts != 0.0;

        // record amp (inverting stage -> flip back), interstage coupling cap
        const auto vin = inputVolts * (double) samples[i];
        const auto plate = biased ? curve.process (vin + biasVolts) - biasPlateOffset : curve.process (vin);
        const auto drive = coupling.highpass (-plate);

        // record EQ and head: field with soft core saturation
        const auto hLin = fieldPerVolt * preEmphasis.process (drive);
        const auto h = hLin / std::sqrt (1.0 + hLin * hLin * (1.0 / (headSaturation * headSaturation)));

        // tape
        const auto m = tape.process (h);

        // playback
        auto y = deEmphasis.process (m);
        y = rolloff.process (y).hp;
        y += bumpGain * bump.process (y).bp;
        y = gapLoss.lowpass (y);

        const auto out = outputGain * y;
        samples[i] = std::abs (out) < 1.0e-30 ? 0.0f : (float) out;   // no denormal floats downstream
    }

    coupling.flush();
    preEmphasis.flush();
    deEmphasis.flush();
    rolloff.flush();
    bump.flush();
    gapLoss.flush();
}

} // namespace umber::dsp
