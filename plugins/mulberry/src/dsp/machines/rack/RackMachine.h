#pragma once

#include "dsp/common/ClockedDomain.h"
#include "dsp/common/Interpolators.h"
#include "dsp/common/Modulation.h"
#include "dsp/machines/Machine.h"

namespace mulberry::dsp
{

/**
    Rack: an early rack digital delay. An op-amp front end, an anti-aliasing filter (cutoff =
    Rate), sampling at the unit's own clock (2.4 x Rate) through the shared ClockedDomain, a
    gain-ranging converter (Bits of mantissa, four 6 dB gain steps chosen syllabically from a peak
    detector with hold, so the noise floor rides the signal), memory, a zero-order-hold DAC and a
    reconstruction filter. Every repeat is converted again, so grain accumulates down the trail.

    Time jumps with a 10 ms crossfade (the units of the era did not glide); Wobble is a sine LFO
    on the memory address (read between words, as the later units did).

    Param A (Bits): 8 ... 16. Param B (Rate): bandwidth 6 ... 20 kHz. See NOTES.md.
*/
class RackMachine final : public EchoMachine
{
public:
    explicit RackMachine (uint32_t seed = 1) : randomSeed (seed) {}

    static constexpr int modes = 2;                 // 4th-order Butterworth filters
    static constexpr double clockPerBandwidth = 2.4;
    static constexpr float frontEndLimit = 2.0f;    // op-amp rails (record level)
    static constexpr float crossfadeMs = 10.0f;
    static constexpr float wobbleHz = 0.4f;
    static constexpr float wobbleDeviation = 0.0008f;
    static constexpr float noiseDb = -86.0f;
    static constexpr float holdMs = 30.0f;          // gain ranging: the syllabic hold

    void prepare (double sampleRate, SlotMemory& memory) override;
    void reset() noexcept override;
    void process (LoopContext& loop, const std::array<ReadList, 2>& reads,
                  const MachineSettings& settings, int numSamples) noexcept override;
    double predictedPassGain (double hz, const MachineSettings& settings) const noexcept override;
    float currentTimeMs (int line) const noexcept override;

    /** The converter: Bits of mantissa at a gain-range step (0 .. 3: x1 .. x8), clipping at full scale. */
    static float convert (double v, int bits, int range) noexcept
    {
        const auto q = std::ldexp (1.0, bits - 1);
        const auto g = std::ldexp (1.0, range);
        const auto x = std::clamp (v * g, -1.0, 1.0 - 1.0 / q);
        return (float) (std::round (x * q) / q / g);
    }

private:
    struct Line
    {
        ClockedDomain<modes, numBuses> domain;
        MirroredRing memory;
        double delayNow = 0.0, delayFrom = 0.0;     // words (before the filters' delay)
        double lastTarget = -1.0;
        float fade = 1.0f, fadeStep = 0.0f;
        int range = 0, holdCount = 0;
        float peak = 0.0f;
        double previousIn = 0.0, previousResidual = 0.0;
        myzel::dsp::SineLfo lfo;
        WhiteNoise noise;
    };

    void setRate (double hz) noexcept;
    float frontEnd (Line& line, float x) noexcept;

    uint32_t randomSeed;
    double fs = 48000.0;
    std::array<Line, 2> lines;
    double rateHz = 0.0, clock = 0.0, filterDelay = 0.0;
    ModalFilter<modes> filter;
    double dualMix = 0.0;
    bool snapNext = true;
    std::array<float, 2> timeMs {};
};

} // namespace mulberry::dsp
