// Match's tables (InkcapCalibrate [header]) and their gate (InkcapCalibrate --report).
//
// Method, as Umber's Auto gain (tests/support/MatchGates.h): deterministic pink noise at -18 dBFS
// RMS through the real chain with one module on, Match off and Mix 100 %; the K-weighted loudness
// out against in is what the module adds, and the table holds its negative. On every core.

#include "calibrate/Calibrate.h"

#include "support/MatchGates.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

namespace inkcap::calibrate
{

using namespace inkcap::dsp;

namespace
{
    using gates::alone;
    using gates::defaultControls;
    using gates::gainDb;

    const char* tableName (Module m)
    {
        switch (m)
        {
            case Module::glitch: return "glitchTable";
            case Module::crush: return "crushTable";
            case Module::bits:  return "bitsTable";
            case Module::hiss:  return "hissTable";
            case Module::room:  return "roomTable";
            case Module::warble:
            default:            return "";
        }
    }

    const char* controlNames (Module m)
    {
        switch (m)
        {
            case Module::glitch: return "Length, Rate (one row per grid value)";
            case Module::crush: return "Drive, Tone";
            case Module::bits:  return "Rate, Depth";
            case Module::hiss:  return "Tone, Follow";
            case Module::room:  return "Size, Duck";
            case Module::warble:
            default:            return "";
        }
    }

    /** Runs every job on all cores. */
    std::vector<double> measureAll (const std::vector<ChainSettings>& jobs)
    {
        std::vector<double> results (jobs.size(), 0.0);
        std::atomic<size_t> next { 0 }, done { 0 };

        auto worker = [&]
        {
            for (auto k = next++; k < jobs.size(); k = next++)
            {
                results[k] = gainDb (jobs[k]);
                const auto d = ++done;
                if (d % 100 == 0 || d == jobs.size())
                    std::fprintf (stderr, "\r  %zu / %zu", d, jobs.size());
            }
        };

        std::vector<std::thread> threads;
        for (unsigned t = 0; t < std::max (1u, std::thread::hardware_concurrency()); ++t)
            threads.emplace_back (worker);
        for (auto& t : threads)
            t.join();
        std::fprintf (stderr, "\n");
        return results;
    }

    int typesOf (Module m) { return (int) moduleInfo (m).types.size(); }

    std::pair<float, float> whereOf (Module m, const ChainSettings& s)
    {
        match::Controls c;
        switch (m)
        {
            case Module::glitch: c = match::controlsOf (s.glitch); break;
            case Module::crush:  c = match::controlsOf (s.crush); break;
            case Module::bits:   c = match::controlsOf (s.bits); break;
            case Module::hiss:   c = match::controlsOf (s.hiss); break;
            case Module::room:   c = match::controlsOf (s.room); break;
            case Module::warble:
            default:             break;
        }
        return { c.a, c.b };
    }

    /** One table's shape, as MatchTables.h declares it. */
    struct Shape
    {
        Module module;
        int types, aPoints, bPoints, intensityPoints;
        const char* typeName;
    };

    constexpr std::array<Shape, 5> shapes { {
        { Module::glitch, match::GlitchTable::types, match::GlitchTable::aPoints, match::GlitchTable::bPoints, match::GlitchTable::intensityPoints, "GlitchTable" },
        { Module::crush, match::ModuleTable::types, match::ModuleTable::aPoints, match::ModuleTable::bPoints, match::ModuleTable::intensityPoints, "ModuleTable" },
        { Module::bits, match::ModuleTable::types, match::ModuleTable::aPoints, match::ModuleTable::bPoints, match::ModuleTable::intensityPoints, "ModuleTable" },
        { Module::hiss, match::ModuleTable::types, match::ModuleTable::aPoints, match::ModuleTable::bPoints, match::ModuleTable::intensityPoints, "ModuleTable" },
        { Module::room, match::ModuleTable::types, match::ModuleTable::aPoints, match::ModuleTable::bPoints, match::ModuleTable::intensityPoints, "ModuleTable" },
    } };

    int indexIn (const Shape& sh, int type, int a, int b, int i)
    {
        return ((type * sh.aPoints + a) * sh.bPoints + b) * sh.intensityPoints + i;
    }
}

int writeTables (const std::string& path)
{
    using namespace match;

    std::vector<ChainSettings> jobs;
    struct Slot
    {
        size_t table;
        int index;
    };
    std::vector<Slot> slots;

    for (size_t k = 0; k < shapes.size(); ++k)
    {
        const auto& sh = shapes[k];
        for (int t = 0; t < typesOf (sh.module); ++t)
            for (int a = 0; a < sh.aPoints; ++a)
                for (int b = 0; b < sh.bPoints; ++b)
                    for (int i = 1; i < sh.intensityPoints; ++i)
                    {
                        // Glitch's last axis is the share of slots that fire: at Chaos 0, every Nth
                        // slot is the intensity whose density is 1 / N.
                        const auto intensity = sh.module == Module::glitch ? std::sqrt (glitchDensityAt (i) / glitch::maxDensity)
                                                                           : positionAt (i, sh.intensityPoints);
                        jobs.push_back (alone (sh.module, t, positionAt (a, sh.aPoints), positionAt (b, sh.bPoints), intensity, 1.0f, false));
                        slots.push_back ({ k, indexIn (sh, t, a, b, i) });
                    }
    }

    std::fprintf (stderr, "Measuring %zu settings\n", jobs.size());
    const auto results = measureAll (jobs);

    std::array<std::vector<float>, shapes.size()> tables;
    for (size_t k = 0; k < shapes.size(); ++k)
        tables[k].assign ((size_t) (shapes[k].types * shapes[k].aPoints * shapes[k].bPoints * shapes[k].intensityPoints), 0.0f);
    for (size_t k = 0; k < results.size(); ++k)
        tables[slots[k].table][(size_t) slots[k].index] = std::min ((float) -results[k], maxBoostDb);

    std::ostringstream h;
    h << "#pragma once\n\n"
      << "// GENERATED by tools/calibrate (InkcapCalibrate). Do not edit by hand: re-run it whenever a\n"
      << "// module's level changes (src/dsp/chain/NOTES.md, Match).\n"
      << "//\n"
      << "// Per module: [type][a][b][intensity], the dB that restores the K-weighted loudness of pink\n"
      << "// noise at -18 dBFS RMS. a and b in even steps over their range (bipolar controls from -100 to\n"
      << "// +100; Glitch's b one row per Rate), intensity in even steps (Glitch: the share of slots that\n"
      << "// fire, Match.h); none is 0 dB. Boosts stop at +" << (int) maxBoostDb << " dB.\n\n"
      << "#include \"dsp/chain/Match.h\"\n\n"
      << "namespace inkcap::dsp::match\n{\n";

    char number[32];
    for (size_t k = 0; k < shapes.size(); ++k)
    {
        const auto& sh = shapes[k];
        h << "\n// " << moduleInfo (sh.module).name << ": " << controlNames (sh.module) << "\ninline constexpr " << sh.typeName << " "
          << tableName (sh.module) << " { {\n";
        for (int t = 0; t < sh.types; ++t)
        {
            h << "    // " << (t < typesOf (sh.module) ? moduleInfo (sh.module).types[(size_t) t] : "(unused)") << "\n";
            for (int a = 0; a < sh.aPoints; ++a)
                for (int b = 0; b < sh.bPoints; ++b)
                {
                    h << "   ";
                    for (int i = 0; i < sh.intensityPoints; ++i)
                    {
                        // Two decimals are a hundredth of a dB; adding 0 turns -0.00 into 0.00.
                        const auto v = std::round (100.0 * (double) tables[k][(size_t) indexIn (sh, t, a, b, i)]) / 100.0;
                        std::snprintf (number, sizeof (number), " %.2ff,", v + 0.0);
                        h << number;
                    }
                    h << "\n";
                }
        }
        h << "} };\n";
    }
    h << "\n} // namespace inkcap::dsp::match\n";

    std::ofstream file (path);
    if (! file)
    {
        std::fprintf (stderr, "Could not write %s\n", path.c_str());
        return 1;
    }
    file << h.str();
    std::printf ("Wrote %s\n", path.c_str());
    return 0;
}

int report()
{
    // The gate (PLAN.md): within +-1 dB over the whole Damage range per module, Match on. Damage in
    // 5 % steps at Amount 100 and 50 % land between the table's intensity points; the controls at
    // their defaults and at positions between the grid's.
    std::printf ("Match on: loudness of pink noise at -18 dBFS RMS, out - in (dB), Damage 0 .. 100 %% in 5 %% steps\n");
    std::printf ("%-24s %22s %22s %24s\n", "module, type", "controls default, worst", "between grid, worst", "Match off at Damage 100");

    auto failures = 0;
    for (int m = 0; m < numModules; ++m)
    {
        const auto module = (Module) m;
        for (int t = 0; t < typesOf (module); ++t)
        {
            std::vector<ChainSettings> centred, between;
            for (int d = 1; d <= 20; ++d)
                for (const auto amount : { 1.0f, 0.5f })
                {
                    centred.push_back (alone (module, t, defaultControls (module).a, defaultControls (module).b, amount, 0.05f * (float) d, true));
                    for (const auto& [a, b] : { std::pair { 0.1f, 0.4f }, { 0.65f, 0.9f }, { 0.9f, 0.15f }, { 0.08f, 0.08f }, { 0.42f, 0.58f } })
                        between.push_back (alone (module, t, a, b, amount, 0.05f * (float) d, true));
                }

            struct Worst
            {
                double db = 0.0;
                size_t at = 0;
            };
            auto worst = [] (const std::vector<double>& r)
            {
                Worst w;
                for (size_t k = 0; k < r.size(); ++k)
                    if (std::abs (r[k]) > std::abs (w.db))
                        w = { r[k], k };
                return w;
            };
            const auto c = worst (measureAll (centred)).db;
            const auto wb = worst (measureAll (between));
            const auto b = wb.db;
            const auto raw = gainDb (alone (module, t, defaultControls (module).a, defaultControls (module).b, 1.0f, 1.0f, false));
            const auto pass = std::abs (c) <= 1.0;
            failures += pass ? 0 : 1;

            const auto label = std::string (moduleInfo (module).name) + ", " + moduleInfo (module).types[(size_t) t];
            const auto& where = between[wb.at];
            const auto [wa, wbb] = whereOf (module, where);
            std::printf ("%-24s %18.2f dB %18.2f dB %20.2f dB  %s   (worst between: a %.2f, b %.2f, Damage %.2f)\n", label.c_str(), c, b, raw,
                         pass ? "ok" : "FAIL", (double) wa, (double) wbb, (double) where.damage);
        }
    }
    std::printf ("\n%s: %d of the module types outside +-1 dB at their controls' defaults\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace inkcap::calibrate
