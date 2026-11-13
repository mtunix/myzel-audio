#pragma once

#include "dsp/common/CaptureBuffer.h"
#include "dsp/common/ValueNoise.h"
#include "dsp/modules/Module.h"

#include <cstdint>

namespace inkcap::dsp
{

/**
    Warble, module 2: pitch wander from a short modulated delay (PLAN.md, The modules).

    The read head rests restSamples behind the input, where every read is the flat windowed sinc,
    and swings from there: delay = rest + centre + depth * m (t), with m in [-1, 1] a sine at the
    Rate (free, or a synced note value) mixed with value noise by Chaos (SeedRng::smooth, so the
    wander is the same from any start). Depth follows Amount x Damage; the centre follows it at no
    more than 0.5 % of the speed, so a depth change bends the pitch by at most that, and a module
    that comes on starts from the rest. Spread gives the right channel its own phase and wander.

    The rest is the module's latency, constant, on or off. Damage 0 and Off are exact: the head
    reads the input restSamples back to the bit. On/Off crossfades over 20 ms. See NOTES.md.
*/
class WarbleModule
{
public:
    void prepare (const ModuleSpec&);
    void reset() noexcept;
    void process (const AudioBlock&, const WarbleSettings&, const BlockContext&) noexcept;

    int latencySamples() const noexcept { return restSamples; }
    float intensity() const noexcept { return lastIntensity; }
    float activity() const noexcept { return lastActivity; }
    static constexpr double maxTailSeconds = 0.03;

    /** Where the head rests: the first delay the windowed sinc reads without looking ahead (NOTES.md, the rest). */
    static constexpr int restSamples = CaptureBuffer::sincFrom + 1;

    /** The LFO's frequency (Hz) and the head's delay in samples now (tests, tools). */
    double lfoHz() const noexcept { return hz; }
    double centreSamples() const noexcept { return centre; }

    static constexpr double longestSwingSeconds = 0.015;    // depth and centre each: the delay stays within the plan's 30 ms
    static constexpr double centreSlope = 0.005;            // the centre moves at most this fraction of the speed
    static constexpr double depthSeconds = 0.03;            // depth's glide (each of its two poles)
    static constexpr double shapeSeconds = 0.03;            // Chaos's and Spread's glide
    static constexpr double seedFadeSeconds = 0.25;         // a new seed's wander fades in

private:
    void anchorPhase (const GridClock&, int64_t sample, double noteBeats, bool sync) noexcept;

    double fs = 48000.0;
    int channels = 2;
    CaptureBuffer capture;
    OnOffFade onOff;

    bool resting = true, primed = false;
    double phase = 0.0, shift = 0.0, noisePosition = 0.0;
    /** Two one-poles in series: a glide whose slope never steps (a step in slope would be a step in pitch). */
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

    Glide depth, share, spread;
    double centre = 0.0, swingNow = 0.0;
    WarbleType active = WarbleType::wow;    // the type playing; a new one waits for the head to come home
    bool jumpShape = true;
    double hz = 0.0;
    bool lastSync = false;
    int lastNote = -1;
    double lastDelay = 0.0;

    uint32_t seed = 0;
    ValueNoise wander, wanderRight, oldWander, oldWanderRight;     // streams warbleDrift, warbleSpread
    Fade seedFade;
    bool seedKnown = false;

    float lastIntensity = 0.0f, lastActivity = 0.0f;
};

static_assert (ChainModule<WarbleModule, WarbleSettings>);

} // namespace inkcap::dsp
