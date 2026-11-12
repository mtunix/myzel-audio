#pragma once

#include "dsp/Machines.h"

#include <array>
#include <cstddef>

namespace mulberry::dsp
{

/** Exact comparison without -Wfloat-equal noise (plan values are copied, never computed twice). */
inline bool same (float a, float b) noexcept { return ! (a < b) && ! (a > b); }

/** Buses a read feeds: the feedback into line 0 / line 1, and the wet output left / right. */
enum Bus
{
    feedback0 = 0,
    feedback1,
    outLeft,
    outRight,
    numBuses
};

/**
    One read head on a line: where it reads (a fraction of the line's Time plus an absolute
    offset) and how much it sends to each bus. Output heads and feedback taps are separate reads,
    because the machines move feedback taps by their in-loop latencies (diffuser, shifter).
*/
struct Read
{
    float fraction = 1.0f;
    float offsetMs = 0.0f;
    std::array<float, numBuses> weights {};

    bool isFeedback() const noexcept { return ! same (weights[feedback0], 0.0f) || ! same (weights[feedback1], 0.0f); }
};

inline constexpr int maxReadsPerLine = 20;
inline constexpr int maxPatternSteps = 16;

struct LinePlan
{
    std::array<Read, maxReadsPerLine> reads {};
    int numReads = 0;
    float inputFromLeft = 0.0f;     // input routing into this line
    float inputFromRight = 0.0f;

    void add (const Read& r) noexcept
    {
        if (numReads < maxReadsPerLine)
            reads[(std::size_t) numReads++] = r;
    }
};

/**
    What a mode does, compiled into lines and heads. The machines only know lines and reads, so
    every mode works on every machine.

    - Feedback weights into a line sum to at most 1 over all reads (Loop's head sum, Dual's
      cross-feed), so Feedback alone sets the loop gain.
    - Output pans preserve the mono sum: a read at pan p sends (1-p)/2 left and (1+p)/2 right.
*/
struct TapPlan
{
    std::array<LinePlan, 2> lines {};
    bool twoTransports = false;     // Dual: two machines, decorrelated; otherwise one transport/clock
    std::array<int, 2> holdLine { 0, 0 };           // Hold's loop length per output channel:
    std::array<float, 2> holdFraction { 1.0f, 1.0f };   // a fraction of that line's Time

    /** Same reads in the same places (only weights or routing differ): a weight ramp is enough. */
    bool samePositions (const TapPlan& other) const noexcept;
};

struct TapPlanInput
{
    Mode mode = Mode::single;
    int headMask = 1;               // Loop's heads (bits for 1, 2, 3 x Time); 1 on other machines
    float groove = 0.0f;            // maps::grooveOffset: odd echoes / even steps move by this
    float feelMs = 0.0f;            // every output head moves by this (taps stay: the loop keeps time)
    float width = 1.0f;             // 0 .. 1
    int steps = 4;                  // Pattern: 1 .. 16
    std::array<float, maxPatternSteps> stepLevels {};   // 0 .. 1
    PanRule panRule = PanRule::alternate;
    float crossFeed = 0.0f;         // Dual: 0 .. 1
    bool monoInput = false;

    bool operator== (const TapPlanInput&) const = default;
};

TapPlan compileTapPlan (const TapPlanInput& input) noexcept;

/** Pattern pan of step k (0-based) of n under a rule, before Width: -1 .. 1. */
float patternPan (PanRule rule, int k, int n) noexcept;

} // namespace mulberry::dsp
