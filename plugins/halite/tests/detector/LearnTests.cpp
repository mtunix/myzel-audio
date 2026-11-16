// Learn (PLAN.md, Learn; NOTES.md, M3: Learn): the histograms, the rule, the ess candidates and
// the capture on the detector's own hops.

#include "Signals.h"

#include "dsp/detector/Learn.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <random>

using namespace halite;
using namespace halite::testing;

namespace
{
    constexpr double rate = 48000.0;
    constexpr double hopSeconds = dsp::Detector::hopSize / rate;

    /** A value in the middle of its 0.1 dB bin, so a level read back is exact to the bin. */
    float centred (float db) { return dsp::LevelHistogram::lowEdgeOf (dsp::LevelHistogram::binOf (db)) + 0.05f; }

    struct Counted
    {
        dsp::LevelHistogram::Counts counts {};
        std::uint32_t total = 0;

        void add (float db)
        {
            ++counts[(size_t) dsp::LevelHistogram::binOf (db)];
            ++total;
        }
    };

    /** A voice: a vowel throughout, with an ess (4.5 … 12 kHz noise) of the given level every 0.6 s, 120 ms long. */
    Samples voiceWithEsses (const std::vector<double>& essRms, double seconds, unsigned seed = 3)
    {
        const auto n = samplesFor (seconds, rate);
        auto x = vowel (150.0, n, rate, 0.1, seed);
        for (size_t k = 0; k < essRms.size(); ++k)
        {
            const auto from = samplesFor (0.6 + 0.6 * (double) k, rate);
            const auto to = std::min (n, from + samplesFor (0.12, rate));
            x = sum (x, gated (bandNoise (4500.0, 12000.0, n, rate, seed + 10 + (unsigned) k, essRms[k]), from, to, rate));
        }
        return x;
    }

    /** Feeds the probe's hops to a Learn, warm for `warmSeconds`, then a capture; returns its result. */
    dsp::LearnResult capture (dsp::Learn& learn, const dsp::probe::Result& r, const dsp::DetectorSettings& s, double warmSeconds = 0.5)
    {
        const auto warm = (size_t) std::lround (warmSeconds / hopSeconds);
        for (size_t i = 0; i < std::min (warm, r.hops.size()); ++i)
            learn.hop (r.hops[i], s);
        learn.request();
        for (auto i = warm; i < r.hops.size() && learn.phase() != dsp::Learn::Phase::captured; ++i)
            learn.hop (r.hops[i], s);

        dsp::LevelHistogram::Counts esses {}, voiced {};
        learn.capturedEsses().read (esses);
        learn.capturedVoiced().read (voiced);
        return dsp::learnThresholds (esses, learn.capturedEsses().total(), voiced, learn.capturedVoiced().total(), learn.capturedHarshOn(),
                                     learn.capturedHopSeconds(), learn.getTuning());
    }

    /** A hop whose only reading is S, at the given level. */
    dsp::DetectorFrame frameAt (float sibilanceDb)
    {
        dsp::DetectorFrame f;
        f.decisions[0].sibilanceDb = sibilanceDb;
        return f;
    }

    /** Feeds `hops` hops of S at `db`. */
    void feed (dsp::Learn& learn, float db, int hops, const dsp::DetectorSettings& s)
    {
        for (int i = 0; i < hops; ++i)
            learn.hop (frameAt (db), s);
    }

    std::vector<float> capturedLevels (const dsp::Learn& learn)
    {
        dsp::LevelHistogram::Counts c {};
        learn.capturedEsses().read (c);
        std::vector<float> levels;
        for (int b = 0; b < dsp::LevelHistogram::numBins; ++b)
            for (std::uint32_t k = 0; k < c[(size_t) b]; ++k)
                levels.push_back (dsp::LevelHistogram::lowEdgeOf (b));
        return levels;
    }
}

TEST_CASE ("Learn: the histogram reads levels to its bins", "[learn]")
{
    Counted h;
    for (const auto db : { -20.0f, -10.0f, 0.0f })
        h.add (centred (db));
    CHECK (dsp::histogramLevelDb (h.counts, h.total, 0.5) == Catch::Approx (centred (-10.0f)).margin (1.0e-4));
    CHECK (dsp::histogramLevelDb (h.counts, h.total, 0.0) == Catch::Approx (centred (-20.0f) - 0.05f).margin (1.0e-4));
    CHECK (dsp::histogramLevelDb (h.counts, h.total, 1.0) == Catch::Approx (centred (0.0f) + 0.05f).margin (1.0e-4));
    CHECK (std::isnan (dsp::histogramLevelDb (Counted{}.counts, 0, 0.5)));

    // Beyond the bins: the edges.
    CHECK (dsp::LevelHistogram::binOf (-300.0f) == 0);
    CHECK (dsp::LevelHistogram::binOf (std::numeric_limits<float>::quiet_NaN()) == 0);
    CHECK (dsp::LevelHistogram::binOf (60.0f) == dsp::LevelHistogram::numBins - 1);

    // The atomic histogram: adds, removes, never below zero, reads back.
    dsp::LevelHistogram live;
    live.add (100);
    live.add (100);
    live.remove (100);
    live.remove (100);
    live.remove (100);
    live.add (7);
    dsp::LevelHistogram::Counts read {};
    live.read (read);
    CHECK (live.total() == 1);
    CHECK (read[100] == 0);
    CHECK (read[7] == 1);
    live.clear();
    CHECK (live.total() == 0);
}

TEST_CASE ("Learn: the rule", "[learn]")
{
    // The constants chosen on the tune split (NOTES.md, M3: Learn; PLAN.md, Settled 54).
    const dsp::LearnTuning t;
    CHECK (juce::exactlyEqual (t.captureSeconds, 8.0));
    CHECK (juce::exactlyEqual (t.liveSeconds, 60.0));
    CHECK (juce::exactlyEqual (t.floorDb, -24.0f));
    CHECK (juce::exactlyEqual (t.floorAbsDb, -60.0f));
    CHECK (juce::exactlyEqual (t.splitAbsDb, 6.0f));
    CHECK (juce::exactlyEqual (t.hangoverMs, 20.0));
    CHECK (juce::exactlyEqual (t.minLengthMs, 25.0));
    CHECK (juce::exactlyEqual (t.medianReductionDb, 4.0f));
    CHECK (juce::exactlyEqual (t.quietShare, 0.25));
    CHECK (t.minEsses == 3);
    CHECK (juce::exactlyEqual (t.harshShare, 0.07));
    CHECK (juce::exactlyEqual (t.minVoicedSeconds, 1.0));
    Counted esses, voiced;

    // Twenty-one esses evenly from -30 to -10 dB: the median less 4 dB is over the quietest quarter.
    for (int k = 0; k <= 20; ++k)
        esses.add (centred (-30.0f + (float) k));
    auto r = dsp::learnThresholds (esses.counts, esses.total, voiced.counts, voiced.total, false, hopSeconds, t);
    REQUIRE (r.essSet);
    CHECK (r.esses == 21);
    CHECK (r.essDb == Catch::Approx (dsp::histogramLevelDb (esses.counts, esses.total, 0.5) - 4.0f).margin (1.0e-4));
    CHECK (r.essDb == Catch::Approx (-24.0f).margin (0.1));
    CHECK_FALSE (r.harshSet);

    // A narrow spread: the quietest quarter binds, so it stays untouched.
    Counted narrow;
    for (int k = 0; k <= 20; ++k)
        narrow.add (centred (-20.0f + 0.1f * (float) k));
    r = dsp::learnThresholds (narrow.counts, narrow.total, voiced.counts, voiced.total, false, hopSeconds, t);
    REQUIRE (r.essSet);
    CHECK (r.essDb == Catch::Approx (dsp::histogramLevelDb (narrow.counts, narrow.total, 0.25)).margin (1.0e-4));

    // Too few esses: the threshold stays.
    Counted few;
    for (int k = 0; k < t.minEsses - 1; ++k)
        few.add (-15.0f);
    r = dsp::learnThresholds (few.counts, few.total, voiced.counts, voiced.total, false, hopSeconds, t);
    CHECK_FALSE (r.essSet);
    CHECK (r.esses == t.minEsses - 1);
    few.add (-15.0f);
    CHECK (dsp::learnThresholds (few.counts, few.total, voiced.counts, voiced.total, false, hopSeconds, t).essSet);

    // Harsh: the loudest share of the voiced frames, with Harsh on and enough of them.
    const auto enough = (int) std::ceil (t.minVoicedSeconds / hopSeconds);
    for (int k = 0; k < enough; ++k)
        voiced.add (-20.0f + 20.0f * (float) k / (float) enough);
    r = dsp::learnThresholds (few.counts, few.total, voiced.counts, voiced.total, true, hopSeconds, t);
    REQUIRE (r.harshSet);
    CHECK (r.harshDb == Catch::Approx (-20.0f + 20.0f * (float) (1.0 - t.harshShare)).margin (0.11));
    CHECK (r.voicedSeconds >= t.minVoicedSeconds);
    CHECK_FALSE (dsp::learnThresholds (few.counts, few.total, voiced.counts, voiced.total, false, hopSeconds, t).harshSet);
    Counted short_;
    for (int k = 0; k < enough / 2; ++k)
        short_.add (-10.0f);
    CHECK_FALSE (dsp::learnThresholds (few.counts, few.total, short_.counts, short_.total, true, hopSeconds, t).harshSet);
}

TEST_CASE ("Learn: ess candidates", "[learn]")
{
    dsp::LearnTuning t;
    dsp::EssCandidates c;
    c.prepare (hopSeconds, t);
    const auto hangover = (int) std::lround (t.hangoverMs * 0.001 / hopSeconds);
    const auto minimum = (int) std::lround (t.minLengthMs * 0.001 / hopSeconds);

    // Runs a level sequence; returns the peaks of the candidates that ended.
    const auto run = [&] (const std::vector<std::pair<float, int>>& levels, float floor, float split) {
        std::vector<float> peaks;
        c.reset();
        for (const auto& [db, hops] : levels)
            for (int i = 0; i < hops; ++i)
                if (auto peak = 0.0f; c.push (db, floor, split, peak))
                    peaks.push_back (peak);
        return peaks;
    };

    // One run with a peak, ended by the hangover.
    auto peaks = run ({ { -40.0f, 50 }, { -20.0f, 40 }, { -12.0f, 1 }, { -20.0f, 40 }, { -40.0f, hangover + 1 } }, -24.0f, 0.0f);
    REQUIRE (peaks.size() == 1);
    CHECK (juce::exactlyEqual (peaks[0], -12.0f));
    CHECK (c.lengthHops() == 81);

    // Not before the hangover is over.
    CHECK (run ({ { -20.0f, 60 }, { -40.0f, hangover } }, -24.0f, 0.0f).empty());

    // A dip shorter than the hangover joins two runs; a longer one splits them.
    CHECK (run ({ { -20.0f, 60 }, { -40.0f, hangover }, { -18.0f, 60 }, { -40.0f, hangover + 1 } }, -24.0f, 0.0f).size() == 1);
    CHECK (run ({ { -20.0f, 60 }, { -40.0f, hangover + 2 }, { -18.0f, 60 }, { -40.0f, hangover + 1 } }, -24.0f, 0.0f).size() == 2);

    // Shorter than the minimum length: a stop's burst, not counted.
    CHECK (run ({ { -20.0f, minimum - 1 }, { -40.0f, hangover + 1 } }, -24.0f, 0.0f).empty());
    CHECK (run ({ { -20.0f, minimum }, { -40.0f, hangover + 1 } }, -24.0f, 0.0f).size() == 1);

    // Allround's split: hits on a level that never falls under the floor, 10 dB apart from their valleys.
    std::vector<std::pair<float, int>> hits;
    for (int k = 0; k < 5; ++k)
    {
        hits.emplace_back (-20.0f, 60);
        hits.emplace_back (-30.0f, hangover + 5);
    }
    CHECK (run (hits, -60.0f, 0.0f).empty());
    CHECK (run (hits, -60.0f, t.splitAbsDb).size() == 5);

    // A capture that stops counts the open run, if long enough.
    c.reset();
    for (int i = 0; i < minimum; ++i)
        if (auto peak = 0.0f; c.push (-10.0f, -24.0f, 0.0f, peak))
            FAIL ("ended early");
    auto open = c;
    auto peak = 0.0f;
    CHECK (open.flush (peak));
    CHECK (juce::exactlyEqual (peak, -10.0f));
}

TEST_CASE ("Learn: a capture learns the plan's rule on its esses", "[learn]")
{
    // Twelve esses at levels spread over 14 dB in 8 s of voice.
    std::vector<double> levels;
    for (int k = 0; k < 12; ++k)
        levels.push_back (0.02 * std::pow (10.0, (double) ((k * 5) % 12 - 6) * 1.2 / 20.0));
    const auto x = voiceWithEsses (levels, 9.0);
    auto s = voiceSettings();
    const auto r = probeOf (x, rate, s);

    // Each ess's largest S over its span, from the probe: what the rule should be read on.
    Counted expected;
    for (size_t k = 0; k < levels.size(); ++k)
    {
        const auto from = hopAt (0.6 + 0.6 * (double) k, rate), to = hopAt (0.6 + 0.6 * (double) k + 0.12, rate);
        auto peak = dsp::silentDb;
        for (auto i = from - 4; i < to + 40; ++i)
            peak = std::max (peak, dsp::Learn::essLevelDb (r.hops[(size_t) i]));
        expected.add (peak);
    }

    dsp::Learn learn;
    learn.prepare (rate);
    const auto result = capture (learn, r, s, 0.3);
    REQUIRE (learn.phase() == dsp::Learn::Phase::captured);
    INFO ("candidates " << result.esses);
    CHECK (result.esses == 12);
    REQUIRE (result.essSet);
    const auto rule = dsp::learnThresholds (expected.counts, expected.total, {}, 0, false, hopSeconds);
    CHECK (result.essDb == Catch::Approx (rule.essDb).margin (1.0e-4));
    CHECK (juce::exactlyEqual (learn.progress(), 1.0f));

    // At the threshold learned, the median ess is cut by the rule's 4 dB, or the quietest quarter binds.
    CHECK (result.essDb >= dsp::histogramLevelDb (expected.counts, expected.total, 0.25) - 1.0e-4f);
}

TEST_CASE ("Learn: what it learns does not depend on the threshold", "[learn]")
{
    const auto x = voiceWithEsses ({ 0.01, 0.03, 0.02, 0.015, 0.04, 0.025, 0.012, 0.03, 0.02, 0.05, 0.018, 0.022 }, 9.0, 5);
    std::vector<dsp::LevelHistogram::Counts> histograms;
    std::vector<float> learned;
    for (const auto threshold : { -36.0f, -17.0f, 10.0f })
    {
        auto s = voiceSettings();
        s.thresholdDb = threshold;
        s.harshOn = true;
        const auto r = probeOf (x, rate, s);
        dsp::Learn learn;
        learn.prepare (rate);
        const auto result = capture (learn, r, s);
        dsp::LevelHistogram::Counts c {};
        learn.capturedEsses().read (c);
        histograms.push_back (c);
        learned.push_back (result.essDb);
        CHECK (result.essSet);
    }
    CHECK (histograms[0] == histograms[1]);
    CHECK (histograms[1] == histograms[2]);
    CHECK (juce::exactlyEqual (learned[0], learned[2]));
}

TEST_CASE ("Learn: silence, a sung vowel and a steady noise set no threshold", "[learn]")
{
    const auto n = samplesFor (9.0, rate);
    auto s = voiceSettings();
    s.harshOn = true;

    SECTION ("silence")
    {
        dsp::Learn learn;
        learn.prepare (rate);
        const auto r = capture (learn, probeOf (Samples ((size_t) n, 0.0f), rate, s), s);
        CHECK (r.esses == 0);
        CHECK_FALSE (r.essSet);
        CHECK_FALSE (r.harshSet);
    }
    SECTION ("a bright sung vowel: no esses; its voiced frames set Harsh")
    {
        const auto x = harmonic ({ .f0 = 330.0, .topHz = 12000.0, .tiltDbPerOctave = -6.0, .vibratoCents = 30.0, .rms = 0.1 }, n, rate);
        dsp::Learn learn;
        learn.prepare (rate);
        const auto r = capture (learn, probeOf (x, rate, s), s);
        CHECK (r.esses == 0);
        CHECK_FALSE (r.essSet);
        CHECK (r.harshSet);
        CHECK (r.voicedSeconds > 6.0);
    }
    SECTION ("steady band noise: one long run, too few")
    {
        const auto x = sum (vowel (150.0, n, rate, 0.1), bandNoise (4500.0, 12000.0, n, rate, 9u, 0.05));
        dsp::Learn learn;
        learn.prepare (rate);
        const auto r = capture (learn, probeOf (x, rate, s), s);
        CHECK (r.esses < dsp::LearnTuning{}.minEsses);
        CHECK_FALSE (r.essSet);
    }
}

TEST_CASE ("Learn: Harsh Threshold from the voiced frames", "[learn]")
{
    const auto n = samplesFor (9.0, rate);
    // A vowel with a harsh resonance at 3.2 kHz, and a few esses.
    auto x = harmonic ({ .f0 = 180.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -9.0, .peakHz = 3200.0, .peakDb = 12.0, .rms = 0.1 }, n, rate);
    for (int k = 0; k < 6; ++k)
    {
        const auto from = samplesFor (1.0 + 1.2 * k, rate);
        x = sum (x, gated (bandNoise (4500.0, 12000.0, n, rate, 40u + (unsigned) k, 0.03), from, from + samplesFor (0.12, rate), rate));
    }
    auto s = voiceSettings();
    s.harshOn = true;
    const auto r = probeOf (x, rate, s);

    dsp::Learn learn;
    learn.prepare (rate);
    const auto result = capture (learn, r, s);
    REQUIRE (result.harshSet);

    // The same frames by the definition, over the capture's span: the rule reads their loudest share.
    Counted voiced;
    const auto from = (size_t) std::lround (0.5 / hopSeconds);
    const auto to = from + (size_t) std::lround (8.0 / hopSeconds);
    for (auto i = from; i < to; ++i)
        if (const auto h = dsp::Learn::voicedHarshDb (r.hops[i], s, {}); std::isfinite (h))
            voiced.add (h);
    CHECK (result.voicedSeconds == Catch::Approx ((double) voiced.total * hopSeconds).margin (2.0 * hopSeconds));
    CHECK (result.harshDb == Catch::Approx (dsp::histogramLevelDb (voiced.counts, voiced.total, 1.0 - dsp::LearnTuning{}.harshShare)).margin (0.11));

    // Esses are not voiced frames; Harsh off, no Harsh Threshold.
    for (size_t i = 0; i < r.hops.size(); ++i)
        if (dsp::Learn::essLevelDb (r.hops[i]) > dsp::LearnTuning{}.floorDb)
            CHECK_FALSE (std::isfinite (dsp::Learn::voicedHarshDb (r.hops[i], s, {})));
    // A frame both cues call harmonic but whose S is over the ess floor (a voiced /z/) is not voiced.
    dsp::DetectorFrame f;
    f.paths[0].noise = f.paths[0].harshNoise = 0.0f;
    f.paths[0].harshDb = -30.0f;
    f.paths[0].harshRelativeDb = -12.0f;
    f.decisions[0].sibilanceDb = dsp::LearnTuning{}.floorDb - 1.0f;
    CHECK (juce::exactlyEqual (dsp::Learn::voicedHarshDb (f, s, {}), -12.0f));
    f.decisions[0].sibilanceDb = dsp::LearnTuning{}.floorDb + 1.0f;
    CHECK_FALSE (std::isfinite (dsp::Learn::voicedHarshDb (f, s, {})));
    f.paths[0].bodyFloored = true;
    f.decisions[0].sibilanceDb = dsp::LearnTuning{}.floorDb - 1.0f;
    CHECK_FALSE (std::isfinite (dsp::Learn::voicedHarshDb (f, s, {})));

    s.harshOn = false;
    dsp::Learn off;
    off.prepare (rate);
    CHECK_FALSE (capture (off, probeOf (x, rate, s), s).harshSet);
}

TEST_CASE ("Learn: the live histogram keeps the last window", "[learn]")
{
    // A 2 s window instead of 60, esses every 0.6 s: about three in it, then none after silence.
    dsp::LearnTuning t;
    t.liveSeconds = 2.0;
    std::vector<double> levels (12, 0.03);
    auto x = voiceWithEsses (levels, 8.0, 7);
    x.resize ((size_t) samplesFor (12.0, rate), 0.0f);
    const auto s = voiceSettings();
    const auto r = probeOf (x, rate, s);

    dsp::Learn learn;
    learn.prepare (rate, t);
    std::uint32_t most = 0;
    for (size_t i = 0; i < r.hops.size(); ++i)
    {
        learn.hop (r.hops[i], s);
        if ((double) i * hopSeconds > 4.0 && (double) i * hopSeconds < 7.0)
        {
            CHECK (learn.live().total() >= 2);
            CHECK (learn.live().total() <= 4);
        }
        most = std::max (most, learn.live().total());
    }
    CHECK (most <= 4);
    CHECK (learn.live().total() == 0);
    CHECK (learn.phase() == dsp::Learn::Phase::idle);

    // A change of Detection empties it.
    learn.prepare (rate, t);
    for (size_t i = 0; i < (size_t) std::lround (5.0 / hopSeconds); ++i)
        learn.hop (r.hops[i], s);
    REQUIRE (learn.live().total() > 0);
    auto allround = s;
    allround.detection = Detection::allround;
    learn.hop (r.hops[0], allround);
    CHECK (learn.live().total() == 0);
}

TEST_CASE ("Learn: a capture starts again on a new request or a change of Detection, and not on a reset", "[learn]")
{
    const auto x = voiceWithEsses (std::vector<double> (28, 0.03), 18.0, 11);
    auto s = voiceSettings();
    const auto r = probeOf (x, rate, s);
    const auto half = (size_t) std::lround (4.0 / hopSeconds);

    dsp::Learn learn;
    learn.prepare (rate);
    const auto first = learn.request();
    for (size_t i = 0; i < half; ++i)
        learn.hop (r.hops[i], s);
    CHECK (learn.phase() == dsp::Learn::Phase::capturing);
    CHECK (learn.captureId() == first);
    CHECK (learn.progress() == Catch::Approx (0.5).margin (0.01));

    // A host's reset (a transport start, a loop) does not restart it.
    learn.reset();
    learn.hop (r.hops[half], s);
    CHECK (learn.progress() > 0.5f);

    // A second request starts over.
    const auto second = learn.request();
    learn.hop (r.hops[half + 1], s);
    CHECK (learn.captureId() == second);
    CHECK (learn.progress() < 0.01f);

    // So does another Detection, its levels in another unit.
    for (size_t i = half + 2; i < 2 * half; ++i)
        learn.hop (r.hops[i], s);
    auto allround = s;
    allround.detection = Detection::allround;
    learn.hop (r.hops[2 * half], allround);
    CHECK (learn.progress() < 0.01f);
    CHECK (learn.capturedDetection() == Detection::allround);

    for (auto i = 2 * half + 1; i < r.hops.size() && learn.phase() != dsp::Learn::Phase::captured; ++i)
        learn.hop (r.hops[i], allround);
    CHECK (learn.phase() == dsp::Learn::Phase::captured);
    CHECK (learn.captureId() == second);
}

TEST_CASE ("Learn: a capture counts the esses that begin inside it", "[learn]")
{
    // A 1 s capture, so the case stays short; the rule is the same at 8 s.
    dsp::LearnTuning t;
    t.captureSeconds = 1.0;
    const auto s = voiceSettings();
    const auto hangover = (int) std::lround (t.hangoverMs * 0.001 / hopSeconds);
    const auto ess = (int) std::lround (0.06 / hopSeconds), gap = (int) std::lround (0.1 / hopSeconds);
    const auto captureHops = (int) std::lround (t.captureSeconds / hopSeconds);

    dsp::Learn learn;
    learn.prepare (rate, t);
    feed (learn, -40.0f, gap, s);

    // A run at 0 dB for 20 ms, then Learn, then the same run 20 dB lower: its peak came before the press.
    feed (learn, 0.0f, (int) std::lround (0.02 / hopSeconds), s);
    const auto first = learn.request();
    feed (learn, -20.0f, ess, s);
    feed (learn, -40.0f, gap, s);
    auto fed = ess + gap;

    // Three esses inside the capture, then one still open when it stops.
    for (const auto db : { -10.0f, -12.0f, -14.0f })
    {
        feed (learn, db, ess, s);
        feed (learn, -40.0f, gap, s);
        fed += ess + gap;
    }
    REQUIRE (fed + ess < captureHops);
    feed (learn, -40.0f, captureHops - fed - ess, s);
    feed (learn, -5.0f, ess, s);
    REQUIRE (learn.phase() == dsp::Learn::Phase::captured);
    REQUIRE (learn.captureId() == first);

    const auto one = capturedLevels (learn);
    INFO ("capture 1 holds " << one.size());
    REQUIRE (one.size() == 4);
    CHECK (one.front() == Catch::Approx (-14.0f).margin (0.11));
    CHECK (one.back() == Catch::Approx (-5.0f).margin (0.11));

    // Pressed again at once: the run counted when the first capture stopped is not counted again.
    learn.request();
    feed (learn, -5.0f, ess, s);
    feed (learn, -40.0f, hangover + gap, s);
    feed (learn, -8.0f, ess, s);
    feed (learn, -40.0f, captureHops, s);
    REQUIRE (learn.phase() == dsp::Learn::Phase::captured);
    const auto two = capturedLevels (learn);
    REQUIRE (two.size() == 1);
    CHECK (two.front() == Catch::Approx (-8.0f).margin (0.11));

    // The live histogram has every run once: the straddling one, the three, the one across both captures, the last.
    CHECK (learn.live().total() == 6);
}

TEST_CASE ("Learn: a host's prepare keeps a capture at the same rate and restarts a running one at a new rate", "[learn]")
{
    dsp::LearnTuning t;
    t.captureSeconds = 1.0;
    const auto s = voiceSettings();
    const auto half = (int) std::lround (0.5 / hopSeconds);
    const auto ess = (int) std::lround (0.06 / hopSeconds), gap = (int) std::lround (0.1 / hopSeconds);
    const auto esses = [&] (dsp::Learn& learn, int n) {
        for (int k = 0; k < n; ++k)
        {
            feed (learn, -10.0f - (float) k, ess, s);
            feed (learn, -40.0f, gap, s);
        }
    };

    dsp::Learn learn;
    learn.prepare (rate, t);
    const auto first = learn.request();
    esses (learn, 3);
    const auto progress = learn.progress();
    REQUIRE (learn.phase() == dsp::Learn::Phase::capturing);

    // A new block size or latency: the capture runs on where it was.
    learn.prepare (rate, t);
    CHECK (learn.phase() == dsp::Learn::Phase::capturing);
    CHECK (learn.captureId() == first);
    CHECK (juce::exactlyEqual (learn.progress(), progress));
    feed (learn, -40.0f, 2 * half, s);
    REQUIRE (learn.phase() == dsp::Learn::Phase::captured);
    const auto levels = capturedLevels (learn);
    CHECK (levels.size() == 3);

    // Finished but not yet read: prepares at the same rate and at a new one leave it as it was.
    learn.prepare (rate, t);
    learn.prepare (2.0 * rate, t);
    CHECK (learn.phase() == dsp::Learn::Phase::captured);
    CHECK (learn.captureId() == first);
    CHECK (capturedLevels (learn) == levels);
    CHECK (juce::exactlyEqual (learn.capturedHopSeconds(), hopSeconds));
    CHECK (juce::exactlyEqual (learn.hopSeconds(), 0.5 * hopSeconds));

    // A new rate during a capture: it starts again on the next hop, for the same request.
    const auto second = learn.request();
    esses (learn, 2);
    REQUIRE (learn.captureId() == second);
    learn.prepare (rate, t);
    CHECK (learn.phase() == dsp::Learn::Phase::idle);
    CHECK (learn.captureId() != second);
    CHECK (learn.live().total() == 0);
    learn.hop (frameAt (-40.0f), s);
    CHECK (learn.captureId() == second);
    CHECK (learn.phase() == dsp::Learn::Phase::capturing);
    CHECK (learn.progress() < 0.01f);
    esses (learn, 4);
    feed (learn, -40.0f, 2 * half, s);
    REQUIRE (learn.phase() == dsp::Learn::Phase::captured);
    CHECK (capturedLevels (learn).size() == 4);
    CHECK (juce::exactlyEqual (learn.capturedHopSeconds(), hopSeconds));
}
