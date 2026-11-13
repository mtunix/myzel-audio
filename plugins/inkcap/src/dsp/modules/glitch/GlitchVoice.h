#pragma once

#include "common/dsp/Filters.h"
#include "dsp/common/CaptureBuffer.h"
#include "dsp/modules/glitch/GlitchPattern.h"

#include <array>
#include <cstdint>

namespace inkcap::dsp::glitch
{

/**
    One event playing: a read head program through the capture buffer, as a function of the
    samples since the event started (never of the block or of what played before, so an event
    sounds the same in every render). Both channels follow the same head.

    - Skip: the piece before the start repeats, each jump back crossfaded (2 ms, equal power),
      then a 3 ms return to the input. Integer delays: no interpolation.
    - Chew: the head wanders back and forth by up to a millisecond (the crinkle's slur) while the
      highs fade into a 12 dB/octave low-pass (the event's depth is its loss at 10 kHz: a head
      lifting off the tape) and the level dips; then it recovers on the input.
    - Stall: the speed falls to zero (the level with it), holds, comes back up, and the head,
      now behind, catches up with the input through a 12 ms crossfade.
    - Bend: the speed sags by the event's semitones, holds, glides back, then catches up.

    Stall, Bend and Chew start on the input (delay 0, speed 1) and only then fall behind, so they
    need no fade in. See NOTES.md for the curves.
*/
class Voice
{
public:
    void prepare (double sampleRate) noexcept { fs = sampleRate; }

    void start (const Event&) noexcept;
    void stop() noexcept { playing = false; }
    bool isPlaying() const noexcept { return playing; }

    /**
        One frame, after the capture buffer took it: out[c] for each channel. When the event is
        over, out is the input and isPlaying() turns false.
    */
    void render (const CaptureBuffer&, const float* input, float* out, int numChannels) noexcept;

    const Event& event() const noexcept { return current; }
    float envelope() const noexcept { return env; }
    float progress() const noexcept { return total > 0 ? (float) std::min (1.0, (double) tau / (double) total) : 0.0f; }

    static constexpr double lossReferenceHz = 10000.0;

private:
    void renderSkip (const CaptureBuffer&, const float* input, float* out, int numChannels) noexcept;
    void renderChew (const CaptureBuffer&, const float* input, float* out, int numChannels) noexcept;
    void renderBehind (const CaptureBuffer&, const float* input, float* out, int numChannels) noexcept;

    /** Stall and Bend: how far behind the input the head is, and its speed, at tau. */
    void headAt (double t, double& lag, double& speed) const noexcept;

    double fs = 48000.0;
    Event current;
    bool playing = false;
    int64_t tau = 0, total = 0;
    float env = 0.0f;

    // Skip
    int piece = 1, skipFade = 1, returnFade = 1;

    // Chew
    double attack = 1.0, releaseStart = 0.0, release = 1.0, slurLength = 1.0, slurDepth = 0.0;
    float dipDb = 0.0f;
    std::array<myzel::dsp::TptSvf, CaptureBuffer::maxChannels> lift;

    // Stall and Bend: the speed curve's segments (samples) and the fall in speed
    double down = 1.0, hold = 0.0, up = 1.0, fall = 1.0, endLag = 0.0;
    int catchUp = 1;
};

} // namespace inkcap::dsp::glitch
