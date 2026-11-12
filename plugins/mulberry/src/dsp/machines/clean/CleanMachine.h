#pragma once

#include "common/dsp/GrainVoice.h"
#include "dsp/common/Interpolators.h"
#include "dsp/common/Oversampler2x.h"
#include "dsp/common/Modulation.h"
#include "dsp/machines/Machine.h"

namespace mulberry::dsp
{

/**
    Clean: ideal memory. Windowed-sinc reads, a record stage that writes the sample as recorded
    and adds its limiting (a tanh ceiling, computed at 8x, linear phase) a few samples later, a
    sine/random chorus as Wobble.

    Param A (Glide): how a Time change happens. 0 jumps by crossfading two read heads (40 ms, no
    pitch), 1 glides like a tape transport; in between, that share of the change jumps and the
    rest glides.
    Param B (Shift): the feedback runs through a granular shifter (Cerulean Halo's grain voice),
    so each repeat climbs or falls by the interval. 0 bypasses it exactly.

    See NOTES.md.
*/
class CleanMachine final : public EchoMachine
{
public:
    explicit CleanMachine (uint32_t seed = 1) : randomSeed (seed) {}

    void prepare (double sampleRate, SlotMemory& memory) override;
    void reset() noexcept override;
    void process (LoopContext& loop, const std::array<ReadList, 2>& reads,
                  const MachineSettings& settings, int numSamples) noexcept override;
    double predictedPassGain (double hz, const MachineSettings& settings) const noexcept override;
    float currentTimeMs (int line) const noexcept override;

    static constexpr float limit = 3.0f;            // record-level ceiling (+9.5 dBFS)
    static constexpr float safetyCeiling = 5.0f;    // bounds what the band-limited residual leaves (never at playing level)
    static constexpr float crossfadeMs = 40.0f;
    static constexpr float wobbleHz = 0.8f;         // x Wobble Rate
    static constexpr float wobbleDeviation = 0.001f;// peak pitch deviation at nominal Wobble
    static constexpr float wobbleRandom = 0.25f;    // nominal share of the random part
    static constexpr float noiseDb = -100.0f;       // nominal floor (RMS)
    static constexpr float shiftFadeMs = 30.0f;

    /** The record stage's transfer: a tanh scaled to the limit. */
    static double clip (double x) noexcept
    {
        const auto e = std::exp (-2.0 * std::abs (x) / (double) limit);
        return std::copysign ((double) limit * (1.0 - e) / (1.0 + e), x);
    }

    /**
        The record stage's limiting, added after the fact: the residual clip(x) - x computed at
        8x through linear-phase half-bands, so it lines up exactly with the sample it belongs to
        (latency host samples ago). The written sample itself is never filtered.
    */
    class Limiter
    {
    public:
        void reset() noexcept
        {
            outer.reset();
            mid.reset();
            inner.reset();
            align = 0.0;
        }

        /**
            The residuals of two lines for the samples recorded `latency` samples ago (the lines
            are independent, so the processor overlaps them).
        */
        static void processPair (Limiter& a, Limiter& b, double xa, double xb, double& ra, double& rb) noexcept
        {
            std::array<double, 2> twiceA, twiceB;
            a.outer.upsample (xa, twiceA[0], twiceA[1]);
            b.outer.upsample (xb, twiceB[0], twiceB[1]);
            for (size_t i = 0; i < 2; ++i)
            {
                std::array<double, 2> fourA, fourB;
                a.mid.upsample (twiceA[i], fourA[0], fourA[1]);
                b.mid.upsample (twiceB[i], fourB[0], fourB[1]);
                for (size_t j = 0; j < 2; ++j)
                {
                    fourA[j] = a.eightTimes (fourA[j]);
                    fourB[j] = b.eightTimes (fourB[j]);
                }
                twiceA[i] = a.mid.downsample (fourA[0], fourA[1]);
                twiceB[i] = b.mid.downsample (fourB[0], fourB[1]);
            }
            ra = a.outer.downsample (twiceA[0], twiceA[1]);
            rb = b.outer.downsample (twiceB[0], twiceB[1]);
        }

        static constexpr int latency = 39;   // 28.5 + 8.25 + 2.125 + 0.125

    private:
        double eightTimes (double q) noexcept
        {
            double u0, u1;
            inner.upsample (q, u0, u1);
            // One 8x sample of delay makes the whole latency a whole number of host samples.
            const auto delayed = align;
            align = clip (u1) - u1;
            return inner.downsample (delayed, clip (u0) - u0);
        }

        LinearPhaseHalfBand<14> outer { 90.0 };
        LinearPhaseHalfBand<8> mid { 95.0 };
        LinearPhaseHalfBand<4> inner { 90.0 };
        double align = 0.0;
    };

private:
    /** A line's time: a glided part and a jumped part (crossfaded), as Glide splits a change. */
    struct LineTime
    {
        InertialGlide glide;    // samples
        double jumpNow = 0.0, jumpTarget = 0.0, jumpFrom = 0.0;
        float fade = 1.0f, fadeStep = 0.0f;
        double lastTarget = 0.0;
        bool fading() const noexcept { return fade < 1.0f; }
    };

    void updateTime (LineTime& t, double targetSamples, float glide, bool snap) noexcept;

    uint32_t randomSeed;
    double fs = 48000.0;
    std::array<MirroredRing, 2> rings;
    std::array<LineTime, 2> times;
    std::array<Limiter, 2> limiters;
    bool snapNext = true;

    // Wobble (one transport unless Dual)
    std::array<myzel::dsp::SineLfo, 2> lfo;
    std::array<myzel::dsp::SmoothRandom, 2> drift;
    std::array<float, 2> modNow {}, modStep {};
    float dualMix = 0.0f;

    std::array<WhiteNoise, 2> noise;

    // Shift
    std::array<myzel::dsp::DelayLine, 2> shiftHistory;
    std::array<myzel::dsp::GrainVoice, 2> shifter;
    float shiftMix = 0.0f, shiftMixStep = 0.0f;
    float shiftRatio = 1.0f;
    InertialGlide shiftLatency;     // samples: feedback reads move earlier by the shifter's latency
};

} // namespace mulberry::dsp
