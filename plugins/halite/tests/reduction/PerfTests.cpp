// The chain's CPU cost against the budget (PLAN.md, Performance budget): at 48 kHz stereo with the
// detector, Split or Wide ≤ 0.5 % of a core, Spectral ≤ 1.5 %, Harsh adds ≤ 0.3 %, a Shape
// crossfade ≤ 2 %, Unlinked Spectral ≤ 2.5 %; doubled at 88.2/96 kHz, quadrupled at 176.4/192.
// Beside each average, the costliest single block: a block that does far more than its share runs
// past its deadline however low the average. Reports only, in process CPU time (blocks in wall
// time) with the load average beside it; pin it to one core for stable numbers:
// taskset -c 2 HaliteTests_reduction "[.perf]"

#include "ChainRun.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace halite;
using namespace halite::testing;

namespace
{
    constexpr std::array rates { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
    constexpr double seconds = 10.0;

    void printLoadAverage()
    {
       #if defined (__unix__) || defined (__APPLE__)
        double load[3] {};
        if (getloadavg (load, 3) == 3)
            std::printf ("Load average %.2f, %.2f, %.2f (1, 5, 15 min)\n", load[0], load[1], load[2]);
       #endif
    }

    /** The plan's budgets are set at 48 kHz; the Spectral frame scales with the rate, so they do too. */
    double budgetScale (double rate) { return rate < 60000.0 ? 1.0 : (rate < 120000.0 ? 2.0 : 4.0); }

    /** Blocks in the first pass, and those where each band cut. */
    struct Activity
    {
        int blocks = 0, ess = 0, harsh = 0;
    };

    struct Cost
    {
        double percent = 0.0;       // of one core, in real time
        double peakPercent = 0.0;   // the costliest block, of its own duration
    };

    /**
        Percent of one core to run the chain in real time, 512-sample blocks: the fastest of five
        passes; and the costliest block of each pass, the smallest of those (a block the OS took the
        core from in one pass is not the chain's).
    */
    Cost costOf (double rate, const dsp::ChainSettings& base, const Channels& in, const Automation& automate = {}, Activity* activity = nullptr)
    {
        const juce::ScopedNoDenormals noDenormals;
        constexpr int block = 512;
        const auto n = (int) in[0].size();
        auto best = 1.0e9, peak = 1.0e9;

        for (int pass = 0; pass < 5; ++pass)
        {
            auto chain = std::make_unique<dsp::HaliteChain>();
            chain->prepare (rate, block, 2, base.detector.latency);
            auto buffer = in;
            auto s = base;
            auto worst = 0.0;

            const auto t0 = std::clock();
            for (int pos = 0; pos + block <= n; pos += block)
            {
                if (automate)
                    automate (pos, s);
                std::array<float*, 2> channels { buffer[0].data() + pos, buffer[1].data() + pos };
                const auto b0 = std::chrono::steady_clock::now();
                chain->process (channels.data(), 2, block, s);
                worst = std::max (worst, std::chrono::duration<double> (std::chrono::steady_clock::now() - b0).count());
                if (activity != nullptr && pass == 0)
                {
                    ++activity->blocks;
                    activity->ess += chain->meters().essReductionDb[0].load() > 0.0f ? 1 : 0;
                    activity->harsh += chain->meters().harshReductionDb[0].load() > 0.0f ? 1 : 0;
                }
            }
            best = std::min (best, (double) (std::clock() - t0) / CLOCKS_PER_SEC);
            peak = std::min (peak, worst);
        }
        return { 100.0 * best / (n / rate), 100.0 * peak / (block / rate) };
    }

    double percentOfCore (double rate, const dsp::ChainSettings& base, const Channels& in, const Automation& automate = {},
                          Activity* activity = nullptr)
    {
        return costOf (rate, base, in, automate, activity).percent;
    }

    /** The signals every case runs on, at one rate. */
    struct Material
    {
        Channels voice;         // a voice with an ess every 0.5 s on both channels, slightly different, so Linked switches paths
        Channels essAlways;     // the same voice under an ess that never stops
        Channels harshAlways;   // a voice whose harsh resonance swells and fades, no ess
        Channels bothAlways;    // the same, under an ess 0.4 s of every 0.5 s: the Harsh band holds its cut through each
    };

    Material materialAt (double rate)
    {
        const auto n = samplesFor (seconds, rate);
        Material m;
        auto left = vowel (180.0, n, rate, 0.1, 1);
        auto right = vowel (182.0, n, rate, 0.08, 2);
        const auto ess = bandNoise (4500.0, 12000.0, n, rate, 3, 0.05);
        auto l = left, r = right;
        for (int i = 0; i < n; ++i)
        {
            const auto inEss = (i % samplesFor (0.5, rate)) < samplesFor (0.12, rate);
            l[(size_t) i] += inEss ? ess[(size_t) i] : 0.0f;
            r[(size_t) i] += inEss ? 0.8f * ess[(size_t) i] : 0.0f;
        }
        m.voice = { l, r };
        m.essAlways = { sum (left, ess), sum (right, scaled (ess, 0.8)) };

        auto swelling = harmonic ({ .f0 = 180.0, .topHz = 4000.0, .peakHz = 3200.0, .peakDb = 24.0, .rms = 0.05 }, n, rate);
        for (int i = 0; i < n; ++i)
            swelling[(size_t) i] *= (float) (1.0 + 0.5 * std::sin (2.0 * std::numbers::pi * 3.0 * i / rate));
        m.harshAlways = { sum (left, swelling), sum (right, scaled (swelling, 0.8)) };

        auto gatedEss = ess;
        for (int i = 0; i < n; ++i)
            gatedEss[(size_t) i] *= (i % samplesFor (0.5, rate)) < samplesFor (0.4, rate) ? 1.0f : 0.0f;
        m.bothAlways = { sum (m.harshAlways[0], gatedEss), sum (m.harshAlways[1], scaled (gatedEss, 0.8)) };
        return m;
    }

    dsp::ChainSettings settingsFor (Shape shape, StereoMode stereo, bool harsh)
    {
        dsp::ChainSettings s;
        s.shape = shape;
        s.detector.thresholdDb = -12.0f;
        s.detector.stereo = stereo;
        s.detector.harshOn = harsh;
        return s;
    }

    /** One row of the table: a case, its budget at 48 kHz (0: reported only), and how to measure it at a rate. */
    struct Case
    {
        std::string name;
        double budget = 0.0;
        std::function<Cost (double rate, const Material&, std::string& note)> measure;
    };

    Automation shapeEvery30Ms (double rate, Shape a, Shape b)
    {
        return [rate, a, b] (int pos, dsp::ChainSettings& s) { s.shape = (pos / samplesFor (0.03, rate)) % 2 == 0 ? a : b; };
    }

    std::string activityNote (const Activity& a)
    {
        return juce::String::formatted ("cutting %.0f / %.0f %%", 100.0 * a.ess / std::max (1, a.blocks), 100.0 * a.harsh / std::max (1, a.blocks))
            .toStdString();
    }
}

TEST_CASE ("Perf: chain CPU against the budget, stereo, 44.1 to 192 kHz", "[.perf]")
{
    const auto plain = [] (Shape shape, StereoMode stereo, bool harsh)
    {
        return [=] (double rate, const Material& m, std::string&) { return costOf (rate, settingsFor (shape, stereo, harsh), m.voice); };
    };
    const auto harshShare = [] (Shape shape, StereoMode stereo)
    {
        return [=] (double rate, const Material& m, std::string&)
        {
            return Cost { percentOfCore (rate, settingsFor (shape, stereo, true), m.voice) - percentOfCore (rate, settingsFor (shape, stereo, false), m.voice), 0.0 };
        };
    };
    const auto fading = [] (Shape a, Shape b, StereoMode stereo)
    {
        return [=] (double rate, const Material& m, std::string&)
        { return costOf (rate, settingsFor (a, stereo, true), m.voice, shapeEvery30Ms (rate, a, b)); };
    };
    enum class Busy { ess, harsh, both };
    const auto busy = [] (Shape shape, StereoMode stereo, bool harsh, Busy what)
    {
        return [=] (double rate, const Material& m, std::string& note)
        {
            auto s = settingsFor (shape, stereo, harsh);
            s.detector.harshThresholdDb = -30.0f;
            Activity a;
            const auto& signal = what == Busy::ess ? m.essAlways : (what == Busy::harsh ? m.harshAlways : m.bothAlways);
            const auto cost = costOf (rate, s, signal, {}, &a);
            note = activityNote (a);
            return cost;
        };
    };

    // Detect swept every block: Split's band glides all the time, so each path designs twice per hop.
    const auto swept = [] (Shape shape, StereoMode stereo, bool harsh)
    {
        return [=] (double rate, const Material& m, std::string& note)
        {
            const Automation sweep = [rate] (int pos, dsp::ChainSettings& s)
            {
                const auto phase = std::sin (2.0 * std::numbers::pi * pos / rate);
                s.detector.detectLowHz = (float) (4500.0 * std::exp2 (phase));
                s.detector.detectHighHz = (float) (12000.0 * std::exp2 (0.5 * phase));
            };
            Activity a;
            auto s = settingsFor (shape, stereo, harsh);
            s.detector.harshThresholdDb = -30.0f;
            const auto cost = costOf (rate, s, m.essAlways, sweep, &a);
            note = activityNote (a);
            return cost;
        };
    };

    const std::vector<Case> cases {
        { "Split, Linked", 0.5, plain (Shape::split, StereoMode::linked, false) },
        { "Split, Unlinked", 0.5, plain (Shape::split, StereoMode::unlinked, false) },
        { "Split, Mid", 0.5, plain (Shape::split, StereoMode::mid, false) },
        { "Split, Linked, Live", 0.5,
          [] (double rate, const Material& m, std::string&)
          {
              auto s = settingsFor (Shape::split, StereoMode::linked, false);
              s.detector.latency = LatencyMode::live;
              return costOf (rate, s, m.voice);
          } },
        { "Wide, Linked", 0.5, plain (Shape::wide, StereoMode::linked, false) },
        { "Wide, Unlinked", 0.5, plain (Shape::wide, StereoMode::unlinked, false) },
        { "Spectral, Linked", 1.5, plain (Shape::spectral, StereoMode::linked, false) },
        { "Spectral, Mid", 1.5, plain (Shape::spectral, StereoMode::mid, false) },
        { "Spectral, Unlinked (two Stfts)", 2.5, plain (Shape::spectral, StereoMode::unlinked, false) },
        { "Harsh's share: Split, Linked", 0.3, harshShare (Shape::split, StereoMode::linked) },
        { "Harsh's share: Wide, Linked", 0.3, harshShare (Shape::wide, StereoMode::linked) },
        { "Harsh's share: Spectral, Linked", 0.3, harshShare (Shape::spectral, StereoMode::linked) },
        { "Harsh's share: Spectral, Unlinked", 0.3, harshShare (Shape::spectral, StereoMode::unlinked) },
        { "Split, Linked, Harsh on", 0.8, plain (Shape::split, StereoMode::linked, true) },
        { "Spectral, Linked, Harsh on", 1.8, plain (Shape::spectral, StereoMode::linked, true) },
        { "Spectral, Unlinked, Harsh on", 2.8, plain (Shape::spectral, StereoMode::unlinked, true) },
        { "Crossfade all the time: Split <-> Wide, Harsh on", 2.0, fading (Shape::split, Shape::wide, StereoMode::linked) },
        { "Crossfade all the time: Spectral <-> Split, Linked, Harsh on", 2.0, fading (Shape::spectral, Shape::split, StereoMode::linked) },
        { "Crossfade all the time: Spectral <-> Split, Unlinked, Harsh on", 2.5, fading (Shape::spectral, Shape::split, StereoMode::unlinked) },
        { "Worst: Split, an ess all the time, Harsh on", 0.8, busy (Shape::split, StereoMode::linked, true, Busy::ess) },
        { "Worst: Split, harshness all the time", 0.8, busy (Shape::split, StereoMode::linked, true, Busy::harsh) },
        { "Worst: Split, both bands cutting", 0.8, busy (Shape::split, StereoMode::linked, true, Busy::both) },
        { "Worst: Split, Detect swept every block, an ess all the time", 0.5, swept (Shape::split, StereoMode::linked, false) },
        { "Worst: Wide, Detect swept every block, an ess all the time", 0.5, swept (Shape::wide, StereoMode::linked, false) },
        { "Worst: Split, Unlinked, an ess all the time", 0.5, busy (Shape::split, StereoMode::unlinked, false, Busy::ess) },
        { "Worst: Split, Unlinked, an ess all the time, Harsh on", 0.8, busy (Shape::split, StereoMode::unlinked, true, Busy::ess) },
        { "Worst: Split, Unlinked, both bands cutting", 0.8, busy (Shape::split, StereoMode::unlinked, true, Busy::both) },
        { "Worst: Split, Unlinked, Detect swept every block, an ess all the time", 0.5, swept (Shape::split, StereoMode::unlinked, false) },
        { "Worst: Split, Unlinked, Detect swept every block, an ess all the time, Harsh on", 0.8, swept (Shape::split, StereoMode::unlinked, true) },
        { "Worst: Spectral, Linked, an ess all the time, Harsh on", 1.8, busy (Shape::spectral, StereoMode::linked, true, Busy::ess) },
        { "Worst: Spectral, Unlinked, an ess all the time, Harsh on", 2.8, busy (Shape::spectral, StereoMode::unlinked, true, Busy::ess) },
        { "Worst: Spectral, Linked, both bands cutting", 1.8, busy (Shape::spectral, StereoMode::linked, true, Busy::both) },
        { "Worst: Spectral, Unlinked, both bands cutting", 2.8, busy (Shape::spectral, StereoMode::unlinked, true, Busy::both) },
    };

    std::printf ("\nChain CPU, %% of one core, stereo, detector included, 512-sample blocks, fastest of 5 passes over %.0f s.\n"
                 "Budget at 48 kHz (PLAN.md, Performance budget), x2 at 88.2/96 kHz and x4 at 176.4/192 kHz; Harsh on adds its 0.3 %%;\n"
                 "the crossfade rows run a new Shape every 30 ms (two shapes all the time); \"cutting\": blocks where Ess / Harsh cut.\n",
                 seconds);
    printLoadAverage();

    // Rate by rate, so only one rate's signals are in memory.
    std::vector<std::vector<std::pair<Cost, std::string>>> readings (cases.size());
    for (const auto rate : rates)
    {
        const auto material = materialAt (rate);
        for (size_t c = 0; c < cases.size(); ++c)
        {
            std::string note;
            const auto cost = cases[c].measure (rate, material, note);
            readings[c].emplace_back (cost, note);
        }
        std::printf ("%.1f kHz measured. ", rate / 1000.0);
        printLoadAverage();
        std::fflush (stdout);
    }

    std::printf ("\n| Case | Budget |");
    for (const auto rate : rates)
        std::printf (" %.1f kHz |", rate / 1000.0);
    std::printf ("\n|---|---|");
    for (size_t r = 0; r < rates.size(); ++r)
        std::printf ("---|");
    std::printf ("\n");

    auto over = 0;
    for (size_t c = 0; c < cases.size(); ++c)
    {
        std::printf ("| %s | %.1f |", cases[c].name.c_str(), cases[c].budget);
        for (size_t r = 0; r < rates.size(); ++r)
        {
            const auto& [cost, note] = readings[c][r];
            const auto ok = cost.percent <= cases[c].budget * budgetScale (rates[r]);
            over += ok ? 0 : 1;
            std::printf (" %.3f%s%s%s |", cost.percent, ok ? "" : " **over**", note.empty() ? "" : ", ", note.c_str());
        }
        std::printf ("\n");
    }
    std::printf ("\n%d of %zu readings over their budget.\n", over, cases.size() * rates.size());

    std::printf ("\nThe costliest 512-sample block of each case, %% of the block's duration (Harsh's share: not measured):\n\n| Case |");
    for (const auto rate : rates)
        std::printf (" %.1f kHz |", rate / 1000.0);
    std::printf ("\n|---|");
    for (size_t r = 0; r < rates.size(); ++r)
        std::printf ("---|");
    std::printf ("\n");
    for (size_t c = 0; c < cases.size(); ++c)
    {
        if (readings[c][0].first.peakPercent <= 0.0)
            continue;
        std::printf ("| %s |", cases[c].name.c_str());
        for (size_t r = 0; r < rates.size(); ++r)
            std::printf (" %.2f |", readings[c][r].first.peakPercent);
        std::printf ("\n");
    }
    printLoadAverage();
}

TEST_CASE ("Perf: the block that starts a Spectral engine, 64-sample blocks", "[.perf]")
{
    // Every event that starts a Spectral engine (a Shape or Stereo change, a transport start or
    // jump, a change of latency) costs its block one frame, as any block may: none catches up on
    // frames before it (NOTES.md, Crossfades). Stereo, an ess all the time, Harsh on; per event the
    // block that holds it, in wall time, the median over the events, against the ordinary blocks.
    printLoadAverage();
    constexpr int block = 64;
    std::printf ("\n| Rate | Event | Event block (us) | %% of the block | Ordinary blocks: mean / p99 / largest (us) |\n|---|---|---|---|---|\n");
    for (const auto rate : { 48000.0, 96000.0, 192000.0 })
    {
        const auto material = materialAt (rate);
        const auto& in = material.essAlways;
        const auto n = (int) in[0].size();

        struct Event
        {
            const char* name;
            std::function<void (int pos, dsp::ChainSettings&, std::int64_t& position, bool& playing)> drive;
        };
        const auto period = samplesFor (0.25, rate);
        const std::vector<Event> events {
            { "Shape: Split to Spectral", [=] (int pos, dsp::ChainSettings& s, std::int64_t&, bool&)
              { s.shape = (pos / period) % 2 == 0 ? Shape::split : Shape::spectral; } },
            { "Stereo in Spectral: Linked to Unlinked", [=] (int pos, dsp::ChainSettings& s, std::int64_t&, bool&)
              {
                  s.shape = Shape::spectral;
                  s.detector.stereo = (pos / period) % 2 == 0 ? StereoMode::linked : StereoMode::unlinked;
              } },
            { "Transport start (Spectral)", [=] (int pos, dsp::ChainSettings& s, std::int64_t&, bool& playing)
              {
                  s.shape = Shape::spectral;
                  playing = (pos / period) % 2 == 1;
              } },
            { "Transport jump (Spectral)", [=] (int pos, dsp::ChainSettings& s, std::int64_t& position, bool&)
              {
                  s.shape = Shape::spectral;
                  if (pos % period < block)
                      position += 480000;
              } },
            { "Latency: Live to Normal (Spectral)", [=] (int pos, dsp::ChainSettings& s, std::int64_t&, bool&)
              {
                  s.shape = Shape::spectral;
                  s.detector.latency = (pos / period) % 2 == 0 ? LatencyMode::live : LatencyMode::normal;
              } },
        };

        for (const auto& event : events)
        {
            const juce::ScopedNoDenormals noDenormals;
            dsp::HaliteChain chain;
            chain.prepare (rate, block, 2, LatencyMode::normal);
            auto buffer = in;
            auto s = settingsFor (Shape::split, StereoMode::linked, true);
            std::int64_t position = 0;
            auto playing = true;
            std::vector<double> marked, ordinary;
            for (int pos = 0; pos + block <= n; pos += block)
            {
                const auto before = s;
                const auto wasPlaying = playing;
                const auto from = position;
                event.drive (pos, s, position, playing);
                const auto latencyBefore = chain.status().latencySamples.load();
                std::array<float*, 2> channels { buffer[0].data() + pos, buffer[1].data() + pos };
                const auto t0 = std::chrono::steady_clock::now();
                chain.process (channels.data(), 2, block, s, nullptr, 0, { position, true, playing });
                const auto us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
                // A change of latency switches once the audio has faded out, 10 ms after it is asked for.
                const auto happens = before.shape != s.shape || before.detector.stereo != s.detector.stereo || (playing && ! wasPlaying)
                                     || position != from || chain.status().latencySamples.load() != latencyBefore;
                position += block;
                if (pos < samplesFor (0.5, rate))
                    continue;
                (happens ? marked : ordinary).push_back (us);
            }
            std::sort (marked.begin(), marked.end());
            std::sort (ordinary.begin(), ordinary.end());
            auto mean = 0.0;
            for (const auto us : ordinary)
                mean += us / (double) ordinary.size();
            const auto median = marked.empty() ? 0.0 : marked[marked.size() / 2];
            std::printf ("| %.1f kHz | %s | %.1f | %.1f | %.1f / %.1f / %.1f |\n", rate / 1000.0, event.name, median, 100.0 * median * 1.0e-6 / (block / rate),
                         mean, ordinary[(size_t) (0.99 * (double) ordinary.size())], ordinary.back());
        }
    }
    printLoadAverage();
}
