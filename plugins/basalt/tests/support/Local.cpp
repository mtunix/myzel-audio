// The gates that run locally, never in CI: perf (PLAN.md, Performance budget) and the corpus's
// reduction statistics (for Learn, M3).

#include "support/GateKit.h"

#include "support/Corpus.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace basalt::measure
{

using namespace kit;

namespace
{
    constexpr int perfBlock = 512;
    constexpr double perfSeconds = 10.0;
    constexpr double switchBudgetPercent = 1.5;     // a model switch: two models for 30 ms
    constexpr double characterBudgetPercent = 1.5;  // Character on adds at most this
    constexpr double keyPathBudgetPercent = 0.1;    // look-ahead 10 ms, every key EQ stage and the side chain on add at most this

    struct Cost
    {
        double percent = 0.0;       // of one core, in real time
        double peakPercent = 0.0;   // the costliest block, of its own duration
    };

    /**
        Percent of one core to run the device in real time, as Halite's perf test: the fastest of
        five passes in process CPU time, and the costliest block of each pass (wall time), the
        smallest of those (a block the OS took the core from in one pass is not the plugin's).
    */
    Cost costOf (DeviceKind kind, const Settings& s, const Channels& in, double rate, const Automation& automate, bool sideChain)
    {
        const auto n = (int) in[0].size();
        auto best = 1.0e9, peak = 1.0e9;
        for (int pass = 0; pass < 5; ++pass)
        {
            auto device = makeDevice (kind);
            device->prepare (s, rate, 2, sideChain ? 2 : 0, perfBlock);
            auto buffer = in;
            auto current = s;
            auto worst = 0.0;

            const auto t0 = std::clock();
            for (int pos = 0; pos + perfBlock <= n; pos += perfBlock)
            {
                if (automate)
                {
                    auto next = current;
                    automate (pos, next);
                    if (! (next == current))
                    {
                        device->set (next);
                        current = next;
                    }
                }
                std::array<float*, 2> channels { buffer[0].data() + pos, buffer[1].data() + pos };
                std::array<const float*, 2> key { in[0].data() + pos, in[1].data() + pos };
                const auto b0 = std::chrono::steady_clock::now();
                device->process (channels.data(), 2, sideChain ? key.data() : nullptr, sideChain ? 2 : 0, perfBlock);
                worst = std::max (worst, std::chrono::duration<double> (std::chrono::steady_clock::now() - b0).count());
            }
            best = std::min (best, (double) (std::clock() - t0) / CLOCKS_PER_SEC);
            peak = std::min (peak, worst);
        }
        return { 100.0 * best / (n / rate), 100.0 * peak / (perfBlock / rate) };
    }

    /** Two decorrelated pink noises swelling by up to 20 dB at 2 and 3 Hz, so the detectors attack and release all the time. */
    Channels perfMaterial (double rate)
    {
        const auto n = samplesFor (perfSeconds, rate);
        auto l = pink (n, rate, 41u, -20.0), r = pink (n, rate, 42u, -22.0);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t) i] *= (float) gainOf (10.0 * std::sin (2.0 * std::numbers::pi * 2.0 * i / rate));
            r[(size_t) i] *= (float) gainOf (10.0 * std::sin (2.0 * std::numbers::pi * 3.0 * i / rate + 1.0));
        }
        return { l, r };
    }

    std::string loadAverage()
    {
#if defined(__unix__) || defined(__APPLE__)
        double load[3] {};
        if (getloadavg (load, 3) == 3)
            return format ("load average %.2f, %.2f, %.2f", load[0], load[1], load[2]);
#endif
        return {};
    }
}

std::vector<Row> perf (const Options& o)
{
    std::vector<Model> models;
    if (o.model)
        models = { *o.model };
    else
        models = offeredModels();

    const auto in = perfMaterial (o.rate);
    const auto group = format ("%g kHz stereo, %d-sample blocks, %s", o.rate / 1000.0, perfBlock, deviceName (o.device));

    // PLAN.md states the budget at 48 kHz; the per-sample cost scales with the rate, so other rates are reported.
    const auto budgetRate = std::abs (o.rate - 48000.0) < 0.5;
    std::vector<Row> rows;
    const auto row = [&] (const std::string& item, const Settings& s, double budget, const Automation& automate = {}, const std::string& notExercised = {},
                          bool sideChain = false)
    {
        const auto cost = costOf (o.device, s, in, o.rate, automate, sideChain);
        const auto note = format ("costliest block %.1f %% of its time", cost.peakPercent);
        if (! notExercised.empty())
            rows.push_back (reported ("perf", group, item, cost.percent, budget, "% of a core", "not exercised: " + notExercised + "; " + note));
        else if (! budgetRate)
            rows.push_back (reported ("perf", group, item, cost.percent, budget, "% of a core", "the budget is set at 48 kHz; " + note));
        else
            rows.push_back (gated ("perf", group, item, cost.percent, budget, 0.0, budget, "% of a core", note));
    };

    for (const auto model : models)
    for (const auto detector : detectorsFor (o, model))
    {
        // A model this build has no engine for runs Clean's (Settled 65): its cost is Clean's, not its own.
        const auto own = model == Model::clean || distinctEngines (model, Model::clean);
        const auto engineless = own ? std::string() : std::string ("runs Clean's engine in this build (1.1 builds its own)");
        const auto label = modelLabel (model, detector);
        const auto budget = specOf (model).budgetPercent;
        auto s = o.base;
        s.model = model;
        s.vcaDetector = detector;
        for (int st = 0; st < numStereoModes; ++st)
        {
            s.stereo = static_cast<StereoMode> (st);
            row (format ("%s, %s, Normal", label.c_str(), stereoName (s.stereo)), s, budget, {}, engineless);
        }
        s.stereo = StereoMode::linked;

        // The costliest ordinary setting: every stage of the key path at work, the full look-ahead
        // window, and the side-chain bus on (its key line runs whether it is heard or not).
        for (const auto mode : { StereoMode::linked, StereoMode::midSide })
        {
            auto w = s;
            w.stereo = mode;
            w.lookaheadMs = 10.0;
            w.keyHpfHz = 100.0;
            w.keyLpfHz = 12000.0;
            w.keyBellDb = 6.0;
            row (format ("%s, %s, Normal, look-ahead 10 ms, key HPF, LPF and bell, side chain on", label.c_str(), stereoName (mode)), w,
                 budget + keyPathBudgetPercent, {}, engineless, true);
        }

        s.latency = LatencyMode::live;
        row (format ("%s, Linked, Live", label.c_str()), s, budget, {}, engineless);

        s.latency = LatencyMode::normal;
        for (const auto mode : { CharacterMode::warm, CharacterMode::iron, CharacterMode::edge })
        {
            auto c = s;
            c.character = mode;
            constexpr const char* names[] { "Off", "Warm", "Iron", "Edge" };
            row (format ("%s, Linked, Normal, Character %s", label.c_str(), names[static_cast<int> (mode)]), c, budget + characterBudgetPercent, {},
                 characterBuilt ? engineless : std::string ("Character is M3, run as Off"));
        }

        // A switch every 100 ms keeps two models running most of the time: the warm-up, then the 30 ms crossfade.
        const auto offered = offeredModels();
        const auto next = offered[(size_t) (static_cast<int> (runningModel (model)) + 1) % offered.size()];
        const auto rate = o.rate;
        row (format ("%s <-> %s every 100 ms, Linked, Normal", label.c_str(), modelKey (next)), s, switchBudgetPercent,
             [s, next, rate] (int pos, Settings& x)
             {
                 x = s;
                 if ((int) (pos / (0.1 * rate)) % 2 == 1)
                     x.model = next;
             },
             ! own ? engineless : (distinctEngines (model, next) ? std::string() : std::string ("both run the same engine in this build")));
        if (specOf (model).detectorSwitch && detector == VcaDetector::peak)
            row (format ("%s, Peak <-> RMS every 100 ms, Linked, Normal", modelKey (model)), s, switchBudgetPercent,
                 [s, rate] (int pos, Settings& x)
                 {
                     x = s;
                     if ((int) (pos / (0.1 * rate)) % 2 == 1)
                         x.vcaDetector = VcaDetector::rms;
                 },
                 distinctDetectors() ? std::string() : std::string ("both detectors run the same engine in this build"));
    }
    rows.push_back (reported ("perf", group, "while measuring", std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), "",
                              loadAverage()));
    return rows;
}

//==============================================================================
std::vector<Row> corpus (const Options& o)
{
    const auto dir = o.corpusDir.empty() ? defaultCorpusDir() : o.corpusDir;
    const auto names = excerptNames (dir);
    if (names.empty())
        return { gated ("corpus", "corpus", "excerpts.tsv in " + dir, 0.0, 1.0, 1.0, 1.0e9, "excerpts", "not found") };

    auto s = o.base;
    s.model = o.modelOrClean();
    const auto group = format ("%s, T %g, %s, Knee %g, Attack %g ms, Release %g ms%s", modelKey (s.model), s.thresholdDb, ratioText (s.ratio).c_str(), s.kneeDb,
                               s.attackMs, s.releaseMs, s.autoRelease ? " Auto" : "");

    std::vector<Job> jobs;
    for (const auto& name : names)
        jobs.push_back ([&o, &dir, s, group, name]
        {
            const auto e = loadExcerpt (dir, name);
            if (! e)
                return std::vector<Row> { gated ("corpus", group, name, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0, "dB", "could not be read") };

            const auto r = renderKey (o.device, s, e->audio, Probe::external, { e->rate, { 512 }, {}, {} });
            const auto frame = std::max (1, (int) std::lround (e->rate / 1000.0));
            const auto n = (int) e->audio[0].size();

            // Per 1 ms frame: the mean reduction (the larger channel's) and the key's level on the detector scale.
            std::vector<double> reduction, level;
            for (int f = 0; f + frame <= n; f += frame)
            {
                auto sum = 0.0, power = 0.0;
                for (int i = f; i < f + frame; ++i)
                {
                    sum += std::max (r.traces[0].reductionDb[(size_t) i], r.traces[1].reductionDb[(size_t) i]);
                    for (const auto& ch : e->audio)
                        power = std::max (power, (double) ch[(size_t) i] * ch[(size_t) i]);
                }
                reduction.push_back (sum / frame);
                level.push_back (power > 0.0 ? 10.0 * std::log10 (2.0 * power) : -200.0);
            }

            const auto median = percentile (level, 50.0);
            std::vector<double> loud;
            auto reducing = 0;
            for (size_t i = 0; i < reduction.size(); ++i)
            {
                if (level[i] > median)
                    loud.push_back (reduction[i]);
                reducing += reduction[i] > 0.5 ? 1 : 0;
            }
            const auto loudMean = loud.empty() ? 0.0 : mean (loud, 0, (int) loud.size());
            return std::vector<Row> { reported ("corpus", group, format ("%s (%s, %g kHz, %zu ch)", name.c_str(), e->category.c_str(), e->rate / 1000.0, e->audio.size()),
                                                loudMean, std::numeric_limits<double>::quiet_NaN(), "dB",
                                                format ("mean on the loud half; all frames: mean %.2f, p50 %.2f, p95 %.2f, max %.2f dB; reducing > 0.5 dB %.1f %% of the time",
                                                        mean (reduction, 0, (int) reduction.size()), percentile (reduction, 50.0), percentile (reduction, 95.0),
                                                        percentile (reduction, 100.0), 100.0 * reducing / std::max<size_t> (reduction.size(), 1))) };
        });
    return runJobs (jobs, o.jobs);
}

} // namespace basalt::measure
