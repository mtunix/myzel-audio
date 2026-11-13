#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace inkcap::dsp
{

/** What the host says about the song position at a block's first sample. */
struct HostPosition
{
    bool hasPpq = false;
    bool playing = false;
    double ppq = 0.0;
    bool hasSeconds = false;
    double seconds = 0.0;
    double bpm = 0.0;               // 0: the host gives none
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    bool looping = false;           // the host's loop (cycle) is on, between these positions
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;
    bool hasBarStart = false;       // the position of the current bar's first beat
    double barStartPpq = 0.0;
};

/**
    A slot grid in quarter notes with swing: slots come in pairs, and the second slot of a pair
    starts at `swing` of the pair (0.5 straight, 0.75 a dotted feel), as Mulberry's Groove. Slot 0
    starts at beat 0; slots before it (a pre-roll) have negative indices.
*/
struct Grid
{
    double slotBeats = 0.25;
    double swing = 0.5;

    /** Song positions within this many quarter notes of a boundary count as on it: far below a
        sample at any rate and tempo, far above double rounding of a position. */
    static constexpr double epsilon = 1.0e-9;

    double pairBeats() const noexcept { return 2.0 * slotBeats; }

    double slotStart (int64_t slot) const noexcept
    {
        const auto pair = floorDiv (slot, 2);
        const auto odd = slot - 2 * pair;
        return (double) pair * pairBeats() + (odd != 0 ? pairBeats() * std::clamp (swing, 0.5, 0.75) : 0.0);
    }

    double slotLength (int64_t slot) const noexcept { return slotStart (slot + 1) - slotStart (slot); }

    /** The slot a position lies in (a position within epsilon before a start is in that slot). */
    int64_t slotAt (double ppq) const noexcept
    {
        const auto p = ppq + epsilon;
        const auto pair = (int64_t) std::floor (p / pairBeats());
        const auto offset = p - (double) pair * pairBeats();
        return 2 * pair + (offset >= pairBeats() * std::clamp (swing, 0.5, 0.75) ? 1 : 0);
    }

    /** The first slot that starts at or after a position. */
    int64_t firstSlotFrom (double ppq) const noexcept
    {
        const auto k = slotAt (ppq);
        return slotStart (k) < ppq - epsilon ? k + 1 : k;
    }

    static int64_t floorDiv (int64_t a, int64_t b) noexcept
    {
        const auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }
};

/**
    The song position of every sample, for everything in Inkcap that follows the timeline
    (Glitch's slots, Warble's synced and free phases, value noise).

    Samples are counted from reset() (the processing index). Each block, advance() takes the
    host's position and decides whether it continues the last block's timeline. If it does, the
    position keeps coming from the same anchor (an anchor position plus samples times the
    tempo), so a render is the same to the bit for any block size; a tempo change re-anchors
    where it happens. If it does not (the first block, play, stop, a locate, a loop), the block
    starts a new timeline segment at the host's position: jumped() is true for that block.

    Without a playing transport (a stopped host, the Standalone) the position runs free from 0 at
    the host's tempo, or the fallback BPM when it gives none, counted from the block where it
    started running free. No allocation; real-time safe.
*/
class GridClock
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        processed = 0;
        primed = false;
        isJump = true;
    }

    void advance (const HostPosition& host, double fallbackBpm, int numSamples) noexcept
    {
        const auto follows = host.hasPpq && host.playing && std::isfinite (host.ppq);
        const auto hostBpm = host.bpm > 0.0 && std::isfinite (host.bpm) ? host.bpm : 0.0;
        const auto bpm = std::clamp (hostBpm > 0.0 ? hostBpm : fallbackBpm, minBpm, maxBpm);
        blockFirst = processed;
        numerator = std::clamp (host.timeSigNumerator, 1, 64);
        denominator = std::clamp (host.timeSigDenominator, 1, 64);

        isJump = ! primed || follows != following;

        if (follows && ! isJump)
        {
            const auto predicted = ppqAt (blockFirst);
            isJump = std::abs (host.ppq - predicted) > ppqPerSample() + Grid::epsilon;

            // A tempo change re-anchors on the host's position (within a sample of ours).
            if (! isJump && (bpm < tempo || bpm > tempo))
                anchor (blockFirst, host.ppq, secondsAt (blockFirst), bpm);
        }
        else if (! follows && ! isJump && (bpm < tempo || bpm > tempo))
        {
            anchor (blockFirst, ppqAt (blockFirst), secondsAt (blockFirst), bpm);
        }

        if (isJump)
        {
            if (follows)
                anchor (blockFirst, host.ppq, host.hasSeconds && std::isfinite (host.seconds) ? host.seconds : host.ppq * 60.0 / bpm, bpm);
            else
                anchor (blockFirst, 0.0, 0.0, bpm);
        }

        following = follows;
        primed = true;
        processed += std::max (numSamples, 0);
    }

    /** This block starts a new timeline segment (first block, transport start or stop, locate, loop). */
    bool jumped() const noexcept { return isJump; }

    /** A new segment starts at this sample (a module that gets a block in pieces sees it once). */
    bool jumpedAt (int64_t sample) const noexcept { return isJump && sample == blockFirst; }

    /** Following a playing host transport (else free-running). */
    bool isFollowingHost() const noexcept { return following; }

    double getSampleRate() const noexcept { return fs; }
    double getBpm() const noexcept { return tempo; }
    double ppqPerSample() const noexcept { return tempo / (60.0 * fs); }
    int getTimeSigNumerator() const noexcept { return numerator; }
    int getTimeSigDenominator() const noexcept { return denominator; }

    /** The processing index of this block's first sample. */
    int64_t blockStart() const noexcept { return blockFirst; }

    double ppqAt (int64_t sample) const noexcept { return anchorPpq + (double) (sample - anchorSample) * ppqPerSample(); }
    double secondsAt (int64_t sample) const noexcept { return anchorSeconds + (double) (sample - anchorSample) / fs; }

    /** The first sample whose position is at or after ppq (within Grid::epsilon). */
    int64_t sampleAt (double ppq) const noexcept
    {
        return anchorSample + (int64_t) std::ceil ((ppq - Grid::epsilon - anchorPpq) / ppqPerSample());
    }

    static constexpr double minBpm = 20.0, maxBpm = 999.0;

private:
    void anchor (int64_t sample, double ppq, double seconds, double bpm) noexcept
    {
        anchorSample = sample;
        anchorPpq = ppq;
        anchorSeconds = seconds;
        tempo = bpm;
    }

    double fs = 48000.0;
    int64_t processed = 0, blockFirst = 0, anchorSample = 0;
    double anchorPpq = 0.0, anchorSeconds = 0.0, tempo = 120.0;
    int numerator = 4, denominator = 4;
    bool primed = false, isJump = true, following = false;
};

} // namespace inkcap::dsp
