// ViridianMeasure: renders every built model through the real chain and prints the quality gates
// of PLAN.md ("Measurement and quality gates"); writes the curves that compare.py checks against
// ngspice.
//
//     ViridianMeasure                     every gate of every built model
//     ViridianMeasure --quick             skip the curve grid, the aliasing grid and the CPU runs
//     ViridianMeasure --aliasing          the aliasing grid and tone scan alone
//     ViridianMeasure --curves <file>     the Console's grid through the chain at 44.1 ... 192 kHz, as JSON lines
//     ViridianMeasure --jobs <n>          threads for the curve grid (default: every core)

#include "support/ChainGates.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

using namespace viridian;
using namespace viridian::gates;

namespace
{
    int failures = 0;

    void line (const char* label, double value, const char* unit, bool pass)
    {
        std::printf ("  %-58s %10.4f %-4s %s\n", label, value, unit, pass ? "ok" : "FAIL");
        failures += pass ? 0 : 1;
    }

    void report (const char* label, double value, const char* unit)
    {
        std::printf ("  %-58s %10.4f %-4s\n", label, value, unit);
    }

    struct GridResult
    {
        std::vector<NamedSettings> grid;
        std::vector<std::array<Curve, std::size (rates)>> curves;
        std::vector<size_t> flatOf;                                  // each setting's bandsFlat() twin in curves
    };

    /** Every setting of both cards' grids and their flat twins at every rate, on `jobs` threads (a chain per thread and rate). */
    GridResult runGrid (int jobs)
    {
        GridResult r;
        for (const bool alt : { false, true })
            for (auto& s : consoleGrid (alt))
                r.grid.push_back (std::move (s));

        // The twins repeat (a twin keeps only the frequencies, Q and shapes): each runs once.
        std::vector<dsp::ChainSettings> settings;
        for (const auto& g : r.grid)
            settings.push_back (g.settings);
        for (const auto& g : r.grid)
        {
            const auto twin = bandsFlat (g.settings);
            auto same = [&] (const dsp::ChainSettings& s) { return s.alt == twin.alt && s.positions == twin.positions; };
            const auto it = std::find_if (settings.begin() + (long) r.grid.size(), settings.end(), same);
            r.flatOf.push_back ((size_t) std::distance (settings.begin(), it));
            if (it == settings.end())
                settings.push_back (twin);
        }
        r.curves.resize (settings.size());

        const auto hz = curveHz();
        std::atomic<size_t> next { 0 };
        auto worker = [&]
        {
            std::array<std::unique_ptr<dsp::EqChain>, std::size (rates)> chains;
            std::array<std::vector<Complex>, std::size (rates)> amps;
            for (size_t i = next++; i < settings.size(); i = next++)
                for (size_t k = 0; k < std::size (rates); ++k)
                {
                    if (chains[k] == nullptr)
                    {
                        chains[k] = std::make_unique<dsp::EqChain>();
                        chains[k]->prepare (rates[k], 512, 2, settings[i]);
                        amps[k] = ampsOnlyResponse (*chains[k], rates[k], hz);
                    }
                    r.curves[i][k] = consoleCurve (*chains[k], settings[i], rates[k], hz, amps[k]);
                }
        };
        std::vector<std::thread> threads;
        for (int t = 0; t < std::max (1, jobs); ++t)
            threads.emplace_back (worker);
        for (auto& t : threads)
            t.join();
        return r;
    }

    void writeComplex (std::ofstream& f, const char* key, const std::vector<Complex>& h)
    {
        f << '"' << key << "\": [";
        for (size_t k = 0; k < h.size(); ++k)
        {
            char buffer[64];
            std::snprintf (buffer, sizeof (buffer), "%s[%.17g, %.17g]", k == 0 ? "" : ", ", h[k].real(), h[k].imag());
            f << buffer;
        }
        f << ']';
    }

    std::string potsJson (const dsp::console::Pots& p)
    {
        char text[1024];
        std::snprintf (text, sizeof (text),
                       "{\"hpf_in\": %d, \"hpf_freq\": %.17g, \"lpf_in\": %d, \"lpf_freq\": %.17g, "
                       "\"hf_gain\": %.17g, \"hf_freq\": %.17g, \"hf_bell\": %d, "
                       "\"hmf_gain\": %.17g, \"hmf_freq\": %.17g, \"hmf_q\": %.17g, "
                       "\"lmf_gain\": %.17g, \"lmf_freq\": %.17g, \"lmf_q\": %.17g, "
                       "\"lf_gain\": %.17g, \"lf_freq\": %.17g, \"lf_bell\": %d}",
                       p.hpfIn ? 1 : 0, p.hpf, p.lpfIn ? 1 : 0, p.lpf, p.hf.gain, p.hf.freq, p.hf.bell ? 1 : 0, p.hmf.gain,
                       p.hmf.freq, p.hmf.q, p.lmf.gain, p.lmf.freq, p.lmf.q, p.lf.gain, p.lf.freq, p.lf.bell ? 1 : 0);
        return text;
    }

    /** One JSON object per line: the setting, the pots the networks ran, and the curves per rate. */
    bool writeCurves (const std::string& path, const GridResult& r)
    {
        std::ofstream f (path);
        if (! f)
            return false;
        const auto hz = curveHz();
        f << "{\"hz\": [";
        for (size_t k = 0; k < hz.size(); ++k)
        {
            char buffer[40];
            std::snprintf (buffer, sizeof (buffer), "%s%.17g", k == 0 ? "" : ", ", hz[k]);
            f << buffer;
        }
        f << "], \"rates\": [";
        for (size_t k = 0; k < std::size (rates); ++k)
            f << (k == 0 ? "" : ", ") << rates[k];
        f << "]}\n";

        for (size_t i = 0; i < r.grid.size(); ++i)
        {
            const auto& s = r.grid[i].settings;
            const auto pots = potsJson (r.curves[i][0].pots);
            const auto& flat = r.curves[r.flatOf[i]];
            const auto flatPots = potsJson (flat[0].pots);
            f << "{\"name\": \"" << r.grid[i].name << "\", \"card\": \"" << (s.alt ? "later" : "early") << "\", \"positions\": [";
            for (size_t c = 0; c < s.positions.size(); ++c)
                f << (c == 0 ? "" : ", ") << s.positions[c];
            f << "], \"acting_hz\": [";
            const auto eq = resolveEq (Model::console, s.alt, s.positions);
            bool first = true;
            for (const auto& [freq, gain] : { std::pair { Control::lowFreq, Control::lowGain }, std::pair { Control::lmFreq, Control::lmGain },
                                              std::pair { Control::hmFreq, Control::hmGain }, std::pair { Control::highFreq, Control::highGain },
                                              std::pair { Control::hpfFreq, Control::hpfFreq }, std::pair { Control::lpfFreq, Control::lpfFreq } })
            {
                const bool filter = freq == gain;
                if (filter ? eq[freq].off : std::abs (eq[gain].value) < 1.0e-6f)
                    continue;
                f << (first ? "" : ", ") << eq[freq].value;
                first = false;
            }
            f << "], \"pots\": " << pots << ", \"flat_pots\": " << flatPots << ", ";
            writeComplex (f, "analog", r.curves[i][0].analog);
            for (size_t k = 0; k < std::size (rates); ++k)
            {
                const auto rate = std::to_string ((int) rates[k]);
                f << ", ";
                writeComplex (f, ("measured_" + rate).c_str(), r.curves[i][k].measured);
                f << ", ";
                writeComplex (f, ("display_" + rate).c_str(), r.curves[i][k].display);
                f << ", ";
                writeComplex (f, ("flat_measured_" + rate).c_str(), flat[k].measured);
            }
            f << "}\n";
        }
        return (bool) f;
    }

    void curveGates (const GridResult& r)
    {
        const auto hz = curveHz();
        std::printf ("\nConsole: %zu settings through the chain, 20 Hz ... 20 kHz (ngspice: tools/measure/compare.py)\n",
                     r.grid.size());
        for (size_t k = 0; k < std::size (rates); ++k)
        {
            double audio = 0.0, analog = 0.0;
            for (size_t i = 0; i < r.grid.size(); ++i)
            {
                const auto& c = r.curves[i];
                audio = std::max (audio, worstDb (c[k].display, c[k].measured, hz));
                analog = std::max (analog, worstDb (c[k].analog, c[k].measured, hz));
            }
            char label[96];
            std::snprintf (label, sizeof (label), "%.1f kHz: display = audio, worst", rates[k] / 1000.0);
            line (label, audio, "dB", audio <= 0.05);
            std::snprintf (label, sizeof (label), "%.1f kHz: measured against the netlists' H(s), worst", rates[k] / 1000.0);
            report (label, analog, "dB");
        }
    }

    /**
        Settled 19: the longest callback around every switch and model change (at Drive 0 and
        +24 dB), and the first one after prepare() and after reset(), best of five runs, within
        three quarters of the period at 96 kHz in blocks of 32 and 48 kHz in blocks of 64.
    */
    void callbackGates()
    {
        std::printf ("\nConsole: the longest callback around a change (best of five runs)\n");
        for (const auto& [fs, block] : { std::pair { 96000.0, 32 }, std::pair { 48000.0, 64 } })
        {
            const auto period = 1000.0 * block / fs;
            double worst = 0.0;
            std::string where;
            // At Drive +24 dB the amps' 8x clip runs on every sample (OpAmpClip), as the [.perf] test has it.
            for (const auto drive : { 0.0f, 24.0f })
                for (auto c : consoleChanges())
                {
                    c.before.driveDb = c.after.driveDb = drive;
                    if (drive > 0.0f)
                        c.name += ", Drive +24 dB";
                    if (const auto ms = worstCallbackMs (c.before, c.after, fs, block); ms > worst)
                        worst = ms, where = c.name;
                }
            for (const bool alt : { false, true })
            {
                auto s = consoleWorking (alt);
                s.tolerance = 0.5f;
                const auto [prepared, reset] = firstCallbackMs (s, fs, block);
                if (std::max (prepared, reset) > worst)
                    worst = std::max (prepared, reset), where = "the first callback after prepare or reset";
            }
            char label[160];
            std::snprintf (label, sizeof (label), "%.0f kHz, blocks of %d (period %.3f ms): %s", fs / 1000.0, block, period, where.c_str());
            line (label, worst, "ms", worst <= 0.75 * period);

            // Every knob moving and a switch changing every 50 ms, Tolerance and Drive both ways.
            double automated = 0.0;
            std::string automatedWhere;
            for (const bool alt : { false, true })
                for (const auto tolerance : { 0.0f, 0.5f })
                    for (const auto drive : { 0.0f, 24.0f })
                    {
                        auto s = consoleWorking (alt);
                        s.tolerance = tolerance;
                        s.driveDb = drive;
                        if (const auto ms = automatedCallbackMs (s, fs, block); ms > automated)
                            automated = ms, automatedWhere = std::string (alt ? "later" : "early") + ", Tolerance " + std::to_string ((int) (100.0f * tolerance))
                                                             + " %, Drive " + std::to_string ((int) drive);
                    }
            std::snprintf (label, sizeof (label), "  every knob moving, switches changing (%s)", automatedWhere.c_str());
            line (label, automated, "ms", automated <= 0.75 * period);
        }
    }

    void consoleGates (bool alt, bool quick)
    {
        std::printf ("\nConsole, %s card (Alt %s)\n", alt ? "later" : "early", alt ? "on" : "off");
        const auto flat = consoleFlat (alt);
        const auto working = consoleWorking (alt);

        // Settled 12: the drawn card lifts by itself, so its flat is gated against its netlists' own.
        for (const auto fs : rates)
        {
            const auto f = flatness (flat, fs);
            char label[96];
            std::snprintf (label, sizeof (label), "flat (Init, EQ In) vs the netlists' own, %.1f kHz", fs / 1000.0);
            line (label, f.fromNetwork, "dB", f.fromNetwork <= 0.1);
            std::snprintf (label, sizeof (label), "  the circuit's own lift over amps only, %.1f kHz", fs / 1000.0);
            report (label, f.fromAmps, "dB");
        }

        for (const auto fs : { 44100.0, 96000.0 })
        {
            char label[96];
            std::snprintf (label, sizeof (label), "Drive -12 ... +24 dB, small-signal level, %.1f kHz", fs / 1000.0);
            const auto d = driveDeviationDb (working, fs);
            line (label, d, "dB", d <= 0.1);
            std::snprintf (label, sizeof (label), "Drive jumping -12 <-> +24 dB, %.1f kHz: crest", fs / 1000.0);
            const auto jumps = driveJumpCrestDb (working, fs);
            line (label, jumps, "dB", jumps <= -60.0);
            std::snprintf (label, sizeof (label), "Output jumping -24 <-> +24 dB, %.1f kHz: crest", fs / 1000.0);
            const auto outputJumps = outputJumpCrestDb (working, fs);
            line (label, outputJumps, "dB", outputJumps <= -60.0);
        }

        std::printf ("    THD (H2 ... H9), dB re the fundamental, 96 kHz, Init:\n");
        std::printf ("    %-22s %10s %10s %10s\n", "", "50 Hz", "1 kHz", "10 kHz");
        for (const auto drive : { 0.0f, 12.0f, 24.0f })
            for (const auto level : { -40.0, -20.0, -12.0, -6.0, -1.0 })
            {
                if (drive > 0.0f && level > -12.0)
                    continue;
                char label[64];
                std::snprintf (label, sizeof (label), "Drive %+3.0f, %5.1f dBFS", drive, level);
                std::printf ("    %-22s %10.1f %10.1f %10.1f\n", label, thdDb (flat, 50.0, level, drive), thdDb (flat, 1000.0, level, drive),
                             thdDb (flat, 10000.0, level, drive));
            }

        // PLAN.md Settled 16: 7 kHz at every Drive, at Init and with every band at work; every
        // setting class and tone in aliasingGates().
        for (float drive = -12.0f; drive <= 24.0f; drive += 3.0f)
        {
            char label[96];
            std::snprintf (label, sizeof (label), "7 kHz -1 dBFS, Drive %+.0f dB, 44.1 kHz: worst alias", drive);
            const auto alias = std::max (worstAliasDbc (flat, drive), worstAliasDbc (working, drive));
            line (label, alias, "dBc", alias <= -80.0);
        }

        double sweeps = -300.0;
        for (const auto c : { Control::hpfFreq, Control::lowFreq, Control::lowGain, Control::lmFreq, Control::lmGain,
                              Control::lmQ, Control::hmFreq, Control::hmGain, Control::hmQ, Control::highFreq,
                              Control::highGain, Control::lpfFreq })
            sweeps = std::max (sweeps, sweepCrestDb (working, c, 44100.0));
        line ("every knob swept end to end, 44.1 kHz: worst crest", sweeps, "dB", sweeps <= -60.0);

        // Knobs moved together: each band's frequency and gain (and Q), every knob; swept and jumped
        // between their travel points (at the tone's worst phase) from every base (shapes, filter
        // states), one thread per base; every knob jumped between 1000 random settings per base,
        // 1000 preset changes and 500 pairs A/B-toggled every 50 and 200 ms, on every core, from
        // seeds of their own (measureSeeds).
        for (const auto fs : rates)
        {
            struct Worst
            {
                double swept = -300.0, jumped = -300.0, random = -300.0;
                std::string sweptWhere, jumpedWhere, randomWhere;
            };
            const auto bases = consoleBases (alt);
            std::vector<Worst> perBase (bases.size());
            std::vector<std::thread> threads;
            for (size_t b = 0; b < bases.size(); ++b)
                threads.emplace_back ([&, b]
                {
                    auto& w = perBase[b];
                    for (const auto& [name, group] : consoleGroups (bases[b]))
                    {
                        if (const auto c = groupSweepCrestDb (bases[b].settings, group, fs); c > w.swept)
                            w.swept = c, w.sweptWhere = bases[b].name + ": " + name;
                        if (const auto c = groupJumpsCrestDb (bases[b].settings, group, fs); c > w.jumped)
                            w.jumped = c, w.jumpedWhere = bases[b].name + ": " + name;
                        if (quick)
                            break;
                    }
                });
            for (auto& t : threads)
                t.join();
            for (size_t b = 0; b < bases.size(); ++b)
            {
                const auto [random, pair] = randomJumpsCrestDb (bases[b], fs, quick ? 3 : 1000, allCores(), measureSeeds);
                perBase[b].random = random;
                perBase[b].randomWhere = bases[b].name + ", pair " + std::to_string (pair);
            }
            Worst all;
            for (const auto& w : perBase)
            {
                if (w.swept > all.swept)
                    all.swept = w.swept, all.sweptWhere = w.sweptWhere;
                if (w.jumped > all.jumped)
                    all.jumped = w.jumped, all.jumpedWhere = w.jumpedWhere;
                if (w.random > all.random)
                    all.random = w.random, all.randomWhere = w.randomWhere;
            }
            char label[200];
            std::snprintf (label, sizeof (label), "knobs swept together, %.1f kHz (%s)", fs / 1000.0, all.sweptWhere.c_str());
            line (label, all.swept, "dB", all.swept <= -60.0);
            std::snprintf (label, sizeof (label), "knobs jumped together, %.1f kHz (%s)", fs / 1000.0, all.jumpedWhere.c_str());
            line (label, all.jumped, "dB", all.jumped <= -60.0);
            std::snprintf (label, sizeof (label), "every knob jumped between random settings, %.1f kHz (%s)", fs / 1000.0, all.randomWhere.c_str());
            line (label, all.random, "dB", all.random <= -60.0);
            // A whole setting's change: against the tone's level around each click (GateTests.cpp).
            const auto presets = presetJumpPairs (alt, quick ? 3 : 1000, measureSeeds);
            const auto [preset, presetPair] = jumpPairsCrestDb (presets, fs, allCores(), true);
            std::snprintf (label, sizeof (label), "every parameter jumped as a preset change does, %.1f kHz (pair %d)", fs / 1000.0, presetPair);
            line (label, preset, "dB", preset <= -60.0);
            const auto [overshoot, overshootPair] = jumpPairsOvershootDb (presets, fs, allCores());
            std::snprintf (label, sizeof (label), "  the largest level after it over the steady ones (pair %d)", overshootPair);
            report (label, overshoot, "dB");
            const auto togglePairs = presetJumpPairs (alt, quick ? 3 : 500, measureSeeds);
            for (const auto period : { 0.05, 0.2 })
            {
                const auto [toggled, togglePair] = togglePairsCrestDb (togglePairs, fs, period, allCores(), true);
                std::snprintf (label, sizeof (label), "A/B toggled every %.0f ms, %.1f kHz (pair %d)", 1000.0 * period, fs / 1000.0, togglePair);
                line (label, toggled, "dB", toggled <= -60.0);
            }
            if (quick)
                break;
        }

        // A host jumping a knob in one block, between both ends of its travel and the middle.
        for (const auto fs : rates)
        {
            double jumps = -300.0, overshoot = -300.0;
            for (const auto c : { Control::hpfFreq, Control::lowFreq, Control::lowGain, Control::lmFreq, Control::lmGain,
                                  Control::lmQ, Control::hmFreq, Control::hmGain, Control::hmQ, Control::highFreq,
                                  Control::highGain, Control::lpfFreq })
                for (const auto from : travelPoints (c))
                    for (const auto to : travelPoints (c))
                        if (std::abs (from - to) > 0.01f)
                        {
                            jumps = std::max (jumps, jumpCrestDb (working, c, from, to, fs));
                            overshoot = std::max (overshoot, jumpOvershootDb (working, c, from, to, fs));
                        }
            char label[96];
            std::snprintf (label, sizeof (label), "every knob jumped, %.1f kHz: worst crest", fs / 1000.0);
            line (label, jumps, "dB", jumps <= -60.0);
            std::snprintf (label, sizeof (label), "  the largest peak in 30 ms over the steady one");
            report (label, overshoot, "dB");
            if (quick)
                break;
        }

        auto stepCrest = [&] (const std::function<void (dsp::ChainSettings&)>& change)
        {
            const auto at = (size_t) (0.5 * 44100.0);
            return automationCrestDb (
                [&] (size_t pos)
                {
                    auto s = working;
                    if (pos >= at)
                        change (s);
                    return s;
                },
                44100.0, 1.0);
        };
        double steps = -300.0;
        for (const auto& change : std::vector<std::function<void (dsp::ChainSettings&)>> {
                 [] (dsp::ChainSettings& s) { s.positions[(size_t) Control::lowShape] = 1.0f - s.positions[(size_t) Control::lowShape]; },
                 [] (dsp::ChainSettings& s) { s.positions[(size_t) Control::highShape] = 1.0f - s.positions[(size_t) Control::highShape]; },
                 [] (dsp::ChainSettings& s) { s.positions[(size_t) Control::hpfFreq] = 0.0f; },
                 [] (dsp::ChainSettings& s) { s.positions[(size_t) Control::lpfFreq] = 1.0f; },
                 [] (dsp::ChainSettings& s) { s.eqIn = false; },
                 [] (dsp::ChainSettings& s) { s.stereo = StereoMode::side; } })
            steps = std::max (steps, stepCrest (change));
        line ("shape keys, filter Off, EQ In, Stereo: worst crest", steps, "dB", steps <= -60.0);
        const auto altCrest = stepCrest ([] (dsp::ChainSettings& s) { s.alt = ! s.alt; });
        line ("Alt (the other card): crest", altCrest, "dB", altCrest <= -60.0);
        double models = -300.0;
        for (int m = 0; m < numModels; ++m)
            if (m != (int) Model::console)
            {
                models = std::max (models, stepCrest ([m] (dsp::ChainSettings& s) { s.model = static_cast<Model> (m); }));
                models = std::max (models, automationCrestDb (
                                               [&] (size_t pos)
                                               {
                                                   auto s = working;
                                                   if (pos < (size_t) (0.5 * 44100.0))
                                                       s.model = static_cast<Model> (m);
                                                   return s;
                                               },
                                               44100.0, 1.0));
            }
        line ("model changes to and from every model: worst crest", models, "dB", models <= -60.0);

        {
            auto s = working;
            s.driveDb = 18.0f;
            dsp::EqChain chain;
            chain.prepare (48000.0, 512, 2, s);
            std::mt19937 rng (5);
            std::uniform_real_distribution<float> dist (-0.25f, 0.25f);
            auto x = Stereo::silence (48000);
            for (auto& v : x.left)
                v = dist (rng);
            x.right = x.left;
            const auto out = render (chain, s, x);
            const auto same = std::memcmp (out.left.data(), out.right.data(), out.size() * sizeof (float)) == 0;
            line ("Tolerance 0: L and R bit-identical (1 = yes)", same ? 1.0 : 0.0, "", same);
        }
        {
            auto s = working;
            s.driveDb = 24.0f;
            dsp::EqChain chain;
            chain.prepare (48000.0, 512, 2, s);
            const auto out = render (chain, s, Stereo::silence (96000));
            auto zero = [] (float v) { return juce::exactlyEqual (v, 0.0f); };
            const auto silent = std::all_of (out.left.begin(), out.left.end(), zero) && std::all_of (out.right.begin(), out.right.end(), zero);
            line ("Noise 0: silence in, exact silence out (1 = yes)", silent ? 1.0 : 0.0, "", silent);
        }

        if (! quick)
        {
            // PLAN.md, Performance budget (Settled 22, Provisional 10): the worst case with the
            // controls at rest up to the amps' rails at Drive 0 against the budget, driven into
            // them (Drive, the input's level, a boost) against the ceiling (consoleCpuCases()); a
            // model crossfade against the two budgets' sum. Medians of seven runs.
            const auto& console = info (Model::console);
            std::printf ("  CPU, 48 kHz stereo, at rest (budget %.0f %% up to the rails at Drive 0, ceiling %.0f %% into them):\n",
                         console.cpuBudgetPercent, console.cpuDriveCeilingPercent);
            for (const auto& c : consoleCpuCases (alt))
            {
                const auto limit = c.intoRails ? console.cpuDriveCeilingPercent : console.cpuBudgetPercent;
                const auto cpu = medianOf (7, [&] { return cpuPercent (c.settings, false, 3.0, c.input); });
                line ((std::string ("  ") + c.name + (c.intoRails ? " (ceiling)" : "")).c_str(), cpu, "%", cpu <= limit);
            }
            auto s = working;
            s.noise = s.tolerance = 1.0f;
            for (int m = 0; m < numModels; ++m)
                if (m != (int) Model::console)
                {
                    const auto other = static_cast<Model> (m);
                    const auto sum = console.cpuBudgetPercent + info (other).cpuBudgetPercent;
                    const auto changing = medianOf (7, [&] { return modelChangeCpuPercent (s, other, 3.0); });
                    char label[128];
                    std::snprintf (label, sizeof (label), "CPU while a change to %s and back runs both (budget %.0f %%)", info (other).name, sum);
                    line (label, changing, "%", changing <= sum);
                }
            report ("CPU, 48 kHz stereo, four band frequencies moving", cpuPercent (working, true), "%");
            for (const auto peak : { -6.0, -3.0 })
            {
                char label[96];
                std::snprintf (label, sizeof (label), "CPU, 48 kHz stereo, noise peaking at %.0f dBFS, Drive 0, every band at work", peak);
                report (label, medianOf (7, [&] { return cpuPercent (working, false, 3.0, (float) fromDb (peak)); }), "%");
            }
            auto driven = working;
            driven.driveDb = 6.0f;
            report ("CPU, 48 kHz stereo, at rest, Drive +6 dB, every band at work", medianOf (7, [&] { return cpuPercent (driven, false, 3.0); }), "%");
        }
    }

    /**
        PLAN.md Settled 16, Provisional 4, 6 and 11: -80 dBc at every Drive and every setting, the
        full grid (aliasGrid(), 14 170 renders), the tone scan (aliasToneScan(), 524) and input near
        Nyquist (aliasNyquistScan(), 390), on `jobs` threads.
    */
    void aliasingGates (int jobs)
    {
        std::printf ("\nAliasing, Console, 44.1 kHz, a tone at -1 dBFS, Drive -12 ... +24 dB in 3 dB steps\n");
        const auto grid = aliasGrid();
        const auto results = aliasDbc (grid, jobs);
        for (const auto tone : aliasTones)
            for (const auto* card : { "early", "later" })
            {
                // Each setting's worst over Drive, the three worst settings (the ctest gate's choice).
                std::vector<std::pair<double, size_t>> settings;
                for (size_t k = 0; k < grid.size(); ++k)
                {
                    if (! myzel::dsp::isExactly (grid[k].toneHz, tone) || ! grid[k].name.starts_with (card))
                        continue;
                    auto same = [&] (const auto& e) { return grid[e.second].name == grid[k].name; };
                    if (auto it = std::find_if (settings.begin(), settings.end(), same); it == settings.end())
                        settings.push_back ({ results[k], k });
                    else if (results[k] > it->first)
                        *it = { results[k], k };
                }
                std::sort (settings.begin(), settings.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
                const auto& [worst, at] = settings.front();
                char label[200];
                std::snprintf (label, sizeof (label), "%5.0f Hz, %s card: worst alias (%s, Drive %+.0f dB)", tone, card,
                               grid[at].name.substr (std::string (card).size() + 7).c_str(), grid[at].driveDb);
                line (label, worst, "dBc", worst <= -80.0);
                for (size_t i = 1; i < std::min<size_t> (3, settings.size()); ++i)
                {
                    const auto& g = grid[settings[i].second];
                    std::snprintf (label, sizeof (label), "  then %s, Drive %+.0f dB", g.name.substr (std::string (card).size() + 7).c_str(), g.driveDb);
                    report (label, settings[i].first, "dBc");
                }
            }
        int over = 0;
        for (size_t k = 0; k < grid.size(); ++k)
            if (results[k] > -80.0)
            {
                ++over;
                std::printf ("    over -80 dBc: %s, Drive %+.0f dB, %.0f Hz: %.1f dBc\n", grid[k].name.c_str(), grid[k].driveDb, grid[k].toneHz, results[k]);
            }
        char label[96];
        std::snprintf (label, sizeof (label), "renders over -80 dBc, of %zu", grid.size());
        line (label, over, "", over == 0);

        const auto scan = aliasToneScan();
        const auto scanned = aliasDbc (scan, jobs);
        const auto worst = std::max_element (scanned.begin(), scanned.end());
        const auto& w = scan[(size_t) std::distance (scanned.begin(), worst)];
        std::snprintf (label, sizeof (label), "2 ... 15 kHz in 100 Hz steps, Init, Drive +12 and +24: worst (%s, %.0f Hz, Drive %+.0f)",
                       w.name.substr (0, 5).c_str(), w.toneHz, w.driveDb);
        line (label, *worst, "dBc", *worst <= -80.0);

        const auto nyquist = aliasNyquistScan();
        const auto near = aliasDbc (nyquist, jobs, AliasReference::input);
        const auto top = std::max_element (near.begin(), near.end());
        const auto& t = nyquist[(size_t) std::distance (near.begin(), top)];
        std::snprintf (label, sizeof (label), "18 ... 21.8 kHz in, Init, Drive 0 ... +24, re the input: worst (%s, %.0f Hz, Drive %+.0f)",
                       t.name.substr (0, 5).c_str(), t.toneHz, t.driveDb);
        line (label, *top, "dB", *top <= -80.0);
    }
} // namespace

int main (int argc, char** argv)
{
    bool quick = false, aliasingOnly = false;
    std::string curvesPath;
    int jobs = (int) std::max (1u, std::thread::hardware_concurrency());

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--quick") == 0)
            quick = true;
        else if (std::strcmp (argv[i], "--aliasing") == 0)
            aliasingOnly = true;
        else if (std::strcmp (argv[i], "--curves") == 0 && i + 1 < argc)
            curvesPath = argv[++i];
        else if (std::strcmp (argv[i], "--jobs") == 0 && i + 1 < argc)
            jobs = std::max (1, std::atoi (argv[++i]));
        else
        {
            std::fprintf (stderr, "usage: ViridianMeasure [--quick] [--aliasing] [--curves <file>] [--jobs <n>]\n");
            return 2;
        }
    }

    if (! curvesPath.empty())
    {
        const auto grid = runGrid (jobs);
        if (! writeCurves (curvesPath, grid))
        {
            std::fprintf (stderr, "cannot write %s\n", curvesPath.c_str());
            return 2;
        }
        curveGates (grid);
        return failures == 0 ? 0 : 1;
    }

    if (aliasingOnly)
    {
        aliasingGates (jobs);
        std::printf ("\n%s\n", failures == 0 ? "every gate passes" : (std::to_string (failures) + " gate(s) FAIL").c_str());
        return failures == 0 ? 0 : 1;
    }

    {
        dsp::EqChain chain;
        for (const auto& m : modelInfos())
            if (chain.model (m.model).isPlaceholder())
                std::printf ("%s: not built yet (a placeholder that passes the signal flat)\n", m.name);
    }

    if (! quick)
        curveGates (runGrid (jobs));
    for (const bool alt : { false, true })
        consoleGates (alt, quick);
    if (! quick)
    {
        aliasingGates (jobs);
        callbackGates();
    }

    std::printf ("\n%s\n", failures == 0 ? "every gate passes" : (std::to_string (failures) + " gate(s) FAIL").c_str());
    return failures == 0 ? 0 : 1;
}
