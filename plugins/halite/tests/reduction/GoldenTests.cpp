// Golden-file regression tests (PLAN.md, gates: golden renders per shape × detection × band from
// the committed excerpts): two committed VCTK excerpts back to back (p251 251, then p225 033) through
// the chain, mono at 48 kHz, in every Shape, Detection and band (Ess only, Ess and Harsh), at fixed
// settings. Each file in tests/golden/<shape>-<detection>-<band>.f32 holds two half seconds of the
// output (float32): 2.0 s of the input, where the Harsh band acts on the first speaker and Spectral's
// target is still warming up, and 5.0 s, the second speaker's strongest esses. The whole render runs
// every time, so its state reaches the windows. A refactor that changes the sound fails here.
// Recorded once, for the sound M2 intends; after an intended sound change only, re-record (and say
// so in the commit):
//
//     HALITE_UPDATE_GOLDEN=1 build/plugins/halite/tests/HaliteTests_reduction "[golden]"

#include "ChainRun.h"
#include "support/Corpus.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace halite;
using namespace halite::testing;

namespace
{
    constexpr double rate = 48000.0;
    constexpr std::array excerpts { "vctk-p251-251", "vctk-p225-033" };
    constexpr std::array windows { 2.0, 5.0 };      // s of the input, each windowSeconds long; the second excerpt starts at 3.02 s
    constexpr double windowSeconds = 0.5;

    struct Golden
    {
        Shape shape;
        Detection detection;
        bool harsh;

        std::string name() const
        {
            return std::string (shape == Shape::split ? "split" : (shape == Shape::spectral ? "spectral" : "wide")) + "-"
                   + (detection == Detection::voice ? "voice" : "allround") + "-" + (harsh ? "ess-harsh" : "ess");
        }
    };

    std::vector<Golden> goldens()
    {
        std::vector<Golden> g;
        for (const auto shape : { Shape::split, Shape::spectral, Shape::wide })
            for (const auto detection : { Detection::voice, Detection::allround })
                for (const auto harsh : { false, true })
                    g.push_back ({ shape, detection, harsh });
        return g;
    }

    /** Fixed values, not the defaults: M3 moves the defaults, which must not move these files. */
    dsp::ChainSettings settingsFor (const Golden& g)
    {
        dsp::ChainSettings s;
        s.shape = g.shape;
        auto& d = s.detector;
        d.detection = g.detection;
        d.thresholdDb = -17.6f;
        d.thresholdAbsDb = -36.0f;
        d.rangeDb = 8.0f;
        d.detectLowHz = 4500.0f;
        d.detectHighHz = 12000.0f;
        d.focus = 0.7f;
        d.speedAuto = true;
        d.lookaheadMs = 5.0f;
        d.harshOn = g.harsh;
        d.harshThresholdDb = -10.0f;
        d.harshThresholdAbsDb = -30.0f;
        d.harshRangeDb = 4.0f;
        d.harshFreqHz = 3200.0f;
        d.harshWidthOct = 1.0f;
        d.harshSpeedAuto = true;
        return s;
    }

    Samples committedInput()
    {
        const auto entries = corpus::committedTier (juce::File (HALITE_TEST_DATA_DIR).getChildFile ("corpus"));
        Samples x;
        for (const auto* id : excerpts)
        {
            const auto entry = std::find_if (entries.begin(), entries.end(), [id] (const corpus::Entry& e) { return e.id == id; });
            REQUIRE (entry != entries.end());
            corpus::Recording r;
            juce::String error;
            REQUIRE (corpus::load (*entry, r, error));
            REQUIRE (juce::exactlyEqual (r.sampleRate, rate));
            x.insert (x.end(), r.audio.begin(), r.audio.end());
        }
        return x;
    }

    std::string goldenPath (const Golden& g)
    {
        return std::string (HALITE_TEST_DATA_DIR) + "/golden/" + g.name() + ".f32";
    }

    struct Render
    {
        Samples out, alignedIn;     // the windows of the output, and of the input delayed by the latency
    };

    Render render (const Golden& g, const Samples& in)
    {
        ChainRun run;
        run.rate = rate;
        run.settings = settingsFor (g);
        const auto out = runChain ({ in }, run)[0];
        const auto latency = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
        Render r;
        for (const auto from : windows)
        {
            const auto start = samplesFor (from, rate), n = samplesFor (windowSeconds, rate);
            REQUIRE (start + latency + n <= (int) out.size());
            r.out.insert (r.out.end(), out.begin() + start + latency, out.begin() + start + latency + n);
            r.alignedIn.insert (r.alignedIn.end(), in.begin() + start, in.begin() + start + n);
        }
        return r;
    }
}

TEST_CASE ("Golden: every shape, detection and band matches its recorded render of the committed excerpts", "[golden]")
{
    const auto update = std::getenv ("HALITE_UPDATE_GOLDEN") != nullptr;
    const auto in = committedInput();

    for (const auto& g : goldens())
    {
        INFO (g.name());
        const auto r = render (g, in);

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
            FAIL_CHECK ("no golden file for " << g.name() << " (" << path << ")");
            continue;
        }

        Samples golden (r.out.size());
        file.read (reinterpret_cast<char*> (golden.data()), (std::streamsize) (golden.size() * sizeof (float)));
        REQUIRE ((size_t) file.gcount() == golden.size() * sizeof (float));

        // The error against what the golden render removes, not against the output: the removed
        // part is 20–40 dB under the voice, and it is what a change of the sound changes.
        double errorSq = 0.0, removedSq = 0.0;
        for (size_t i = 0; i < golden.size(); ++i)
        {
            const auto e = (double) r.out[i] - golden[i];
            const auto removed = (double) r.alignedIn[i] - golden[i];
            errorSq += e * e;
            removedSq += removed * removed;
        }
        REQUIRE (removedSq > 0.0);
        const auto errorDb = 10.0 * std::log10 ((errorSq + 1.0e-30) / removedSq);
        INFO ("difference " << errorDb << " dB relative to what the golden render removes");
        CHECK (errorDb < -60.0);
    }
}

TEST_CASE ("Golden: what each render removes, report", "[.report]")
{
    // For choosing the window and the settings: what each golden configuration removes, per 0.5 s.
    const auto in = committedInput();
    std::printf ("\nGolden renders: removed RMS per 0.5 s of input (dBFS), %.2f s in all; the windows start at %.1f and %.1f s\n", in.size() / rate,
                 windows[0], windows[1]);
    for (const auto& g : goldens())
    {
        ChainRun run;
        run.rate = rate;
        run.settings = settingsFor (g);
        const auto out = runChain ({ in }, run)[0];
        const auto latency = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
        std::printf ("  %-26s", g.name().c_str());
        auto changed = 0;
        for (size_t i = (size_t) latency; i < out.size(); ++i)
            changed += sameBits (out[i], in[i - (size_t) latency]) ? 0 : 1;
        for (int from = 0; from + samplesFor (0.5, rate) <= (int) in.size() - latency; from += samplesFor (0.5, rate))
        {
            auto sq = 0.0;
            for (int i = from; i < from + samplesFor (0.5, rate); ++i)
            {
                const auto removed = (double) in[(size_t) i] - out[(size_t) (i + latency)];
                sq += removed * removed;
            }
            std::printf (" %6.1f", 10.0 * std::log10 (sq / samplesFor (0.5, rate) + 1.0e-30));
        }
        std::printf ("  (%.0f %% of samples changed)\n", 100.0 * changed / (double) (out.size() - (size_t) latency));
    }
}
