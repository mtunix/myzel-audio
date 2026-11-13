#include "dsp/ModuleInfo.h"

#include "dsp/modules/bits/BitsControls.h"
#include "dsp/modules/crush/CrushControls.h"
#include "dsp/modules/glitch/GlitchControls.h"
#include "dsp/modules/room/RoomControls.h"
#include "dsp/modules/warble/WarbleControls.h"

#include <algorithm>

namespace inkcap
{

namespace
{
    constexpr std::array<const char*, 5> glitchTypes { "Skip", "Chew", "Stall", "Bend", "Any" };
    constexpr std::array<const char*, 3> warbleTypes { "Wow", "Flutter", "Drift" };
    constexpr std::array<const char*, 4> crushTypes  { "Tape", "Cassette", "Valve", "Fuzz" };
    constexpr std::array<const char*, 3> bitsTypes   { "Sampler 12", "Sampler 8", "Phone" };
    constexpr std::array<const char*, 3> hissTypes   { "Vinyl", "Tape", "Hum" };
    constexpr std::array<const char*, 4> roomTypes   { "Closet", "Room", "Spring", "Hall" };

    // Defaults are provisional until the M3 voicing pass: a fresh instance is worn tape (Warble,
    // Crush, Hiss); the malfunction, the converter and the space wait for a key press.
    const std::array<ModuleInfo, numModules> infos { {
        { "glitch", "Glitch", glitchTypes, (int) GlitchType::skip,      false },
        { "warble", "Warble", warbleTypes, (int) WarbleType::wow,       true },
        { "crush",  "Crush",  crushTypes,  (int) CrushType::tape,       true },
        { "bits",   "Bits",   bitsTypes,   (int) BitsType::sampler12,   false },
        { "hiss",   "Hiss",   hissTypes,   (int) HissType::vinyl,       true },
        { "room",   "Room",   roomTypes,   (int) RoomType::room,        false },
    } };
}

const ModuleInfo& moduleInfo (Module m) noexcept
{
    return infos[(size_t) std::clamp ((int) m, 0, numModules - 1)];
}

// Each module owns its read-outs (its Controls file), so voicing one never touches another's.
std::string typedControlText (TypedControl control, int type, bool synced, float percent)
{
    switch (control)
    {
        case TypedControl::glitchLength: return dsp::glitch::lengthText (type, percent);
        case TypedControl::warbleRate:   return dsp::warble::rateText (type, synced, percent);
        case TypedControl::crushDrive:   return dsp::crush::driveText (type, percent);
        case TypedControl::bitsRate:     return dsp::bits::rateText (type, percent);
        case TypedControl::bitsDepth:    return dsp::bits::depthText (type, percent);
        case TypedControl::roomSize:     return dsp::room::sizeText (type, percent);
    }
    return {};
}

float typedControlFromText (TypedControl control, int type, bool synced, std::string_view text)
{
    switch (control)
    {
        case TypedControl::glitchLength: return dsp::glitch::lengthFromText (type, text);
        case TypedControl::warbleRate:   return dsp::warble::rateFromText (type, synced, text);
        case TypedControl::crushDrive:   return dsp::crush::driveFromText (type, text);
        case TypedControl::bitsRate:     return dsp::bits::rateFromText (type, text);
        case TypedControl::bitsDepth:    return dsp::bits::depthFromText (type, text);
        case TypedControl::roomSize:     return dsp::room::sizeFromText (type, text);
    }
    return -1.0f;
}

} // namespace inkcap
