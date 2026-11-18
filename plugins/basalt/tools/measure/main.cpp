// BasaltMeasure: renders signals through Basalt's processor, its controls set through the
// parameters as a host sets them, and measures the gates of PLAN.md (Measurement and quality
// gates) with the core's and each model's rows (src/dsp/dynamics/NOTES.md,
// src/dsp/models/<m>/NOTES.md). The measurements live in tests/support, shared with
// tests/GateTests.cpp; this prints them in full.
//
//     BasaltMeasure all                              the fast gates (identity, static, ballistics, clicks, stereo, distortion), full grids
//     BasaltMeasure static ballistics --model vca    some gates for one model
//     BasaltMeasure ballistics --device reference    on the transcription of the NOTES: what a correct engine reads
//     BasaltMeasure selftest                         every measurement against a known answer
//     BasaltMeasure --help                           every option

#include "common/dsp/FloatCompare.h"
#include "support/Gates.h"

#include <juce_events/juce_events.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

using namespace basalt;
using namespace basalt::measure;

namespace
{
    constexpr auto usage = R"(BasaltMeasure GATE... [options]

Gates
  identity     Character off, a gain of exactly 1, Mix 100: the output is the latency-aligned input bit for bit;
               every model (unless --model), Normal and Live, every stereo mode, look-ahead, key, block sizes 1 ... 4096
  static       steady reduction against level, 1 kHz sines and the constant-magnitude carrier, over Threshold,
               Ratio, Knee and Range: +-0.25 dB of the model's curve; 100 and 50 Hz reported; pink noise against
               the transcription; Range
  ballistics   attack (63.2 %) and release (36.8 %, the Opto's 50 %) on carrier steps over the model's grid: +-10 % or
               +-0.25 sample; look-ahead shifts; Range; the model's own rows from its NOTES (Clean, VCA and Vari-mu Auto,
               VCA RMS times, FET All's lag, the Opto's history and depth, Upward's lift ballistics and Auto)
  clicks       every knob min <-> max, model and detector switches, Character, latency, bypass: 3rd-difference crest
               of 330 Hz sines <= -60 dB (Halite's method), at Attack 0.02, 10 and (switches) 300 ms, on a key each
               knob reaches (two levels, inside the knee, tones in the key EQ's bands, releases); no swell
  stereo       Linked (bit-identical gains), Link 50 and 0 %, Unlinked, Mid, Side, Mid/Side, the M/S round trip
  distortion   THD+N of a 1 kHz sine at 6 dB of reduction over Attack x Release: <= -80 dBc where the model's NOTES
               gate it (Clean: Attack >= 10 ms, Release >= 50 ms), reported elsewhere; THD (harmonics 2-10) beside it
  perf         % of one core at the rate, stereo, per model and stereo mode, Live, Character, a model switch;
               gated at 48 kHz (PLAN.md's budget), reported at other rates and for what this build does not have
  corpus       every committed excerpt through the model: reduction statistics (for Learn)
  selftest     the measurements on known answers (a one-pole, the transcription of the NOTES, synthetic signals)
  all          identity, static, ballistics, clicks, stereo, distortion

Options
  --model NAME       clean (default; identity and perf: every model), vca, fet, opto, vari-mu, upward, diode-bridge, pwm, pumping
  --device NAME      processor (default), reference (BasaltMeasure's transcription of the NOTES; Clean), one-pole
  --rate HZ          sample rate (default 48000)
  --latency LIST     normal, live or normal,live (default)
  --probe NAME       external (default: the test signal on the side chain, a probe on the main bus, so the gain is
                     known at every sample) or internal (the test signal on the main bus)
  --quick            the reduced grids CI runs
  --jobs N           worker threads (default 2)
  --set KEY=VALUE    a control for corpus and perf, by parameter ID (latency for the session setting), repeatable;
                     vcaDetector also limits VCA's gates to that detector (by default they run both)
  --corpus DIR       the committed excerpts (default plugins/basalt/tests/corpus)
  --tsv FILE         every row as TSV (- for stdout)
  --dump DIR         ballistics steps and clicks also write their raw renders to DIR: NAME.f32 (float32 per sample: main
                     input, main output, key, channel 0) and NAME.txt (rate, latency), for checking a row by hand
  --all-rows         print every row; by default a group whose rows all pass is one line
  --help

Exit status: 0 when every gated row passes, 1 when one fails, 2 on a usage error.
)";

    const std::vector<std::string> gateNames { "identity", "static", "ballistics", "clicks", "stereo", "distortion", "perf", "corpus", "selftest" };

    std::vector<Row> run (const std::string& gate, const Options& o)
    {
        if (gate == "identity")
            return identity (o);
        if (gate == "static")
            return staticCurve (o);
        if (gate == "ballistics")
            return ballistics (o);
        if (gate == "clicks")
            return clicks (o);
        if (gate == "stereo")
            return stereo (o);
        if (gate == "distortion")
            return distortion (o);
        if (gate == "perf")
            return perf (o);
        if (gate == "corpus")
            return corpus (o);
        return selfTest (o);
    }

    std::string number (double v)
    {
        if (std::isnan (v))
            return "n/a";
        if (std::isinf (v))
            return v > 0 ? "inf" : "-inf";
        char text[32];
        std::snprintf (text, sizeof (text), std::abs (v) >= 1.0e5 || (! myzel::dsp::isExactly (v, 0.0) && std::abs (v) < 1.0e-3) ? "%.3g" : "%.4f", v);
        auto s = std::string (text);
        if (s.find ('.') != std::string::npos && s.find ('e') == std::string::npos)
        {
            while (s.back() == '0')
                s.pop_back();
            if (s.back() == '.')
                s.pop_back();
        }
        return s;
    }

    /** How far a gated row is from failing, in units of its own tolerance (smaller is closer). */
    double margin (const Row& r)
    {
        if (r.verdict != Verdict::pass)
            return -1.0;
        const auto width = std::isfinite (r.low) && std::isfinite (r.high) && r.high > r.low ? 0.5 * (r.high - r.low) : 1.0;
        return std::min (r.measured - r.low, r.high - r.measured) / width;
    }

    std::string clean (std::string s)
    {
        for (auto& c : s)
            if (c == '\t' || c == '\n' || c == '|')
                c = c == '|' ? '/' : ' ';
        return s;
    }

    void printTable (const std::string& gate, const std::vector<Row>& rows, bool allRows)
    {
        std::printf ("\n## %s\n\n| Group | Item | Measured | Target | Bounds | Verdict | Note |\n|---|---|---|---|---|---|---|\n", gate.c_str());
        std::vector<std::string> order;
        std::map<std::string, std::vector<const Row*>> groups;
        for (const auto& r : rows)
        {
            if (! groups.contains (r.group))
                order.push_back (r.group);
            groups[r.group].push_back (&r);
        }

        const auto line = [] (const Row& r)
        {
            std::printf ("| %s | %s | %s %s | %s | %s | %s | %s |\n", clean (r.group).c_str(), clean (r.item).c_str(), number (r.measured).c_str(),
                         r.unit.c_str(), number (r.target).c_str(), boundsText (r).c_str(), r.verdict == Verdict::fail ? "**FAIL**" : verdictName (r.verdict),
                         clean (r.note).c_str());
        };

        for (const auto& g : order)
        {
            const auto& members = groups[g];
            const auto failing = std::count_if (members.begin(), members.end(), [] (const Row* r) { return r->verdict == Verdict::fail; });
            const auto gatedRows = std::count_if (members.begin(), members.end(), [] (const Row* r) { return r->verdict == Verdict::pass || r->verdict == Verdict::fail; });
            if (allRows || members.size() == 1 || gatedRows < (long) members.size())
            {
                for (const auto* r : members)
                    line (*r);
                continue;
            }
            if (failing == 0)
            {
                const auto* closest = *std::min_element (members.begin(), members.end(), [] (const Row* a, const Row* b) { return margin (*a) < margin (*b); });
                std::printf ("| %s | all %zu pass; closest to its bounds: %s | %s %s | %s | %s | pass | %s |\n", clean (g).c_str(), members.size(),
                             clean (closest->item).c_str(), number (closest->measured).c_str(), closest->unit.c_str(), number (closest->target).c_str(),
                             boundsText (*closest).c_str(), clean (closest->note).c_str());
                continue;
            }
            std::printf ("| %s | %ld of %zu fail: | | | | | |\n", clean (g).c_str(), (long) failing, members.size());
            auto shown = 0;
            for (const auto* r : members)
                if (r->verdict == Verdict::fail && shown++ < 25)
                    line (*r);
            if (shown > 25)
                std::printf ("| %s | ... %d more failing rows (--all-rows or --tsv) | | | | | |\n", clean (g).c_str(), shown - 25);
        }
    }

    void writeTsv (const std::string& path, const std::vector<std::pair<std::string, std::vector<Row>>>& results, const Options& o)
    {
        FILE* f = path == "-" ? stdout : std::fopen (path.c_str(), "w");
        if (f == nullptr)
        {
            std::fprintf (stderr, "cannot write %s\n", path.c_str());
            return;
        }
        std::fprintf (f, "gate\tdevice\tmodel\trate\tgroup\titem\tmeasured\ttarget\tlow\thigh\tunit\tverdict\tnote\n");
        for (const auto& [gate, rows] : results)
            for (const auto& r : rows)
                std::fprintf (f, "%s\t%s\t%s\t%g\t%s\t%s\t%.9g\t%.9g\t%.9g\t%.9g\t%s\t%s\t%s\n", gate.c_str(), deviceName (o.device),
                              o.model ? modelKey (*o.model) : "", o.rate, clean (r.group).c_str(), clean (r.item).c_str(), r.measured, r.target, r.low, r.high,
                              r.unit.c_str(), verdictName (r.verdict), clean (r.note).c_str());
        if (f != stdout)
            std::fclose (f);
    }

    int usageError (const std::string& message)
    {
        std::fprintf (stderr, "%s\n\n%s", message.c_str(), usage);
        return 2;
    }
}

int main (int argc, char** argv)
{
    // The processor's parameters and their attachments expect JUCE's message manager to exist.
    const juce::ScopedJuceInitialiser_GUI juce;

    Options o;
    std::vector<std::string> gates;
    std::string tsv;
    auto allRows = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto missing = false;
        const auto value = [&] () -> std::string
        {
            if (i + 1 >= argc || std::strncmp (argv[i + 1], "--", 2) == 0)
            {
                missing = true;
                return {};
            }
            return argv[++i];
        };

        if (a == "--help" || a == "-h")
        {
            std::fputs (usage, stdout);
            return 0;
        }
        if (a == "all")
            gates.insert (gates.end(), { "identity", "static", "ballistics", "clicks", "stereo", "distortion" });
        else if (std::find (gateNames.begin(), gateNames.end(), a) != gateNames.end())
            gates.push_back (a);
        else if (a == "--model")
        {
            const auto m = modelFromKey (value());
            if (! m)
                return usageError ("unknown model");
            o.model = *m;
            o.base.model = *m;
        }
        else if (a == "--device")
        {
            const auto d = value();
            if (d == "processor")
                o.device = DeviceKind::processor;
            else if (d == "reference")
                o.device = DeviceKind::reference;
            else if (d == "one-pole")
                o.device = DeviceKind::onePole;
            else
                return usageError ("unknown device " + d);
        }
        else if (a == "--rate")
        {
            o.rate = std::atof (value().c_str());
            if (o.rate < 8000.0 || o.rate > 384000.0)
                return usageError ("rate out of range");
        }
        else if (a == "--latency")
        {
            const auto v = value();
            if (v == "normal")
                o.latencies = { LatencyMode::normal };
            else if (v == "live")
                o.latencies = { LatencyMode::live };
            else if (v == "normal,live" || v == "both")
                o.latencies = { LatencyMode::normal, LatencyMode::live };
            else
                return usageError ("unknown latency " + v);
        }
        else if (a == "--probe")
        {
            const auto v = value();
            if (v != "external" && v != "internal")
                return usageError ("unknown probe " + v);
            o.probe = v == "external" ? Probe::external : Probe::internal;
        }
        else if (a == "--quick")
            o.quick = true;
        else if (a == "--jobs")
            o.jobs = std::max (1, std::atoi (value().c_str()));
        else if (a == "--set")
        {
            const auto kv = value();
            const auto eq = kv.find ('=');
            if (eq == std::string::npos || ! setSetting (o.base, kv.substr (0, eq), kv.substr (eq + 1)))
                return usageError ("cannot set " + kv);
            if (kv.substr (0, eq) == "vcaDetector")
                o.detectors = { o.base.vcaDetector };
        }
        else if (a == "--corpus")
            o.corpusDir = value();
        else if (a == "--tsv")
            tsv = value();
        else if (a == "--dump")
            o.dumpDir = value();
        else if (a == "--all-rows")
            allRows = true;
        else
            return usageError ("unknown argument " + a);

        if (missing)
            return usageError ("missing value for " + a);
    }
    if (gates.empty())
        return usageError ("no gate given");
    if (o.model)
        o.base.model = *o.model;

    std::printf ("# BasaltMeasure\n\nDevice %s, %g Hz, model %s, probe %s, latency %s%s, %d jobs\n", deviceName (o.device), o.rate,
                 o.model ? modelKey (*o.model) : "clean (identity: every model)", o.probe == Probe::external ? "external" : "internal",
                 o.latencies.size() == 2 ? "Normal and Live" : latencyName (o.latencies[0]), o.quick ? ", quick grids" : "", o.jobs);

    std::vector<std::pair<std::string, std::vector<Row>>> results;
    auto ok = true;
    for (const auto& gate : gates)
    {
        const auto t0 = std::chrono::steady_clock::now();
        auto rows = run (gate, o);
        const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        printTable (gate, rows, allRows);

        std::map<Verdict, int> counts;
        for (const auto& r : rows)
            ++counts[r.verdict];
        std::printf ("\n%s: %d pass, %d fail, %d reported, %d not specified (%.1f s)\n", gate.c_str(), counts[Verdict::pass], counts[Verdict::fail],
                     counts[Verdict::reported], counts[Verdict::notSpecified], seconds);
        std::fflush (stdout);
        ok = ok && passed (rows);
        results.emplace_back (gate, std::move (rows));
    }

    if (! tsv.empty())
        writeTsv (tsv, results, o);

    std::printf ("\n%s\n", ok ? "Every gated row passes." : "Some gated rows FAIL.");
    return ok ? 0 : 1;
}
