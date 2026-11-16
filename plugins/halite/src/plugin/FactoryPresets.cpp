#include "plugin/FactoryPresets.h"

#include "plugin/Parameters.h"

namespace halite
{

namespace
{
    /** A factory preset at the parameters' defaults, so an entry lists only what it changes. */
    myzel::plugin::FactorySnapshot preset (const char* category, const char* name,
                                           std::initializer_list<std::pair<const char*, float>> changes = {})
    {
        myzel::plugin::FactorySnapshot s;
        s.name = name;
        s.category = category;

        for (const auto& [id, value] : changes)
            s.values.set (id, value);

        return s;
    }
}

myzel::plugin::PresetFormat presetFormat()
{
    myzel::plugin::PresetFormat f;
    f.pluginName = "Halite";
    f.presetType = params::presetTag;
    f.version = 1;
    f.parameterIds = params::soundParameterIds();

    // From the M3 measurements and the controls' meaning, not by ear (docs/MANUAL.md, Presets). Choices are
    // indices in the frozen lists; the absolute thresholds assume a source near -18 dBFS RMS.
    using namespace params;
    constexpr float spectral = 1.0f, wide = 2.0f, allround = 1.0f, mid = 2.0f, on = 1.0f, off = 0.0f;

    f.factory = {
        preset ("Utility", "Init"),
        preset ("Utility", "Gentle", { { threshold, -14.0f }, { range, 5.0f } }),
        preset ("Utility", "Firm", { { threshold, -21.0f }, { range, 12.0f } }),
        preset ("Utility", "Live tracking", { { threshold, -18.0f }, { lookahead, 0.0f } }),
        preset ("Utility", "Wide band", { { shape, wide }, { threshold, -16.0f }, { range, 6.0f } }),

        preset ("Vocals", "Lead female", { { detectLow, 5500.0f }, { detectHigh, 14000.0f } }),
        preset ("Vocals", "Lead male", { { detectLow, 4000.0f }, { detectHigh, 11000.0f } }),
        preset ("Vocals", "Rap", { { shape, spectral }, { threshold, -18.0f }, { range, 10.0f }, { speedAuto, off }, { release, 15.0f } }),
        preset ("Vocals", "Choir", { { shape, spectral }, { threshold, -19.0f }, { range, 6.0f }, { detectLow, 5000.0f }, { detectHigh, 13000.0f },
                                     { speedAuto, off }, { release, 40.0f } }),
        preset ("Vocals", "Backing stack", { { threshold, -21.0f }, { range, 12.0f }, { speedAuto, off }, { release, 30.0f } }),
        preset ("Vocals", "Whisper", { { shape, spectral }, { threshold, -15.0f }, { range, 4.0f }, { focus, 85.0f } }),
        preset ("Vocals", "Bright pop", { { shape, spectral }, { threshold, -19.0f }, { range, 10.0f }, { detectLow, 5000.0f }, { detectHigh, 16000.0f },
                                          { focus, 80.0f } }),
        preset ("Vocals", "Harsh lead", { { harshOn, on }, { harshThreshold, -11.0f }, { harshRange, 5.0f } }),
        preset ("Vocals", "Shouty rock", { { threshold, -18.0f }, { range, 10.0f }, { harshOn, on }, { harshThreshold, -12.0f }, { harshRange, 6.0f },
                                           { harshFreq, 2800.0f }, { harshWidth, 1.2f } }),

        preset ("Speech", "Podcast", { { threshold, -18.0f }, { keyHpf, 80.0f } }),
        preset ("Speech", "Voice-over", { { shape, spectral }, { threshold, -18.0f }, { range, 9.0f }, { focus, 75.0f }, { keyHpf, 80.0f } }),
        preset ("Speech", "Audiobook", { { threshold, -16.0f }, { range, 6.0f }, { speedAuto, off }, { release, 40.0f } }),
        preset ("Speech", "Harsh room mic", { { harshOn, on }, { harshThreshold, -11.0f }, { harshRange, 5.0f }, { harshFreq, 2500.0f }, { harshWidth, 1.5f } }),

        preset ("Instruments", "Acoustic guitar", { { detection, allround }, { thresholdAbs, -32.0f }, { range, 6.0f }, { detectLow, 3000.0f },
                                                    { detectHigh, 10000.0f }, { speedAuto, off }, { release, 30.0f } }),
        preset ("Instruments", "Hi-hat tamer", { { detection, allround }, { thresholdAbs, -18.0f }, { range, 6.0f }, { detectLow, 6000.0f },
                                                 { detectHigh, 16000.0f }, { speedAuto, off }, { release, 40.0f } }),
        preset ("Instruments", "Cymbal bus", { { detection, allround }, { shape, spectral }, { thresholdAbs, -22.0f }, { range, 5.0f },
                                               { detectLow, 4000.0f }, { detectHigh, 16000.0f }, { speedAuto, off }, { release, 60.0f } }),
        preset ("Instruments", "Brass", { { detection, allround }, { thresholdAbs, -30.0f }, { range, 4.0f }, { detectLow, 5000.0f }, { detectHigh, 12000.0f },
                                          { harshOn, on }, { harshThresholdAbs, -24.0f }, { harshRange, 4.0f }, { harshFreq, 2500.0f }, { harshWidth, 1.2f } }),

        preset ("Mix", "Mastering air", { { detection, allround }, { shape, spectral }, { thresholdAbs, -30.0f }, { range, 3.0f }, { detectLow, 6000.0f },
                                          { detectHigh, 16000.0f }, { speedAuto, off }, { release, 60.0f } }),
        preset ("Mix", "Bus", { { detection, allround }, { thresholdAbs, -28.0f }, { range, 4.0f }, { detectLow, 5000.0f } }),
        preset ("Mix", "Centre vocal", { { detection, allround }, { stereo, mid }, { thresholdAbs, -30.0f }, { range, 4.0f }, { detectLow, 5000.0f },
                                         { detectHigh, 11000.0f } }),
    };
    return f;
}

} // namespace halite
