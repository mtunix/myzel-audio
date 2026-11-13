// The voicing ladder (InkcapCalibrate --voicing): what each module does at Damage 25, 50 and
// 100 %, in the unit a listener hears it in, against the targets in each module's NOTES.md.

#include "calibrate/Calibrate.h"

#include "dsp/modules/bits/BitsModule.h"
#include "dsp/modules/crush/CrushModule.h"
#include "dsp/modules/glitch/GlitchPattern.h"
#include "dsp/modules/hiss/HissModule.h"
#include "dsp/modules/room/RoomModule.h"
#include "dsp/modules/warble/WarbleModule.h"
#include "dsp/modules/warble/WarbleShape.h"
#include "support/Loudness.h"
#include "support/ModuleHarness.h"

#include <cstdio>
#include <cstring>

namespace inkcap::calibrate
{

namespace
{
    using namespace inkcap::dsp;
    constexpr double fs = loudness::calibrationRate;

    // Every measurement runs at Damage 100 %, so the intensity is the Amount; the ladder then
    // reads it at each Damage through the module's curve.
    testing::Macros full (float chaos = 0.0f) { return { 1.0f, chaos }; }

    double eventsPerBar (GlitchType type, double i, float chaos)
    {
        const GlitchSettings g { true, (float) i, type, defaultGlitchRate, 0.5f, 0.5f };
        const auto p = glitch::patternFor (g, 1.0f, chaos);
        constexpr int bars = 512;
        const auto slotsPerBar = (int64_t) std::lround (4.0 / p.grid.slotBeats);
        int count = 0;
        for (int64_t k = 0; k < bars * slotsPerBar; ++k)
            count += glitch::eventAt (0x4393'0A3Du, p, k).has_value() ? 1 : 0;
        return (double) count / bars;
    }

    double warbleCents (WarbleType type, double i)
    {
        WarbleModule w;
        w.prepare ({ fs, 512, 2 });
        const WarbleSettings s { true, (float) i, type, 0.5f, false, 0.0f };
        const auto lfo = warble::freeHz (type, 0.5f);
        const auto x = analysis::sine (1000.0, fs, std::max (4.0, 5.0 / lfo), 0.5);
        const auto out = testing::renderModule (w, s, full(), { x, x }, 512, fs);
        const auto track = analysis::frequencyTrack (out.left, fs, (size_t) (0.5 * fs));
        const auto swing = analysis::fitSine (track, lfo, fs).first;
        return 1200.0 * std::log2 (1.0 + swing / 1000.0);
    }

    /** Harmonics 2-9 of a 200 Hz sine at -6 dBFS (a programme's peaks) against the fundamental (dB). */
    double crushThd (CrushType type, double i, float drive = 0.5f)
    {
        CrushModule c;
        c.prepare ({ fs, 512, 2 });
        const CrushSettings s { true, (float) i, type, drive, 0.0f };
        const auto x = analysis::sine (200.0, fs, 1.5, 0.5);
        const auto out = testing::renderModule (c, s, full(), { x, x }, 512, fs);
        const auto from = (size_t) (0.5 * fs);
        const auto n = (size_t) fs;
        const auto f = analysis::toneAmplitude (out.left, 200.0, fs, from, n);
        double h = 0.0;
        for (int k = 2; k <= 9; ++k)
            h += std::pow (analysis::toneAmplitude (out.left, 200.0 * k, fs, from, n), 2.0);
        return analysis::toDb (std::sqrt (h) / f);
    }

    struct BitsState
    {
        double clockKhz, topKhz, bits;
    };

    BitsState bitsState (BitsType type, double i)
    {
        BitsModule b;
        b.prepare ({ fs, 512, 2 });
        const BitsSettings s { true, (float) i, type, 0.5f, 0.5f, 0.0f };
        const auto x = analysis::sine (500.0, fs, 0.3, 0.25);
        testing::renderModule (b, s, full(), { x, x }, 512, fs);
        const auto& v = bits::voicingOf (type);
        return { b.clockNow() / 1000.0, std::min (v.filterRatio * b.clockNow(), BitsModule::maxCutoffPerRate * fs) / 1000.0, b.bitsNow() };
    }

    /** The bed with silence in, Follow and Tone neutral (dBFS RMS, both channels). */
    double hissBedDb (HissType type, double i)
    {
        HissModule h;
        h.prepare ({ fs, 512, 2 });
        const HissSettings s { true, (float) i, type, 0.0f, 0.0f };
        const std::vector<float> silence ((size_t) (4.0 * fs), 0.0f);
        const auto out = testing::renderModule (h, s, full (0.25f), { silence, silence }, 512, fs);
        const auto from = (size_t) (0.5 * fs);
        return analysis::toDb (std::hypot (analysis::rms (out.left, from), analysis::rms (out.right, from)) / std::sqrt (2.0));
    }

    /** The room's wet against the dry it adds to (dB), on pink noise. */
    double roomWetToDry (RoomType type, double i)
    {
        RoomModule r;
        r.prepare ({ fs, 512, 2 });
        const RoomSettings s { true, (float) i, type, 0.5f, 0.0f };
        const auto in = loudness::pinkNoise (fs, 4.0);
        const auto out = testing::renderModule (r, s, full(), in, 512, fs);
        const auto dryGain = 1.0 - RoomModule::dryStepBack * i;
        std::vector<float> wet (out.left.size()), dry (out.left.size());
        for (size_t k = 0; k < wet.size(); ++k)
        {
            dry[k] = (float) (dryGain * in.left[k]);
            wet[k] = out.left[k] - dry[k];
        }
        const auto from = (size_t) (1.0 * fs);
        return analysis::toDb (analysis::rms (wet, from) / analysis::rms (dry, from));
    }

    void ladder (const char* label, Module m, const std::function<double (double)>& measure, const char* unit, const char* format = "%9.1f")
    {
        std::printf ("  %-28s", label);
        for (const auto amount : { 1.0f, 0.5f })
            for (const auto damage : { 0.25f, 0.5f, 1.0f })
            {
                const auto i = (double) moduleIntensity (m, amount, damage);
                std::printf (format, measure (i));
            }
        std::printf ("  %s\n", unit);
    }

    void header (const char* module)
    {
        std::printf ("\n%s%*s%s\n", module, (int) (30 - std::strlen (module)), "", "      Amount 100 %              Amount 50 %");
        std::printf ("%30s%9s%9s%9s%9s%9s%9s\n", "Damage", "25", "50", "100", "25", "50", "100");
    }
}

int voicing()
{
    std::printf ("Inkcap voicing ladder: each module at Damage 25 / 50 / 100 %%, Amount 100 and 50 %%, its controls at 50 %%\n");
    std::printf ("Damage curves (intensity = Amount x curve): ");
    for (int m = 0; m < numModules; ++m)
        std::printf ("%s d^%.2f%s", moduleInfo ((Module) m).name, (double) damageExponents[(size_t) m], m + 1 < numModules ? ", " : "\n");

    header ("Glitch");
    for (int t = 0; t < 5; t += 4)
    {
        const auto label = std::string (moduleInfo (Module::glitch).types[(size_t) t]) + ", events per bar (1/16)";
        ladder (label.c_str(), Module::glitch, [t] (double i) { return eventsPerBar ((GlitchType) t, i, 0.25f); }, "", "%9.2f");
    }

    header ("Warble");
    for (int t = 0; t < 3; ++t)
    {
        const auto label = std::string (moduleInfo (Module::warble).types[(size_t) t]) + ", peak deviation";
        ladder (label.c_str(), Module::warble, [t] (double i) { return warbleCents ((WarbleType) t, i); }, "cents");
    }

    header ("Crush (harmonics 2-9 of 200 Hz at -6 dBFS)");
    for (int t = 0; t < 4; ++t)
    {
        const auto name = std::string (moduleInfo (Module::crush).types[(size_t) t]);
        ladder ((name + ", THD, Drive 50 %").c_str(), Module::crush, [t] (double i) { return crushThd ((CrushType) t, i); }, "dB");
        ladder ((name + ", THD, Drive 100 %").c_str(), Module::crush, [t] (double i) { return crushThd ((CrushType) t, i, 1.0f); }, "dB");
    }

    header ("Bits");
    for (int t = 0; t < 3; ++t)
    {
        const auto name = std::string (moduleInfo (Module::bits).types[(size_t) t]);
        ladder ((name + ", filters").c_str(), Module::bits, [t] (double i) { return bitsState ((BitsType) t, i).topKhz; }, "kHz");
        ladder ((name + ", depth").c_str(), Module::bits, [t] (double i) { return bitsState ((BitsType) t, i).bits; }, "bits");
    }

    header ("Hiss");
    for (int t = 0; t < 3; ++t)
    {
        const auto label = std::string (moduleInfo (Module::hiss).types[(size_t) t]) + ", bed RMS";
        ladder (label.c_str(), Module::hiss, [t] (double i) { return hissBedDb ((HissType) t, i); }, "dBFS");
    }

    header ("Room");
    for (int t = 0; t < 4; ++t)
    {
        const auto label = std::string (moduleInfo (Module::room).types[(size_t) t]) + ", wet re dry";
        ladder (label.c_str(), Module::room, [t] (double i) { return roomWetToDry ((RoomType) t, i); }, "dB");
    }
    return 0;
}

} // namespace inkcap::calibrate
