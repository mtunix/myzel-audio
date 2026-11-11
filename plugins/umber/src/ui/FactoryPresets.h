#pragma once

#include <juce_core/juce_core.h>

#include <span>

namespace umber::ui
{

/** One factory preset, in plain parameter units (see plugin/Parameters.h). */
struct FactoryPreset
{
    const char* name;
    const char* category;
    float drive;
    int model;          // 0 Reel, 1 Desk, 2 Class A, 3 Valve, 4 Fuzz
    bool slam;
    float tone;         // -10 .. +10
    float lowCutHz;     // 20 = Off
    bool weight;
    float highCutHz;    // 20000 = Off
    bool autoGain;
    float mix;          // percent
    float outputDb;
    int stereoMode = 0; // 0 Stereo, 1 Mid, 2 Side
    float emphasis = 0.0f;  // -10 (lows) .. +10 (highs)
    float bias = 0.0f;      // -10 (cold) .. +10 (hot)
};

/** The built-in presets, compiled into the binary (no external files needed). */
std::span<const FactoryPreset> factoryPresets();

} // namespace umber::ui
