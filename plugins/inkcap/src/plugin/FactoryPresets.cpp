#include "plugin/FactoryPresets.h"

#include "dsp/common/SeedCode.h"
#include "plugin/Parameters.h"
#include "plugin/SeedState.h"

namespace inkcap
{

namespace
{
    using G = GlitchType;
    using W = WarbleType;
    using C = CrushType;
    using B = BitsType;
    using H = HissType;
    using R = RoomType;

    // Glitch Rate indices (glitchRates).
    constexpr int r1_4D = 0, r1_4 = 1, r1_8 = 4, r1_16 = 7, r1_32 = 10;

    // An entry names only what differs from the defaults; the modules it leaves out keep theirs.
   #if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
   #endif

    // Voiced against the ladder in each module's NOTES.md: Glitch fires about 8 x (Amount x Damage)^2
    // times a bar at 1/16, Wow swings about 34 x (Amount x Damage)^1.3 cents, and Match keeps every
    // preset near the input's loudness. Each seed is a word, so the preset's chaos has a name.
    constexpr FactoryPreset presets[] = {
        // Utility
        { .name = "Init", .category = "Utility" },
        { .name = "Clean Through", .category = "Utility", .seed = "clean", .damage = 0.0f,
          .crush = { .amount = 55.0f }, .hiss = { .amount = 40.0f },
          .room = { .on = true, .amount = 35.0f, .type = R::closet } },
        { .name = "Seed Explorer", .category = "Utility", .seed = "explore", .damage = 55.0f, .chaos = 85.0f,
          .glitch = { .on = true, .amount = 60.0f, .type = G::any },
          .warble = { .amount = 60.0f, .type = W::drift } },
        { .name = "Every Module", .category = "Utility", .seed = "everything",
          .glitch = { .on = true, .type = G::any }, .bits = { .on = true },
          .room = { .on = true, .type = R::closet } },

        // Tape & Cassette
        { .name = "Worn Reel", .category = "Tape & Cassette", .seed = "reel", .damage = 55.0f, .chaos = 20.0f,
          .warble = { .amount = 45.0f, .rate = "0.4 Hz" },
          .crush = { .amount = 70.0f, .drive = "+14 dB", .tone = -10.0f },
          .hiss = { .amount = 45.0f, .type = H::tape } },
        { .name = "Hot Studio Tape", .category = "Tape & Cassette", .seed = "studio", .damage = 65.0f, .chaos = 10.0f,
          .warble = { .amount = 25.0f, .type = W::flutter, .rate = "7 Hz" },
          .crush = { .amount = 85.0f, .drive = "+19 dB" },
          .hiss = { .amount = 25.0f, .type = H::tape } },
        { .name = "Cassette Dub", .category = "Tape & Cassette", .seed = "dub", .damage = 55.0f, .chaos = 30.0f,
          .warble = { .amount = 45.0f, .type = W::flutter, .rate = "5 Hz" },
          .crush = { .amount = 65.0f, .type = C::cassette, .drive = "+14 dB", .tone = -25.0f },
          .hiss = { .type = H::tape, .tone = 20.0f } },
        { .name = "Pocket Cassette", .category = "Tape & Cassette", .seed = "pocket", .damage = 65.0f, .chaos = 45.0f,
          .warble = { .amount = 60.0f, .rate = "1.1 Hz", .spread = 20.0f },
          .crush = { .amount = 60.0f, .type = C::cassette, .drive = "+12 dB", .tone = -35.0f },
          .hiss = { .amount = 55.0f, .type = H::tape } },
        { .name = "Memo Recorder", .category = "Tape & Cassette", .seed = "memo", .damage = 70.0f, .chaos = 40.0f,
          .warble = { .amount = 55.0f, .type = W::flutter, .rate = "9 Hz" },
          .crush = { .amount = 80.0f, .type = C::cassette, .drive = "+18 dB", .tone = -50.0f },
          .bits = { .on = true, .amount = 40.0f, .type = B::phone },
          .hiss = { .amount = 70.0f, .type = H::tape },
          .room = { .on = true, .amount = 30.0f, .type = R::closet, .size = "1.2 m" }, .output = -3.0f },
        { .name = "Chewed Tape", .category = "Tape & Cassette", .seed = "chewed", .damage = 60.0f,
          .glitch = { .on = true, .amount = 55.0f, .type = G::chew, .rate = r1_8, .length = "250 ms" },
          .warble = { .type = W::drift },
          .crush = { .amount = 55.0f, .type = C::cassette },
          .hiss = { .amount = 45.0f, .type = H::tape } },
        { .name = "Tape Stop Fills", .category = "Tape & Cassette", .seed = "brake", .damage = 55.0f, .chaos = 15.0f,
          .glitch = { .on = true, .amount = 70.0f, .type = G::stall, .rate = r1_4, .length = "450 ms" },
          .warble = { .amount = 30.0f },
          .hiss = { .amount = 30.0f, .type = H::tape } },

        // Vinyl
        { .name = "Dusty Record", .category = "Vinyl", .seed = "dust", .damage = 55.0f, .chaos = 30.0f,
          .warble = { .amount = 40.0f, .rate = "0.55 Hz" },
          .crush = { .amount = 40.0f, .type = C::valve, .drive = "+12 dB" },
          .hiss = { .amount = 60.0f, .tone = -10.0f } },
        { .name = "Shellac", .category = "Vinyl", .seed = "shellac", .damage = 75.0f, .chaos = 35.0f,
          .warble = { .rate = "1.3 Hz" },
          .crush = { .amount = 55.0f, .type = C::valve, .tone = -80.0f },
          .hiss = { .amount = 80.0f, .tone = -40.0f },
          .room = { .on = true, .amount = 30.0f, .type = R::closet, .size = "1.0 m" } },
        { .name = "Thrift Store Find", .category = "Vinyl", .seed = "thrift", .damage = 65.0f, .chaos = 70.0f,
          .warble = { .amount = 55.0f }, .crush = { .amount = 40.0f },
          .hiss = { .amount = 85.0f, .follow = -20.0f } },
        { .name = "Warped Record", .category = "Vinyl", .seed = "warped", .damage = 70.0f, .chaos = 20.0f,
          .warble = { .amount = 85.0f, .rate = "0.55 Hz" },
          .crush = { .amount = 35.0f, .type = C::valve },
          .hiss = { .amount = 55.0f } },
        { .name = "Crackle Only", .category = "Vinyl", .seed = "crackle", .damage = 60.0f, .chaos = 40.0f,
          .warble = { .on = false }, .crush = { .on = false }, .hiss = { .amount = 70.0f } },
        { .name = "Late Night Vinyl", .category = "Vinyl", .seed = "midnight", .damage = 45.0f,
          .warble = { .amount = 35.0f },
          .crush = { .amount = 40.0f, .tone = -20.0f },
          .hiss = { .amount = 45.0f, .follow = 40.0f },
          .room = { .on = true, .amount = 25.0f, .size = "4 m" } },

        // Lo-fi beats
        { .name = "Bedroom Beat", .category = "Lo-fi beats", .seed = "bedroom", .damage = 55.0f,
          .warble = { .amount = 45.0f }, .crush = { .amount = 55.0f },
          .bits = { .on = true, .amount = 60.0f } },
        { .name = "Dusty Sampler", .category = "Lo-fi beats", .seed = "sampler", .damage = 60.0f, .chaos = 30.0f,
          .warble = { .amount = 30.0f },
          .crush = { .type = C::valve, .drive = "+14 dB" },
          .bits = { .on = true, .amount = 70.0f, .type = B::sampler8, .rate = "16 kHz", .depth = "9 bits" },
          .hiss = { .amount = 55.0f },
          .room = { .on = true, .amount = 30.0f, .type = R::closet } },
        { .name = "Rainy Window", .category = "Lo-fi beats", .seed = "rain", .damage = 55.0f, .chaos = 45.0f,
          .warble = { .amount = 60.0f, .type = W::drift, .rate = "0.1 Hz" },
          .crush = { .type = C::cassette, .tone = -40.0f },
          .hiss = { .type = H::tape, .tone = -30.0f },
          .room = { .on = true, .amount = 35.0f, .size = "6 m" } },
        { .name = "Study Session", .category = "Lo-fi beats", .seed = "study", .damage = 45.0f, .chaos = 20.0f,
          .warble = { .amount = 40.0f }, .crush = { .amount = 45.0f },
          .bits = { .on = true, .amount = 45.0f }, .hiss = { .amount = 40.0f } },
        { .name = "Crunchy Loop", .category = "Lo-fi beats", .seed = "crunch", .damage = 65.0f,
          .warble = { .on = false },
          .crush = { .amount = 40.0f, .type = C::fuzz, .drive = "+16 dB" },
          .bits = { .on = true, .amount = 75.0f, .type = B::sampler8, .rate = "11 kHz", .depth = "8 bits" },
          .hiss = { .amount = 35.0f } },
        { .name = "Sunday Cassette", .category = "Lo-fi beats", .seed = "sunday", .chaos = 30.0f,
          .warble = { .rate = "0.8 Hz" }, .crush = { .type = C::cassette },
          .hiss = { .amount = 45.0f, .type = H::tape },
          .room = { .on = true, .amount = 35.0f, .type = R::closet } },
        { .name = "Swung Sampler", .category = "Lo-fi beats", .seed = "swing", .damage = 55.0f, .chaos = 30.0f,
          .glitch = { .on = true, .amount = 45.0f, .rate = r1_16, .length = "60 ms", .groove = 62.0f },
          .warble = { .on = false }, .crush = { .amount = 40.0f },
          .bits = { .on = true, .amount = 60.0f, .rate = "22 kHz" },
          .hiss = { .amount = 40.0f } },

        // Broken
        { .name = "Skipping CD", .category = "Broken", .seed = "laser", .damage = 60.0f, .chaos = 35.0f,
          .glitch = { .on = true, .amount = 65.0f, .rate = r1_16, .length = "80 ms" },
          .warble = { .on = false }, .crush = { .on = false }, .hiss = { .on = false } },
        { .name = "Eaten Tape", .category = "Broken", .seed = "eaten", .damage = 70.0f, .chaos = 55.0f,
          .glitch = { .on = true, .amount = 75.0f, .type = G::chew, .rate = r1_8, .length = "350 ms" },
          .warble = { .amount = 55.0f, .type = W::drift },
          .crush = { .amount = 60.0f, .type = C::cassette },
          .hiss = { .type = H::tape } },
        { .name = "Power Cut", .category = "Broken", .seed = "blackout", .damage = 60.0f, .chaos = 30.0f,
          .glitch = { .on = true, .amount = 80.0f, .type = G::stall, .rate = r1_4D, .length = "600 ms" },
          .warble = { .on = false }, .crush = { .amount = 35.0f },
          .hiss = { .amount = 35.0f, .type = H::hum } },
        { .name = "Dying Battery", .category = "Broken", .seed = "battery", .damage = 70.0f, .chaos = 60.0f,
          .glitch = { .on = true, .amount = 60.0f, .type = G::bend, .rate = r1_8, .length = "400 ms" },
          .warble = { .amount = 65.0f, .type = W::drift },
          .crush = { .amount = 45.0f, .type = C::cassette },
          .bits = { .on = true, .type = B::sampler8 },
          .hiss = { .amount = 40.0f, .type = H::tape } },
        { .name = "Malfunction", .category = "Broken", .seed = "malfunction", .damage = 75.0f, .chaos = 80.0f,
          .glitch = { .on = true, .amount = 75.0f, .type = G::any },
          .warble = { .amount = 60.0f, .type = W::drift },
          .crush = { .amount = 35.0f, .type = C::fuzz },
          .bits = { .on = true, .amount = 45.0f, .type = B::sampler8, .jitter = 40.0f },
          .hiss = { .amount = 45.0f },
          .room = { .on = true, .amount = 30.0f, .type = R::closet } },
        { .name = "Stutter Grid", .category = "Broken", .seed = "stutter", .damage = 70.0f, .chaos = 0.0f,
          .glitch = { .on = true, .amount = 80.0f, .rate = r1_32, .length = "25 ms" },
          .warble = { .on = false }, .crush = { .on = false }, .hiss = { .on = false } },
        { .name = "Sampler Sag", .category = "Broken", .seed = "sag", .damage = 55.0f, .chaos = 30.0f,
          .glitch = { .on = true, .amount = 55.0f, .type = G::bend, .rate = r1_8 },
          .warble = { .on = false }, .crush = { .amount = 30.0f },
          .bits = { .on = true, .amount = 60.0f }, .hiss = { .on = false } },

        // Spaces
        { .name = "Small Booth", .category = "Spaces", .seed = "booth", .chaos = 20.0f,
          .warble = { .on = false }, .crush = { .amount = 30.0f }, .hiss = { .on = false },
          .room = { .on = true, .amount = 70.0f, .type = R::closet, .size = "1.2 m" } },
        { .name = "Garage", .category = "Spaces", .seed = "garage", .damage = 55.0f,
          .warble = { .on = false }, .crush = { .amount = 35.0f, .type = C::valve },
          .hiss = { .amount = 30.0f, .type = H::hum },
          .room = { .on = true, .amount = 70.0f, .size = "7 m" } },
        { .name = "Spring Tank", .category = "Spaces", .seed = "spring", .damage = 55.0f, .chaos = 30.0f,
          .warble = { .on = false }, .crush = { .amount = 45.0f, .type = C::valve },
          .hiss = { .amount = 20.0f, .type = H::hum },
          .room = { .on = true, .amount = 75.0f, .type = R::spring, .size = "2.0 s" } },
        { .name = "Old Hall", .category = "Spaces", .seed = "hall", .damage = 60.0f, .chaos = 35.0f,
          .warble = { .amount = 40.0f, .type = W::drift },
          .crush = { .amount = 35.0f, .tone = -20.0f },
          .hiss = { .amount = 30.0f, .type = H::tape },
          .room = { .on = true, .amount = 70.0f, .type = R::hall, .size = "24 m" } },
        { .name = "Tiled Bathroom", .category = "Spaces", .seed = "tiles", .damage = 55.0f, .chaos = 20.0f,
          .warble = { .on = false }, .crush = { .on = false }, .hiss = { .on = false },
          .room = { .on = true, .amount = 80.0f, .size = "2.8 m" } },
        { .name = "Ducked Hall", .category = "Spaces", .seed = "ducked", .damage = 60.0f,
          .warble = { .amount = 30.0f }, .crush = { .amount = 30.0f }, .hiss = { .on = false },
          .room = { .on = true, .amount = 85.0f, .type = R::hall, .size = "30 m", .duck = 65.0f } },

        // Drums
        { .name = "Tape Crushed Drums", .category = "Drums", .seed = "kit", .damage = 70.0f, .chaos = 15.0f,
          .warble = { .on = false }, .crush = { .amount = 85.0f, .drive = "+20 dB" }, .hiss = { .on = false },
          .room = { .on = true, .amount = 40.0f, .type = R::closet } },
        { .name = "Breakbeat Sampler", .category = "Drums", .seed = "breakbeat", .damage = 60.0f, .chaos = 20.0f,
          .warble = { .on = false }, .crush = { .amount = 45.0f, .type = C::valve },
          .bits = { .on = true, .amount = 70.0f, .rate = "22 kHz", .depth = "12 bits" },
          .hiss = { .amount = 35.0f } },
        { .name = "Glitched Hats", .category = "Drums", .seed = "hats", .damage = 60.0f, .chaos = 50.0f,
          .glitch = { .on = true, .amount = 60.0f, .rate = r1_32, .length = "30 ms" },
          .warble = { .on = false }, .crush = { .on = false },
          .bits = { .on = true, .amount = 40.0f, .type = B::sampler8 }, .hiss = { .on = false } },
        { .name = "Room Mic Smash", .category = "Drums", .seed = "smash", .damage = 70.0f, .chaos = 15.0f,
          .warble = { .on = false }, .crush = { .amount = 55.0f, .type = C::fuzz, .drive = "+20 dB" },
          .hiss = { .on = false },
          .room = { .on = true, .amount = 60.0f, .size = "5 m", .duck = 40.0f } },
        { .name = "Lo-fi Kit", .category = "Drums", .seed = "drumkit", .damage = 60.0f, .chaos = 30.0f,
          .warble = { .on = false }, .crush = { .amount = 60.0f, .type = C::cassette },
          .bits = { .on = true, .amount = 55.0f, .type = B::sampler8 },
          .hiss = { .amount = 40.0f },
          .room = { .on = true, .amount = 30.0f, .type = R::closet } },

        // Keys
        { .name = "Warm Keys", .category = "Keys", .seed = "keys",
          .crush = { .amount = 45.0f }, .hiss = { .amount = 30.0f, .type = H::tape },
          .room = { .on = true, .amount = 35.0f, .size = "5 m" } },
        { .name = "Detuned Upright", .category = "Keys", .seed = "upright", .damage = 60.0f, .chaos = 60.0f,
          .warble = { .amount = 70.0f, .type = W::drift },
          .crush = { .amount = 30.0f, .type = C::valve },
          .hiss = { .amount = 40.0f },
          .room = { .on = true, .amount = 30.0f } },
        { .name = "Music Box Memory", .category = "Keys", .seed = "musicbox", .damage = 65.0f, .chaos = 35.0f,
          .warble = { .amount = 75.0f, .rate = "0.9 Hz" },
          .crush = { .amount = 45.0f, .type = C::cassette, .tone = -30.0f },
          .room = { .on = true, .amount = 40.0f, .type = R::hall, .size = "18 m" } },
        { .name = "Wobbly Organ", .category = "Keys", .seed = "organ", .damage = 55.0f, .chaos = 20.0f,
          .warble = { .amount = 60.0f, .type = W::flutter, .rate = "6 Hz", .spread = 80.0f },
          .crush = { .type = C::valve },
          .hiss = { .amount = 25.0f, .type = H::hum },
          .room = { .on = true, .amount = 30.0f, .type = R::spring } },

        // Vocals
        { .name = "Phone Call", .category = "Vocals", .seed = "hello", .damage = 70.0f, .chaos = 15.0f,
          .warble = { .on = false }, .crush = { .on = false },
          .bits = { .on = true, .amount = 85.0f, .type = B::phone },
          .hiss = { .amount = 30.0f, .type = H::hum, .tone = -50.0f },
          .room = { .on = true, .amount = 25.0f, .type = R::closet, .size = "1.0 m" } },
        { .name = "Old Radio Voice", .category = "Vocals", .seed = "wireless", .damage = 65.0f, .chaos = 30.0f,
          .warble = { .amount = 25.0f, .type = W::flutter },
          .crush = { .amount = 55.0f, .type = C::valve, .tone = 20.0f },
          .bits = { .on = true, .amount = 60.0f, .type = B::sampler8, .rate = "9 kHz", .depth = "10 bits" },
          .hiss = { .amount = 40.0f, .type = H::hum } },
        { .name = "Tape Double", .category = "Vocals", .seed = "double", .chaos = 30.0f,
          .warble = { .amount = 45.0f, .type = W::flutter, .spread = 90.0f },
          .crush = { .amount = 40.0f }, .hiss = { .on = false },
          .room = { .on = true, .amount = 25.0f, .type = R::closet } },
        { .name = "Distant Memory", .category = "Vocals", .seed = "memory", .damage = 60.0f, .chaos = 40.0f,
          .warble = { .amount = 45.0f },
          .crush = { .amount = 40.0f, .type = C::cassette, .tone = -45.0f },
          .hiss = { .amount = 40.0f, .type = H::tape },
          .room = { .on = true, .amount = 60.0f, .type = R::hall, .size = "22 m" } },

        // Guitar
        { .name = "Fuzz Box", .category = "Guitar", .seed = "fuzz", .damage = 70.0f, .chaos = 10.0f,
          .warble = { .on = false }, .crush = { .amount = 80.0f, .type = C::fuzz, .drive = "+26 dB" },
          .hiss = { .on = false },
          .room = { .on = true, .amount = 35.0f, .type = R::spring, .size = "1.4 s" } },
        { .name = "Spring Amp", .category = "Guitar", .seed = "amp", .damage = 55.0f, .chaos = 20.0f,
          .warble = { .on = false }, .crush = { .amount = 65.0f, .type = C::valve, .drive = "+16 dB" },
          .hiss = { .amount = 20.0f, .type = H::hum },
          .room = { .on = true, .amount = 55.0f, .type = R::spring, .size = "1.8 s" } },
        { .name = "Wobbly Clean", .category = "Guitar", .seed = "wobble",
          .warble = { .amount = 40.0f, .rate = "1.5 Hz", .spread = 70.0f },
          .crush = { .amount = 30.0f }, .hiss = { .on = false },
          .room = { .on = true, .amount = 30.0f } },
        { .name = "Broken Pedal", .category = "Guitar", .seed = "pedal", .damage = 65.0f, .chaos = 55.0f,
          .glitch = { .on = true, .amount = 45.0f, .rate = r1_8 },
          .warble = { .on = false }, .crush = { .amount = 60.0f, .type = C::fuzz },
          .bits = { .on = true, .amount = 35.0f, .type = B::sampler8 }, .hiss = { .on = false } },
    };

   #if defined(__GNUC__)
    #pragma GCC diagnostic pop
   #endif

    float flag (bool b) { return b ? 1.0f : 0.0f; }

    /** A typed control written as text, as a position (0 .. 100); nullptr: the default position. */
    float typed (TypedControl control, int type, bool synced, const char* text)
    {
        if (text == nullptr)
            return 50.0f;
        const auto position = typedControlFromText (control, type, synced, text);
        jassert (position >= 0.0f);     // a preset's text that does not parse: the factory-preset test names it
        return position >= 0.0f ? position : 50.0f;
    }
}

std::span<const FactoryPreset> factoryPresets()
{
    return presets;
}

myzel::plugin::PresetFormat presetFormat()
{
    myzel::plugin::PresetFormat format;
    format.pluginName = "Inkcap";
    format.presetType = params::presetTag;
    format.version = 1;
    format.parameterIds = params::soundParameterIds();
    format.lockableIds = { params::mix };
    format.stateProperties = SeedState::presetProperties();

    for (const auto& p : factoryPresets())
    {
        myzel::plugin::FactorySnapshot s { p.name, p.category, {} };
        auto& v = s.values;
        v.set (params::damage, p.damage);
        v.set (params::chaos, p.chaos);

        v.set (params::glitchOn, flag (p.glitch.on));
        v.set (params::glitchAmount, p.glitch.amount);
        v.set (params::glitchType, (float) p.glitch.type);
        v.set (params::glitchRate, (float) p.glitch.rate);
        v.set (params::glitchLength, typed (TypedControl::glitchLength, (int) p.glitch.type, false, p.glitch.length));
        v.set (params::glitchGroove, p.glitch.groove);

        v.set (params::warbleOn, flag (p.warble.on));
        v.set (params::warbleAmount, p.warble.amount);
        v.set (params::warbleType, (float) p.warble.type);
        v.set (params::warbleRate, typed (TypedControl::warbleRate, (int) p.warble.type, p.warble.sync, p.warble.rate));
        v.set (params::warbleSync, flag (p.warble.sync));
        v.set (params::warbleSpread, p.warble.spread);

        v.set (params::crushOn, flag (p.crush.on));
        v.set (params::crushAmount, p.crush.amount);
        v.set (params::crushType, (float) p.crush.type);
        v.set (params::crushDrive, typed (TypedControl::crushDrive, (int) p.crush.type, false, p.crush.drive));
        v.set (params::crushTone, p.crush.tone);

        v.set (params::bitsOn, flag (p.bits.on));
        v.set (params::bitsAmount, p.bits.amount);
        v.set (params::bitsType, (float) p.bits.type);
        v.set (params::bitsRate, typed (TypedControl::bitsRate, (int) p.bits.type, false, p.bits.rate));
        v.set (params::bitsDepth, typed (TypedControl::bitsDepth, (int) p.bits.type, false, p.bits.depth));
        v.set (params::bitsJitter, p.bits.jitter);

        v.set (params::hissOn, flag (p.hiss.on));
        v.set (params::hissAmount, p.hiss.amount);
        v.set (params::hissType, (float) p.hiss.type);
        v.set (params::hissTone, p.hiss.tone);
        v.set (params::hissFollow, p.hiss.follow);

        v.set (params::roomOn, flag (p.room.on));
        v.set (params::roomAmount, p.room.amount);
        v.set (params::roomType, (float) p.room.type);
        v.set (params::roomSize, typed (TypedControl::roomSize, (int) p.room.type, false, p.room.size));
        v.set (params::roomDuck, p.room.duck);

        v.set (params::mix, p.mix);
        v.set (params::output, p.output);

        const auto parsed = dsp::seedcode::parse (p.seed);
        jassert (parsed.has_value());
        const auto seed = parsed.has_value() ? parsed->seed : SeedState::initial().seed;
        v.set (seedstate::seed, juce::String (dsp::seedcode::format (seed)));
        v.set (seedstate::seedLabel, juce::String::fromUTF8 (parsed.has_value() ? parsed->label.c_str() : ""));
        format.factory.push_back (std::move (s));
    }

    return format;
}

} // namespace inkcap
