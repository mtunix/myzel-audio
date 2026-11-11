#pragma once

#include "dsp/Engine.h"
#include "dsp/common/Fdn.h"

namespace cerulean::dsp
{

/**
    Rise: volume swells. Notes fade in with no attack, into a lush, slow cathedral.

    in -> onset detector (on the undelayed input) ----------------------------+ trigger
       -> bandwidth LP -> 5 ms wet look-ahead -> 4-stage diffuser per side     |
       -> routing (2 ms crossfade on a trigger) -> tank A / tank B (16-line FDNs)
       -> each tank x its own swell gain (S-curve over Rise time) --------------+-> out

    The swell sits at each tank's *output*, and every new note is routed to the quieter of two
    tanks, whose output then swells in. So a new note fades in on top of the previous note's
    tail, which keeps ringing untouched (a single tank with an input-side swell would also leave
    old tails alone, but erases impulses and clicks completely, so its tail could never be
    measured; a single tank with an output swell would duck the old tail on every new note).

    Param A (Rise time): swell time, maps::riseTimeMs (50 ms .. 4 s).
    Param B (Blend):     0 = only the wet swells, 1 = the dry swells as well (getDryGain()).
    See NOTES.md for the detector, the curve and the measurements.
*/
class RiseEngine final : public Engine
{
public:
    explicit RiseEngine (uint32_t randomSeed = 1) : seed (randomSeed) {}

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept override;

    const float* getDryGain() const noexcept override { return dryGain.data(); }

    void setDetectorInput (const float* left, const float* right) noexcept override
    {
        detectorL = left;
        detectorR = right;
    }

    /** Swell shape: 0 at p = 0, 1 at p = 1, flat at both ends (smoothstep squared). */
    static float swellCurve (float p) noexcept
    {
        p = std::clamp (p, 0.0f, 1.0f);
        const auto s = p * p * (3.0f - 2.0f * p);
        return s * s;
    }

    /** Number of note onsets detected since reset() (for tests and metering). */
    int getTriggerCount() const noexcept { return triggerCount; }

private:
    static constexpr int lines = 16;

    /**
        A swell gain: on a trigger it ducks whatever it held (fast exponential) while the
        S-curve rises over the Rise time, so it never jumps. Optionally closes slowly when the
        note has died away (the dry: a slow-gear style gate, so the next attack is hidden).
    */
    struct Swell
    {
        float value = 0.0f, start = 0.0f, duck = 0.0f, phase = 1.0f;
        bool rising = false, closing = false;

        void reset() noexcept { *this = {}; }

        void trigger() noexcept
        {
            start = value;
            duck = 1.0f;
            phase = 0.0f;
            rising = true;
            closing = false;
        }

        /** inc: phase per sample, duckK / closeK: per-sample decay factors, already freeze-scaled. */
        float next (float inc, float duckK, float closeK) noexcept
        {
            if (closing)
            {
                value *= closeK;
                return value;
            }
            if (rising)
            {
                phase = std::min (1.0f, phase + inc);
                duck *= duckK;
                const auto held = start * duck;
                value = held + (1.0f - held) * swellCurve (phase);
                if (phase >= 1.0f && held < 1.0e-6f)
                {
                    rising = false;
                    value = 1.0f;
                }
            }
            return value;
        }
    };

    /**
        Note-onset detector: a fast envelope must jump +9 dB above its running baseline (and a
        -60 dBFS floor) to trigger; it re-arms once the note has fallen 12 dB below its peak (or
        to the floor), never sooner than 50 ms. See NOTES.md.
    */
    struct OnsetDetector
    {
        float attack = 0.0f, release = 0.0f, baseCoeff = 0.0f, peakRelease = 0.0f, gate = 0.0f;
        int refractory = 0;

        float env = 0.0f, baseline = 0.0f, peak = 0.0f;
        int since = 0;
        bool armed = true;

        struct Event
        {
            bool rearm = false, trigger = false;
        };

        void reset() noexcept
        {
            env = baseline = peak = 0.0f;
            since = 0;
            armed = true;
        }

        Event process (float level) noexcept;
    };

    uint32_t seed;
    double fs = 48000.0;

    // Detectors: the wet one listens to the (pre-delayed) engine input, which is when the note
    // reaches the tanks; the dry one to the player's undelayed signal (setDetectorInput), so the
    // dry swell starts with the pick whatever the pre-delay.
    OnsetDetector wetDetector, dryDetector;
    const float* detectorL = nullptr;
    const float* detectorR = nullptr;
    float wetDuckK = 0.0f, dryDuckK = 0.0f, dryCloseK = 0.0f, energyK = 0.0f;   // per-sample decay factors
    int triggerCount = 0;

    // Wet path
    OnePole bandwidthL, bandwidthR;
    DelayLine lookAheadL, lookAheadR;
    int lookAheadSamples = 1;
    AllpassChain<6> diffuserL, diffuserR;
    std::array<Fdn<lines>, 2> tanks;
    std::array<Swell, 2> tankSwell;
    std::array<float, 2> tankEnergy {};
    int currentTank = 0;
    float route = 0.0f, routeTarget = 0.0f, routeStep = 0.0f;   // 0 = all to tank A, 1 = tank B
    LinearRamp diffusionGain, blendRamp, freezeRamp;

    // Dry
    Swell drySwell;
    std::array<float, maxSubBlock> dryGain {};
};

} // namespace cerulean::dsp
