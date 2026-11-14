#pragma once

#include "dsp/Machines.h"
#include "dsp/TapPlan.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mulberry
{

/**
    What a machine is, for everything outside its DSP: parameter text, the UI, the processor and
    the tests. Header-only and JUCE-free.
*/
struct MachineInfo
{
    Machine machine = Machine::clean;
    const char* name = "";          // display name, also the Machine choice parameter's strings
    const char* key = "";           // lower case: golden files and MulberryMeasure names
    const char* tagline = "";       // a few words on its character (studio list)
    const char* paramA = "";        // front-panel legend of Param A on this machine
    const char* paramB = "";
    int paramASteps = 0;            // > 0: Param A is a switch with this many positions
    int paramBSteps = 0;
    float cpuBudgetPercent = 2.0f;  // PLAN.md, 48 kHz stereo, Single mode
};

inline constexpr std::array<MachineInfo, numMachines> machineInfos { {
    { Machine::clean,  "Clean",   "clean",  "Ideal memory",  "GLIDE",   "SHIFT",     0, 25, 2.0f },
    { Machine::rack,   "Rack",    "rack",   "Early digital", "BITS",    "RATE",      9, 0,  3.0f },
    { Machine::bucket, "Bucket",  "bucket", "BBD chip",      "CHIP",    "COMPANDER", 4, 0,  4.0f },
    { Machine::oilCan, "Oil Can", "oilcan", "Oil drum",      "BRUSHES", "OIL",       0, 0,  4.0f },
    { Machine::loop,   "Loop",    "loop",   "Tape loop",     "HEADS",   "WEAR",      7, 0,  5.0f },
    { Machine::reel,   "Reel",    "reel",   "Tape deck",     "SPEED",   "BIAS",      3, 0,  6.0f },
    { Machine::drum,   "Drum",    "drum",   "Magnetic drum", "HEADS",   "SWELL",    15, 0,  5.0f },
} };

/** The machines in panel and list order: the electronic memories, then the moving media. */
inline constexpr std::array<Machine, numMachines> machineOrder { Machine::clean, Machine::rack, Machine::bucket,
                                                                 Machine::oilCan, Machine::drum, Machine::loop, Machine::reel };

inline const MachineInfo& info (Machine m) noexcept
{
    return machineInfos[(size_t) std::clamp ((int) m, 0, numMachines - 1)];
}

/** Mappings from the shared Param A / Param B (0..1) to each machine's physical settings. */
namespace character
{
    inline float clamp01 (float x) noexcept { return std::clamp (x, 0.0f, 1.0f); }

    /** A stepped control: 0..1 -> 0 .. steps-1, equal shares of the knob travel. */
    inline int step (float x01, int steps) noexcept
    {
        return std::clamp ((int) std::floor (clamp01 (x01) * (float) steps), 0, steps - 1);
    }

    /** The normalised value at the centre of a step (what a stepped knob snaps to). */
    inline float stepCentre (int index, int steps) noexcept
    {
        return ((float) std::clamp (index, 0, steps - 1) + 0.5f) / (float) steps;
    }

    // Clean
    inline float glide (float a) noexcept { return clamp01 (a); }
    inline int shiftSemitones (float b) noexcept { return step (b, 25) - 12; }

    // Rack
    inline int bits (float a) noexcept { return 8 + step (a, 9); }
    inline float rateHz (float b) noexcept { return 6000.0f * std::pow (20.0f / 6.0f, clamp01 (b)); }

    // Bucket
    inline int chipStages (float a) noexcept { return 1024 << step (a, 4); }
    inline float compander (float b) noexcept { return clamp01 (b); }

    // Oil Can
    inline float brushes (float a) noexcept { return clamp01 (a); }
    inline float oil (float b) noexcept { return clamp01 (b); }

    // Loop: the classic seven head combinations, as a bit mask of heads 1..3.
    inline constexpr std::array<int, 7> headMasks { 0b001, 0b010, 0b100, 0b011, 0b110, 0b101, 0b111 };
    inline constexpr std::array<const char*, 7> headNames { "1", "2", "3", "1+2", "2+3", "1+3", "1+2+3" };
    inline int headMask (float a) noexcept { return headMasks[(size_t) step (a, 7)]; }
    inline float wear (float b) noexcept { return clamp01 (b); }

    // Reel
    inline constexpr std::array<float, 3> speedsIps { 7.5f, 15.0f, 30.0f };
    inline float speedIps (float a) noexcept { return speedsIps[(size_t) step (a, 3)]; }
    inline float bias (float b) noexcept { return 2.0f * clamp01 (b) - 1.0f; }   // -1 under .. +1 over

    // Drum: every combination of its four heads (1/4 .. 4/4 of Time), singles first.
    inline constexpr std::array<int, 15> drumHeadMasks { 0b0001, 0b0010, 0b0100, 0b1000, 0b0011, 0b0110, 0b1100, 0b0101,
                                                         0b1010, 0b1001, 0b0111, 0b1110, 0b1011, 0b1101, 0b1111 };
    inline constexpr std::array<const char*, 15> drumHeadNames { "1", "2", "3", "4", "1+2", "2+3", "3+4", "1+3",
                                                                 "2+4", "1+4", "1+2+3", "2+3+4", "1+2+4", "1+3+4", "1+2+3+4" };
    inline int drumHeadMask (float a) noexcept { return drumHeadMasks[(size_t) step (a, 15)]; }
    inline float swell (float b) noexcept { return clamp01 (b); }
}

/**
    The machine's own heads in the tap plan: Loop's three (Param A), Drum's four (Param A) with
    its Swell (Param B). Pattern mode replaces the heads; Drum's Swell still spreads the feedback.
*/
inline void setMachineHeads (dsp::TapPlanInput& in, Machine m, float paramA, float paramB) noexcept
{
    in.headMask = 1;
    in.headUnit = 1.0f;
    in.swellHeads = 0;
    in.swell = 0.0f;

    if (m == Machine::loop && in.mode != Mode::pattern)
        in.headMask = character::headMask (paramA);

    if (m == Machine::drum)
    {
        in.headUnit = 0.25f;
        in.swellHeads = 4;
        in.swell = character::swell (paramB);
        in.headMask = in.mode == Mode::pattern ? 1 : character::drumHeadMask (paramA);
    }
}

/** Global mappings shared by the parameters, the DSP and the UI. */
namespace maps
{
    inline constexpr float minTimeMs = 1.0f;
    inline constexpr float maxTimeMs = 4000.0f;
    inline constexpr float maxFeedback = 1.2f;
    inline constexpr float ringingFeedbackCap = 0.95f;   // a slot ringing out never runs away

    /** Wobble knob 0..1 -> multiple of the machine's nominal instability (0.5 = nominal, 1 = 4x). */
    inline float wobbleScale (float w01) noexcept
    {
        const auto w = std::clamp (w01, 0.0f, 1.0f);
        return w <= 0.5f ? 2.0f * w : 1.0f + 6.0f * (w - 0.5f);
    }

    /** Groove 0.5..0.75 (swing) -> the plan's g: odd echoes land at Time * (1 + g). */
    inline float grooveOffset (float swing) noexcept { return 2.0f * (std::clamp (swing, 0.5f, 0.75f) - 0.5f); }

    /** Pedal-law mix: the dry stays full to 50 %, then fades; the wet is full from 50 %. */
    inline float dryGain (float mix01) noexcept { const auto m = std::clamp (mix01, 0.0f, 1.0f); return m <= 0.5f ? 1.0f : 2.0f * (1.0f - m); }
    inline float wetGain (float mix01) noexcept { return std::min (1.0f, 2.0f * std::clamp (mix01, 0.0f, 1.0f)); }
}

} // namespace mulberry
