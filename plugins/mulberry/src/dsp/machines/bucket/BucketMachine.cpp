#include "dsp/machines/bucket/BucketMachine.h"

#include "dsp/MachineInfo.h"

namespace mulberry::dsp
{

namespace
{
    constexpr double clockGlideMs = 60.0;   // the time pot and its smoothing: the clock glides
    constexpr float compressorRef = 0.25f;
}

double BucketMachine::filterHz (int stages) noexcept
{
    switch (stages)
    {
        case 1024: return 6500.0;
        case 2048: return 5000.0;
        case 4096: return 3800.0;
        default:   return 3200.0;
    }
}

void BucketMachine::setChip (int stages) noexcept
{
    chip = stages;
    filter = ModalFilter<modes>::butterworth (filterHz (stages));

    // Low-frequency group delay of input and output filter together: the clock allows for it, so
    // the echo lands on Time.
    const auto f = 50.0;
    const auto a = std::arg (filter.response (f) * filter.response (f));
    const auto b = std::arg (filter.response (2.0 * f) * filter.response (2.0 * f));
    filterDelay = -(b - a) / (2.0 * std::numbers::pi * f);

    for (auto& line : lines)
        line.domain.setFilters (filter, filter);
}

double BucketMachine::imageExcess (const ModalFilter<modes>& filter, double clock) noexcept
{
    // Worst over the band of: (aliases into f through the input) x (images out of f through the hold and output).
    const auto sincAt = [clock] (double hz) { const auto x = std::numbers::pi * hz / clock; return x > 1.0e-9 ? std::abs (std::sin (x) / x) : 1.0; };
    double worstIn = 0.0, worstOut = 0.0;
    for (double f = 10.0; f < 0.5 * clock; f += 0.5 * clock / 64.0)
    {
        double in = 0.0, out = 0.0;
        for (int k = -8; k <= 8; ++k)
        {
            const auto g = std::abs (f + k * clock);
            in += std::norm (filter.response (g));
            out += std::norm (filter.response (g)) * sincAt (g) * sincAt (g);
        }
        worstIn = std::max (worstIn, in);
        worstOut = std::max (worstOut, out);
    }
    return std::max (1.0, std::sqrt (worstIn * worstOut));
}

double BucketMachine::clockFor (double timeSeconds, int stages) const noexcept
{
    const auto t = std::max (timeSeconds - filterDelay, 1.0e-4);
    return std::min ((double) stages / (2.0 * t), maxClock);
}

void BucketMachine::prepare (double sampleRate, SlotMemory&)
{
    fs = sampleRate;
    for (size_t l = 0; l < 2; ++l)
    {
        auto& line = lines[l];
        line.buckets.assign ((size_t) maxBuckets, 0.0f);
        line.compressorHistory.assign ((size_t) std::ceil (9.0 * fs / LoopContext::maxSubBlock) + 2, 1.0f);
        line.domain.prepare (fs, ModalFilter<modes>::butterworth (filterHz (4096)), ModalFilter<modes>::butterworth (filterHz (4096)));
        line.drift.setSeed (randomSeed * 3203u + 7u * (uint32_t) (l + 1));
        line.noise.setSeed (randomSeed * 9533u + 11u * (uint32_t) (l + 1));
    }
    setChip (4096);
    reset();
}

void BucketMachine::reset() noexcept
{
    for (size_t l = 0; l < 2; ++l)
    {
        auto& line = lines[l];
        line.domain.reset();
        line.write = 0;
        line.written = 0;
        line.compressorEnv = 0.0f;
        line.expanderEnv = {};
        std::fill (line.compressorHistory.begin(), line.compressorHistory.end(), 1.0f);
        line.historyWrite = 0;
        line.compressorGain = 1.0f;
        line.excessClock = -1.0;
        line.excess = 1.0;
        line.drift.reset();
        line.lfoPhase = l == 0 ? 0.0 : 0.31;
        line.modulation = 0.0;
        line.noise.reset();
        line.bleedSign = false;
    }
    dualMix = 0.0;
    snapNext = true;
}

void BucketMachine::process (LoopContext& loop, const std::array<ReadList, 2>& reads,
                             const MachineSettings& s, int n) noexcept
{
    const auto stages = character::chipStages (s.paramA);
    if (stages != chip)
    {
        setChip (stages);
        for (auto& line : lines)
            line.excessClock = -1.0;
    }

    const auto exponent = 0.5f * character::compander (s.paramB);
    const auto expandExponent = exponent / (1.0f - exponent);
    const auto envAttack = 1.0f - std::exp (-1.0f / msToSamples (4.0f, fs));
    const auto envRelease = 1.0f - std::exp (-1.0f / msToSamples (40.0f, fs));

    const auto dualStep = (double) n / (0.1 * fs);
    dualMix = snapNext ? (s.twoTransports ? 1.0 : 0.0) : std::clamp (dualMix + (s.twoTransports ? dualStep : -dualStep), 0.0, 1.0);

    // Clock: follows Time (glide), wobbles with a triangle LFO (and a little drift).
    const auto scale = maps::wobbleScale (s.wobble);
    const auto shape = shapeGains (s.wobbleShape, 0.2f);
    const auto rate = wobbleHz * std::clamp (s.wobbleRate, 0.1f, 8.0f);
    const auto depth = (double) (scale * wobbleDeviation);
    const auto glide = 1.0 - std::exp (-(double) n / (clockGlideMs * 0.001 * fs));

    std::array<double, 2> modulation {};
    std::array<double, 2> clockFrom {}, clockRatio {};
    for (size_t l = 0; l < 2; ++l)
    {
        auto& line = lines[l];
        const auto target = clockFor ((double) std::clamp (s.timeMs[l], maps::minTimeMs, maps::maxTimeMs) * 0.001, stages);
        clockFrom[l] = snapNext ? target : line.clock;
        line.clock = snapNext ? target : line.clock * std::pow (target / line.clock, glide);
        clockRatio[l] = std::pow (line.clock / clockFrom[l], 1.0 / n);
        // Below the chip's shortest time the clock is capped and fewer stages are used.
        line.timeMs = line.clock < maxClock * 0.999 ? (float) (((double) stages * 0.5 / line.clock + filterDelay) * 1000.0)
                                                    : std::clamp (s.timeMs[l], maps::minTimeMs, maps::maxTimeMs);
        line.lfoPhase += (double) rate * n / fs;
        line.lfoPhase -= std::floor (line.lfoPhase);
        line.drift.setRate (rate * 1.7f, fs);
        const auto tri = 4.0 * std::abs (line.lfoPhase - 0.5) - 1.0;
        modulation[l] = depth * (0.8 * shape.periodic * tri + 0.2 * shape.random * (double) line.drift.advance (n));
    }
    modulation[1] = modulation[0] + dualMix * (modulation[1] - modulation[0]);
    std::array<double, 2> modFrom {}, modStep {};
    for (size_t l = 0; l < 2; ++l)
    {
        modFrom[l] = snapNext ? modulation[l] : lines[l].modulation;
        modStep[l] = (modulation[l] - modFrom[l]) / n;
        lines[l].modulation = modulation[l];
    }
    snapNext = false;

    // Taps in buckets: fixed stages of the chain (the clock moves the time, not the taps), set
    // by the target clock; and each tap's loss (charge transfer per stage, leakage over its dwell).
    std::array<std::array<int, ReadList::maxReads>, 2> taps {};
    std::array<std::array<float, ReadList::maxReads>, 2> tapLoss {};
    for (size_t l = 0; l < 2; ++l)
    {
        const auto timeS = (double) std::clamp (s.timeMs[l], maps::minTimeMs, maps::maxTimeMs) * 0.001;
        const auto targetClock = clockFor (timeS, stages);
        const auto chain = (timeS - filterDelay) * targetClock;     // buckets for one Time

        // The image excess follows the clock as it glides (recomputed when it has moved 0.5 %).
        auto& line = lines[l];
        if (std::abs (line.clock - line.excessClock) > 0.005 * line.excessClock)
        {
            line.excessClock = line.clock;
            line.excess = imageExcess (filter, line.clock);
        }
        const auto excess = line.excess;
        for (int i = 0; i < reads[l].count; ++i)
        {
            const auto& e = reads[l].entries[(size_t) i];
            auto offset = (double) e.read.offsetMs * 0.001;
            if (e.feedback)
                offset += s.feedbackOffsetSamples / fs;
            const auto k = std::clamp ((int) std::lround ((double) e.read.fraction * chain + offset * targetClock), 1, maxBuckets - 2);
            taps[l][(size_t) i] = k;
            tapLoss[l][(size_t) i] = (float) (std::exp (-2.0 * k * stageLoss - (double) k / lines[l].clock / leakSeconds) / excess);
        }
    }

    // The compressor's gain one Time ago (per line): the expander never exceeds its inverse.
    std::array<float, 2> expandLimit {};
    for (size_t l = 0; l < 2; ++l)
    {
        auto& line = lines[l];
        const auto size = (int) line.compressorHistory.size();
        const auto back = std::clamp ((int) std::lround ((double) line.timeMs * 0.001 * fs / LoopContext::maxSubBlock), 1, size - 1);
        auto at = line.historyWrite - back;
        if (at < 0)
            at += size;
        expandLimit[l] = 1.0f / std::max (line.compressorHistory[(size_t) at], 1.0e-3f);
        line.compressorHistory[(size_t) line.historyWrite] = line.compressorGain;
        line.historyWrite = (line.historyWrite + 1) % size;
    }

    const auto noiseLevel = noiseGain (s.noise) * dbToGain (noiseDb);
    const auto bleed = noiseGain (s.noise) * dbToGain (noiseDb + 8.0f);

    for (int k = 0; k < n; ++k)
    {
        std::array<float, numBuses> sum {};

        for (size_t l = 0; l < 2; ++l)
        {
            auto& line = lines[l];
            const auto& list = reads[l];

            // The clock glides and wobbles per sample, so the sampling instants never step.
            if (k == 0 || std::abs (clockRatio[l] - 1.0) > 1.0e-12 || std::abs (modStep[l]) > 0.0)
                line.domain.setClock (clockFrom[l] * std::pow (clockRatio[l], k + 1) * (1.0 + modFrom[l] + modStep[l] * (k + 1)));
            const auto events = line.domain.beginSample();

            for (int ev = 0; ev < events; ++ev)
            {
                // The device samples its input filter and passes the charge on.
                const auto charge = transfer (line.domain.sampleInput())
                                  + (noiseLevel > 0.0f ? (double) (noiseLevel * line.noise.next()) : 0.0);
                line.buckets[(size_t) line.write] = (float) charge;
                if (++line.write == maxBuckets)
                    line.write = 0;
                ++line.written;

                std::array<double, numBuses> value {};
                for (int i = 0; i < list.count; ++i)
                {
                    const auto& e = list.entries[(size_t) i];
                    const auto gain = planGain (e, k);
                    const auto tap = taps[l][(size_t) i];
                    if (gain <= 0.0f || tap > line.written)
                        continue;
                    auto index = line.write - 1 - tap;
                    if (index < 0)
                        index += maxBuckets;
                    const auto v = (double) (line.buckets[(size_t) index] * gain * tapLoss[l][(size_t) i]);
                    const auto weights = weightsAt (e, k);
                    for (size_t b = 0; b < (size_t) numBuses; ++b)
                        value[b] += v * weights[b];
                }

                if (bleed > 0.0f)
                {
                    line.bleedSign = ! line.bleedSign;
                    value[outLeft] += line.bleedSign ? bleed : -bleed;
                    value[outRight] += line.bleedSign ? bleed : -bleed;
                }

                for (int b = 0; b < numBuses; ++b)
                    line.domain.pushOutput (b, value[(size_t) b]);
                line.domain.nextEvent();
            }

            // Expander: after the chip, one per output (each tap mix has its own detector, as a
            // chip output each with its own expander half would).
            for (size_t b = 0; b < (size_t) numBuses; ++b)
            {
                const auto y = (float) line.domain.output ((int) b);
                auto expand = 1.0f;
                if (exponent > 0.0f)
                {
                    const auto a = std::abs (y);
                    auto& env = line.expanderEnv[b];
                    env += (a - env) * (a > env ? envAttack : envRelease);
                    expand = std::min (std::clamp (std::pow (std::max (env, 1.0e-5f) / compressorRef, expandExponent), 1.0f / 64.0f, 4.0f),
                                       expandLimit[l]);
                }
                sum[b] += y * expand;
            }
        }

        float rec0, rec1;
        loop.step (k, sum[feedback0], sum[feedback1], sum[outLeft], sum[outRight], rec0, rec1);
        const std::array<float, 2> rec { rec0, rec1 };

        for (size_t l = 0; l < 2; ++l)
        {
            // Compressor: before the chip.
            auto& line = lines[l];
            auto x = rec[l];
            if (exponent > 0.0f)
            {
                const auto a = std::abs (x);
                line.compressorEnv += (a - line.compressorEnv) * (a > line.compressorEnv ? envAttack : envRelease);
                line.compressorGain = std::clamp (std::pow (compressorRef / std::max (line.compressorEnv, 1.0e-4f), exponent), 1.0f / 8.0f, 8.0f);
                x *= line.compressorGain;
            }
            line.domain.endSample ((double) x);
        }
    }

    for (auto& line : lines)
    {
        line.domain.flush();
        line.compressorEnv = flushTiny (line.compressorEnv);
        for (auto& env : line.expanderEnv)
            env = flushTiny (env);
    }
}

double BucketMachine::predictedPassGain (double hz, const MachineSettings& s) const noexcept
{
    // A steady tone: filters, the hold's sinc, the chip's losses; the compander then squares the
    // loss in dB at full setting (the mistracking), (L)^(1 / (1 - e)).
    const auto stages = character::chipStages (s.paramA);
    const auto f = ModalFilter<modes>::butterworth (filterHz (stages));
    const auto timeS = (double) std::clamp (s.timeMs[0], maps::minTimeMs, maps::maxTimeMs) * 0.001;
    const auto clock = clockFor (timeS, stages);
    const auto x = std::numbers::pi * hz / clock;
    const auto hold = x > 1.0e-9 ? std::sin (x) / x : 1.0;
    const auto k = std::max (1.0, std::round ((timeS - filterDelay) * clock));
    const auto chipLoss = std::exp (-2.0 * k * stageLoss - k / clock / leakSeconds) / imageExcess (f, clock);
    const auto t = std::tanh (transferOffset / rail);
    const auto small = 1.0 - t * t;
    const auto linear = std::abs (f.response (hz) * f.response (hz)) * std::abs (hold) * chipLoss * small;
    const auto e = 0.5 * (double) character::compander (s.paramB);
    return std::pow (linear, 1.0 / (1.0 - e));
}

float BucketMachine::currentTimeMs (int line) const noexcept
{
    return lines[(size_t) std::clamp (line, 0, 1)].timeMs;
}

} // namespace mulberry::dsp
