#pragma once

#include "common/plugin/AbCompare.h"
#include "common/plugin/PresetManager.h"
#include "dsp/ModuleInfo.h"

#include <span>

namespace inkcap
{

/**
    One factory preset, in the parameters' own units (see plugin/Parameters.h).

    Every member defaults to the parameter's own default, so an entry lists only what it changes
    (C++20 designated initialisers, in declaration order):

        { .name = "Worn Reel", .category = "Tape & Cassette", .seed = "reel", .damage = 55.0f,
          .crush = { .drive = "+14 dB" } },

    The typed controls (Glitch Length, Warble Rate, Crush Drive, Bits Rate and Depth, Room Size)
    are written as a user would type them ("120 ms", "1/8", "+14 dB", "22 kHz", "10 bits",
    "3.2 m"), read through the parameters' own parsers; nullptr keeps the default position. The
    seed is a word, hashed as a pasted word is: it becomes the preset's seed and its label.

    "Init" relies on these defaults matching params::createLayout(); a test checks it.
*/
struct FactoryPreset
{
    const char* name = "";
    const char* category = "";
    const char* seed = "inkcap";

    float damage = 50.0f;           // percent
    float chaos = 25.0f;            // percent

    struct Glitch
    {
        bool on = false;
        float amount = 50.0f;
        GlitchType type = GlitchType::skip;
        int rate = defaultGlitchRate;   // index into glitchRates
        const char* length = nullptr;
        float groove = 50.0f;           // percent swing, 50 .. 75
    } glitch;

    struct Warble
    {
        bool on = true;
        float amount = 50.0f;
        WarbleType type = WarbleType::wow;
        bool sync = false;
        const char* rate = nullptr;     // "0.55 Hz" free, "1/2" synced
        float spread = 0.0f;
    } warble;

    struct Crush
    {
        bool on = true;
        float amount = 50.0f;
        CrushType type = CrushType::tape;
        const char* drive = nullptr;
        float tone = 0.0f;              // -100 dark .. +100 bright
    } crush;

    struct Bits
    {
        bool on = false;
        float amount = 50.0f;
        BitsType type = BitsType::sampler12;
        const char* rate = nullptr;
        const char* depth = nullptr;
        float jitter = 0.0f;
    } bits;

    struct Hiss
    {
        bool on = true;
        float amount = 50.0f;
        HissType type = HissType::vinyl;
        float tone = 0.0f;              // -100 .. +100
        float follow = 0.0f;            // -100 ducks .. +100 follows
    } hiss;

    struct Room
    {
        bool on = false;
        float amount = 50.0f;
        RoomType type = RoomType::room;
        const char* size = nullptr;
        float duck = 0.0f;
    } room;

    float mix = 100.0f;             // percent
    float output = 0.0f;            // dB
};

/** The built-in presets, compiled into the binary, in browser order (categories in their first appearance). */
std::span<const FactoryPreset> factoryPresets();

/**
    Inkcap's presets: the sound parameters (params::soundParameterIds), the seed and its label
    (kept while the seed Lock is on), Mix kept by the mix lock, and the factory presets, each with
    its own seed.
*/
myzel::plugin::PresetFormat presetFormat();

using myzel::plugin::AbCompare;
using myzel::plugin::PresetManager;

} // namespace inkcap
