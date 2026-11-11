#include "ui/FactoryPresets.h"

namespace cerulean::ui
{

namespace
{
    // Algorithms (plugin order, see dsp/Algorithms.h).
    constexpr int room = 0, hall = 1, plate = 2, spring = 3, nimbus = 4, halo = 5, rise = 6, billow = 7, choir = 8, echo = 9;

    // Note values (see NoteValue in plugin/Parameters.h).
    constexpr int n1_16 = 2, n1_8 = 5, n1_8D = 6, n1_4 = 8, n1_4D = 9;

    // Param A positions with discrete meanings (see dsp/Mappings.h).
    constexpr float minus12 = 0.0f, plus5 = 25.0f, plus7 = 50.0f, plus12 = 75.0f, plus12plus19 = 100.0f;   // Halo
    constexpr float oneSpring = 0.0f, twoSprings = 50.0f, threeSprings = 100.0f;                          // Spring
    constexpr float vowelA = 0.0f, vowelE = 25.0f, vowelO = 75.0f, vowelU = 100.0f;                       // Choir

    /*
        The factory set: ~40 presets, voiced mainly for guitar and synth (a few for vocals and
        drums), grouped by character. Every field left out stays at the parameter's default
        (see FactoryPresets.h). Mix values assume an insert: under 50 % the dry stays at full level.

        Categories, in browser order: Utility, Rooms, Halls & Plates, Springs, Ambient, Shimmer,
        Swells & Blooms, Voices, Echoes.
    */
    constexpr FactoryPreset presets[] = {
        // Utility --------------------------------------------------------------------------
        { .name = "Init", .category = "Utility" },
        { .name = "Send Hall", .category = "Utility", .algorithm = hall, .decay = 2.6f, .preDelay = 25.0f, .size = 60.0f,
          .mix = 100.0f, .wetOnly = true, .inLowCut = 120.0f },

        // Rooms ----------------------------------------------------------------------------
        { .name = "Small Wood Room", .category = "Rooms", .algorithm = room, .decay = 0.7f, .preDelay = 6.0f, .size = 35.0f,
          .mod = 10.0f, .tone = 1.5f, .paramA = 35.0f, .paramB = 55.0f, .mix = 30.0f },
        { .name = "Drum Chamber", .category = "Rooms", .algorithm = room, .decay = 1.2f, .preDelay = 10.0f, .size = 55.0f,
          .mod = 15.0f, .tone = 2.5f, .paramA = 45.0f, .paramB = 75.0f, .mix = 25.0f },
        { .name = "Vocal Booth", .category = "Rooms", .algorithm = room, .decay = 0.4f, .preDelay = 2.0f, .size = 15.0f,
          .mod = 5.0f, .tone = -1.0f, .paramA = 25.0f, .paramB = 35.0f, .mix = 22.0f },
        { .name = "Live Room", .category = "Rooms", .algorithm = room, .decay = 1.5f, .preDelay = 12.0f, .size = 78.0f,
          .mod = 20.0f, .tone = 1.0f, .paramA = 62.0f, .paramB = 70.0f, .mix = 30.0f },
        { .name = "Dark Studio", .category = "Rooms", .algorithm = room, .decay = 0.9f, .preDelay = 8.0f, .size = 45.0f,
          .mod = 12.0f, .tone = -3.5f, .paramA = 50.0f, .paramB = 12.0f, .mix = 30.0f, .lowEnd = 0.8f },

        // Halls & Plates -------------------------------------------------------------------
        { .name = "Concert Hall", .category = "Halls & Plates", .algorithm = hall, .decay = 3.2f, .preDelay = 28.0f, .size = 65.0f,
          .mod = 30.0f, .tone = -1.0f, .paramA = 45.0f, .paramB = 55.0f, .mix = 30.0f },
        { .name = "Cathedral", .category = "Halls & Plates", .algorithm = hall, .decay = 7.5f, .preDelay = 45.0f, .size = 92.0f,
          .mod = 35.0f, .tone = -2.0f, .paramA = 30.0f, .paramB = 85.0f, .mix = 38.0f, .lowEnd = 1.3f },
        { .name = "Eighties Hall", .category = "Halls & Plates", .algorithm = hall, .decay = 2.8f, .preDelay = 35.0f, .size = 60.0f,
          .mod = 75.0f, .tone = 2.5f, .paramA = 20.0f, .paramB = 25.0f, .mix = 32.0f, .modRate = 1.6f, .width = 130.0f },
        { .name = "Bright Plate", .category = "Halls & Plates", .algorithm = plate, .decay = 2.2f, .preDelay = 12.0f,
          .mod = 25.0f, .tone = 3.5f, .paramA = 70.0f, .paramB = 35.0f, .mix = 28.0f },
        { .name = "Vintage Plate", .category = "Halls & Plates", .algorithm = plate, .decay = 1.8f, .preDelay = 18.0f,
          .mod = 15.0f, .tone = -2.0f, .paramA = 55.0f, .paramB = 20.0f, .mix = 28.0f, .lowEnd = 0.8f },
        { .name = "Steel Sheen", .category = "Halls & Plates", .algorithm = plate, .decay = 2.6f, .preDelay = 8.0f,
          .mod = 20.0f, .tone = 3.0f, .paramA = 80.0f, .paramB = 88.0f, .mix = 25.0f, .inLowCut = 150.0f },
        { .name = "Snare Plate", .category = "Halls & Plates", .algorithm = plate, .decay = 1.0f, .preDelay = 0.0f, .size = 40.0f,
          .mod = 10.0f, .tone = 2.0f, .paramA = 85.0f, .paramB = 50.0f, .mix = 25.0f, .inLowCut = 200.0f },

        // Springs --------------------------------------------------------------------------
        { .name = "Surf Tank", .category = "Springs", .algorithm = spring, .decay = 2.4f, .preDelay = 0.0f,
          .mod = 10.0f, .tone = 1.0f, .paramA = threeSprings, .paramB = 65.0f, .mix = 35.0f },
        { .name = "Two-Spring Combo", .category = "Springs", .algorithm = spring, .decay = 2.0f, .preDelay = 0.0f,
          .mod = 8.0f, .paramA = twoSprings, .paramB = 40.0f, .mix = 28.0f },
        { .name = "Dub Splash", .category = "Springs", .algorithm = spring, .decay = 3.6f, .preDelay = 0.0f,
          .mod = 12.0f, .tone = -1.0f, .paramA = threeSprings, .paramB = 100.0f, .mix = 45.0f, .inLowCut = 120.0f },
        { .name = "Subtle Spring", .category = "Springs", .algorithm = spring, .decay = 1.6f, .preDelay = 0.0f,
          .mod = 5.0f, .tone = -0.5f, .paramA = oneSpring, .paramB = 15.0f, .mix = 22.0f },

        // Ambient --------------------------------------------------------------------------
        { .name = "Endless Nimbus", .category = "Ambient", .algorithm = nimbus, .decay = 12.0f, .preDelay = 40.0f, .size = 80.0f,
          .mod = 55.0f, .tone = -2.5f, .paramA = 80.0f, .paramB = 40.0f, .mix = 45.0f,
          .lowEnd = 1.3f, .diffusion = 90.0f, .width = 130.0f },
        { .name = "Fog Bank", .category = "Ambient", .algorithm = nimbus, .decay = 25.0f, .preDelay = 20.0f, .size = 85.0f,
          .mod = 45.0f, .tone = -4.0f, .paramA = 100.0f, .paramB = 70.0f, .mix = 48.0f, .lowEnd = 1.4f },
        { .name = "Distant Horizon", .category = "Ambient", .algorithm = nimbus, .decay = 15.0f, .preDelay = 80.0f, .size = 75.0f,
          .mod = 40.0f, .tone = -1.5f, .paramA = 70.0f, .paramB = 100.0f, .mix = 42.0f, .width = 140.0f },
        { .name = "Glacier", .category = "Ambient", .algorithm = nimbus, .decay = 60.0f, .preDelay = 30.0f, .size = 90.0f,
          .mod = 30.0f, .tone = -3.0f, .paramA = 85.0f, .paramB = 55.0f, .mix = 50.0f, .lowEnd = 1.2f, .modRate = 0.6f },
        { .name = "Pad Machine", .category = "Ambient", .algorithm = hall, .decay = 9.0f, .preDelay = 30.0f, .size = 95.0f,
          .mod = 60.0f, .tone = -1.0f, .paramA = 15.0f, .paramB = 70.0f, .mix = 45.0f, .width = 140.0f },
        { .name = "Night Sky", .category = "Ambient", .algorithm = nimbus, .decay = 18.0f, .preDelay = 50.0f, .size = 70.0f,
          .mod = 50.0f, .tone = -6.0f, .paramA = 60.0f, .paramB = 60.0f, .mix = 45.0f, .duck = true, .lowEnd = 1.6f },

        // Shimmer --------------------------------------------------------------------------
        { .name = "Octave Halo", .category = "Shimmer", .algorithm = halo, .decay = 8.0f, .preDelay = 30.0f, .size = 70.0f,
          .mod = 40.0f, .tone = 1.0f, .paramA = plus12, .paramB = 50.0f, .mix = 40.0f, .inLowCut = 150.0f },
        { .name = "Fifth Halo", .category = "Shimmer", .algorithm = halo, .decay = 10.0f, .preDelay = 30.0f, .size = 70.0f,
          .mod = 40.0f, .paramA = plus7, .paramB = 45.0f, .mix = 40.0f, .inLowCut = 150.0f },
        { .name = "Heavenly", .category = "Shimmer", .algorithm = halo, .decay = 14.0f, .preDelay = 40.0f, .size = 85.0f,
          .mod = 45.0f, .tone = 0.5f, .paramA = plus12plus19, .paramB = 70.0f, .mix = 50.0f, .width = 130.0f, .inLowCut = 200.0f },
        { .name = "Undertow", .category = "Shimmer", .algorithm = halo, .decay = 9.0f, .preDelay = 25.0f, .size = 75.0f,
          .mod = 35.0f, .tone = -2.0f, .paramA = minus12, .paramB = 50.0f, .mix = 40.0f },
        { .name = "Subtle Sparkle", .category = "Shimmer", .algorithm = halo, .decay = 5.0f, .preDelay = 20.0f, .size = 60.0f,
          .mod = 30.0f, .tone = 1.5f, .paramA = plus12, .paramB = 22.0f, .mix = 30.0f },
        { .name = "Fourth Lift", .category = "Shimmer", .algorithm = halo, .decay = 7.0f, .preDelay = 30.0f, .size = 65.0f,
          .mod = 40.0f, .paramA = plus5, .paramB = 40.0f, .mix = 38.0f, .duck = true },

        // Swells & Blooms ------------------------------------------------------------------
        { .name = "Violin Swell", .category = "Swells & Blooms", .algorithm = rise, .decay = 4.0f, .preDelay = 0.0f,
          .mod = 30.0f, .paramA = 55.0f, .paramB = 0.0f, .mix = 50.0f },
        { .name = "Bowed Guitar", .category = "Swells & Blooms", .algorithm = rise, .decay = 6.0f, .preDelay = 0.0f, .size = 70.0f,
          .mod = 35.0f, .tone = -1.0f, .paramA = 62.0f, .paramB = 100.0f, .mix = 45.0f },
        { .name = "Quick Fade-In", .category = "Swells & Blooms", .algorithm = rise, .decay = 2.5f, .preDelay = 0.0f,
          .mod = 25.0f, .paramA = 25.0f, .paramB = 100.0f, .mix = 30.0f },
        { .name = "Slow Bloom", .category = "Swells & Blooms", .algorithm = billow, .decay = 6.0f, .preDelay = 15.0f, .size = 60.0f,
          .mod = 40.0f, .tone = -1.5f, .paramA = 60.0f, .paramB = 55.0f, .mix = 40.0f },
        { .name = "Post-Rock Build", .category = "Swells & Blooms", .algorithm = billow, .decay = 12.0f, .preDelay = 20.0f, .size = 80.0f,
          .mod = 45.0f, .tone = -0.5f, .paramA = 85.0f, .paramB = 85.0f, .mix = 50.0f, .width = 135.0f },
        { .name = "Gentle Bloom", .category = "Swells & Blooms", .algorithm = billow, .decay = 4.0f, .preDelay = 15.0f,
          .mod = 30.0f, .paramA = 30.0f, .paramB = 25.0f, .mix = 30.0f, .duck = true },

        // Voices ---------------------------------------------------------------------------
        { .name = "Choir Of Ahs", .category = "Voices", .algorithm = choir, .decay = 6.0f, .preDelay = 25.0f, .size = 60.0f,
          .mod = 45.0f, .paramA = vowelA, .paramB = 66.0f, .mix = 45.0f },
        { .name = "Ghost Choir", .category = "Voices", .algorithm = choir, .decay = 12.0f, .preDelay = 40.0f, .size = 80.0f,
          .mod = 65.0f, .tone = -2.0f, .paramA = 88.0f, .paramB = 100.0f, .mix = 48.0f },
        { .name = "Talking Hall", .category = "Voices", .algorithm = choir, .decay = 4.0f, .preDelay = 20.0f,
          .mod = 80.0f, .paramA = vowelE, .paramB = 25.0f, .mix = 35.0f, .modRate = 0.5f },
        { .name = "Monks", .category = "Voices", .algorithm = choir, .decay = 9.0f, .preDelay = 30.0f, .size = 75.0f,
          .mod = 35.0f, .tone = -3.0f, .paramA = vowelO, .paramB = 80.0f, .mix = 45.0f, .lowEnd = 1.3f },
        { .name = "Distant Oohs", .category = "Voices", .algorithm = choir, .decay = 8.0f, .preDelay = 60.0f, .size = 70.0f,
          .mod = 50.0f, .tone = -1.0f, .paramA = vowelU, .paramB = 55.0f, .mix = 40.0f },

        // Echoes ---------------------------------------------------------------------------
        { .name = "Tape Echo Wash", .category = "Echoes", .algorithm = echo, .decay = 3.0f, .preDelay = 0.0f,
          .mod = 25.0f, .tone = -2.0f, .paramA = 60.0f, .paramB = 45.0f, .mix = 35.0f, .duck = true,
          .echoSync = true, .echoNote = n1_8D },
        { .name = "Dub Echo Verb", .category = "Echoes", .algorithm = echo, .decay = 4.0f, .preDelay = 0.0f,
          .mod = 30.0f, .tone = -3.0f, .paramB = 80.0f, .mix = 40.0f, .inLowCut = 150.0f,
          .echoSync = true, .echoNote = n1_4D },
        { .name = "Slapback Room", .category = "Echoes", .algorithm = echo, .decay = 0.8f, .preDelay = 0.0f,
          .mod = 15.0f, .paramA = 40.0f, .paramB = 5.0f, .mix = 30.0f },
        { .name = "Ambient Echoes", .category = "Echoes", .algorithm = echo, .decay = 8.0f, .preDelay = 0.0f, .size = 70.0f,
          .mod = 40.0f, .tone = -1.0f, .paramB = 60.0f, .mix = 45.0f, .duck = true, .echoSync = true, .echoNote = n1_4 },
        { .name = "Synced Hall", .category = "Echoes", .algorithm = hall, .decay = 2.5f, .size = 55.0f,
          .mix = 30.0f, .preDelaySync = true, .preDelayNote = n1_8 },
        { .name = "Rhythmic Room", .category = "Echoes", .algorithm = room, .decay = 1.0f, .size = 60.0f,
          .paramA = 40.0f, .paramB = 60.0f, .mix = 30.0f, .preDelaySync = true, .preDelayNote = n1_16 },
    };
}

std::span<const FactoryPreset> factoryPresets()
{
    return presets;
}

} // namespace cerulean::ui
