#pragma once

#include "dsp/models/EqModel.h"

#include "common/dsp/OpAmpClip.h"

#include <array>
#include <complex>
#include <memory>

namespace viridian::dsp
{

/**
    Console: the transformerless console channel EQ, early or later card on Alt (NOTES.md). Three
    stages: the line input amp (an NE5534 into its rails, 2x, its clip at 8x), the card's network
    (both curve families, host rate) and the card's summing amp at its output (the same). Switch changes (shapes,
    filters in or out, the card, EQ In, Stereo, Tolerance on or off) build the idle one of two
    pre-allocated sets of networks, one bounded design step per stepSamples, run it unheard for
    switchSeconds, then crossfade to it over switchSeconds.
*/
class ConsoleModel final : public EqModel
{
public:
    ConsoleModel();
    ~ConsoleModel() override;

    void prepare (double hostRate, int maxBlock, int numChannels) override;
    void build() override;
    void beginBuild() noexcept override;
    bool ready() const noexcept override;
    void reset() noexcept override;
    void setSettings (const ModelSettings&) noexcept override;
    std::span<Stage* const> stages() noexcept override { return list; }

    /** Input sample 1.0 (0 dBFS) in volts peak: +18 dBu (EBU R68). */
    static double voltsPerUnit() noexcept;

    /**
        The NE5534's figures with the cards' 22 pF compensation (NOTES.md, "Amplifier"): open-loop
        gain at 10 kHz, output swing at +-18 V into 600 ohm or more, slew rate.
    */
    static constexpr double openLoopGain = 2200.0, swing = 16.0, slewRate = 6.0e6;

    /** The channel's input amp: a follower, the whole open-loop gain in its loop. */
    static myzel::dsp::LineAmpParams inputAmpParams() noexcept;

    /** T8, the card's output: an inverting summing amp, a quarter of it. */
    static myzel::dsp::LineAmpParams summingAmpParams() noexcept;

    /** The hiss and hum at Noise 100 %, in dBu (NOTES.md, "Implementation"). */
    static constexpr double hissDbu = -90.0, humDbu = -100.0, thirdDbu = -106.0, humHz = 50.0;

    /** Pot spread at Tolerance 100 %: +-20 %, carbon pots. */
    static constexpr double maxSpread = 0.2;

    static constexpr double switchSeconds = 0.01;

    /** A switch change or a model change takes one bounded design step per stepSamples (NOTES.md, "Switches"). */
    static constexpr int stepSamples = 32;

    /** The network as it runs now (channel 0 of the active voice): realised and analog (tests). */
    std::complex<double> networkResponse (double hz) const noexcept;
    std::complex<double> networkAnalogResponse (double hz) const noexcept;

    /** True while a switch change crossfades. */
    bool isSwitching() const noexcept;

private:
    class AmpStage;
    class NetworkStage;

    std::unique_ptr<AmpStage> input, output;
    std::unique_ptr<NetworkStage> network;
    std::array<Stage*, 3> list {};
};

} // namespace viridian::dsp
