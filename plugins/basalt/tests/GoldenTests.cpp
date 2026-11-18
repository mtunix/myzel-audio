// Golden-file regression tests (PLAN.md, gates: per-model renders, re-recorded only for an intended
// sound change): committed corpus excerpts through the chain, each model at a setting its NOTES
// name for its goldens (models/<m>/NOTES.md, Gate table), VCA in both detectors, Upward with its
// Floor. Each file in tests/golden/<name>.f32 holds 0.75 s of the left output channel (float32)
// from 3.5 s of the input, at the excerpt's own rate; the whole excerpt runs every time, so the
// models' memories reach the window. A refactor that changes the sound fails here. Recorded for
// the sound M2 intends; after an intended sound change only, re-record (and say so in the commit):
//
//     BASALT_UPDATE_GOLDEN=1 build-dev/plugins/basalt/tests/BasaltTests "[golden]"

#include "dsp/chain/BasaltChain.h"
#include "support/Corpus.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>

using namespace basalt;
using namespace basalt::measure;

namespace
{
    constexpr double windowFrom = 3.5, windowSeconds = 0.75;
    constexpr int block = 512;

    struct Golden
    {
        const char* name;
        const char* excerpt;
        dsp::ChainSettings settings;
    };

    /** Fixed values, not the defaults: M3 voices the defaults, which must not move these files. */
    dsp::ChainSettings make (Model model, double thresholdDb, double ratio, double kneeDb, double attackMs, double releaseMs, bool autoRelease)
    {
        dsp::ChainSettings s;
        s.model = model;
        s.thresholdDb = thresholdDb;
        s.inverseRatio = std::isinf (ratio) ? 0.0 : 1.0 / ratio;
        s.kneeDb = kneeDb;
        s.attackMs = attackMs;
        s.releaseMs = releaseMs;
        s.autoRelease = autoRelease;
        return s;
    }

    std::vector<Golden> goldens()
    {
        const auto inf = std::numeric_limits<double>::infinity();
        std::vector<Golden> g {
            { "clean-vocal", "albumdb-extraordinary-vocal", make (Model::clean, -24.0, 4.0, 6.0, 10.0, 150.0, true) },
            { "vca-peak-mix", "albumdb-diva-mix", make (Model::vca, -16.0, 4.0, 0.0, 10.0, 150.0, true) },
            { "vca-rms-overheads", "albumdb-extraordinary-overheads", make (Model::vca, -42.0, 4.0, 6.0, 15.79, 80.0, false) },
            { "fet-snare", "albumdb-extraordinary-snare", make (Model::fet, -30.0, 4.0, 0.0, 0.8, 50.0, false) },
            { "fet-all-overheads", "albumdb-extraordinary-overheads", make (Model::fet, -30.0, inf, 0.0, 0.8, 50.0, false) },
            { "fet-vocal", "albumdb-extraordinary-vocal", make (Model::fet, -24.0, 20.0, 0.0, 0.2, 300.0, false) },
            { "opto-vocal", "albumdb-extraordinary-vocal", make (Model::opto, -24.0, 4.0, 6.0, 10.0, 150.0, true) },
            { "opto-bass", "albumdb-extraordinary-bass", make (Model::opto, -30.0, 4.0, 6.0, 10.0, 60.0, true) },
            { "opto-mix", "albumdb-it-aint-easy-mix", make (Model::opto, -24.0, 2.0, 6.0, 10.0, 150.0, true) },
            { "varimu-mix", "albumdb-it-aint-easy-mix", make (Model::variMu, -24.0, 4.0, 6.0, 50.0, 2000.0, true) },
            { "varimu-overheads", "albumdb-extraordinary-overheads", make (Model::variMu, -30.0, 4.0, 6.0, 0.4, 300.0, false) },
            { "upward-speech", "vctk-p228-009", make (Model::upward, -20.0, 3.0, 6.0, 10.0, 150.0, true) },
            { "upward-piano", "albumdb-karma-piano", make (Model::upward, -20.0, 3.0, 6.0, 10.0, 150.0, true) },
        };
        g[2].settings.vcaDetector = VcaDetector::rms;
        for (auto i : { 11, 12 })
        {
            g[(size_t) i].settings.floorDb = -70.0;
            g[(size_t) i].settings.lookaheadMs = 5.0;
        }
        return g;
    }

    struct Render
    {
        Samples out, alignedIn;     // the window of the left output, and of the left input
    };

    Render render (const Golden& g)
    {
        const auto e = loadExcerpt (defaultCorpusDir(), g.excerpt);
        REQUIRE (e.has_value());
        auto audio = e->audio;
        if (audio.size() == 1)
            audio.push_back (audio[0]);
        const auto in = audio;

        dsp::BasaltChain chain;
        chain.prepare (e->rate, block, 2, LatencyMode::normal);
        const auto n = (int) audio[0].size();
        for (int pos = 0; pos < n; pos += block)
        {
            std::array<float*, 2> ch { audio[0].data() + pos, audio[1].data() + pos };
            chain.process (ch.data(), 2, std::min (block, n - pos), g.settings);
        }

        const auto latency = dsp::BasaltChain::latencySamples (LatencyMode::normal, e->rate);
        const auto start = samplesFor (windowFrom, e->rate), length = samplesFor (windowSeconds, e->rate);
        REQUIRE (start + latency + length <= n);
        Render r;
        r.out.assign (audio[0].begin() + start + latency, audio[0].begin() + start + latency + length);
        r.alignedIn.assign (in[0].begin() + start, in[0].begin() + start + length);
        return r;
    }

    std::string goldenPath (const Golden& g)
    {
        return std::string (BASALT_TEST_DATA_DIR) + "/golden/" + g.name + ".f32";
    }
}

TEST_CASE ("Golden: every model's render of the committed excerpts matches its recording", "[golden]")
{
    const auto update = std::getenv ("BASALT_UPDATE_GOLDEN") != nullptr;

    for (const auto& g : goldens())
    {
        INFO (g.name);
        const auto r = render (g);
        const auto path = goldenPath (g);
        if (update)
        {
            std::ofstream file (path, std::ios::binary);
            file.write (reinterpret_cast<const char*> (r.out.data()), (std::streamsize) (r.out.size() * sizeof (float)));
            continue;
        }

        // A missing file fails: a checkout without tests/golden must not pass with nothing compared.
        std::ifstream file (path, std::ios::binary);
        if (! file)
        {
            FAIL_CHECK ("no golden file for " << g.name << " (" << path << ")");
            continue;
        }
        Samples golden (r.out.size());
        file.read (reinterpret_cast<char*> (golden.data()), (std::streamsize) (golden.size() * sizeof (float)));
        REQUIRE ((size_t) file.gcount() == golden.size() * sizeof (float));

        // The error against what the golden render changes (taken out, or for Upward added): the
        // change sits 10-30 dB under the music, and it is what a change of the sound moves. Float
        // noise between compilers reads under −100 dB; a real change of a model, above −60.
        double errorSq = 0.0, changedSq = 0.0;
        for (size_t i = 0; i < golden.size(); ++i)
        {
            const auto e = (double) r.out[i] - golden[i];
            const auto changed = (double) r.alignedIn[i] - golden[i];
            errorSq += e * e;
            changedSq += changed * changed;
        }
        REQUIRE (changedSq > 0.0);
        const auto errorDb = 10.0 * std::log10 ((errorSq + 1.0e-30) / changedSq);
        INFO ("difference " << errorDb << " dB relative to what the golden render changes");
        CHECK (errorDb < -60.0);
    }
}

TEST_CASE ("Golden: what each render changes, report", "[.report]")
{
    // For choosing the settings: the RMS of what each golden setting changes over its window, against the input's.
    for (const auto& g : goldens())
    {
        const auto r = render (g);
        double changedSq = 0.0, inSq = 0.0;
        for (size_t i = 0; i < r.out.size(); ++i)
        {
            changedSq += std::pow ((double) r.alignedIn[i] - r.out[i], 2.0);
            inSq += (double) r.alignedIn[i] * r.alignedIn[i];
        }
        std::printf ("  %-20s input %6.1f dBFS RMS, changed %6.1f dBFS RMS (%5.1f dB under the input)\n", g.name,
                     10.0 * std::log10 (inSq / (double) r.out.size() + 1.0e-30), 10.0 * std::log10 (changedSq / (double) r.out.size() + 1.0e-30),
                     10.0 * std::log10 ((inSq + 1.0e-30) / (changedSq + 1.0e-30)));
    }
}
