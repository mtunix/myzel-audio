// InkcapMeasure: renders through the real chain and prints the quality gates of PLAN.md
// ("Measurement and quality gates") that exist so far. Pure DSP, no JUCE.
//
//     InkcapMeasure            every gate
//     InkcapMeasure --quick    skip the long ones (bar 17, Match, Mix, CPU)

#include "dsp/modules/glitch/GlitchModule.h"
#include "dsp/modules/room/RoomModule.h"
#include "dsp/modules/warble/WarbleModule.h"
#include "dsp/modules/warble/WarbleShape.h"
#include "support/MatchGates.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::gates;

namespace
{
    constexpr double fs = 48000.0;
    int failures = 0;

    void line (const char* label, double value, const char* unit, bool pass)
    {
        std::printf ("  %-56s %10.2f %-4s %s\n", label, value, unit, pass ? "ok" : "FAIL");
        failures += pass ? 0 : 1;
    }

    void flag (const char* label, bool pass)
    {
        std::printf ("  %-56s %15s %s\n", label, pass ? "yes" : "no", pass ? "ok" : "FAIL");
        failures += pass ? 0 : 1;
    }

    const char* glitchName (int type)
    {
        constexpr const char* names[] { "Skip", "Chew", "Stall", "Bend", "Any" };
        return names[std::clamp (type, 0, 4)];
    }

    ChainSettings only (Module m, float damage, float chaos, int type)
    {
        auto s = everyModule (damage, chaos, 1.0f, type);
        s.glitch.on = m == Module::glitch;
        s.warble.on = m == Module::warble;
        s.crush.on = m == Module::crush;
        s.bits.on = m == Module::bits;
        s.hiss.on = m == Module::hiss;
        s.room.on = m == Module::room;
        return s;
    }

    void latency()
    {
        std::printf ("\nLatency\n");
        int worst = 0;
        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            InkcapChain chain;
            chain.prepare (rate, 512, 2);
            worst = std::max (worst, std::abs (chain.getLatencySamples() - 9));
        }
        line ("reported latency vs the pair's 3 and Warble's rest of 6, worst", worst, "smp", worst == 0);
    }

    void damageZero()
    {
        std::printf ("\nDamage 0 is clean (null against the aligned dry, every Amount, Type, Chaos)\n");
        for (int m = 0; m < numModules; ++m)
        {
            double worst = -300.0;
            for (int type = 0; type < (int) moduleInfo ((Module) m).types.size(); ++type)
                for (const auto chaos : { 0.0f, 1.0f })
                    worst = std::max (worst, damageZeroNullDb (only ((Module) m, 0.0f, chaos, type), fs));
            const auto label = std::string (moduleInfo ((Module) m).name) + " alone";
            line (label.c_str(), worst, "dB", worst <= -100.0);
        }
        double worst = -300.0;
        for (int type = 0; type < 5; ++type)
            for (const auto mix : { 1.0f, 0.4f })
            {
                auto s = everyModule (0.0f, 0.7f, 1.0f, type);
                s.mix = mix;
                worst = std::max (worst, damageZeroNullDb (s, fs));
            }
        line ("every module on, Mix 100 and 40 %", worst, "dB", worst <= -100.0);
    }

    void determinism (bool quick)
    {
        std::printf ("\nDeterminism\n");
        bool same = true;
        for (int type = 0; type < 5; ++type)
            same = same && sameForEveryBlockSize (everyModule (0.85f, 0.6f, 1.0f, type), fs);
        flag ("bit-identical for block sizes 1 .. 4096 (every Glitch type)", same);

        if (quick)
            return;
        for (int type = 0; type < 5; ++type)
        {
            auto s = everyModule (0.5f, 0.25f, 0.5f, type);
            s.room.type = RoomType::closet;     // 500 ms outlasts only the Closet's tail; Spring and Hall: InkcapTests_room
            const auto db = barSeventeenDb (s, fs);
            const auto label = std::string ("from bar 17 vs from bar 1, after 500 ms, Glitch ") + glitchName (type);
            line (label.c_str(), db, "dB", db <= -80.0);
        }
    }

    void periodic()
    {
        std::printf ("\nChaos 0 is periodic\n");
        bool regular = true;
        for (int type = 0; type < 5; ++type)
            for (const auto damage : { 0.3f, 0.7f, 1.0f })
            {
                GlitchSettings g { true, 1.0f, (GlitchType) type, defaultGlitchRate, 0.5f, 0.5f };
                const auto p = glitch::patternFor (g, damage, 0.0f);
                const auto period = glitch::periodFor (p.density) * (type == (int) GlitchType::any ? 4 : 1);
                for (int64_t k = 0; k < 2000; ++k)
                {
                    const auto a = glitch::eventAt (0x4393'0A3Du, p, k), b = glitch::eventAt (0x4393'0A3Du, p, k + period);
                    regular = regular && a.has_value() == b.has_value() && (! a || (a->type == b->type && a->repeats == b->repeats));
                }
            }
        flag ("Glitch event list repeats with the pattern's period", regular);

        // Warble at Chaos 0: the frequency of a 1 kHz tone swings as a sine at the LFO rate.
        for (int type = 0; type < 3; ++type)
        {
            WarbleModule w;
            w.prepare ({ fs, 512, 2 });
            WarbleSettings s { true, 1.0f, (WarbleType) type, 0.5f, false, 0.0f };
            const auto lfo = warble::freeHz ((WarbleType) type, 0.5f);
            const auto x = analysis::sine (1000.0, fs, std::max (4.0, 5.0 / lfo), 0.5);
            const auto out = testing::renderModule (w, s, testing::Macros { 1.0f, 0.0f }, { x, x }, 512, fs);
            const auto track = analysis::frequencyTrack (out.left, fs, (size_t) (0.5 * fs));

            // What a pure sine at the LFO rate does not explain, relative to its swing.
            const auto [swing, residual] = analysis::fitSine (track, lfo, fs);
            const auto share = 100.0 * residual / std::max (swing, 1.0e-9);
            const auto label = std::string ("Warble ") + moduleInfo (Module::warble).types[(size_t) type] + ": residual beside a pure LFO";
            line (label.c_str(), share, "%", share <= 3.0);
        }
    }

    void clicks()
    {
        std::printf ("\nNo unintended clicks (3rd-difference crest on a 330 Hz sine, gate -60 dB)\n");
        const auto x = analysis::sine (330.0, fs, 4.0, 0.5);
        const Stereo in { x, x };
        for (int type = 0; type < 5; ++type)
        {
            auto worst = -300.0;
            for (const auto length : { 0.0f, 1.0f })
            {
                auto s = only (Module::glitch, 0.8f, 0.3f, type);
                s.glitch.length = length;
                const auto out = render (s, in, 256, fs);
                worst = std::max (worst, analysis::clickCrestDb (out.left, (size_t) (0.1 * fs)));
            }
            const auto label = std::string ("Glitch ") + glitchName (type) + " event edges";
            line (label.c_str(), worst, "dB", worst <= -60.0);
        }

        auto s = everyModule (0.7f, 0.3f);
        s.glitch.on = false;
        s.hiss.on = false;      // noise and crackle are broadband by design: InkcapTests_hiss checks its fades on Hum
        s.bits.on = false;      // its steps are the sound: the plan's click gate excepts Bits
        const auto out = render (s, in, 256, fs, {}, [] (int64_t start, ChainSettings& c)
        {
            const auto t = (double) start / fs;
            c.bypass = t > 0.5 && t < 1.0;
            c.mix = t < 1.5 ? 1.0f : 0.4f;
            c.outputDb = t < 2.0 ? 0.0f : -9.0f;
            c.warble.on = ! (t > 2.5 && t < 3.0);
            c.warble.type = t < 3.2 ? WarbleType::wow : WarbleType::flutter;
            c.seed = t < 3.5 ? 1u : 2u;
            c.damage = t < 3.7 ? 0.7f : 0.0f;
        });
        const auto crest = analysis::clickCrestDb (out.left, (size_t) (0.1 * fs));
        line ("Bypass, Mix, Output, Warble on/off, type, seed, Damage", crest, "dB", crest <= -60.0);
    }

    void silence()
    {
        std::printf ("\nSilence (Hiss off, every other module on, the longest Hall and Spring)\n");
        for (const auto type : { RoomType::hall, RoomType::spring })
        {
            auto s = everyModule (1.0f, 0.5f, 1.0f, 0);
            s.hiss.on = false;
            s.room.type = type;
            s.room.size = 1.0f;
            const auto burst = (size_t) (0.5 * fs);
            auto in = analysis::programme (fs, 0.5 + RoomModule::maxTailSeconds * 2.0, 31);
            std::fill (in.left.begin() + (std::ptrdiff_t) burst, in.left.end(), 0.0f);
            std::fill (in.right.begin() + (std::ptrdiff_t) burst, in.right.end(), 0.0f);
            const auto out = render (s, in, 512, fs);

            auto lastSound = burst;
            auto subnormals = 0;
            for (size_t k = burst; k < out.size(); ++k)
            {
                if (std::abs (out.left[k]) > 0.0f || std::abs (out.right[k]) > 0.0f)
                    lastSound = k;
                subnormals += std::fpclassify (out.left[k]) == FP_SUBNORMAL || std::fpclassify (out.right[k]) == FP_SUBNORMAL;
            }
            const auto seconds = (double) (lastSound - burst) / fs;
            const auto label = std::string ("exact silence after the input stops, ") + (type == RoomType::hall ? "Hall" : "Spring");
            line (label.c_str(), seconds, "s", lastSound + 1 < out.size() && subnormals == 0);
        }
    }

    void tempo()
    {
        std::printf ("\nTempo: Glitch events on the grid\n");
        struct Log final : GlitchModule::EventLog
        {
            std::vector<std::pair<int64_t, int64_t>> starts;    // slot, sample
            void fired (const glitch::Event& e, int64_t sample, bool) override { starts.emplace_back (e.slot, sample); }
        };

        int64_t worst = 0;
        size_t events = 0;
        for (const auto bpm : { 60.0, 97.0, 140.0, 200.0 })
            for (const auto swing : { 0.5f, 0.62f, 0.75f })
                for (const auto rate : { 0, 4, 7, 11 })
                {
                    GlitchModule module;
                    module.prepare ({ fs, 333, 2 });
                    Log log;
                    module.setEventLog (&log);
                    GlitchSettings g { true, 1.0f, GlitchType::skip, rate, 0.5f, swing };
                    const testing::Transport t { true, 5.37, bpm };
                    const auto in = analysis::programme (fs, 4.0, 3);
                    testing::renderModule (module, g, testing::Macros { 1.0f, 1.0f }, in, 333, fs, t);

                    const Grid grid { glitchRates[(size_t) rate].beats, swing };
                    for (const auto& [slot, sample] : log.starts)
                    {
                        // On the grid: the first sample at or after the slot's start (within Grid::epsilon).
                        const auto exact = (grid.slotStart (slot) - Grid::epsilon - 5.37) * 60.0 / bpm * fs;
                        worst = std::max (worst, std::abs (sample - (int64_t) std::ceil (exact)));
                        ++events;
                    }
                }
        const auto label = "worst start vs the grid, 60-200 BPM, swing, " + std::to_string (events) + " events";
        line (label.c_str(), (double) worst, "smp", worst == 0 && events > 100);
    }

    void matchGate()
    {
        std::printf ("\nMatch: pink noise at -18 dBFS, loudness out - in with Match on, Damage 25 .. 100 %%, Amount 100 %%\n");
        for (int m = 0; m < numModules; ++m)
        {
            const auto module = (Module) m;
            const auto c = defaultControls (module);
            double worst = 0.0;
            for (int type = 0; type < (int) moduleInfo (module).types.size(); ++type)
                for (const auto damage : { 0.25f, 0.5f, 0.75f, 1.0f })
                {
                    const auto db = gainDb (alone (module, type, c.a, c.b, 1.0f, damage, true));
                    worst = std::abs (db) > std::abs (worst) ? db : worst;
                }
            const auto label = std::string (moduleInfo (module).name) + ", every type at its controls' defaults, worst";
            line (label.c_str(), worst, "dB", std::abs (worst) <= 1.0);
        }
    }

    void mixComb()
    {
        std::printf ("\nMix at 50 %%: against the in-phase sum of wet and dry, 100 Hz .. 0.4 fs, worst (each module alone, light Damage)\n");
        for (const auto rate : { 44100.0, 48000.0 })
        {
            double worst = 0.0;
            for (int type = 0; type < 4; ++type)
                for (const auto damage : { 0.05f, 0.5f })
                {
                    auto s = only (Module::crush, damage, 0.0f, type);
                    s.crush.drive = 0.0f;
                    worst = std::min (worst, mixCombDb (s, rate));
                }
            for (int type = 0; type < 3; ++type)
                for (const auto damage : { 0.1f, 0.4f })
                {
                    auto s = only (Module::bits, damage, 0.0f, type);
                    s.bits = { true, 1.0f, (BitsType) type, 0.5f, 1.0f, 0.0f };
                    worst = std::min (worst, mixCombDb (s, rate, type == 2 ? 1000.0 : 100.0));
                }
            const auto label = std::string ("Crush and Bits, every type, at ") + (rate < 45000.0 ? "44.1" : "48") + " kHz";
            line (label.c_str(), worst, "dB", worst > -1.0);
        }
    }

    void cpu()
    {
        std::printf ("\nCPU at 48 kHz, stereo, one core\n");
        ChainSettings idle;
        idle.glitch.on = idle.warble.on = idle.crush.on = idle.bits.on = idle.hiss.on = idle.room.on = false;
        const auto idleLoad = cpuPercent (idle, fs);
        line ("idle (every module off)", idleLoad, "%", idleLoad <= 0.2);
        auto defaults = everyModule (1.0f, 0.5f, 1.0f, 0);
        defaults.room.type = RoomType::room;
        const auto defaultLoad = cpuPercent (defaults, fs);
        line ("every module at full Damage, the default types", defaultLoad, "%", defaultLoad <= 3.0);
        const auto fullLoad = cpuPercent (heaviest(), fs);
        line ("every module at full Damage, each at its heaviest", fullLoad, "%", fullLoad <= 4.0);
        const auto crushLoad = cpuPercent (heaviest ((int) Module::crush), fs);
        line ("Crush alone, Tape (its heaviest), full Damage", crushLoad, "%", crushLoad <= 1.5);
    }
}

int main (int argc, char** argv)
{
    bool quick = false;
    for (int i = 1; i < argc; ++i)
        quick = quick || std::strcmp (argv[i], "--quick") == 0;

    std::printf ("Inkcap quality gates (PLAN.md, Measurement and quality gates)\n");
    latency();
    damageZero();
    determinism (quick);
    periodic();
    clicks();
    silence();
    tempo();
    if (! quick)
    {
        matchGate();
        mixComb();
        cpu();
    }

    std::printf ("\n%s: %d gate%s failed\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
