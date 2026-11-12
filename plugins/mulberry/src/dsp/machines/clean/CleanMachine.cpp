#include "dsp/machines/clean/CleanMachine.h"

#include "dsp/MachineInfo.h"

namespace mulberry::dsp
{

namespace
{
    constexpr double glideTauMs = 200.0;
    constexpr double glideAccelMs = 30.0;
    constexpr double glideMaxSlew = 0.5;        // samples per sample: at most an octave down / +50 % up
}

void CleanMachine::prepare (double sampleRate, SlotMemory& memory)
{
    fs = sampleRate;
    for (size_t l = 0; l < 2; ++l)
    {
        rings[l].attach (memory.audio[l].data(), memory.capacity);
        times[l].glide.prepare (fs, glideTauMs, glideAccelMs, glideMaxSlew);
        lfo[l].setStartPhase (l == 0 ? 0.0f : 0.37f);
        drift[l].setSeed (randomSeed * 7919u + 13u * (uint32_t) (l + 1));
        noise[l].setSeed (randomSeed * 104729u + 31u * (uint32_t) (l + 1));
        shiftHistory[l].setMaxDelay ((int) std::ceil (myzel::dsp::GrainVoice::historyFor (fs, 2.0f)));
        shifter[l].prepare (fs, randomSeed * 2654435761u + 17u * (uint32_t) (l + 1));
    }
    shiftLatency.prepare (fs, 30.0, 10.0, 0.25);
    reset();
}

void CleanMachine::reset() noexcept
{
    for (size_t l = 0; l < 2; ++l)
    {
        rings[l].restart();
        times[l] = LineTime { times[l].glide };
        times[l].glide.snap (0.0);
        limiters[l].reset();
        lfo[l].reset();
        drift[l].reset();
        noise[l].reset();
        shiftHistory[l].reset();
        shifter[l].reset();
        modNow[l] = modStep[l] = 0.0f;
    }
    shiftMix = shiftMixStep = 0.0f;
    shiftRatio = 1.0f;
    dualMix = 0.0f;
    shiftLatency.snap (0.0);
    snapNext = true;
}

void CleanMachine::updateTime (LineTime& t, double target, float glide, bool snap) noexcept
{
    if (snap)
    {
        t.glide.snap (target);
        t.jumpNow = t.jumpTarget = t.jumpFrom = 0.0;
        t.fade = 1.0f;
        t.lastTarget = target;
        return;
    }

    const auto change = target - t.lastTarget;
    if (std::abs (change) > 0.0)
    {
        t.glide.setTarget (t.glide.getTarget() + glide * change);
        t.jumpTarget += (1.0 - glide) * change;
        t.lastTarget = target;
    }

    if (! t.fading() && std::abs (t.jumpTarget - t.jumpNow) > 1.0e-3)
    {
        t.jumpFrom = t.jumpNow;
        t.jumpNow = t.jumpTarget;
        t.fade = 0.0f;
        t.fadeStep = 1.0f / std::max (1.0f, msToSamples (crossfadeMs, fs));
    }
}

void CleanMachine::process (LoopContext& loop, const std::array<ReadList, 2>& reads,
                            const MachineSettings& s, int n) noexcept
{
    const auto glide = character::glide (s.paramA);

    for (size_t l = 0; l < 2; ++l)
    {
        const auto ms = std::clamp (s.timeMs[l], maps::minTimeMs, maps::maxTimeMs);
        updateTime (times[l], (double) msToSamples (ms, fs), glide, snapNext);
    }

    // Wobble: a chorus on the read position, one transport for both lines unless Dual.
    const auto scale = maps::wobbleScale (s.wobble);
    const auto rate = wobbleHz * std::clamp (s.wobbleRate, 0.1f, 8.0f);
    const auto shape = shapeGains (s.wobbleShape, wobbleRandom);
    const auto depth = scale * wobbleDeviation * (float) fs / (myzel::dsp::twoPi * rate);
    // Dual decorrelates the right line's wobble; the change blends over 100 ms, so it never jumps.
    const auto dualStep = (float) n / msToSamples (100.0f, fs);
    dualMix = snapNext ? (s.twoTransports ? 1.0f : 0.0f)
                       : std::clamp (dualMix + (s.twoTransports ? dualStep : -dualStep), 0.0f, 1.0f);
    std::array<float, 2> ends {};
    for (size_t l = 0; l < 2; ++l)
    {
        lfo[l].setRate (rate, fs);
        drift[l].setRate (rate * 1.3f, fs);
        const auto sine = lfo[l].advance (n), rnd = drift[l].advance (n);
        ends[l] = depth * ((1.0f - wobbleRandom) * shape.periodic * sine + wobbleRandom * shape.random * rnd);
    }
    ends[1] = ends[0] + dualMix * (ends[1] - ends[0]);
    for (size_t l = 0; l < 2; ++l)
    {
        if (snapNext)
            modNow[l] = ends[l];
        modStep[l] = (ends[l] - modNow[l]) / (float) n;
    }

    // Shift
    const auto semitones = character::shiftSemitones (s.paramB);
    const auto wantShift = semitones != 0;
    if (wantShift)
        shiftRatio = std::pow (2.0f, (float) semitones / 12.0f);
    const auto mixTarget = wantShift ? 1.0f : 0.0f;
    if (snapNext)
        shiftMix = mixTarget;
    shiftMixStep = std::clamp (mixTarget - shiftMix, -(float) n / msToSamples (shiftFadeMs, fs), (float) n / msToSamples (shiftFadeMs, fs)) / (float) n;
    const auto latencyTarget = (double) myzel::dsp::GrainVoice::meanLatencySeconds (shiftRatio) * fs;
    if (snapNext)
        shiftLatency.snap (latencyTarget);
    else
        shiftLatency.setTarget (latencyTarget);
    const auto shiftActive = shiftMix > 0.0f || shiftMixStep > 0.0f;
    for (auto& v : shifter)
        v.setRatio (shiftRatio);

    const auto noiseLevel = noiseGain (s.noise) * dbToGain (noiseDb);
    const auto feedbackOffset = s.feedbackOffsetSamples;
    const auto msScale = fs * 0.001;
    constexpr auto limitedDelay = (double) (Limiter::latency + 1 + WindowedSinc::half);
    snapNext = false;

    for (int k = 0; k < n; ++k)
    {
        std::array<double, 2> now {}, then {};
        std::array<float, 2> gainNow {}, gainThen {};
        std::array<bool, 2> fading {};

        for (size_t l = 0; l < 2; ++l)
        {
            auto& t = times[l];
            const auto base = t.glide.next() + (double) (modNow[l] += modStep[l]);
            now[l] = base + t.jumpNow;
            then[l] = base + t.jumpFrom;
            fading[l] = t.fading();
            if (fading[l])
            {
                // Equal power, eased so the gains start and end with zero slope: no kink.
                t.fade = std::min (1.0f, t.fade + t.fadeStep);
                const auto eased = t.fade * t.fade * (3.0f - 2.0f * t.fade);
                const auto x = eased * 0.5f * std::numbers::pi_v<float>;
                gainNow[l] = std::sin (x);
                gainThen[l] = std::cos (x);
            }
        }

        std::array<float, numBuses> bus {};
        std::array<float, 2> shiftBus {};
        const auto latency = shiftLatency.next();

        for (size_t l = 0; l < 2; ++l)
        {
            const auto& list = reads[l];
            const auto& ring = rings[l];

            for (int i = 0; i < list.count; ++i)
            {
                const auto& e = list.entries[(size_t) i];
                const auto gain = planGain (e, k);
                if (gain <= 0.0f)
                    continue;

                const auto offset = (double) e.read.offsetMs * msScale + (e.feedback ? feedbackOffset : 0.0);
                const auto fr = (double) e.read.fraction;
                // Output-only heads of a dense pattern pass once: the nearest tabulated fraction.
                const auto fast = ! e.feedback && list.count > 8;
                // The loop only reads samples whose limiting has arrived; outputs read anything.
                const auto least = e.feedback ? limitedDelay : 0.0;
                auto v = ring.read (std::max (fr * now[l] + offset, least), fast);
                if (fading[l])
                    v = gainNow[l] * v + gainThen[l] * ring.read (std::max (fr * then[l] + offset, least), fast);
                v *= gain;
                const auto weights = weightsAt (e, k);
                if (e.feedback)
                {
                    for (size_t b = 0; b < (size_t) numBuses; ++b)
                        bus[b] += v * weights[b];
                }
                else
                {
                    // An output read feeds nothing back (TapPlan): only the two output buses.
                    bus[outLeft] += v * weights[outLeft];
                    bus[outRight] += v * weights[outRight];
                }

                if (e.feedback && shiftActive)
                {
                    auto w = ring.read (std::max (fr * now[l] + offset - latency, least));
                    if (fading[l])
                        w = gainNow[l] * w + gainThen[l] * ring.read (std::max (fr * then[l] + offset - latency, least));
                    shiftBus[0] += w * gain * weights[feedback0];
                    shiftBus[1] += w * gain * weights[feedback1];
                }
            }
        }

        std::array<float, 2> fb { bus[feedback0], bus[feedback1] };
        if (shiftActive)
        {
            shiftMix = std::clamp (shiftMix + shiftMixStep, 0.0f, 1.0f);
            for (size_t d = 0; d < 2; ++d)
            {
                shiftHistory[d].write (shiftBus[d]);
                const auto shifted = shifter[d].process (shiftHistory[d], true);
                fb[d] += shiftMix * (shifted - fb[d]);
            }
        }

        float rec0, rec1;
        loop.step (k, fb[0], fb[1], bus[outLeft], bus[outRight], rec0, rec1);

        // The sample goes in as recorded; its limiting arrives Limiter::latency samples later.
        const std::array<float, 2> rec { rec0, rec1 };
        std::array<double, 2> residual;
        Limiter::processPair (limiters[0], limiters[1], (double) rec0, (double) rec1, residual[0], residual[1]);
        for (size_t l = 0; l < 2; ++l)
        {
            rings[l].write (rec[l] + (noiseLevel > 0.0f ? noiseLevel * noise[l].next() : 0.0f));
            rings[l].addAt (Limiter::latency + 1, (float) residual[l], safetyCeiling);
        }
    }
}

double CleanMachine::predictedPassGain (double, const MachineSettings&) const noexcept
{
    return (double) WindowedSinc::instance().dcGain();
}

float CleanMachine::currentTimeMs (int line) const noexcept
{
    const auto& t = times[(size_t) std::clamp (line, 0, 1)];
    return (float) ((t.glide.get() + t.jumpNow) * 1000.0 / fs);
}

} // namespace mulberry::dsp
