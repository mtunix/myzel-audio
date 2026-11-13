#pragma once

#include "common/dsp/ReverbBlocks.h"

/**
    Reverb building blocks: the generic float blocks (delay lines, allpasses, filters, LFOs,
    smoothers) and the reverb blocks (decay filters, FDN mixing) are shared with the other
    plugins (common/dsp/Blocks.h, common/dsp/ReverbBlocks.h) and imported here.
*/
namespace cerulean::dsp
{

using myzel::dsp::twoPi;
using myzel::dsp::msToSamples;
using myzel::dsp::dbToGain;
using myzel::dsp::flushTiny;
using myzel::dsp::LinearRamp;
using myzel::dsp::Rng;
using myzel::dsp::DelayLine;
using myzel::dsp::Allpass;
using myzel::dsp::OnePole;
using myzel::dsp::Svf;
using myzel::dsp::SmoothRandom;
using myzel::dsp::SineLfo;
using myzel::dsp::GlideValue;
using myzel::dsp::Smoother;
using myzel::dsp::EnvelopeFollower;
using myzel::dsp::AllpassChain;

using myzel::dsp::ShelfCoefficients;
using myzel::dsp::DecayFilter;
using myzel::dsp::BandGains;
using myzel::dsp::computeBandGains;
using myzel::dsp::hadamardInPlace;
using myzel::dsp::householderInPlace;
using myzel::dsp::hadamardSign;

} // namespace cerulean::dsp
