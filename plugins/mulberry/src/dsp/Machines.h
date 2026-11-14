#pragma once

namespace mulberry
{

/** The delay machines. Never reorder: the Machine choice parameter stores the index. */
enum class Machine
{
    clean = 0,
    rack,
    bucket,
    oilCan,
    loop,
    reel,
    drum
};

inline constexpr int numMachines = 7;

/** Routing modes. Never reorder: stored in state. */
enum class Mode
{
    single = 0,
    dual,
    pingPong,
    pattern
};

inline constexpr int numModes = 4;

/** Where Pattern mode places its steps in the stereo field. Never reorder: stored in state. */
enum class PanRule
{
    centre = 0,
    alternate,
    sweep,
    scatter
};

inline constexpr int numPanRules = 4;

enum class HoldMode
{
    latch = 0,
    momentary
};

} // namespace mulberry
