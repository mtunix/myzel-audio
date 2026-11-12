#include "ui/FactoryPresets.h"

#include "plugin/Parameters.h"

namespace mulberry::ui
{

namespace
{
    // Machine and mode indices (Machines.h).
    constexpr int clean = 0, rack = 1, bucket = 2, oilCan = 3, loop = 4, reel = 5;
    constexpr int dual = 1, pingPong = 2, pattern = 3;

    // Notes (NoteValues.h).
    constexpr int n1_8T = 9, n1_8 = 10, n1_8D = 11, n1_4 = 13, n1_4D = 14, n1_2 = 16, n1_1 = 19;

    // Stepped character positions, at the centre of their share of the knob (MachineInfo.h).
    constexpr float bits8 = 5.6f, bits12 = 50.0f, bits16 = 94.4f;
    constexpr float chip1k = 12.5f, chip2k = 37.5f, chip4k = 62.5f, chip8k = 87.5f;
    constexpr float heads1 = 7.1f, heads123 = 92.9f, heads12 = 50.0f;
    constexpr float ips7 = 16.7f, ips15 = 50.0f, ips30 = 83.3f;
    constexpr float shift0 = 50.0f, shiftFifth = 78.0f, shiftOctave = 98.0f;

    constexpr FactoryPreset presets[] = {
        // Utility
        { .name = "Init", .category = "Utility" },
        { .name = "Clean Quarter", .category = "Utility", .machine = clean, .note = n1_4, .feedback = 30.0f, .paramB = shift0, .mix = 30.0f },
        { .name = "Send Echo", .category = "Utility", .note = n1_8D, .feedback = 40.0f, .mix = 100.0f, .wetOnly = true },

        // Slapback
        { .name = "Sun Slap", .category = "Slapback", .sync = false, .time = 110.0f, .feedback = 8.0f, .drive = 4.0f,
          .highCut = 8000.0f, .paramA = ips15, .mix = 30.0f },
        { .name = "Rockabilly Loop", .category = "Slapback", .machine = loop, .sync = false, .time = 95.0f, .feedback = 15.0f,
          .paramA = heads1, .paramB = 30.0f, .mix = 35.0f },
        { .name = "Rack Double", .category = "Slapback", .machine = rack, .mode = dual, .sync = false, .time = 45.0f, .feedback = 0.0f,
          .paramA = bits16, .paramB = 80.0f, .timeRight = 62.0f, .mix = 40.0f },
        { .name = "Bucket Slap", .category = "Slapback", .machine = bucket, .sync = false, .time = 130.0f, .feedback = 12.0f,
          .paramA = chip1k, .paramB = 70.0f, .mix = 30.0f },

        // Tape Echoes
        { .name = "Studio Reel", .category = "Tape Echoes", .note = n1_8D, .feedback = 38.0f, .paramA = ips30, .mix = 30.0f },
        { .name = "Three Heads", .category = "Tape Echoes", .machine = loop, .note = n1_8, .feedback = 45.0f, .paramA = heads123,
          .paramB = 40.0f, .mix = 35.0f },
        { .name = "Worn Loop", .category = "Tape Echoes", .machine = loop, .note = n1_8D, .feedback = 50.0f, .wobble = 75.0f,
          .highCut = 6000.0f, .paramA = heads1, .paramB = 85.0f, .mix = 35.0f, .noise = 40.0f },
        { .name = "Hot Tape", .category = "Tape Echoes", .note = n1_4, .feedback = 50.0f, .drive = 12.0f, .paramA = ips7,
          .paramB = 35.0f, .mix = 32.0f, .output = -2.0f },
        { .name = "Dual Reels", .category = "Tape Echoes", .mode = dual, .note = n1_8D, .feedback = 40.0f, .paramA = ips15,
          .noteRight = n1_4, .mix = 32.0f, .output = -1.0f, .crossFeed = 20.0f },
        { .name = "Tape Ping", .category = "Tape Echoes", .mode = pingPong, .note = n1_8, .feedback = 45.0f, .paramA = ips15, .mix = 30.0f },

        // Analog
        { .name = "Bucket Quarter", .category = "Analog", .machine = bucket, .note = n1_4, .feedback = 40.0f, .paramA = chip4k,
          .paramB = 60.0f, .mix = 32.0f },
        { .name = "Dark Brigade", .category = "Analog", .machine = bucket, .sync = false, .time = 600.0f, .feedback = 55.0f,
          .paramA = chip1k, .paramB = 60.0f, .mix = 35.0f },
        { .name = "Chorus Echo", .category = "Analog", .machine = bucket, .sync = false, .time = 280.0f, .feedback = 35.0f,
          .wobble = 80.0f, .paramA = chip2k, .paramB = 60.0f, .mix = 40.0f, .wobbleRate = 1.5f },
        { .name = "Oil Can Classic", .category = "Analog", .machine = oilCan, .note = n1_8D, .feedback = 45.0f, .paramA = 40.0f,
          .paramB = 40.0f, .mix = 35.0f },
        { .name = "Old Oil", .category = "Analog", .machine = oilCan, .sync = false, .time = 320.0f, .feedback = 50.0f, .wobble = 70.0f,
          .paramA = 70.0f, .paramB = 90.0f, .mix = 38.0f },
        { .name = "Bucket Ping", .category = "Analog", .machine = bucket, .mode = pingPong, .note = n1_8D, .feedback = 45.0f,
          .paramA = chip8k, .paramB = 60.0f, .mix = 32.0f },

        // Digital & Lo-Fi
        { .name = "Rack 12-Bit", .category = "Digital & Lo-Fi", .machine = rack, .note = n1_8D, .feedback = 40.0f, .paramA = bits12,
          .paramB = 40.0f, .mix = 32.0f },
        { .name = "8-Bit Dust", .category = "Digital & Lo-Fi", .machine = rack, .sync = false, .time = 250.0f, .feedback = 55.0f,
          .drive = 6.0f, .paramA = bits8, .paramB = 0.0f, .mix = 35.0f, .output = -2.0f },
        { .name = "Clean Ping-Pong", .category = "Digital & Lo-Fi", .machine = clean, .mode = pingPong, .note = n1_8D, .feedback = 45.0f,
          .paramB = shift0, .mix = 30.0f },
        { .name = "Octave Stairs", .category = "Digital & Lo-Fi", .machine = clean, .note = n1_4, .feedback = 55.0f, .highCut = 9000.0f,
          .paramB = shiftOctave, .mix = 35.0f },
        { .name = "Fifth Ladder", .category = "Digital & Lo-Fi", .machine = clean, .mode = dual, .note = n1_8D, .feedback = 45.0f,
          .paramB = shiftFifth, .noteRight = n1_4, .mix = 32.0f },
        { .name = "Glide Delay", .category = "Digital & Lo-Fi", .machine = clean, .sync = false, .time = 400.0f, .feedback = 50.0f,
          .paramA = 100.0f, .paramB = shift0, .mix = 35.0f },

        // Rhythmic
        { .name = "Gallop", .category = "Rhythmic", .mode = pattern, .note = n1_2, .feedback = 25.0f, .paramA = ips15, .steps = 4,
          .mix = 32.0f, .panRule = 1,
          .stepLevels = { 100.0f, 0.0f, 70.0f, 40.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f } },
        { .name = "Swing Eighths", .category = "Rhythmic", .machine = clean, .note = n1_8, .feedback = 40.0f, .paramB = shift0,
          .mix = 30.0f, .groove = 66.0f },
        { .name = "Cascade", .category = "Rhythmic", .machine = rack, .mode = pattern, .note = n1_2, .feedback = 20.0f, .paramA = bits16,
          .paramB = 70.0f, .steps = 8, .mix = 32.0f, .panRule = 2,
          .stepLevels = { 100.0f, 85.0f, 70.0f, 60.0f, 50.0f, 40.0f, 30.0f, 20.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f } },
        { .name = "Scatter Steps", .category = "Rhythmic", .machine = bucket, .mode = pattern, .note = n1_1, .feedback = 15.0f,
          .paramA = chip4k, .paramB = 60.0f, .steps = 16, .mix = 32.0f, .panRule = 3,
          .stepLevels = { 90.0f, 0.0f, 40.0f, 0.0f, 70.0f, 30.0f, 0.0f, 55.0f, 0.0f, 80.0f, 0.0f, 35.0f, 60.0f, 0.0f, 45.0f, 25.0f } },
        { .name = "Triplet Ping", .category = "Rhythmic", .machine = loop, .mode = pingPong, .note = n1_8T, .feedback = 40.0f,
          .paramA = heads1, .paramB = 30.0f, .mix = 30.0f },
        { .name = "Polyrhythm", .category = "Rhythmic", .machine = clean, .mode = dual, .note = n1_8D, .feedback = 40.0f,
          .paramB = shift0, .noteRight = n1_4, .mix = 30.0f },
        { .name = "Oil Pattern", .category = "Rhythmic", .machine = oilCan, .mode = pattern, .note = n1_2, .feedback = 30.0f,
          .paramA = 30.0f, .paramB = 40.0f, .steps = 6, .mix = 32.0f, .panRule = 1,
          .stepLevels = { 100.0f, 40.0f, 70.0f, 40.0f, 90.0f, 40.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f } },

        // Dub & Runaway
        { .name = "Dub Loop", .category = "Dub & Runaway", .machine = loop, .note = n1_8D, .feedback = 92.0f, .drive = 6.0f,
          .lowCut = 150.0f, .highCut = 3500.0f, .paramA = heads12, .paramB = 50.0f, .mix = 40.0f },
        { .name = "Runaway Reel", .category = "Dub & Runaway", .note = n1_8D, .feedback = 105.0f, .drive = 6.0f, .lowCut = 200.0f,
          .highCut = 4000.0f, .paramA = ips15, .mix = 38.0f },
        { .name = "Bucket Runaway", .category = "Dub & Runaway", .machine = bucket, .note = n1_4, .feedback = 108.0f, .highCut = 3000.0f,
          .paramA = chip2k, .paramB = 60.0f, .mix = 38.0f },
        { .name = "Dub Ping", .category = "Dub & Runaway", .mode = pingPong, .note = n1_8D, .feedback = 85.0f, .lowCut = 120.0f,
          .highCut = 5000.0f, .paramA = ips15, .mix = 38.0f, .duck = true },
        { .name = "Rack Meltdown", .category = "Dub & Runaway", .machine = rack, .note = n1_8, .feedback = 112.0f, .paramA = bits8,
          .paramB = 20.0f, .mix = 35.0f, .output = -3.0f },

        // Ambient & Diffuse
        { .name = "Smear Cloud", .category = "Ambient & Diffuse", .machine = oilCan, .sync = false, .time = 700.0f, .feedback = 65.0f,
          .diffuse = 70.0f, .paramA = 100.0f, .paramB = 60.0f, .mix = 45.0f, .output = -3.0f },
        { .name = "Diffuse Reel", .category = "Ambient & Diffuse", .note = n1_4D, .feedback = 70.0f, .diffuse = 60.0f,
          .highCut = 7000.0f, .paramA = ips30, .mix = 40.0f, .duck = true },
        { .name = "Endless Wash", .category = "Ambient & Diffuse", .machine = clean, .note = n1_2, .feedback = 95.0f, .diffuse = 85.0f,
          .lowCut = 150.0f, .highCut = 6000.0f, .paramB = shift0, .mix = 40.0f },
        { .name = "Octave Shimmer", .category = "Ambient & Diffuse", .machine = clean, .mode = pingPong, .note = n1_4D, .feedback = 65.0f,
          .diffuse = 50.0f, .highCut = 8000.0f, .paramB = shiftOctave, .mix = 38.0f },
        { .name = "Bucket Haze", .category = "Ambient & Diffuse", .machine = bucket, .mode = dual, .note = n1_4D, .feedback = 65.0f,
          .diffuse = 55.0f, .paramA = chip8k, .paramB = 60.0f, .noteRight = n1_2, .mix = 40.0f, .crossFeed = 30.0f },
    };
}

std::span<const FactoryPreset> factoryPresets()
{
    return presets;
}

myzel::plugin::PresetFormat presetFormat()
{
    myzel::plugin::PresetFormat format;
    format.pluginName = "Mulberry";
    format.presetType = "MulberryPreset";
    format.version = 1;
    format.parameterIds = params::soundParameterIds();
    format.lockableIds = { params::mix, params::wetOnly };

    for (const auto& p : factoryPresets())
    {
        auto flag = [] (bool b) { return b ? 1.0f : 0.0f; };

        myzel::plugin::FactorySnapshot s { p.name, p.category, {} };
        auto& v = s.values;
        v.set (params::machine, p.machine);
        v.set (params::mode, p.mode);
        v.set (params::sync, flag (p.sync));
        v.set (params::time, p.time);
        v.set (params::note, p.note);
        v.set (params::feedback, p.feedback);
        v.set (params::drive, p.drive);
        v.set (params::wobble, p.wobble);
        v.set (params::diffuse, p.diffuse);
        v.set (params::lowCut, p.lowCut);
        v.set (params::highCut, p.highCut);
        v.set (params::paramA, p.paramA);
        v.set (params::paramB, p.paramB);
        v.set (params::timeRight, p.timeRight);
        v.set (params::noteRight, p.noteRight);
        v.set (params::width, p.width);
        v.set (params::steps, p.steps);
        v.set (params::mix, p.mix);
        v.set (params::output, p.output);
        v.set (params::duck, flag (p.duck));
        v.set (params::wetOnly, flag (p.wetOnly));

        v.set (params::groove, p.groove);
        v.set (params::feel, p.feel);
        v.set (params::crossFeed, p.crossFeed);
        v.set (params::panRule, p.panRule);
        v.set (params::wobbleRate, p.wobbleRate);
        v.set (params::wobbleShape, p.wobbleShape);
        v.set (params::noise, p.noise);
        v.set (params::trails, flag (p.trails));
        v.set (params::duckAmount, p.duckAmount);
        v.set (params::duckThreshold, p.duckThreshold);
        v.set (params::duckRelease, p.duckRelease);
        for (int i = 0; i < params::maxSteps; ++i)
            v.set (juce::Identifier (params::stepId (i)), p.stepLevels[(size_t) i]);
        format.factory.push_back (std::move (s));
    }

    return format;
}

} // namespace mulberry::ui
