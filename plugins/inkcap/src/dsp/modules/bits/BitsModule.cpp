#include "dsp/modules/bits/BitsModule.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace inkcap::dsp
{

namespace
{
    int64_t nextGridPoint (int64_t sample, int interval) noexcept
    {
        const auto i = (int64_t) interval;
        const auto floored = sample >= 0 ? sample / i : -((-sample + i - 1) / i);
        return (floored + 1) * i;
    }

    double flushed (double v) noexcept { return std::abs (v) < 1.0e-30 ? 0.0 : v; }
}

void BitsModule::LowCut::setup (double hz, double sampleRate) noexcept
{
    g = std::tan (std::numbers::pi * std::clamp (hz, 1.0, 0.45 * sampleRate) / sampleRate);
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

double BitsModule::LowCut::process (double x) noexcept
{
    const auto v3 = x - ic2;
    const auto v1 = a1 * ic1 + a2 * v3;
    const auto v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = flushed (2.0 * v1 - ic1);
    ic2 = flushed (2.0 * v2 - ic2);
    return x - k * v1 - v2;
}

double BitsModule::converterDelay (double sampleRate, double clockHz, double cutoffHz) noexcept
{
    // Two 4th-order Butterworths: 2 x 2.6131 / wc of group delay at DC.
    constexpr double butterworthPair = 2.0 * 2.6131259297527532;
    constexpr double stretch = 1.16;
    return 0.5 * sampleRate / clockHz + stretch * butterworthPair * sampleRate / (2.0 * std::numbers::pi * cutoffHz);
}

double BitsModule::quantise (double v, double step, bool mulaw) noexcept
{
    const auto clipped = std::clamp (v, -1.0, 1.0);
    if (! mulaw)
        return std::clamp (step * std::round (clipped / step), -1.0, 1.0);

    // Compress, quantise the code, expand: fine steps for quiet signals, coarse for loud.
    static const auto range = std::log1p (bits::mu);
    const auto code = std::log1p (bits::mu * std::abs (clipped)) / range;
    const auto quantised = std::min (step * std::round (code / step), 1.0);
    return std::copysign (std::expm1 (quantised * range) / bits::mu, clipped);
}

void BitsModule::prepare (const ModuleSpec& spec)
{
    fs = spec.sampleRate;
    channelCount = std::clamp (spec.numChannels, 1, (int) channels.size());
    const auto filter = myzel::dsp::ModalFilter<modes>::butterworth (0.25 * fs);
    for (auto& c : channels)
        c.domain.prepare (fs, filter, filter);
    shares.assign ((size_t) std::max (spec.maxBlockSize, maxShareBlock), -1.0f);
    delays.assign (shares.size(), 0.0f);

    const auto glide = 1.0 - std::exp (-1.0 / (0.5 * glideSeconds * fs));
    intensityGlide.coefficient = rateGlide.coefficient = depthGlide.coefficient = jitterGlide.coefficient = glide;
    wet.prepare (fs);
    swap.setDuration (fs, swapSeconds);
    reset();
}

void BitsModule::reset() noexcept
{
    for (auto& c : channels)
    {
        c.domain.reset();
        c.lowCut.reset();
    }
    wet.reset();
    swap.jumpTo (1.0f);
    primed = live = false;
    anchorNow = true;
    filterClock = 0.0;
    lowCutHz = -1.0;
    resolution = bits::cleanBits;
    step = std::exp2 (1.0 - resolution);
    seedKnown = false;
    lastIntensity = lastActivity = 0.0f;
}

double BitsModule::nominalClock (double i, double ratePosition) noexcept
{
    if (myzel::dsp::isExactly (i, clockKey.intensity) && myzel::dsp::isExactly (ratePosition, clockKey.rate) && type == clockKey.type)
        return clockKey.hz;
    clockKey = { i, ratePosition, type, 0.0 };

    // From the clock whose filters just reach the cap (where the converter is first heard) to Rate.
    const auto set = bits::clockHz (type, ratePosition);
    const auto clean = std::max (maxCutoffPerRate * fs / bits::voicingOf (type).filterRatio, set);
    clockKey.hz = std::exp (std::log (clean) + std::clamp (i, 0.0, 1.0) * (std::log (set) - std::log (clean)));
    return clockKey.hz;
}

double BitsModule::cutoff (double clockHz) const noexcept
{
    return std::min (bits::voicingOf (type).filterRatio * clockHz, maxCutoffPerRate * fs);
}

double BitsModule::jitterAt (const GridClock& c, int64_t sample, double amount) noexcept
{
    if (amount <= 0.0)
        return 0.0;
    const auto lattice = bits::voicingOf (type).clockHz / bits::jitterLatticeDivider;
    return amount * bits::jitterPeriods * jitter.at (c.secondsAt (sample) * lattice);
}

void BitsModule::restart (const GridClock& c, int64_t sample, double amount) noexcept
{
    // The clock's phase is a function of the song time: the instants a render from bar 1 had here.
    const auto filter = myzel::dsp::ModalFilter<modes>::butterworth (cutoff (clock));
    filterClock = clock;
    const auto phase = clock * c.secondsAt (sample) + jitterAt (c, sample, amount);
    for (auto& ch : channels)
    {
        ch.domain.setFilters (filter, filter);
        ch.domain.setClockPhase (phase);
    }
    anchorNow = false;
}

void BitsModule::process (const AudioBlock& block, const BitsSettings& s, const BlockContext& context) noexcept
{
    const auto target = (double) moduleIntensity (Module::bits, s.amount, context.damage);
    lastIntensity = s.on ? (float) target : 0.0f;
    wet.set (s.on && target > 0.0);
    wanted = (BitsType) std::clamp ((int) s.type, 0, (int) bits::voicings.size() - 1);

    const auto rateTarget = (double) std::clamp (s.rate, 0.0f, 1.0f);
    const auto depthTarget = (double) std::clamp (s.depth, 0.0f, 1.0f);
    const auto jitterTarget = bits::jitterWithChaos (s.jitter, context.chaos);
    if (! primed)
    {
        type = wanted;
        primed = true;
    }

    if (wet.isIdle())
    {
        for (int k = 0; k < block.numSamples; ++k)
        {
            wet.next();
            shares[(size_t) k] = -1.0f;
        }
        live = false;
        type = wanted;
        intensityGlide.jumpTo (target);
        rateGlide.jumpTo (rateTarget);
        depthGlide.jumpTo (depthTarget);
        jitterGlide.jumpTo (jitterTarget);
        lastActivity = 0.0f;
        return;
    }

    if (! live)
    {
        for (auto& ch : channels)
        {
            ch.domain.reset();
            ch.lowCut.reset();
        }
        live = true;
        anchorNow = true;
        lowCutHz = -1.0;
        swap.jumpTo (1.0f);

        // A converter that comes on starts at its settings, under the 20 ms fade: its clock is
        // then the timeline's (a glide from elsewhere would leave the phase where it ended).
        intensityGlide.jumpTo (target);
        rateGlide.jumpTo (rateTarget);
        depthGlide.jumpTo (depthTarget);
        jitterGlide.jumpTo (jitterTarget);
    }

    if (! seedKnown || context.seed != seed)
    {
        seed = context.seed;
        seedKnown = true;
        jitter.setSource (SeedRng (seed, Stream::bitsJitter));
    }

    const auto& c = context.clock;
    if (c.jumpedAt (block.firstSample))
        anchorNow = true;

    const auto nc = std::min (block.numChannels, channelCount);
    double errorSum = 0.0, signalSum = 0.0;

    for (int k = 0; k < block.numSamples; ++k)
    {
        const auto n = block.firstSample + k;
        const auto i = intensityGlide.next (target);
        const auto rate = rateGlide.next (rateTarget);
        const auto depth = depthGlide.next (depthTarget);
        const auto amount = jitterGlide.next (jitterTarget) * i;

        if (wanted != type && swap.target() > 0.0f)
            swap.moveTo (0.0f);
        else if (wanted == type && swap.target() < 1.0f)
            swap.moveTo (1.0f);
        const auto duck = swap.next();
        if (wanted != type && swap.isAt (0.0f))
        {
            type = wanted;
            for (auto& ch : channels)
            {
                ch.domain.reset();
                ch.lowCut.reset();
            }
            anchorNow = true;
            lowCutHz = -1.0;
            swap.moveTo (1.0f);
        }

        const auto& voicing = bits::voicingOf (type);
        clock = nominalClock (i, rate);
        const auto due = anchorNow || n % clockInterval == 0;
        if (anchorNow)
        {
            restart (c, n, amount);
        }
        else if (n % filterInterval == 0)
        {
            if (std::abs (clock - filterClock) > 1.0e-9 * clock)
            {
                const auto filter = myzel::dsp::ModalFilter<modes>::butterworth (cutoff (clock));
                filterClock = clock;
                for (auto& ch : channels)
                    ch.domain.setFilters (filter, filter);
            }
            for (auto& ch : channels)
                ch.domain.flush();
        }

        if (due)
        {
            // The rate to the next grid point carries the jitter's displacement there exactly.
            const auto next = nextGridPoint (n, clockInterval);
            const auto drift = jitterAt (c, next, amount) - jitterAt (c, n, amount);
            const auto hz = clock + drift * fs / (double) (next - n);
            for (auto& ch : channels)
                ch.domain.setClock (hz);
        }

        if (voicing.lowCutHz > 0.0 && (due || lowCutHz < 0.0))
        {
            const auto hz = voicing.lowCutHz * i;
            if (std::abs (hz - lowCutHz) > 1.0e-9)
            {
                lowCutHz = hz;
                for (auto& ch : channels)
                    ch.lowCut.setup (hz, fs);
            }
        }

        const auto wanting = bits::cleanBits + i * (bits::depthBits (type, depth) - bits::cleanBits);
        if (! myzel::dsp::isExactly (wanting, resolution))
        {
            resolution = wanting;
            step = std::exp2 (1.0 - resolution);
        }

        const auto w = wet.next() * duck;
        if (! myzel::dsp::isExactly (clock, delayClock) || ! myzel::dsp::isExactly (filterClock, delayFilterClock))
        {
            delayClock = clock;
            delayFilterClock = filterClock;
            delayNow = converterDelay (fs, clock, cutoff (filterClock));
        }
        shares[(size_t) k] = w;
        delays[(size_t) k] = (float) delayNow;
        for (int ch = 0; ch < nc; ++ch)
        {
            auto& channel = channels[(size_t) ch];
            auto& d = channel.domain;
            const auto x = block.channels[ch][k];
            const auto u = voicing.lowCutHz > 0.0 && lowCutHz > 0.0 ? channel.lowCut.process ((double) x) : (double) x;

            const auto events = d.beginSample();
            for (int e = 0; e < events; ++e)
            {
                const auto v = d.sampleInput();
                const auto q = quantise (v, step, voicing.mulaw);
                if (ch == 0)
                {
                    errorSum += (q - v) * (q - v);
                    signalSum += v * v;
                }
                d.pushOutput (0, q);
                d.nextEvent();
            }
            const auto y = (float) flushed (d.output (0));
            d.endSample (u);
            block.channels[ch][k] = OnOffFade::blend (x, y, w);
        }
    }

    // The display's dot: the converter's noise against the signal, 72 dB (nothing) .. 12 dB (all).
    const auto snr = signalSum > 1.0e-20 ? 10.0 * std::log10 (signalSum / std::max (errorSum, 1.0e-30)) : 120.0;
    lastActivity = (float) std::clamp ((72.0 - snr) / 60.0, 0.0, 1.0) * wet.current();
}

} // namespace inkcap::dsp
