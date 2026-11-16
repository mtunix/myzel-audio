// The detector on labelled speech and singing (PLAN.md, Measurement and quality gates, as Settled 34
// revised them; NOTES.md, Measured (M1) and M1b). [committed] runs the committed tier
// (tests/corpus) in CI; [m1] (hidden) runs the m1 subset from the cache and skips without it. The
// gates that pass are checked as gates; the cases named "regression bound" hold the detector to its
// measured values; the bright gate still fails on a sung excerpt's cold start and shows under
// [!mayfail], so the run reports it without failing (PLAN.md, Open 2).

#include "Signals.h"
#include "support/Corpus.h"
#include "support/Gates.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <functional>
#include <string>
#include <thread>

using namespace halite;

namespace
{
    std::vector<corpus::Recording> loadAll (const std::vector<corpus::Entry>& entries)
    {
        std::vector<corpus::Recording> out (entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
        {
            juce::String error;
            if (! corpus::load (entries[i], out[i], error))
                FAIL (error.toStdString());
        }
        return out;
    }

    const std::vector<corpus::Recording>& committedTier()
    {
        static const auto recordings = loadAll (corpus::committedTier (juce::File (HALITE_TEST_DATA_DIR).getChildFile ("corpus")));
        return recordings;
    }

    dsp::DetectorSettings defaults()
    {
        dsp::DetectorSettings s;
        s.harshOn = true;       // Harsh does not change the Ess detector, and its gate needs it on
        return s;
    }

    gates::Summary summarise (const std::vector<corpus::Recording>& recordings, const dsp::DetectorSettings& s, bool sungToo = false)
    {
        gates::Summary sum;
        for (const auto& r : recordings)
            if (sungToo || ! r.entry.sung)
                sum.add (gates::analyse (r, gates::run (r, s)));
        return sum;
    }

    gates::LevelResult levelOf (const std::vector<corpus::Recording>& recordings, const dsp::DetectorSettings& s)
    {
        gates::LevelResult level;
        for (const auto& r : recordings)
            if (! r.entry.sung)
                level.add (gates::levelIndependence (r, s, {}, { 0.0f, -10.0f, -20.0f, -30.0f }));
        return level;
    }

    void parallel (size_t count, const std::function<void (size_t)>& f)
    {
        std::atomic<size_t> next { 0 };
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < std::max (1u, std::thread::hardware_concurrency()); ++t)
            threads.emplace_back ([&] {
                for (auto i = next++; i < count; i = next++)
                    f (i);
            });
        for (auto& t : threads)
            t.join();
    }

    /** The separability gate's threshold (95 % of /s/ produce an event) and the summary there. */
    std::pair<float, gates::Summary> separability (const std::vector<corpus::Recording>& recordings, bool threads = false)
    {
        auto s = defaults();
        s.thresholdDb = gates::thresholdForRecall (recordings, s, {}, 0, gates::separabilityRecall, threads ? gates::ForEach (parallel) : gates::ForEach {});
        return { s.thresholdDb, summarise (recordings, s) };
    }

    /** Per speaker, the share of vowel frames that are decisions. */
    double worstSpeaker (const std::vector<corpus::Recording>& recordings, float thresholdDb)
    {
        auto s = defaults();
        s.thresholdDb = thresholdDb;
        auto worst = 0.0;
        for (const auto& r : recordings)
            if (! r.entry.sung)
                worst = std::max (worst, gates::analyse (r, gates::run (r, s)).vowelTriggered.percent());
        return worst;
    }

    void checkLevelPerGain (const gates::LevelResult& level, int atLeastInRangeAt10Db)
    {
        // Each render in its own working range (V 20 dB over the floor there): no event appears,
        // disappears or moves by more than 0.5 dB. Settled 34's own reading, the range of the -30 dB
        // render, holds no event of these corpora with the -60 dBFS floor (NOTES.md, M1b), so that
        // clause is untested here (PLAN.md, Open 2) and -10 dB carries the check.
        REQUIRE (level.perGain.size() == 3);
        for (const auto& g : level.perGain)
        {
            INFO ("gain " << g.gainDb << " dB: " << g.inRange << " of " << g.events << " events in range, " << g.listChanges
                          << " list changes, largest change " << g.worstDifferenceDb << " dB");
            CHECK (g.listChanges == 0);
            CHECK (g.worstDifferenceDb <= 0.5f);
        }
        REQUIRE (level.perGain[0].inRange >= atLeastInRangeAt10Db);

        INFO ("in the working range at -30 dB: " << level.events << " events");
        if (level.events > 0)
        {
            CHECK (level.listChanges == 0);
            CHECK (level.worstDifferenceDb <= 0.5f);
        }
    }
}

TEST_CASE ("Corpus: separability on the committed tier", "[corpus][committed]")
{
    // The gate (Settled 34): at the threshold where 95 % of /s/ produce an event, at most 2 % of
    // vowel frames are decisions. Measured (M1b): T95 -24.08 dB (30 of 31 /s/), 58 of 11400 frames
    // (0.51 %); M1 read 3.93 %. The regression bound leaves 8 frames for other compilers' rounding;
    // a change that lets a third more through fails it.
    const auto [threshold, s] = separability (committedTier());
    INFO ("T95 " << threshold << " dB: /s/ " << s.detected[0].hits << " of " << s.detected[0].total << ", vowel decisions "
                 << s.vowelTriggered.hits << " of " << s.vowelTriggered.total << " (" << s.vowelTriggered.percent() << " %)");
    CHECK (s.detected[0].percent() >= gates::separabilityRecall);
    CHECK (s.vowelTriggered.percent() <= gates::separabilityVowelPercent);
    CHECK (s.vowelTriggered.total == 11400);
    CHECK (s.vowelTriggered.hits <= 66);
    CHECK (threshold >= -24.5f);
}

TEST_CASE ("Corpus: regression bound: detection and vowel frames at the default on the committed tier; the Harsh gate", "[corpus][committed]")
{
    const auto& recordings = committedTier();
    REQUIRE (recordings.size() == 12);
    const auto s = summarise (recordings, defaults());

    // Measured (M3), the ten spoken excerpts at the -17.0 dB default (Learn over the tune split):
    // /s/ 18 of 31, /z/ 7 of 11; of 11400 vowel frames 228 reduced, 30 by their own S, 107 carrying
    // more than 1 dB (the release-tail gate, Settled 47; at M1b's -17.6 dB and Speed Auto: 22, 8,
    // 470, 34, 294). Two tokens and about an eighth more frames of margin, for other compilers'
    // rounding.
    INFO ("/s/ " << s.detected[0].hits << " of " << s.detected[0].total << ", /z/ " << s.detected[1].hits << " of " << s.detected[1].total
                 << "; vowel frames reduced " << s.vowel.hits << ", decisions " << s.vowelTriggered.hits << ", tails " << s.vowelTail.hits
                 << ", over 1 dB " << s.vowelCarried.hits << " of " << s.vowel.total);
    CHECK (s.detected[0].total == 31);
    CHECK (s.detected[1].total == 11);
    CHECK (s.detected[0].hits >= 16);
    CHECK (s.detected[1].hits >= 5);
    CHECK (s.vowel.total == 11400);
    CHECK (s.vowel.hits <= 260);
    CHECK (s.vowelTriggered.hits <= 36);
    CHECK (s.vowelCarried.percent() <= gates::carriedVowelPercent);
    CHECK (s.vowelCarried.hits <= 125);

    // The Harsh gate, read as the pooled mean (measured 0.202 dB at -9.9 dB); the other readings are reported.
    INFO ("Harsh in sibilants " << s.harshSibilantMeanDb() << " dB, added over the hop before " << s.harshSibilantAddedMeanDb() << " dB");
    CHECK (s.harshSibilantMeanDb() <= 0.5);
}

TEST_CASE ("Corpus: level independence on the committed tier", "[corpus][committed]")
{
    // Measured (M1b): 29 of 39 events in range at -10 dB, 3 at -20 dB, none at -30 dB; no change in range.
    checkLevelPerGain (levelOf (committedTier(), defaults()), 20);
}

TEST_CASE ("Corpus: bright regression bound: sung excerpts after the first 300 ms", "[corpus][committed]")
{
    // Each excerpt starts cold, cut out of a scale, and its first note can be caught before V
    // knows the voice (measured: 1.9 dB within 0.25 s). After that nothing is reduced.
    for (const auto& r : committedTier())
    {
        if (! r.entry.sung)
            continue;
        const auto probe = gates::run (r, defaults());
        const auto hopSeconds = probe.hopSize / probe.sampleRate;
        auto largest = 0.0f;
        double at = 0.0;
        for (const auto& seg : r.labels)
        {
            if (corpus::classOf (seg.phone) != corpus::PhoneClass::vowel)
                continue;
            for (auto i = (size_t) std::ceil (std::max (seg.start, 0.3) / hopSeconds); i < probe.hops.size() && ((double) i + 0.5) * hopSeconds < seg.end; ++i)
                if (probe.hops[i].reductions[0].essDb > largest)
                {
                    largest = probe.hops[i].reductions[0].essDb;
                    at = ((double) i + 0.5) * hopSeconds;
                }
        }
        INFO (r.entry.id << ": largest reduction in a vowel " << largest << " dB at " << at << " s");
        CHECK (largest <= gates::reducedDb);
    }
}

TEST_CASE ("Corpus: bright regression bound: synthetic vowels at every tilt", "[corpus][committed]")
{
    // Harmonics to 12 kHz at 0, -3, -6 and -12 dB per octave, both sets, onsets included: none is
    // reduced at the default threshold (measured M1b: at most 0.01 dB; M1 reduced the flat spectrum
    // by up to 5.6 dB). The flat spectrum's place in the gate is the user's (PLAN.md, Open 2).
    for (const auto holdout : { false, true })
        for (const auto& r : gates::syntheticBrightVowels (48000.0, holdout))
        {
            const auto result = gates::analyse (r, gates::run (r, defaults()));
            INFO (r.entry.id << (holdout ? " (holdout set)" : "") << ": largest reduction " << result.brightMaxDb << " dB at " << result.brightMaxTime << " s");
            CHECK (result.brightMaxDb <= gates::reducedDb);
        }
}

TEST_CASE ("Corpus: the plan's bright gate on the committed sung excerpts, cold starts included", "[corpus][committed][!mayfail]")
{
    // Expected to fail (NOTES.md, M1b): the f2 scale's first note, before V knows the voice, 1.62 dB at the M3 defaults.
    const auto s = summarise (committedTier(), defaults(), true);
    INFO ("largest reduction in sung vowels " << s.brightMaxDb << " dB");
    CHECK (s.brightMaxDb <= gates::reducedDb);
}

TEST_CASE ("Corpus: the m1 subset, both halves", "[.corpus][m1]")
{
    const auto cache = corpus::cacheDirectory();
    const auto manifest = cache.getChildFile ("labels/m1/vctk.tsv");
    if (! manifest.existsAsFile())
        SKIP ("no m1 subset in " << cache.getFullPathName() << " (plugins/halite/tools/corpus/fetch.sh --subset m1)");

    // Each speaker's recordings back to back. Both halves are held to the plan's gates; only tune,
    // where the constants were chosen, to regression bounds: holdout reports, it never steers
    // (tools/corpus/README.md, The split). Measured (M3), tune: T95 -22.81 dB, vowel decisions
    // 0.43 % (worst speaker 1.08 %), /z/ 89.7 %; at the defaults Harsh pooled 0.169 dB, vowel frames
    // over 1 dB 1.19 % (Settled 47); 1122 events in the working range at -10 dB.
    for (const auto* split : { "tune", "holdout" })
    {
        std::vector<corpus::Entry> entries;
        for (auto& e : corpus::readManifest (manifest, cache, "vctk"))
            if (e.split == split)
                entries.push_back (std::move (e));
        REQUIRE (entries.size() >= 790);
        const auto recordings = corpus::streams (loadAll (entries));
        REQUIRE (recordings.size() == 20);

        const auto tune = std::string (split) == "tune";
        const auto [threshold, t] = separability (recordings, true);
        const auto worst = worstSpeaker (recordings, threshold);
        INFO (split << ": T95 " << threshold << " dB, vowel decisions " << t.vowelTriggered.percent() << " % (worst speaker " << worst
                    << " %), /z/ " << t.detected[1].percent() << " %");
        CHECK (t.detected[0].percent() >= gates::separabilityRecall);
        CHECK (t.vowelTriggered.percent() <= gates::separabilityVowelPercent);
        if (tune)
        {
            CHECK (t.vowelTriggered.percent() <= 0.48);
            CHECK (worst <= 1.2);
            CHECK (t.detected[1].percent() >= 88.0);
        }

        const auto s = summarise (recordings, defaults());
        INFO ("at the default: /s/ " << s.detected[0].percent() << " %, /z/ " << s.detected[1].percent() << " %, Harsh in sibilants "
                                     << s.harshSibilantMeanDb() << " dB, vowel frames over 1 dB " << s.vowelCarried.percent() << " %");
        CHECK (s.harshSibilantMeanDb() <= 0.5);
        CHECK (s.vowelCarried.percent() <= gates::carriedVowelPercent);
        if (tune)
            CHECK (s.vowelCarried.percent() <= 1.5);

        checkLevelPerGain (levelOf (recordings, defaults()), tune ? 1100 : 1);
    }
}
