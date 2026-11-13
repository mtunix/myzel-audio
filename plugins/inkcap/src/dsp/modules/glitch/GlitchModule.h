#pragma once

#include "dsp/common/CaptureBuffer.h"
#include "dsp/modules/Module.h"
#include "dsp/modules/glitch/GlitchPattern.h"
#include "dsp/modules/glitch/GlitchVoice.h"

#include <array>
#include <cstdint>

namespace inkcap::dsp
{

/**
    Glitch, module 1: the source's medium fails on the song's grid (PLAN.md, The modules).

    At each slot of the Rate grid (GridClock, groove included) the pattern decides whether an
    event starts (glitch::eventAt, from the seed in use and the slot index alone); the event
    plays as a read head through a 4 s capture of the input (glitch::Voice). A new event cuts the
    one playing with a 3 ms crossfade. Between events the output is the input, to the bit.

    A new seed takes effect at the next slot. After a transport jump (play, locate, loop) an
    event already under way is not played, and neither is an event whose source lies before the
    jump: from there on, a render is the same as one that played from the start (PLAN.md,
    Seeds and determinism). See NOTES.md.
*/
class GlitchModule
{
public:
    /** Every slot that fires, for tests and tools (audio thread; no allocation of its own). */
    struct EventLog
    {
        virtual ~EventLog() = default;
        virtual void fired (const glitch::Event&, int64_t sample, bool played) = 0;
    };

    void prepare (const ModuleSpec&);
    void reset() noexcept;
    void process (const AudioBlock&, const GlitchSettings&, const BlockContext&) noexcept;

    int latencySamples() const noexcept { return 0; }
    float intensity() const noexcept { return lastIntensity; }
    float activity() const noexcept { return lastActivity; }
    static constexpr double maxTailSeconds = glitch::longestEventSeconds;

    /** The seed the events use now: the instance's seed as of the last slot. */
    uint32_t seedInUse() const noexcept { return seed; }

    /** The event playing, or nullptr. */
    const glitch::Voice* playingVoice() const noexcept { return current >= 0 ? &voices[(size_t) current] : nullptr; }

    void setEventLog (EventLog* log) noexcept { eventLog = log; }

    static constexpr double captureSeconds = 4.0;

private:
    void renderFrames (const AudioBlock&, int from, int to) noexcept;
    void fire (int64_t slot, int64_t sample, const glitch::PatternParams&, uint32_t instanceSeed, bool on) noexcept;
    void cutToInput() noexcept;

    double fs = 48000.0;
    int channels = 2;
    CaptureBuffer capture;
    std::array<glitch::Voice, 2> voices;
    int current = -1, previous = -1;    // the voice playing and the one being cut (-1: the input)
    int cutPosition = 0, cutLength = 1;

    OnOffFade onOff;
    bool idle = true;
    int64_t nextSlot = 0, validFrom = 0;
    Grid grid;
    uint32_t seed = 0;
    float lastIntensity = 0.0f, lastActivity = 0.0f;
    EventLog* eventLog = nullptr;
};

static_assert (ChainModule<GlitchModule, GlitchSettings>);

} // namespace inkcap::dsp
