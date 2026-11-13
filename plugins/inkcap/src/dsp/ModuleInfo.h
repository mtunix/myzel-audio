#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>

namespace inkcap
{

/** The six modules, in signal order (PLAN.md, Signal flow). Never reorder: tables index by it. */
enum class Module
{
    glitch = 0,
    warble,
    crush,
    bits,
    hiss,
    room
};

inline constexpr int numModules = 6;

// Each module's types. The Type choice parameters store the index: never reorder, only append.
enum class GlitchType { skip = 0, chew, stall, bend, any };
enum class WarbleType { wow = 0, flutter, drift };
enum class CrushType  { tape = 0, cassette, valve, fuzz };
enum class BitsType   { sampler12 = 0, sampler8, phone };
enum class HissType   { vinyl = 0, tape, hum };
enum class RoomType   { closet = 0, room, spring, hall };

struct ModuleInfo
{
    const char* key;                        // the parameter ID prefix: "glitch" -> glitchOn, glitchAmount, glitchType
    const char* name;                       // display name
    std::span<const char* const> types;     // display names, in Type order
    int defaultType;
    bool defaultOn;
};

const ModuleInfo& moduleInfo (Module) noexcept;

/**
    Glitch Rate: the grid events sit on, strictly slow to fast (so 1/8D comes before 1/4T). The
    index is stored in state: never reorder.
*/
struct GridRate
{
    const char* name;
    double beats;       // slot length in quarter notes
};

inline constexpr std::array<GridRate, 12> glitchRates { {
    { "1/4D", 1.5 },    { "1/4", 1.0 },         { "1/8D", 0.75 },   { "1/4T", 2.0 / 3.0 },
    { "1/8", 0.5 },     { "1/16D", 0.375 },     { "1/8T", 1.0 / 3.0 },  { "1/16", 0.25 },
    { "1/32D", 0.1875 }, { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 },  { "1/32T", 1.0 / 12.0 },
} };

inline constexpr int numGlitchRates = (int) glitchRates.size();
inline constexpr int defaultGlitchRate = 7;     // 1/16

/**
    The module controls whose meaning depends on the module's type (and Warble's on Sync): stored
    as a 0 .. 100 % position, read out per type ("1.2 Hz", "1/8", "12 bits", "26 kHz"). The
    position is what sessions and presets keep, so a type change re-reads the control instead of
    moving it (as Mulberry's Param A/B per machine).
*/
enum class TypedControl
{
    glitchLength = 0,
    warbleRate,
    crushDrive,
    bitsRate,
    bitsDepth,
    roomSize
};

/** A typed control's read-out at a position (0 .. 100) under a type index. */
std::string typedControlText (TypedControl, int type, bool synced, float percent);

/** Reads a typed value back to a position (0 .. 100); -1 if the text is not understood. */
float typedControlFromText (TypedControl, int type, bool synced, std::string_view text);

} // namespace inkcap
