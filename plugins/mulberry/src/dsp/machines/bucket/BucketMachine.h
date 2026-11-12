#pragma once

#include "dsp/common/ClockedDomain.h"
#include "dsp/common/Modulation.h"
#include "dsp/machines/Machine.h"

#include <vector>

namespace mulberry::dsp
{

/**
    Bucket: a bucket-brigade delay chip with its anti-aliasing and reconstruction filters and an
    NE570-style compander, after Holters & Parker's combined model (DAFx-18, `ClockedDomain`) and
    Raffel & Smith's BBD circuit notes (DAFx-10).

    The clock is stages / (2 Time) (less the filters' delay), capped at 200 kHz; longer times on a
    shorter chip clock lower: darker, with the device's own aliasing and clock images. A time
    change moves the clock (a pitch glide). Wobble is the classic triangle LFO on the clock.

    Param A (Chip): 1024 / 2048 / 4096 / 8192 stages. Param B (Compander): none ... full (2:1
    before, 1:2 after); the chain's loss between them makes the expansion mistrack. The expander
    never undoes more than the compressor did one Time earlier, so the pair breathes but never
    gains around the loop (a compander inside a feedback loop otherwise pumps itself into
    oscillation below 100 %). See NOTES.md.
*/
class BucketMachine final : public EchoMachine
{
public:
    explicit BucketMachine (uint32_t seed = 1) : randomSeed (seed) {}

    static constexpr int modes = 2;                 // 4th-order Butterworth filters
    static constexpr double maxClock = 200000.0;
    static constexpr int maxBuckets = 1 << 15;      // per line: 2 x 8192 stages / 2, Feel, margin
    static constexpr double rail = 1.6;             // charge-transfer limit (record level)
    static constexpr double transferOffset = 0.12;  // its asymmetry (2nd harmonic)
    static constexpr double stageLoss = 2.0e-6;     // charge transfer inefficiency per stage
    static constexpr double leakSeconds = 20.0;     // charge leakage time constant
    static constexpr float wobbleHz = 0.55;         // x Wobble Rate
    static constexpr float wobbleDeviation = 0.002f;// peak pitch deviation at nominal Wobble
    static constexpr float noiseDb = -78.0f;

    /** The filters' cutoff for a chip: shorter chips were built for shorter times. */
    static double filterHz (int stages) noexcept;

    void prepare (double sampleRate, SlotMemory& memory) override;
    void reset() noexcept override;
    void process (LoopContext& loop, const std::array<ReadList, 2>& reads,
                  const MachineSettings& settings, int numSamples) noexcept override;
    double predictedPassGain (double hz, const MachineSettings& settings) const noexcept override;
    float currentTimeMs (int line) const noexcept override;

    /** The charge transfer: a soft limit with an offset operating point, slope <= 1. */
    static double transfer (double v) noexcept
    {
        return rail * (std::tanh ((v + transferOffset) / rail) - std::tanh (transferOffset / rail));
    }

    /** Clock for a time on a chip (Hz), and the filters' delay it allows for. */
    double clockFor (double timeSeconds, int stages) const noexcept;

    /**
        When the clock runs low for the filters, a pass carries images back into the band: the
        loop's gain, summed over the input's aliases and the output's images, can exceed 1 at a
        frequency even with every filter below 1. The device's output is scaled down by that
        excess, so a loop at 100 % still decays (long times on a short chip are a little quieter,
        as well as dark and gritty).
    */
    static double imageExcess (const ModalFilter<modes>& filter, double clock) noexcept;

private:
    struct Line
    {
        ClockedDomain<modes, numBuses> domain;
        std::vector<float> buckets;
        int write = 0;
        long long written = 0;
        double clock = 0.0;                 // gliding, before wobble
        float timeMs = 0.0f;
        float compressorEnv = 0.0f;
        std::array<float, numBuses> expanderEnv {};
        std::vector<float> compressorHistory;    // the compressor's gain per sub-block
        int historyWrite = 0;
        float compressorGain = 1.0f;
        double excessClock = -1.0, excess = 1.0;
        myzel::dsp::SmoothRandom drift;
        double lfoPhase = 0.0, modulation = 0.0;
        WhiteNoise noise;
        bool bleedSign = false;
    };

    void setChip (int stages) noexcept;

    uint32_t randomSeed;
    double fs = 48000.0;
    std::array<Line, 2> lines;
    int chip = 0;
    double filterDelay = 0.0;               // seconds, input + output filters at low frequency
    ModalFilter<modes> filter;
    double dualMix = 0.0;
    bool snapNext = true;
};

} // namespace mulberry::dsp
