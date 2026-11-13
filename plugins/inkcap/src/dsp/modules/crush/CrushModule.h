#pragma once

#include "dsp/common/ValueNoise.h"
#include "dsp/modules/Module.h"
#include "dsp/modules/crush/CrushStages.h"

#include "common/dsp/Oversampler2x.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace inkcap::dsp
{

/**
    Crush, module 3: saturation at 2x (PLAN.md, The modules): Tape and Cassette (a Jiles-Atherton
    coating and a head's losses), Valve (one triode stage at circuit level), Fuzz (our ADAA
    transistor clipper). Per channel, inside the 2x polyphase IIR pair:

        up 2x -> split -> drive -> the type's stage (normalised to unity small-signal gain)
              -> + the band above the split -> the type's colour -> down 2x -> Tone tilt

    Damage scales everything with the intensity (Amount x Damage): the drive in dB, the tape's
    wear, the Tone's tilt. Off and Damage 0 take the integer-delayed clean path, exactly: a ring of
    pairLatencySamples() (3), so the latency never changes, and the module fades between the two
    over 20 ms, as between types. At rest on the clean path nothing else runs. See NOTES.md.
*/
class CrushModule
{
public:
    void prepare (const ModuleSpec&);
    void reset() noexcept;
    void process (const AudioBlock&, const CrushSettings&, const BlockContext&) noexcept;

    int latencySamples() const noexcept { return latency; }
    float intensity() const noexcept { return lastIntensity; }
    float activity() const noexcept { return lastActivity; }
    static constexpr double maxTailSeconds = 0.0;

    /** The 2x polyphase IIR pair's low-frequency group delay (3.07 samples at any rate), rounded. */
    static int pairLatencySamples();

    /**
        Per sample of the last block: how much of the output came through the 2x pair (0 .. 1), or
        -1 where the module took its clean ring. The chain gives Mix's dry the same pair
        (InkcapChain, MatchedDry), so a partial Mix does not comb against the pair's phase.
    */
    const float* pairShares() const noexcept { return shares.data(); }

    /**
        Per sample of the last block: the tape head's group delay at headDelayHz (host samples; 0
        for Valve and Fuzz), which Mix's dry takes too, so a partial Mix of a worn head does not
        comb in the top octaves (NOTES.md, Mix).
    */
    const float* colourDelays() const noexcept { return delays.data(); }
    static constexpr double headDelayHz = 2000.0;

    static constexpr double glideSeconds = 0.02;    // intensity, Drive, Tone and Chaos
    static constexpr double typeFadeSeconds = 0.02;
    static constexpr int updateInterval = 8;        // the head's losses follow the intensity on this grid of the timeline
    static constexpr int sleepSamples = 4096;       // silence in and below sleepLevel out for this long: rest
    static constexpr float sleepLevel = 1.0e-7f;

    /** Chaos: the drive wanders this far either way (dB) at full Chaos and intensity: a machine whose level will not hold. */
    static constexpr double driftDb = 4.0;
    static constexpr double driftLatticeHz = 0.7;   // value noise of the song time (stream crushDrift)
    static constexpr double seedFadeSeconds = 0.25; // a new seed's wander fades in

private:
    struct Channel
    {
        myzel::dsp::Oversampler2x pair;
        crush::TapeStage tape { crush::tapeVoicing(), crush::voicings[0] };
        crush::TapeStage cassette { crush::cassetteVoicing(), crush::voicings[1] };
        crush::ValveStage valve { crush::voicings[2] };
        crush::FuzzStage fuzz { crush::voicings[3] };
        crush::Lowpass1 tilt;
        std::array<float, 8> clean {};      // the integer-delayed clean path, a ring of `latency`

        void prepare (double oversampledRate, double sampleRate);
        void resetStages() noexcept;
        void update (int type, double intensity) noexcept;
        void flush (int type) noexcept;
        double run (int t, double x, const crush::StageFrame& f, crush::Wear& w) noexcept
        {
            switch (t)
            {
                case 0:  return tape.process (x, f, w);
                case 1:  return cassette.process (x, f, w);
                case 2:  return valve.process (x, f, w);
                default: return fuzz.process (x, f, w);
            }
        }

    };

    /** A two-pole glide (two one-poles in series): its slope never steps. */
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

    /** exp (x), recomputed only when x moves: a settled glide costs nothing. */
    struct CachedExp
    {
        double x = 0.0, value = 1.0;

        double at (double v) noexcept
        {
            if (! myzel::dsp::isExactly (v, x))
            {
                x = v;
                value = std::exp (v);
            }
            return value;
        }
    };

    void startType (int type) noexcept;
    double headDelay (int type, double intensity) noexcept;
    static double drivenDb (int type, double intensity, double position, double wanderDb) noexcept;
    void followSeed (uint32_t seed) noexcept;
    double wanderDb (const GridClock&, int64_t sample, double chaos, double intensity, float seedMix) noexcept;

    double fs = 48000.0;
    int channelCount = 2, latency = 3, cleanIndex = 0;
    std::array<Channel, 2> channels;
    std::vector<float> shares, delays;
    std::array<double, 4> typeDelay {}, typeDelayIntensity { -1.0, -1.0, -1.0, -1.0 };
    double currentDelay = 0.0, previousDelay = 0.0;     // the heads' delays, on the update grid

    OnOffFade wet;                          // on and above Damage 0
    Fade typeFade;
    int type = 0, previousType = -1;
    bool primed = false, stagesLive = false, updateNow = true, asleep = false;
    int quietSamples = 0;
    Glide intensityGlide, driveGlide, toneGlide, chaosGlide;
    ValueNoise drift, oldDrift;
    Fade driftSeed;                         // 0 -> 1: from the old seed's wander to the new one's
    double wander = 0.0;                    // the drive's wander (dB), held on the update grid
    uint32_t seed = 0;
    bool seedKnown = false;
    double tiltCoefficient = 0.0;
    CachedExp driveGain, previousDriveGain, tiltGain;

    float lastIntensity = 0.0f, lastActivity = 0.0f;
};

static_assert (ChainModule<CrushModule, CrushSettings>);

} // namespace inkcap::dsp
