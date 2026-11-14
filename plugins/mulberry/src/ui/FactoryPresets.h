#pragma once

#include "common/plugin/AbCompare.h"
#include "common/plugin/PresetManager.h"

#include <juce_core/juce_core.h>

#include <array>
#include <span>

namespace mulberry::ui
{

/**
    One factory preset, in plain parameter units (see plugin/Parameters.h).

    Every member defaults to the parameter's own default, so an entry only lists what it changes
    (C++20 designated initialisers, in declaration order):

        { .name = "Slapback", .category = "Slapback", .machine = reel, .sync = false, .time = 110.0f },

    The "Init" preset relies on these defaults matching params::createLayout(); a test checks it.
*/
struct FactoryPreset
{
    const char* name = "";
    const char* category = "";

    // Front panel
    int machine = 5;                // Machine enum: 0 Clean, 1 Rack, 2 Bucket, 3 Oil Can, 4 Loop, 5 Reel, 6 Drum
    int mode = 0;                   // Mode enum: 0 Single, 1 Dual, 2 Ping-Pong, 3 Pattern
    bool sync = true;
    float time = 375.0f;            // ms (free)
    int note = 11;                  // notes::names: 11 = 1/8D
    float feedback = 35.0f;         // percent, 0 .. 120
    float drive = 0.0f;             // dB, -12 .. +24
    float wobble = 50.0f;           // percent, 50 = the machine's nominal spec
    float diffuse = 0.0f;           // percent
    float lowCut = 20.0f;           // Hz, 20 = Off
    float highCut = 20000.0f;       // Hz, 20000 = Off
    float paramA = 50.0f;           // percent (meaning depends on the machine)
    float paramB = 50.0f;           // percent
    float timeRight = 500.0f;       // ms (Dual, free)
    int noteRight = 13;             // 13 = 1/4
    float width = 100.0f;           // percent
    int steps = 4;                  // Pattern, 1 .. 16
    float mix = 35.0f;              // percent
    float output = 0.0f;            // dB
    bool duck = false;
    bool wetOnly = false;

    // Advanced deck
    float groove = 50.0f;           // percent swing, 50 .. 75
    float feel = 0.0f;              // ms, -30 .. +30
    float crossFeed = 0.0f;         // percent (Dual)
    int panRule = 1;                // PanRule: 0 Centre, 1 Alternate, 2 Sweep, 3 Scatter
    float wobbleRate = 1.0f;        // x the machine's rates
    float wobbleShape = 50.0f;      // percent, 50 = the machine's own mix
    float noise = 0.0f;             // percent
    bool trails = true;
    float duckAmount = 12.0f;       // dB
    float duckThreshold = -40.0f;   // dB
    float duckRelease = 300.0f;     // ms
    std::array<float, 16> stepLevels { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f,
                                       100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f };
};

/** The built-in presets, compiled into the binary (no external files needed). */
std::span<const FactoryPreset> factoryPresets();

/** This plugin's presets: parameter list, file format and the factory presets as snapshots. */
myzel::plugin::PresetFormat presetFormat();

using myzel::plugin::AbCompare;
using myzel::plugin::PresetManager;

} // namespace mulberry::ui
