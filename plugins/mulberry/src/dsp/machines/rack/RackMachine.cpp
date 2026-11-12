#include "dsp/machines/rack/RackMachine.h"

#include "dsp/MachineInfo.h"

namespace mulberry::dsp
{

namespace
{
    /** Antiderivative of the front end's residual a clip(x/a) - x (Padé, as Clean's record stage). */
    double residualIntegral (double x, double a) noexcept
    {
        const auto u = x / a;
        const auto au = std::abs (u);
        if (au <= 3.0)
            return a * a * (u * u / 18.0 + (4.0 / 3.0) * std::log1p (u * u / 3.0)) - 0.5 * x * x;
        const auto edge = a * a * (9.0 / 18.0 + (4.0 / 3.0) * std::log1p (3.0));
        return edge + a * a * (au - 3.0) - 0.5 * x * x;
    }

    double clip (double x, double a) noexcept
    {
        const auto u = std::clamp (x / a, -3.0, 3.0);
        return a * u * (27.0 + u * u) / (27.0 + 9.0 * u * u);
    }
}

void RackMachine::setRate (double hz) noexcept
{
    rateHz = hz;
    clock = clockPerBandwidth * hz;
    filter = ModalFilter<modes>::butterworth (hz);
    const auto f = 50.0;
    const auto a = std::arg (filter.response (f) * filter.response (f));
    const auto b = std::arg (filter.response (2.0 * f) * filter.response (2.0 * f));
    filterDelay = -(b - a) / (2.0 * std::numbers::pi * f);
    for (auto& line : lines)
    {
        line.domain.setFilters (filter, filter);
        line.domain.setClock (clock);
    }
}

void RackMachine::prepare (double sampleRate, SlotMemory& memory)
{
    fs = sampleRate;
    for (size_t l = 0; l < 2; ++l)
    {
        auto& line = lines[l];
        line.memory.attach (memory.audio[l].data(), memory.capacity);
        line.domain.prepare (fs, ModalFilter<modes>::butterworth (12000.0), ModalFilter<modes>::butterworth (12000.0));
        line.lfo.setStartPhase (l == 0 ? 0.0f : 0.25f);
        line.noise.setSeed (randomSeed * 4099u + 3u * (uint32_t) (l + 1));
    }
    setRate (12000.0);
    reset();
}

void RackMachine::reset() noexcept
{
    for (auto& line : lines)
    {
        line.domain.reset();
        line.memory.restart();
        line.delayNow = line.delayFrom = 0.0;
        line.lastTarget = -1.0;
        line.fade = 1.0f;
        line.range = line.holdCount = 0;
        line.peak = 0.0f;
        line.previousIn = line.previousResidual = 0.0;
        line.lfo.reset();
        line.noise.reset();
    }
    dualMix = 0.0;
    snapNext = true;
}

float RackMachine::frontEnd (Line& line, float in) noexcept
{
    // The op-amp: transparent until its rails, the clip's residual antialiased (ADAA), as Clean.
    const auto x = (double) in;
    const auto a = (double) frontEndLimit;
    const auto dx = x - line.previousIn;
    double residual;
    if (std::abs (dx) > 1.0e-5)
    {
        const auto integral = residualIntegral (x, a);
        residual = (integral - line.previousResidual) / dx;
        line.previousResidual = integral;
    }
    else
    {
        const auto mid = 0.5 * (x + line.previousIn);
        residual = clip (mid, a) - mid;
        line.previousResidual = residualIntegral (x, a);
    }
    line.previousIn = x;
    return (float) std::clamp (x + residual, -a, a);
}

void RackMachine::process (LoopContext& loop, const std::array<ReadList, 2>& reads,
                           const MachineSettings& s, int n) noexcept
{
    const auto bits = character::bits (s.paramA);
    const auto rate = (double) character::rateHz (s.paramB);
    if (std::abs (rate - rateHz) > 0.5)
        setRate (rate);

    const auto dualStep = (double) n / (0.1 * fs);
    dualMix = snapNext ? (s.twoTransports ? 1.0 : 0.0) : std::clamp (dualMix + (s.twoTransports ? dualStep : -dualStep), 0.0, 1.0);

    // Address wobble (words): a pitch deviation d at rate f is d fs / (2 pi f) of time.
    const auto scale = maps::wobbleScale (s.wobble);
    const auto lfoHz = wobbleHz * std::clamp (s.wobbleRate, 0.1f, 8.0f);
    const auto depth = (double) (scale * wobbleDeviation) * clock / (2.0 * std::numbers::pi * lfoHz);
    std::array<double, 2> modulation {};
    for (size_t l = 0; l < 2; ++l)
    {
        lines[l].lfo.setRate (lfoHz, fs);
        modulation[l] = depth * (double) lines[l].lfo.advance (n);
    }
    modulation[1] = modulation[0] + dualMix * (modulation[1] - modulation[0]);

    // Time: the memory length jumps, crossfaded over 10 ms.
    const auto fadeEvents = std::max (1.0f, crossfadeMs * 0.001f * (float) clock);
    for (size_t l = 0; l < 2; ++l)
    {
        auto& line = lines[l];
        const auto ms = std::clamp (s.timeMs[l], maps::minTimeMs, maps::maxTimeMs);
        const auto target = std::max (std::round (((double) ms * 0.001 - filterDelay) * clock), 2.0);
        if (snapNext)
        {
            line.delayNow = line.delayFrom = target;
            line.fade = 1.0f;
        }
        else if (std::abs (target - line.delayNow) > 0.5 && line.fade >= 1.0f)
        {
            line.delayFrom = line.delayNow;
            line.delayNow = target;
            line.fade = 0.0f;
            line.fadeStep = 1.0f / fadeEvents;
        }
        timeMs[l] = (float) ((line.delayNow / clock + filterDelay) * 1000.0);
    }
    snapNext = false;

    const auto noiseLevel = noiseGain (s.noise) * dbToGain (noiseDb);
    const auto holdEvents = (int) (holdMs * 0.001f * (float) clock);
    const auto feedbackWords = s.feedbackOffsetSamples / fs * clock;

    for (int k = 0; k < n; ++k)
    {
        std::array<float, numBuses> sum {};

        for (size_t l = 0; l < 2; ++l)
        {
            auto& line = lines[l];
            const auto& list = reads[l];
            const auto events = line.domain.beginSample();

            for (int ev = 0; ev < events; ++ev)
            {
                // Gain ranging: the highest gain that keeps the peak inside the converter, raised
                // only after the hold (syllabic), lowered at once.
                const auto v = line.domain.sampleInput();
                const auto a = (float) std::abs (v);
                line.peak = std::max (a, line.peak * 0.9995f);
                int wanted = 3;
                while (wanted > 0 && line.peak * (float) (1 << wanted) > 0.9f)
                    --wanted;
                if (wanted < line.range)
                {
                    line.range = wanted;
                    line.holdCount = holdEvents;
                }
                else if (wanted > line.range && --line.holdCount <= 0)
                {
                    line.range = wanted;
                    line.holdCount = holdEvents;
                }
                line.memory.write (convert (v, bits, line.range));

                // Fade between the old and the new memory length after a Time jump.
                auto gNew = 1.0f, gOld = 0.0f;
                if (line.fade < 1.0f)
                {
                    line.fade = std::min (1.0f, line.fade + line.fadeStep);
                    const auto e = line.fade * line.fade * (3.0f - 2.0f * line.fade);
                    gNew = std::sin (e * 0.5f * std::numbers::pi_v<float>);
                    gOld = std::cos (e * 0.5f * std::numbers::pi_v<float>);
                }

                std::array<double, numBuses> value {};
                for (int i = 0; i < list.count; ++i)
                {
                    const auto& entry = list.entries[(size_t) i];
                    const auto gain = planGain (entry, k);
                    if (gain <= 0.0f)
                        continue;
                    // Whole words unless the address wobbles.
                    const auto offset = std::round ((double) entry.read.offsetMs * 0.001 * clock + (entry.feedback ? feedbackWords : 0.0))
                                      + modulation[l];
                    const auto at = [&] (double base)
                    {
                        // Between words, linearly (the DAC reads one word; the address moves smoothly).
                        const auto d = std::max ((double) entry.read.fraction * base + offset, 1.0);
                        const auto i0 = (int) d;
                        const auto f = (float) (d - i0);
                        return (1.0f - f) * line.memory.at (i0) + f * line.memory.at (i0 + 1);
                    };
                    auto v2 = at (line.delayNow) * gNew;
                    if (gOld > 0.0f)
                        v2 += at (line.delayFrom) * gOld;
                    const auto weights = weightsAt (entry, k);
                    for (size_t b = 0; b < (size_t) numBuses; ++b)
                        value[b] += (double) (v2 * gain) * weights[b];
                }

                for (int b = 0; b < numBuses; ++b)
                    line.domain.pushOutput (b, value[(size_t) b]);
                line.domain.nextEvent();
            }

            for (int b = 0; b < numBuses; ++b)
                sum[(size_t) b] += (float) line.domain.output (b);
        }

        float rec0, rec1;
        loop.step (k, sum[feedback0], sum[feedback1], sum[outLeft], sum[outRight], rec0, rec1);
        const std::array<float, 2> rec { rec0, rec1 };

        for (size_t l = 0; l < 2; ++l)
        {
            auto& line = lines[l];
            const auto hum = noiseLevel > 0.0f ? noiseLevel * line.noise.next() : 0.0f;
            line.domain.endSample ((double) (frontEnd (line, rec[l]) + hum));
        }
    }

    for (auto& line : lines)
        line.domain.flush();
}

double RackMachine::predictedPassGain (double hz, const MachineSettings& s) const noexcept
{
    const auto rate = (double) character::rateHz (s.paramB);
    const auto f = ModalFilter<modes>::butterworth (rate);
    const auto c = clockPerBandwidth * rate;
    const auto x = std::numbers::pi * hz / c;
    const auto hold = x > 1.0e-9 ? std::sin (x) / x : 1.0;
    return std::abs (f.response (hz) * f.response (hz)) * std::abs (hold);
}

float RackMachine::currentTimeMs (int line) const noexcept
{
    return timeMs[(size_t) std::clamp (line, 0, 1)];
}

} // namespace mulberry::dsp
