#include "ui/FactoryPresets.h"

namespace umber::ui
{

namespace
{
    constexpr int reel = 0, desk = 1, classA = 2, valve = 3, fuzz = 4;
    constexpr int mid = 1, side = 2;
    constexpr float off = 20.0f, open = 20000.0f;

    // name, category, drive, model, slam, tone, lowCut, weight, highCut, auto, mix, output
    // [, stereo mode, emphasis, bias]
    constexpr FactoryPreset presets[] = {
        { "Clean Slate",        "Utility",      3.0f, classA, false,  0.0f, off,    false, open,    true, 100.0f,  0.0f },

        { "Tape Glue Kit",      "Drums",        4.5f, reel,   false,  0.5f, off,    false, 16000.0f, true, 100.0f, 0.0f },
        { "Snare Crack",        "Drums",        6.0f, classA, false,  2.5f, 120.0f, true,  open,    true, 100.0f,  0.0f },
        { "Kick Thump",         "Drums",        5.0f, desk,   false, -1.5f, 40.0f,  true,  8000.0f, true, 100.0f,  0.0f },
        { "Room Smash",         "Drums",        7.5f, fuzz,   true,   1.0f, 150.0f, false, 9000.0f, true,  35.0f,  0.0f },
        { "Overhead Silk",      "Drums",        2.5f, reel,   false,  1.5f, 90.0f,  false, open,    true, 100.0f,  0.0f },
        { "Parallel Crush",     "Drums",        8.0f, valve,  true,   0.0f, off,    false, open,    true,  30.0f,  0.0f },
        { "Hot Snare Crack",    "Drums",        5.0f, classA, false,  1.0f, 120.0f, false, open,    true, 100.0f,  0.0f, 0,  4.0f,  6.0f },
        { "Kick Body",          "Drums",        5.0f, valve,  false, -1.0f, 30.0f,  true,  9000.0f, true, 100.0f, 0.0f, 0, -6.0f, -4.0f },

        { "Round Bass",         "Bass",         4.0f, valve,  false, -2.0f, 30.0f,  true,  10000.0f, true, 100.0f, 0.0f },
        { "Growl Bass",         "Bass",         5.5f, fuzz,   false,  0.5f, 60.0f,  true,  5000.0f, true,  55.0f,  0.0f },
        { "Console DI",         "Bass",         3.5f, desk,   false, -0.5f, off,    false, open,    true, 100.0f,  0.0f },
        { "Top-End Grit",       "Bass",         5.0f, fuzz,   false,  0.0f, 40.0f,  false, open,    true,  70.0f,  0.0f, 0,  7.0f,  0.0f },
        { "Even Sub",           "Bass",         4.5f, reel,   false, -1.0f, 30.0f,  true,  12000.0f, true, 100.0f, 0.0f, 0, -5.0f, -6.0f },

        { "Vocal Warmth",       "Vocals",       3.0f, valve,  false,  1.0f, 100.0f, false, 14000.0f, true, 100.0f, 0.0f },
        { "Radio Voice",        "Vocals",       6.5f, classA, false,  3.0f, 400.0f, false, 3500.0f, true, 100.0f,  0.0f },
        { "Breathy Air",        "Vocals",       2.0f, reel,   false,  3.5f, 150.0f, false, open,    true, 100.0f,  0.0f },
        { "Megaphone",          "Vocals",       6.0f, fuzz,   false,  4.0f, 500.0f, false, 2500.0f, true, 100.0f,  0.0f },
        { "Vocal Edge",         "Vocals",       4.0f, valve,  false,  0.5f, 90.0f,  false, 16000.0f, true, 100.0f, 0.0f, 0,  4.0f,  5.0f },

        { "Amp In A Box",       "Guitar & Keys", 7.0f, valve, false,  1.5f, 80.0f,  false, 7000.0f, true, 100.0f,  0.0f },
        { "Rhodes Bark",        "Guitar & Keys", 5.0f, classA, false, 1.0f, off,    false, 12000.0f, true, 100.0f, 0.0f },
        { "Synth Fuzz Lead",    "Guitar & Keys", 8.0f, fuzz,  true,  -1.0f, off,    false, 6000.0f, true, 100.0f,  0.0f },
        { "Cold Amp Sputter",   "Guitar & Keys", 7.0f, fuzz,  false,  1.0f, 90.0f,  false, 7000.0f, true, 100.0f,  0.0f, 0,  0.0f, -9.0f },
        { "Piano Glow",         "Guitar & Keys", 3.5f, desk,  false,  0.0f, off,    false, open,    true, 100.0f,  0.0f, 0, -2.0f,  5.0f },

        { "Bus Tape 15 ips",    "Mix Bus",      2.0f, reel,   false, -0.5f, off,    false, open,    true, 100.0f,  0.0f },
        { "Console Sum",        "Mix Bus",      2.5f, desk,   false,  0.0f, off,    false, open,    true, 100.0f,  0.0f },
        { "Master Sheen",       "Mix Bus",      1.5f, classA, false,  1.0f, off,    false, open,    true,  60.0f,  0.0f },
        { "Warm Blanket",       "Mix Bus",      3.0f, valve,  false, -2.5f, off,    false, 12000.0f, true, 100.0f, 0.0f },
        { "Side Sheen",         "Mix Bus",      3.0f, classA, false,  0.5f, off,    false, open,    true, 100.0f,  0.0f, side, 4.0f },
        { "Mid Focus",          "Mix Bus",      3.5f, desk,   false,  0.0f, 30.0f,  false, open,    true, 100.0f,  0.0f, mid },
        { "Wide Tape Sides",    "Mix Bus",      4.5f, reel,   false,  0.5f, 60.0f,  false, open,    true, 100.0f,  0.0f, side, 2.0f },
        { "Mid Glue Valve",     "Mix Bus",      3.0f, valve,  false,  0.0f, off,    false, open,    true,  80.0f,  0.0f, mid, 0.0f, -3.0f },

        { "Blown Speaker",      "Lo-Fi & Destruction", 9.0f, fuzz,   true,   2.0f, 300.0f, false, 3000.0f, true, 100.0f, 0.0f },
        { "Cassette Dub",       "Lo-Fi & Destruction", 7.0f, reel,   false, -3.5f, 60.0f,  false, 6000.0f, true, 100.0f, 0.0f },
        { "AM Transmitter",     "Lo-Fi & Destruction", 8.0f, classA, false,  2.0f, 350.0f, false, 2200.0f, true, 100.0f, 0.0f },
        { "Burnt Toast",        "Lo-Fi & Destruction", 9.0f, valve,  true,   4.0f, 200.0f, false, open,    true,  70.0f, 0.0f },
        { "Scorched Earth",     "Lo-Fi & Destruction", 10.0f, fuzz,  true,   0.0f, off,    false, open,    true, 100.0f, 0.0f },
        { "Starved Power Tube", "Lo-Fi & Destruction", 6.0f, fuzz,   false,  1.0f, 80.0f,  false, 9000.0f, true, 100.0f, 0.0f, 0, 3.0f, -7.0f },
    };
}

std::span<const FactoryPreset> factoryPresets()
{
    return presets;
}

} // namespace umber::ui
