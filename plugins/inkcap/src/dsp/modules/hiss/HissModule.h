#pragma once

#include "common/dsp/Filters.h"
#include "dsp/common/ValueNoise.h"
#include "dsp/modules/Module.h"
#include "dsp/modules/hiss/Crackle.h"
#include "dsp/modules/hiss/HissSources.h"

#include <array>
#include <cstdint>

namespace inkcap::dsp
{

/**
    Hiss, module 5: the noise floor of old media, synthesised from the seed and the song position
    (PLAN.md, Settled 10) and added to the signal after Bits.

    - Vinyl: surface noise and crackle (dust and scratches on a turning record) through the RIAA
      playback curve, and rumble.
    - Tape: hiss with a playback head's presence, its level breathing with Chaos.
    - Hum: 50 Hz mains hum and rectifier buzz (Tone moves between them), over an amplifier's hiss.

    Level follows Amount × Damage (hiss::bedLevelDb); Tone tilts the bed dark or bright; Follow
    ducks the noise under the input (-) or lets it ride on it (+). Every random value is a pure
    function of (seed, stream, timeline index), so the noise is the same from any start and for
    any block size; a new seed or a transport jump crossfades whatever has a smooth shape (hum,
    rumble, the wander), never clicks. Damage 0 adds exactly nothing. See NOTES.md.
*/
class HissModule
{
public:
    void prepare (const ModuleSpec&);
    void reset() noexcept;
    void process (const AudioBlock&, const HissSettings&, const BlockContext&) noexcept;

    int latencySamples() const noexcept { return 0; }
    float intensity() const noexcept { return lastIntensity; }
    float activity() const noexcept { return lastActivity; }

    /** Noise is not a tail: when the module stops, so does the noise (over its 20 ms fade). */
    static constexpr double maxTailSeconds = 0.0;

    static constexpr double glideSeconds = 0.02;        // level, Tone, Follow, Chaos: each of two poles
    static constexpr double presenceSeconds = 0.03;     // in from / out to intensity 0
    static constexpr double typeFadeSeconds = 0.04;     // a type change, equal power
    static constexpr double jumpFadeSeconds = 0.02;     // hum and rumble across a transport jump
    static constexpr double seedFadeSeconds = 0.25;     // hum and rumble into a new seed's wander

    /** The bed's gain now (tests). */
    double levelGain() const noexcept { return level.value * presence.current(); }

private:
    /** The wander of one seed: value noises the smooth parts are built from (streams hissRumble, hissWander). */
    struct Wander
    {
        void setSeed (uint32_t seed) noexcept;

        ValueNoise rumbleLateral, rumbleVertical, tapeLevel, humLevel, humDrift, humBuzz, humBalance;
        double swishPhase = 0.0;
    };

    /** What is a smooth function of the timeline (crossfaded across seeds and jumps). */
    struct Smooth
    {
        float rumbleLeft = 0.0f, rumbleRight = 0.0f;
        float swish = 1.0f;         // surface noise level over the turn
        float tapeLevel = 1.0f;
        float humLeft = 0.0f, humRight = 0.0f;
    };

    /** Two one-poles in series that land exactly on their target (so a gain of 0 is exactly 0). */
    struct Glide
    {
        double first = 0.0, value = 0.0, coefficient = 1.0;

        void jumpTo (double v) noexcept { first = value = v; }
        double next (double target) noexcept
        {
            first += (target - first) * coefficient;
            value += (first - value) * coefficient;
            if (std::abs (target - value) < 1.0e-9 && std::abs (target - first) < 1.0e-9)
                first = value = target;
            return value;
        }
        bool isAt (double target) const noexcept { return myzel::dsp::isExactly (value, target) && myzel::dsp::isExactly (first, target); }
    };

    struct ToneFilters
    {
        myzel::dsp::TptOnePole lowpass, highpass, shelf;
        double shelfGain = 1.0;
        void reset() noexcept { lowpass.reset(); highpass.reset(); shelf.reset(); }
        float process (float x) noexcept
        {
            const auto y = highpass.processHighpass (lowpass.processLowpass (x));
            return (float) (y + (shelfGain - 1.0) * shelf.processHighpass (y));
        }
    };

    struct TapeShaping
    {
        myzel::dsp::TptOnePole lowCut;
        myzel::dsp::TptSvf presence, top;
        void reset() noexcept { lowCut.reset(); presence.reset(); top.reset(); }
        double process (double x) noexcept;
    };

    Smooth smoothAt (int64_t t, Wander&, double chaos, double buzzShare, std::array<bool, 3> running) noexcept;
    void followTimeline (const BlockContext&, int64_t first, const hiss::Crackle::Wear&) noexcept;
    void followSeed (uint32_t seed) noexcept;
    void startBlend (double seconds) noexcept;
    void setToneFilters (double tone) noexcept;
    void wake (int64_t t) noexcept;
    hiss::Crackle::Wear wearFor (float intensity, double chaos) const noexcept;

    double fs = 48000.0;
    int channels = 2;

    OnOffFade onOff;
    Fade presence;                          // 0 at intensity 0: lands exactly, so Damage 0 adds exactly nothing
    Glide level, tone, follow, chaos;
    std::array<Fade, 3> typeFade;
    std::array<bool, 3> running {};
    HissType currentType = HissType::vinyl;
    bool primed = false, awake = false;

    // The timeline: index = processing index + offset (song position in samples, per segment).
    int64_t offset = 0, blendOffset = 0;
    bool timelineKnown = false;

    uint32_t seed = 0;
    bool seedKnown = false;
    Wander wander, blendWander;
    Fade blend;                             // 0 -> 1: from (blendWander, blendOffset) to the current
    std::array<SeedRng, 6> noise { SeedRng (0, Stream::hissNoise), SeedRng (0, Stream::hissNoise), SeedRng (0, Stream::hissNoise),
                                   SeedRng (0, Stream::hissNoise), SeedRng (0, Stream::hissNoise), SeedRng (0, Stream::hissNoise) };

    hiss::Crackle crackle;
    hiss::RiaaPlayback riaaLeft, riaaRight;
    hiss::HumSource hum;
    std::array<TapeShaping, 2> tape;
    std::array<myzel::dsp::TptOnePole, 2> amplifier;     // Hum's floor
    std::array<ToneFilters, 2> toneFilters;
    double appliedTone = 2.0;                            // what the tone filters are set to (2: not yet)
    float envelope = 0.0f, attack = 1.0f, release = 1.0f;

    float vinylTrim = 1.0f, tapeTrim = 1.0f, amplifierTrim = 1.0f;
    float lastIntensity = 0.0f, lastActivity = 0.0f;
};

static_assert (ChainModule<HissModule, HissSettings>);

/** The voicing (by measurement, M3): levels and shapes from Amount × Damage, Tone and Chaos. */
namespace hiss
{
    /** The bed's RMS (dBFS) at intensity i > 0: -62 dB at the first touch, -22 dB at full. */
    inline double bedLevelDb (double i) noexcept { return -62.0 + 40.0 * std::pow (std::clamp (i, 0.0, 1.0), 0.6); }

    /** The bed's gain: exactly 0 at intensity 0, fading in along the fade shape below 5 %. */
    inline double bedGain (double i) noexcept
    {
        if (i <= 0.0)
            return 0.0;
        return std::pow (10.0, bedLevelDb (i) / 20.0) * fadeShape (i / 0.05);
    }

    /** Dust events per second: a few at a touch, ~260 at full (most of them tiny). */
    inline double crackleRate (double i) noexcept { return 4.0 + 260.0 * std::pow (std::clamp (i, 0.0, 1.0), 1.6); }

    /** Hum's buzz share from Tone (glided) before Chaos adds its bursts: dark is pure hum. */
    inline double buzzShare (double tone) noexcept { return std::clamp (0.3 + 0.7 * tone, 0.0, 1.0); }

    /** Tone: dark closes the top to 3.3 kHz; bright lifts everything above 3 kHz by up to 6 dB and thins the bottom to 80 Hz. */
    inline double toneLowpassHz (double tone) noexcept { return 16000.0 * std::pow (2.0, tone < 0.0 ? 2.3 * tone : tone); }
    inline double toneHighpassHz (double tone) noexcept { return 20.0 * std::pow (2.0, 2.0 * tone); }
    inline double toneShelfGain (double tone) noexcept { return std::pow (2.0, std::max (tone, 0.0)); }
    constexpr double toneShelfHz = 3000.0;

    constexpr double rumbleLevel = 0.8;         // re the bed, mostly below 30 Hz
    constexpr double humFloor = 0.04;           // the amplifier's hiss under the hum (-28 dB)
}

} // namespace inkcap::dsp
