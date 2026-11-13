#pragma once

#include "dsp/chain/ChainSettings.h"
#include "dsp/common/GridClock.h"

#include <array>
#include <cstdint>
#include <optional>

namespace inkcap::dsp::glitch
{

/**
    Which slots of the Rate grid fire and what each event plays: a pure function of the seed, the
    pattern parameters and the slot index. The audio thread calls it at each slot; the editor
    calls the same function for the coming bars, so the display shows exactly what will play.
    See NOTES.md (Events) for the rules and the numbers.
*/
struct PatternParams
{
    float density = 0.0f;       // share of slots that fire, 0 .. maxDensity (Amount x Damage)
    float severity = 0.0f;      // how deep Chew and Bend go, 0 .. 1 (Amount x Damage)
    float chaos = 0.0f;         // 0: every Nth slot, a fixed pattern; 1: a probability per slot
    GlitchType type = GlitchType::skip;
    float length = 0.5f;        // the Length position, 0 .. 1
    Grid grid;
};

inline constexpr float maxDensity = 0.5f;

/** The pattern a block plays, from the module's settings and the two macros. */
PatternParams patternFor (const GlitchSettings&, float damage, float chaos) noexcept;

/** One glitch: a medium failing for a while, from the start of its slot. */
struct Event
{
    int64_t slot = 0;
    double startBeat = 0.0;         // the slot's start, groove included
    GlitchType type = GlitchType::skip;     // never Any: Any picks one of the four per event

    double lengthSeconds = 0.0;     // Skip: the piece that repeats; Chew, Bend: the event; Stall: the stop
    int repeats = 0;                // Skip: how often the piece plays, 2 .. 8
    double holdSeconds = 0.0;       // Stall: stopped; Bend: at the bottom
    double restartSeconds = 0.0;    // Stall: back up to speed
    float depth = 0.0f;             // Chew: high loss in dB (6 .. 30); Bend: semitones down (1 .. 12)
};

/** The event at a slot, if it fires. */
std::optional<Event> eventAt (uint32_t seed, const PatternParams&, int64_t slot) noexcept;

/** From an event's start until it is back on the input, its end fade included. */
double durationSeconds (const Event&) noexcept;

/** The longest event any setting can make (the capture buffer and the tail cover it). */
inline constexpr double longestEventSeconds = 2.0;

/**
    The steady pattern's periods in slots: musical lengths, so at 1/16 a period is a beat, a bar,
    two bars ... (12 and 24 for triplet feels).
*/
inline constexpr std::array<int, 13> steadyPeriods { 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128 };

/** The steady pattern's period in slots at a density: every Nth slot fires at Chaos 0. 0: none. */
int periodFor (float density) noexcept;

/** The share of slots that fire on average: 1 / N at Chaos 0, the density at Chaos 1 (Match reads it). */
float meanDensity (const PatternParams&) noexcept;

/** Length read-outs: the time a Length position means for a type (Any: none). */
double lengthSecondsFor (GlitchType, float lengthPosition) noexcept;
float lengthPositionFor (GlitchType, double seconds) noexcept;

// Skip's fades: each jump back (abrupt, never a click), and the return to the input.
inline constexpr double skipFadeSeconds = 0.002;
inline constexpr double skipReturnSeconds = 0.003;

// Stall and Bend catch up with the input through this crossfade.
inline constexpr double catchUpSeconds = 0.012;

// A new event cuts the one playing with this crossfade.
inline constexpr double cutSeconds = 0.003;

} // namespace inkcap::dsp::glitch
