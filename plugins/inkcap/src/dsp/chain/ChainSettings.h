#pragma once

#include "dsp/ModuleInfo.h"
#include "dsp/common/SeedCode.h"

#include <cstdint>

namespace inkcap::dsp
{

// Plain values of every parameter the chain reads, taken once per block. Percent parameters
// arrive as 0 .. 1, bipolar ones as -1 .. 1, typed controls as their 0 .. 1 position.

struct GlitchSettings
{
    bool on = false;
    float amount = 0.5f;
    GlitchType type = GlitchType::skip;
    int rate = defaultGlitchRate;       // index into glitchRates
    float length = 0.5f;                // position, read per type
    float groove = 0.5f;                // swing: the off-beat slot's start, 0.5 (straight) .. 0.75
};

struct WarbleSettings
{
    bool on = true;
    float amount = 0.5f;
    WarbleType type = WarbleType::wow;
    float rate = 0.5f;                  // position, read per type and Sync
    bool sync = false;
    float spread = 0.0f;
};

struct CrushSettings
{
    bool on = true;
    float amount = 0.5f;
    CrushType type = CrushType::tape;
    float drive = 0.5f;                 // position, read per type
    float tone = 0.0f;                  // -1 dark .. +1 bright
};

struct BitsSettings
{
    bool on = false;
    float amount = 0.5f;
    BitsType type = BitsType::sampler12;
    float rate = 0.5f;                  // position, read per type
    float depth = 0.5f;                 // position, read per type
    float jitter = 0.0f;
};

struct HissSettings
{
    bool on = true;
    float amount = 0.5f;
    HissType type = HissType::vinyl;
    float tone = 0.0f;                  // -1 dark .. +1 bright
    float follow = 0.0f;                // -1 ducks under the input .. +1 follows it
};

struct RoomSettings
{
    bool on = false;
    float amount = 0.5f;
    RoomType type = RoomType::room;
    float size = 0.5f;                  // position, read per type
    float duck = 0.0f;
};

struct ChainSettings
{
    float damage = 0.5f;
    float chaos = 0.25f;

    GlitchSettings glitch;
    WarbleSettings warble;
    CrushSettings crush;
    BitsSettings bits;
    HissSettings hiss;
    RoomSettings room;

    float mix = 1.0f;
    float outputDb = 0.0f;
    bool match = true;
    bool bypass = false;
    double fallbackBpm = 120.0;         // when the host gives no tempo

    uint32_t seed = seedcode::defaultSeed;
};

} // namespace inkcap::dsp
