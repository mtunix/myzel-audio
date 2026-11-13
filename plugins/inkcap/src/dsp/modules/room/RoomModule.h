#pragma once

#include "dsp/common/ValueNoise.h"
#include "dsp/modules/Module.h"
#include "dsp/modules/room/RoomSpaces.h"

#include <array>
#include <cstdint>

namespace inkcap::dsp
{

/**
    Room, module 6, last: the small space the whole worn sound sits in (PLAN.md, The modules).

    Closet, Room, Spring and Hall (RoomSpaces.h), from Cerulean's blocks in common/dsp. Amount ×
    Damage moves the source into the space: the wet rises to its type's level while the dry
    steps back by up to 3 dB. Size is the space's dimension (Spring: its decay), Duck lowers the
    room while the input plays. The spaces run in fixed 32-sample sub-blocks aligned to the song
    position, and the tanks' modulation is a function of the song position and the seed
    (stream roomModulation), slew-limited so a seed change or a transport jump never clicks.

    Damage 0 and Off are the input to the bit. A type change lets the old space ring out under a
    300 ms fade while the new one fills. Silence in (below -300 dBFS): once the space has rung
    down by 150 dB the module stops working, and an input of exact zeros gives exact zeros out.
    See NOTES.md.
*/
class RoomModule
{
public:
    void prepare (const ModuleSpec&);
    void reset() noexcept;
    void process (const AudioBlock&, const RoomSettings&, const BlockContext&) noexcept;

    int latencySamples() const noexcept { return 0; }
    float intensity() const noexcept { return lastIntensity; }
    float activity() const noexcept { return lastActivity; }

    /** The longest Hall and Spring ring for this long before they are 90 dB down. */
    static constexpr double maxTailSeconds = 4.5;

    static constexpr int subBlock = 32;
    static constexpr double glideSeconds = 0.03;        // Amount × Damage, each of two poles
    static constexpr double sizeSeconds = 0.05;         // Size, each of two poles: the lines' lengths never change speed in a step
    static constexpr double duckSeconds = 0.012;        // the duck's gain, each of two poles (the envelope has its own times)
    static constexpr double presenceSeconds = 0.03;
    static constexpr double fillSeconds = 0.03;         // a space's input fading in or out
    static constexpr double ringOutSeconds = 0.3;       // a space left by a type change
    static constexpr float dryStepBack = 0.3f;          // the dry at full intensity: 1 - this

    /** Stopped on silence (tests). */
    bool isAsleep() const noexcept { return asleep; }

private:
    struct Slot
    {
        Fade input, output;
        bool running = false, dirty = true;
    };

    /** The slew-limited modulation of one tank (Room or Hall), in [-1, 1] per line. */
    struct TankModulation
    {
        room::Modulation value {};
        bool primed = false;
    };

    void updateSpaces (int64_t t, int numSamples, const RoomSettings&, float chaos) noexcept;
    double sizeCoefficient = 1.0;       // the Size glide's, per sub-block
    void modulate (TankModulation&, int64_t end, int numSamples, double hz, double spread, float chaos) noexcept;
    void startSlot (int type) noexcept;
    void resetSlot (int type) noexcept;
    void followSeed (uint32_t) noexcept;
    float wetTrim (int type) const noexcept;

    double fs = 48000.0;
    int channels = 2;

    room::ClosetSpace closet;
    room::TankSpace roomSpace { room::roomVoicing() }, hall { room::hallVoicing() };
    room::SpringTank spring;
    std::array<Slot, 4> slots;
    std::array<TankModulation, 2> modulation;       // Room, Hall
    int currentType = 1;

    OnOffFade onOff;
    Fade presence;
    /** Two one-poles in series that land exactly on their target: a glide whose slope never steps. */
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
    } amount, duckGain, size;
    bool primed = false;

    int64_t offset = 0, nextBoundary = 0;
    bool timelineKnown = false;

    uint32_t seed = 0;
    bool seedKnown = false;
    std::array<ValueNoise, room::tankLines> wander;
    std::array<double, room::tankLines> phase {};

    float envelope = 0.0f, attack = 1.0f, release = 1.0f;
    double duckTarget = 1.0;
    int64_t silentRun = 0;
    bool asleep = false;

    float lastIntensity = 0.0f, lastActivity = 0.0f;
};

static_assert (ChainModule<RoomModule, RoomSettings>);

} // namespace inkcap::dsp
