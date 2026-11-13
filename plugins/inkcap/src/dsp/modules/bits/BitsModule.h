#pragma once

#include "dsp/common/ValueNoise.h"
#include "dsp/modules/Module.h"
#include "dsp/modules/bits/BitsVoicing.h"

#include "common/dsp/ClockedDomain.h"

#include <array>
#include <cstdint>
#include <vector>

namespace inkcap::dsp
{

/**
    Bits, module 4: a sampler's converter, or a phone line (PLAN.md, The modules). Per channel:

        (Phone: 300 Hz low cut) -> anti-alias filter -> sampled at the converter's own clock
        -> quantiser (linear, or mu-law for Phone) -> zero-order hold -> reconstruction filter

    The filters and the sampling are the Holters-Parker clocked domain (common/dsp/ClockedDomain,
    validated in Mulberry's Bucket tests): the converter samples the input filter's exact output
    at its own instants and its held staircase drives the output filter, so it aliases and images
    exactly as such a sampler does.

    Damage moves the converter from transparent to its setting: the clock from twice the host
    rate down to Rate (log-linear in the intensity), the depth from 24 bits down to Depth, the
    filters with the clock (never above 0.4 of the host rate). The clock is tied to the timeline
    (its phase anchored to the song position, its jitter value noise of the song time, stream
    bitsJitter), so a render from bar 17 samples at the instants a render from bar 1 did. Off
    and Damage 0 pass the input untouched, to the bit; the module fades over 20 ms, and ducks for
    10 ms around a type change. The converter adds its filters' delay and one host sample (the
    domain reads its input at the end of each period), as a real one would; latencySamples() is
    0. See NOTES.md.
*/
class BitsModule
{
public:
    void prepare (const ModuleSpec&);
    void reset() noexcept;
    void process (const AudioBlock&, const BitsSettings&, const BlockContext&) noexcept;

    int latencySamples() const noexcept { return 0; }
    float intensity() const noexcept { return lastIntensity; }
    float activity() const noexcept { return lastActivity; }
    static constexpr double maxTailSeconds = 0.0;

    /**
        Per sample of the last block: the converter's share of the output (0 .. 1), or -1 where the
        module passed its input untouched; and the converter's delay (host samples) against its
        input there. The chain delays Mix's dry by that share of it (InkcapChain, MatchedDry).
    */
    const float* converterShares() const noexcept { return shares.data(); }
    const float* converterDelays() const noexcept { return delays.data(); }

    /**
        The converter's delay in host samples: half a clock period (the hold) and the two filters'
        group delay at low frequencies, stretched 16 % so that their phase near the cutoff is
        matched too (NOTES.md, Mix).
    */
    static double converterDelay (double sampleRate, double clockHz, double cutoffHz) noexcept;

    /** The longest converterDelay() any setting gives, in seconds (Phone at its lowest Rate). */
    static constexpr double maxConverterDelaySeconds = 0.004;

    /** The converter's clock (Hz) and depth (bits) at the last sample, for tests and the display. */
    double clockNow() const noexcept { return clock; }
    double bitsNow() const noexcept { return resolution; }

    static constexpr int modes = 2;                 // 4th-order Butterworth filters
    static constexpr double glideSeconds = 0.02;
    static constexpr double swapSeconds = 0.01;     // the duck around a type change, each way
    /** The domain takes the host's samples as impulses: filters open above the host's Nyquist would pass their images. */
    static constexpr double maxCutoffPerRate = 0.4;
    static constexpr int clockInterval = 4;         // the clock follows its glide and jitter on this grid
    static constexpr int filterInterval = 32;       // the filters follow the clock on this grid

    /** The converter: v quantised to `step` (linear) or to `step` in the mu-law domain. */
    static double quantise (double v, double step, bool mulaw) noexcept;

private:
    using Domain = myzel::dsp::ClockedDomain<modes, 1>;

    struct LowCut
    {
        double g = 0.0, k = 1.4142135623730951, a1 = 1.0, a2 = 0.0, a3 = 0.0, ic1 = 0.0, ic2 = 0.0;

        void setup (double hz, double sampleRate) noexcept;
        double process (double x) noexcept;
        void reset() noexcept { ic1 = ic2 = 0.0; }
    };

    struct Channel
    {
        Domain domain;
        LowCut lowCut;
    };

    struct Glide
    {
        double first = 0.0, value = 0.0, coefficient = 1.0;

        void jumpTo (double v) noexcept { first = value = v; }
        double next (double target) noexcept
        {
            first += (target - first) * coefficient;
            value += (first - value) * coefficient;
            return value;
        }
    };

    /** The clock without jitter; kept while intensity, Rate and type stay put (a settled glide). */
    double nominalClock (double intensity, double ratePosition) noexcept;
    double cutoff (double clockHz) const noexcept;
    double jitterAt (const GridClock&, int64_t sample, double amount) noexcept;
    void restart (const GridClock&, int64_t sample, double amount) noexcept;

    double fs = 48000.0;
    int channelCount = 2;
    std::array<Channel, 2> channels;
    std::vector<float> shares, delays;
    double delayClock = -1.0, delayFilterClock = -1.0, delayNow = 0.0;

    OnOffFade wet;
    Fade swap;
    BitsType type = BitsType::sampler12, wanted = BitsType::sampler12;
    bool primed = false, live = false, anchorNow = true;
    Glide intensityGlide, rateGlide, depthGlide, jitterGlide;

    double clock = 0.0, resolution = 24.0, step = 0.0, filterClock = 0.0, lowCutHz = -1.0;
    struct ClockKey
    {
        double intensity = -1.0, rate = -1.0;
        BitsType type = BitsType::sampler12;
        double hz = 0.0;
    } clockKey;
    uint32_t seed = 0;
    bool seedKnown = false;
    ValueNoise jitter;

    float lastIntensity = 0.0f, lastActivity = 0.0f;
};

static_assert (ChainModule<BitsModule, BitsSettings>);

} // namespace inkcap::dsp
