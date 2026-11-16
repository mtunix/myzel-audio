#pragma once

// The choice parameters' values, in their saved index order (PLAN.md, Controls). Sessions and
// automation store the index: never reorder, only append.

namespace halite
{

/** How a reduction is applied (PLAN.md, Reduction). */
enum class Shape
{
    split = 0,  // dynamic shelf or bell over the Detect band
    spectral,   // per-bin, only what exceeds the voice's own top end
    wide        // the whole signal
};

inline constexpr int numShapes = 3;

/** What the threshold is measured against. */
enum class Detection
{
    voice = 0,  // relative threshold, noise cue on
    allround    // absolute threshold, band level only
};

inline constexpr int numDetections = 2;

enum class StereoMode
{
    linked = 0, // the louder channel's detector drives both
    unlinked,   // one detector per channel
    mid,        // detect and reduce only the mid
    side        // detect and reduce only the side
};

inline constexpr int numStereoModes = 4;

enum class KeySource
{
    internal = 0,
    external    // the side-chain bus; falls back to internal when the host leaves it off
};

inline constexpr int numKeySources = 2;

/** What the output carries. Monitoring only: not in presets, not undoable. */
enum class Listen
{
    output = 0,
    removed,        // what both bands take out
    essRemoved,
    harshRemoved,
    essTrigger,     // what the Ess detector hears
    harshTrigger
};

inline constexpr int numListenModes = 6;

/** A session setting, not a parameter: a preset never changes the latency the host sees. */
enum class LatencyMode
{
    normal = 0, // one Spectral frame; look-ahead and every shape
    live        // 0 samples; Split and Wide, no look-ahead
};

inline constexpr int numLatencyModes = 2;

} // namespace halite
