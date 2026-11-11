#pragma once

#include <juce_core/juce_core.h>

#include <span>

namespace cerulean::ui
{

/**
    One factory preset, in plain parameter units (see plugin/Parameters.h).

    Every member defaults to the parameter's own default, so an entry only lists what it changes
    (C++20 designated initialisers, in declaration order):

        { .name = "Big Hall", .category = "Halls & Plates", .algorithm = hall, .decay = 4.5f, .mix = 30.0f },

    The "Init" preset relies on these defaults matching params::createLayout(); a test checks it.
*/
struct FactoryPreset
{
    const char* name = "";
    const char* category = "";

    // Front panel
    int algorithm = 1;              // Algorithm enum: 0 Room, 1 Hall, 2 Plate, 3 Spring, 4 Nimbus,
                                    //                 5 Halo, 6 Rise, 7 Billow, 8 Choir, 9 Echo
    float decay = 3.0f;             // seconds, 0.1 .. 60
    float preDelay = 20.0f;         // ms, 0 .. 500
    float size = 50.0f;             // percent
    float mod = 35.0f;              // percent
    float tone = 0.0f;              // -10 (dark) .. +10 (bright)
    float paramA = 50.0f;           // percent (meaning depends on the algorithm)
    float paramB = 50.0f;           // percent
    float mix = 35.0f;              // percent
    float output = 0.0f;            // dB
    bool duck = false;
    bool wetOnly = false;

    // Advanced drawer
    float lowEnd = 1.0f;            // x low-band RT60, 0.5 .. 2
    float diffusion = 75.0f;        // percent
    float modRate = 1.0f;           // x nominal rate, 0.25 .. 4
    float width = 100.0f;           // percent, 0 .. 150
    float inLowCut = 20.0f;         // Hz, 20 = Off
    float inHighCut = 20000.0f;     // Hz, 20000 = Off
    float duckAmount = 12.0f;       // dB of reduction
    float duckThreshold = -40.0f;   // dB
    float duckRelease = 300.0f;     // ms
    bool preDelaySync = false;
    int preDelayNote = 2;           // NoteValue: 2 = 1/16
    bool echoSync = false;
    int echoNote = 6;               // NoteValue: 6 = 1/8D
    bool trails = true;
};

/** The built-in presets, compiled into the binary (no external files needed). */
std::span<const FactoryPreset> factoryPresets();

} // namespace cerulean::ui
