// HaliteMeasure: runs the plugin's own detector (dsp::probe) over labelled speech and singing and
// reports the detection gates of PLAN.md (Measurement and quality gates); sweeps the detector's
// settings and constants for the corpus measurement of NOTES.md, Measured (M1) and (M1b).
//
//     HaliteMeasure                              the gates on the m1 tune split (VCTK, VocalSet, synthetic)
//     HaliteMeasure --split holdout --level      the held-out split, with level independence
//     HaliteMeasure --data committed --level     the committed tier (tests/corpus)
//     HaliteMeasure --vary threshold=-12,-9,-6 --vary cue=periodicity,flatness
//                                                one line per combination (JSON lines with --json)
//     HaliteMeasure --reduction --data committed the reduction gates through the chain, every shape (Reduction.cpp)
//     HaliteMeasure --help                       every option and key

#include "LearnMeasure.h"
#include "Reduction.h"

#include "support/Corpus.h"
#include "support/Gates.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdio>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>

using namespace halite;

namespace
{
    constexpr auto usage = R"(HaliteMeasure [options]

Data
  --data LIST        vctk, vocalset, librispeech, committed, synthetic, cymbals (default vctk,vocalset,synthetic)
  --split NAME       tune (default), holdout or all; the committed tier has none, synthetic has a set for each
  --subset NAME      the cache's subset manifests, labels/NAME/ (default m1)
  --corpus DIR       the cache (default $HALITE_CORPUS_DIR or ~/.cache/myzel/halite-corpus)
  --committed DIR    the committed tier (default plugins/halite/tests/corpus)
  --limit N          the first N recordings of each corpus
  --stream           each speaker's recordings back to back (one cold start per speaker, not per file)

Detector
  --set KEY=VALUE    a setting or constant (repeatable); keys below
  --vary KEY=A,B,..  one run per combination of every --vary (repeatable)

Report
  --level            add level independence (renders at 0, -10, -20, -30 dB)
  --level-body DB    level independence from a common start: each recording first offset so its
                     median V over vowel frames sits at DB dBFS
  --level-gains LIST the gains (dB) level independence compares with the first (default 0,-10,-20,-30)
  --no-separability  skip the separability gate (it searches each speech corpus's threshold for 95 % of /s/)
  --speakers         a table per speaker
  --histograms       S of sibilants and vowel frames, H - body of vowel frames
  --failures N       the N first missed /s/ /z/ and the recordings with most reduced vowel frames
  --json FILE        one JSON object per run (with --vary, per combination)
  --out FILE         the Markdown report to a file instead of stdout
  --jobs N           threads (default: every core)
  --ripple           B's ripple on band noise and its rise on a tone, at 44.1, 48 and 96 kHz (no corpus)
  --dump ID          every hop of one recording as TSV (levels, cues, S, target, reduction, label; flags:
                     c candidate, h V held, r V rising, j body jumped, o rise hold, J jump hold, f floored)

Learn (the plugin's Learn over the detector)
  --learn            8 s captures at every step of each recording (use --stream), against Learn over the whole
                     recording and the plan's rule on the labels; with Harsh on, Harsh Threshold too. --set and --vary
                     take the Learn constants below as well (--vary: Learn constants only)
  --step S           seconds between capture starts (default 2)
  --normalise DB     scale each recording first to DB dBFS RMS over its labelled speech (the Allround scale)
  --thresholds-out F per speaker: what Learn sets over the whole recording, and the rule on the labels (TSV)
  --thresholds F     run each speaker at its threshold and Harsh Threshold from such a file (the detection gates
                     with --no-separability, and --reduction)

Reduction (the chain, not the detection gates)
  --reduction        every ess through each shape: what the band lost against what the detector asked, where the
                     ess lands against the threshold, the third octaves outside the band, the same for the Harsh
                     band, and Listen Removed's null test; one recording (or with --stream one speaker) in memory per
                     job. The chain runs the plugin's own constants: settings only, no --vary. Synthetic data is skipped
  --shapes LIST      split, spectral, wide (default all three)
  --worst N          the N worst readings of each kind, with where they are

The report sets harshOn=1 unless --set harshOn=0 (Harsh does not change the Ess detector).

Settings: gain (dB, the input before the detector), threshold, thresholdAbs, range (dB); focus (%); detectLow, detectHigh, harshFreq (Hz);
  harshThreshold, harshThresholdAbs, harshRange (dB); harshWidth (oct); lookahead, release, harshRelease (ms);
  speedAuto, harshSpeedAuto, harshOn (0/1); detection (voice, allround); latency (normal, live);
  cue (periodicity, flatness, blend)
Constants (DetectorTuning): bandAveragingMs, bandReleaseMs, bodyReleaseMs, bodyCapHz, bodyCapShare,
  bodyHoldMs, bodyGapDb, bodyGapHoldMs, bodyFloorDb, candidateDb, riseDb, jumpDb, onsetHoldMs, jumpHoldDb, noiseRangeDb,
  presenceKneeDb, presenceSlope, presenceRangeDb, periodicNoise, periodicHarmonic, flatNoiseDb, flatHarmonicDb, periodicLongestLagMs, persistMs, persistRho, persistJumpDb,
  harshNoiseHold, essAutoShare, essAutoMinMs, essAutoMaxMs, harshAutoShare, harshAutoMinMs, harshAutoMaxMs
Learn constants (LearnTuning, with --learn): captureSeconds, floorDb, floorAbsDb, splitAbsDb, hangoverMs, minLengthMs, medianReductionDb,
  quietShare, minEsses, harshShare, minVoicedSeconds, voicedNoise
)";

    using Config = std::vector<std::pair<std::string, std::string>>;

    struct Detector
    {
        dsp::DetectorSettings settings;
        dsp::DetectorTuning tuning;
        float gainDb = 0.0f;        // input gain before the detector
    };

    bool apply (Detector& d, const std::string& key, const std::string& value)
    {
        const auto v = juce::String (value).getDoubleValue();
        const auto f = (float) v;
        auto& s = d.settings;
        auto& t = d.tuning;

        const std::map<std::string, std::function<void()>> keys {
            { "gain", [&] { d.gainDb = f; } },
            { "threshold", [&] { s.thresholdDb = f; } },
            { "thresholdAbs", [&] { s.thresholdAbsDb = f; } },
            { "range", [&] { s.rangeDb = f; } },
            { "focus", [&] { s.focus = f / 100.0f; } },
            { "detectLow", [&] { s.detectLowHz = f; } },
            { "detectHigh", [&] { s.detectHighHz = f; } },
            { "lookahead", [&] { s.lookaheadMs = f; } },
            { "release", [&] { s.releaseMs = f; } },
            { "speedAuto", [&] { s.speedAuto = v >= 0.5; } },
            { "harshRelease", [&] { s.harshReleaseMs = f; } },
            { "harshSpeedAuto", [&] { s.harshSpeedAuto = v >= 0.5; } },
            { "harshOn", [&] { s.harshOn = v >= 0.5; } },
            { "harshThreshold", [&] { s.harshThresholdDb = f; } },
            { "harshThresholdAbs", [&] { s.harshThresholdAbsDb = f; } },
            { "harshRange", [&] { s.harshRangeDb = f; } },
            { "harshFreq", [&] { s.harshFreqHz = f; } },
            { "harshWidth", [&] { s.harshWidthOct = f; } },
            { "bandAveragingMs", [&] { t.bandAveragingMs = v; } },
            { "bandReleaseMs", [&] { t.bandReleaseMs = v; } },
            { "bodyReleaseMs", [&] { t.bodyReleaseMs = v; } },
            { "bodyCapHz", [&] { t.bodyCapHz = v; } },
            { "bodyCapShare", [&] { t.bodyCapShare = v; } },
            { "bodyHoldMs", [&] { t.bodyHoldMs = v; } },
            { "bodyGapDb", [&] { t.bodyGapDb = v; } },
            { "bodyGapHoldMs", [&] { t.bodyGapHoldMs = v; } },
            { "bodyFloorDb", [&] { t.bodyFloorDb = v; } },
            { "candidateDb", [&] { t.candidateDb = f; } },
            { "riseDb", [&] { t.riseDb = f; } },
            { "onsetHoldMs", [&] { t.onsetHoldMs = v; } },
            { "jumpDb", [&] { t.jumpDb = f; } },
            { "jumpHoldDb", [&] { t.jumpHoldDb = f; } },
            { "noiseRangeDb", [&] { t.noiseRangeDb = f; } },
            { "presenceKneeDb", [&] { t.presenceKneeDb = f; } },
            { "presenceSlope", [&] { t.presenceSlope = f; } },
            { "presenceRangeDb", [&] { t.presenceRangeDb = f; } },
            { "periodicNoise", [&] { t.periodicNoise = v; } },
            { "periodicHarmonic", [&] { t.periodicHarmonic = v; } },
            { "flatNoiseDb", [&] { t.flatNoiseDb = v; } },
            { "flatHarmonicDb", [&] { t.flatHarmonicDb = v; } },
            { "periodicLongestLagMs", [&] { t.periodicLongestLagMs = v; } },
            { "persistMs", [&] { t.persistMs = v; } },
            { "persistRho", [&] { t.persistRho = f; } },
            { "persistJumpDb", [&] { t.persistJumpDb = f; } },
            { "harshNoiseHold", [&] { t.harshNoiseHold = f; } },
            { "essAutoShare", [&] { t.essAutoShare = f; } },
            { "essAutoMinMs", [&] { t.essAutoMinMs = f; } },
            { "essAutoMaxMs", [&] { t.essAutoMaxMs = f; } },
            { "harshAutoShare", [&] { t.harshAutoShare = f; } },
            { "harshAutoMinMs", [&] { t.harshAutoMinMs = f; } },
            { "harshAutoMaxMs", [&] { t.harshAutoMaxMs = f; } },
        };

        if (const auto it = keys.find (key); it != keys.end())
        {
            it->second();
            return true;
        }
        if (key == "detection" && (value == "voice" || value == "allround"))
            s.detection = value == "voice" ? Detection::voice : Detection::allround;
        else if (key == "latency" && (value == "normal" || value == "live"))
            s.latency = value == "normal" ? LatencyMode::normal : LatencyMode::live;
        else if (key == "cue" && (value == "periodicity" || value == "flatness" || value == "blend"))
            s.noiseCue = value == "periodicity" ? dsp::NoiseCueKind::periodicity
                                                : (value == "flatness" ? dsp::NoiseCueKind::flatness : dsp::NoiseCueKind::blend);
        else
            return false;
        return true;
    }

    /** The keys that are settings (the chain takes them); every other key is a DetectorTuning constant. */
    const std::set<std::string> settingKeys { "gain", "threshold", "thresholdAbs", "range", "focus", "detectLow", "detectHigh", "lookahead",
                                              "release", "speedAuto", "harshRelease", "harshSpeedAuto", "harshOn", "harshThreshold", "harshThresholdAbs", "harshRange",
                                              "harshFreq", "harshWidth", "detection", "latency", "cue" };

    const char* cueName (dsp::NoiseCueKind k)
    {
        return k == dsp::NoiseCueKind::periodicity ? "periodicity" : (k == dsp::NoiseCueKind::flatness ? "flatness" : "blend");
    }

    /** Runs f (i) for i in [0, n) on `jobs` threads. */
    void parallel (size_t n, int jobs, const std::function<void (size_t)>& f)
    {
        std::atomic<size_t> next { 0 };
        std::vector<std::thread> threads;
        for (int j = 0; j < std::max (1, jobs); ++j)
            threads.emplace_back ([&] {
                for (auto i = next++; i < n; i = next++)
                    f (i);
            });
        for (auto& t : threads)
            t.join();
    }

    struct Corpus
    {
        std::string name;
        std::vector<corpus::Recording> recordings;
        bool gated = false;         // the detection, vowel, Harsh and level gates count here
        std::string note;
    };

    struct Run
    {
        Config config;
        Detector detector;
        std::map<std::string, std::vector<gates::RecordingResult>> results;      // by corpus
        std::map<std::string, gates::LevelResult> level;
        std::map<std::string, float> separabilityDb;                            // T95 by corpus (the sung ones take the gated corpus's)
        std::map<std::string, std::vector<gates::RecordingResult>> separable;    // by corpus, at its T95
    };

    /** Per "corpus/speaker": a threshold and Harsh Threshold (NaN: the setting's), from --thresholds. */
    using SpeakerThresholds = std::map<std::string, std::pair<float, float>>;
    SpeakerThresholds speakerThresholds;

    SpeakerThresholds readThresholds (const juce::File& tsv)
    {
        SpeakerThresholds out;
        juce::StringArray lines;
        tsv.readLines (lines);
        for (int i = 1; i < lines.size(); ++i)
        {
            const auto f = juce::StringArray::fromTokens (lines[i], "\t", "");
            if (f.size() < 4)
                continue;
            const auto value = [] (const juce::String& v) { return v.isEmpty() || v == "nan" ? std::numeric_limits<float>::quiet_NaN() : v.getFloatValue(); };
            out[(f[0] + "/" + f[1]).toStdString()] = { value (f[2]), value (f[3]) };
        }
        return out;
    }

    void applyThresholds (dsp::DetectorSettings& s, const corpus::Recording& rec)
    {
        const auto t = speakerThresholds.find (rec.entry.corpus + "/" + rec.entry.speaker);
        if (t == speakerThresholds.end())
            return;
        const auto voice = s.detection == Detection::voice;
        if (std::isfinite (t->second.first))
            (voice ? s.thresholdDb : s.thresholdAbsDb) = t->second.first;
        if (std::isfinite (t->second.second))
            (voice ? s.harshThresholdDb : s.harshThresholdAbsDb) = t->second.second;
    }

    /** Every recording of `data`; a corpus named in `thresholds` runs at that Relative threshold. */
    Run measure (const std::vector<Corpus>& data, const Config& config, const Detector& detector, bool level, float levelBodyDb,
                 const std::vector<float>& levelGains, int jobs, const std::map<std::string, float>& thresholds = {})
    {
        Run run;
        run.config = config;
        run.detector = detector;

        std::vector<std::pair<size_t, size_t>> work;
        for (size_t c = 0; c < data.size(); ++c)
        {
            run.results[data[c].name].resize (data[c].recordings.size());
            for (size_t r = 0; r < data[c].recordings.size(); ++r)
                work.emplace_back (c, r);
        }

        std::mutex mutex;
        parallel (work.size(), jobs, [&] (size_t i) {
            const auto [c, r] = work[i];
            const auto& rec = data[c].recordings[r];
            auto settings = detector.settings;
            if (const auto t = thresholds.find (data[c].name); t != thresholds.end())
                settings.thresholdDb = t->second;
            else
                applyThresholds (settings, rec);
            const auto voice = settings.detection == Detection::voice;
            auto result = gates::analyse (rec, gates::run (rec, settings, detector.tuning, detector.gainDb),
                                          voice ? settings.thresholdDb : std::numeric_limits<float>::quiet_NaN());
            gates::LevelResult lr;
            const auto speech = ! rec.entry.sung;
            if (level && speech)
                lr = gates::levelIndependence (rec, detector.settings, detector.tuning, levelGains, levelBodyDb);

            const std::scoped_lock lock (mutex);
            run.results[data[c].name][r] = std::move (result);
            if (level && speech)
                run.level[data[c].name].add (lr);
        });
        return run;
    }

    gates::Summary summarise (const std::vector<gates::RecordingResult>& results, const std::function<bool (const gates::RecordingResult&)>& keep = {})
    {
        gates::Summary s;
        for (const auto& r : results)
            if (! keep || keep (r))
                s.add (r);
        return s;
    }

    bool speech (const gates::RecordingResult& r) { return ! r.sung; }

    /** Each speaker's (or synthetic tilt's) summary, in name order. */
    std::vector<std::pair<std::string, gates::Summary>> byPerson (const std::vector<gates::RecordingResult>& results)
    {
        std::set<std::string> people;
        for (const auto& r : results)
            people.insert (r.speaker);
        std::vector<std::pair<std::string, gates::Summary>> out;
        for (const auto& p : people)
            out.emplace_back (p, summarise (results, [&] (const gates::RecordingResult& r) { return r.speaker == p; }));
        return out;
    }

    /** Vowel decisions per speaker: min, median, max (percent). */
    std::array<double, 3> decisionSpread (const std::vector<gates::RecordingResult>& results)
    {
        std::vector<float> v;
        for (const auto& [p, s] : byPerson (results))
            if (s.vowel.total > 0)
                v.push_back ((float) s.vowelTriggered.percent());
        if (v.empty())
            return { 0.0, 0.0, 0.0 };
        return { gates::percentile (v, 0.0), gates::percentile (v, 50.0), gates::percentile (v, 100.0) };
    }

    //==============================================================================
    struct Out
    {
        FILE* file = stdout;
        template <typename... Args>
        void operator() (const char* format, Args... args) { std::fprintf (file, format, args...); }
        void operator() (const char* text) { std::fputs (text, file); }
    };

    const char* verdict (bool pass) { return pass ? "ok" : "**FAIL**"; }

    std::string describe (const Detector& d)
    {
        const auto& s = d.settings;
        const auto& t = d.tuning;
        return juce::String::formatted (
                   "%s, threshold %+.1f dB, range %.1f dB, focus %.0f %%, detect %.0f-%.0f Hz, look-ahead %.1f ms, %s, cue %s, Harsh %s "
                   "(threshold %+.1f dB, %.0f Hz, %.2f oct)\n\nConstants: B %.1f/%.1f ms, V release %.0f ms, body cap min (%.0f Hz, %.2f x Detect Low), "
                   "hold %.0f ms at B-V > %.1f dB or %.0f dB under V, floor %.0f dBFS, rise %.1f dB or jump %.1f dB, "
                   "onset hold %.0f ms (after a jump %.1f dB), noise range %.1f dB, presence knee %.1f dB, slope %.2f to %.1f dB, periodicity %.2f..%.2f to %.1f ms, persistence %.0f ms at rho %.2f within %.1f dB, flatness %.1f..%.1f dB, Harsh noise hold %.2f",
                   s.detection == Detection::voice ? "Voice" : "Allround", s.detection == Detection::voice ? s.thresholdDb : s.thresholdAbsDb,
                   s.rangeDb, 100.0f * s.focus, s.detectLowHz, s.detectHighHz, s.lookaheadMs,
                   s.latency == LatencyMode::normal ? "Normal" : "Live", cueName (s.noiseCue), s.harshOn ? "on" : "off",
                   s.harshThresholdDb, s.harshFreqHz, s.harshWidthOct, t.bandAveragingMs, t.bandReleaseMs, t.bodyReleaseMs, t.bodyCapHz,
                   t.bodyCapShare, t.bodyHoldMs, t.candidateDb, t.bodyGapDb, t.bodyFloorDb, t.riseDb, t.jumpDb, t.onsetHoldMs, t.jumpHoldDb,
                   t.noiseRangeDb, t.presenceKneeDb, t.presenceSlope, t.presenceRangeDb, t.periodicNoise, t.periodicHarmonic, t.periodicLongestLagMs, t.persistMs, t.persistRho,
                   t.persistJumpDb, t.flatNoiseDb, t.flatHarmonicDb, t.harshNoiseHold)
            .toStdString();
    }

    /** Per sibilant, what happens inside its labels: token recall cannot see an ess caught late or let partly through. */
    void coverage (Out& out, const std::vector<Corpus>& data, const std::map<std::string, std::vector<gates::RecordingResult>>& results,
                   const std::function<std::string (const std::string&)>& thresholdOf)
    {
        out ("| Corpus | Threshold | Phoneme | Tokens | Detected | With a decision | Label frames decided | Label frames reduced | Per token reduced: median, p10 "
             "| Mean reduction | First decision: median, p90 | Later than %.0f ms | First reduced: median, p90 | Reduced from the first frame "
             "| Detected with an onset hold inside |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n",
             gates::lateMs);
        for (const auto& c : data)
        {
            if (! results.contains (c.name))
                continue;
            const auto s = summarise (results.at (c.name), speech);
            for (int p = 0; p < corpus::numSibilants; ++p)
            {
                auto cv = s.coverage[(size_t) p];
                if (cv.withDecision.total == 0)
                    continue;
                const auto median = gates::percentile (cv.firstDecisionMs, 50.0), p90 = gates::percentile (cv.firstDecisionMs, 90.0);
                std::vector<float> reducedMs;
                for (const auto ms : cv.firstReducedMs)
                    if (ms >= 0.0f)
                        reducedMs.push_back (ms);
                out ("| %s | %s | %s | %ld | %.1f %% | %.1f %% | %.1f %% | %.1f %% | %.0f, %.0f %% | %.2f dB | %.1f, %.1f ms | %.1f %% (%ld) | %.1f, %.1f ms "
                     "| %.1f %% | %.1f %% |\n",
                     c.name.c_str(), thresholdOf (c.name).c_str(), corpus::sibilantName (p), cv.withDecision.total, s.detected[(size_t) p].percent(),
                     cv.withDecision.percent(), cv.decided.percent(), cv.reduced.percent(), gates::percentile (cv.tokenReducedShare, 50.0),
                     gates::percentile (cv.tokenReducedShare, 10.0), cv.meanReductionDb(), median, p90, cv.late.percent(), cv.late.hits,
                     gates::percentile (reducedMs, 50.0), gates::percentile (reducedMs, 90.0), cv.reducedFromStart.percent(), cv.onsetHeld.percent());
            }
        }
    }

    void report (Out& out, const std::vector<Corpus>& data, const Run& run, bool speakers, bool histograms, int failures, float levelBodyDb,
                 const std::vector<float>& levelGains)
    {
        out ("# Halite detector gates\n\n%s\n\n", describe (run.detector).c_str());
        for (const auto& c : data)
            out ("- **%s**: %zu recordings%s%s\n", c.name.c_str(), c.recordings.size(), c.note.empty() ? "" : ", ", c.note.c_str());

        out ("\n## Detection (an event overlaps the phoneme)\n\n| Corpus | Phoneme | Tokens | Detected | After a vowel | Median length, detected / missed | Gate |\n"
             "|---|---|---|---|---|---|---|\n");
        for (const auto& c : data)
        {
            const auto s = summarise (run.results.at (c.name));
            for (int p = 0; p < corpus::numSibilants; ++p)
            {
                const auto& d = s.detected[(size_t) p];
                if (d.total == 0)
                    continue;
                std::vector<float> hit, miss;
                for (const auto& r : run.results.at (c.name))
                    for (const auto& t : r.tokens)
                        if (t.sibilant == p)
                            (t.detected ? hit : miss).push_back ((float) (1000.0 * (t.end - t.start)));
                const auto& a = s.detectedAfterVowel[(size_t) p];
                out ("| %s | %s | %ld | %.1f %% | %.1f %% (%ld) | %.0f / %.0f ms | %s |\n", c.name.c_str(), corpus::sibilantName (p), d.total,
                     d.percent(), a.percent(), a.total, gates::percentile (hit, 50.0), gates::percentile (miss, 50.0),
                     "reported");
            }
        }

        if (! run.separabilityDb.empty())
        {
            out ("\n## Separability (Settled 34): vowel-frame decisions at the threshold where 95 % of /s/ produce an event\n\n"
                 "T95 is searched per speech corpus (to 0.02 dB). A decision is a vowel frame (over 30 ms from any sibilant) whose own S "
                 "starts an event on its own (target over 0.5 dB); release tails do not count. Per speaker: decisions at the corpus's T95.\n\n"
                 "*S over T95* counts the vowel frames whose S is over T95 itself (the knee puts a decision at S over T95 + 0.23 dB).\n\n"
                 "| Corpus | T95 | /s/ | Vowel decisions | S over T95 | Per speaker: min / median / max | /z/ | /ʃ/ | /ʒ/ | /tʃ/ | /dʒ/ | Gate |\n"
                 "|---|---|---|---|---|---|---|---|---|---|---|---|\n");
            for (const auto& c : data)
            {
                if (! run.separable.contains (c.name))
                    continue;
                const auto& results = run.separable.at (c.name);
                const auto s = summarise (results, speech);
                if (s.vowel.total == 0)
                    continue;
                const auto spread = decisionSpread (results);
                out ("| %s | %+.2f dB | %.1f %% (%ld) | %.2f %% (%ld of %ld) | %.2f %% | %.2f / %.2f / %.2f %% |", c.name.c_str(), run.separabilityDb.at (c.name),
                     s.detected[0].percent(), s.detected[0].total, s.vowelTriggered.percent(), s.vowelTriggered.hits, s.vowel.total,
                     s.vowelOverThreshold.percent(), spread[0], spread[1], spread[2]);
                for (int p = 1; p < corpus::numSibilants; ++p)
                    out (s.detected[(size_t) p].total > 0 ? " %.1f %% |" : " – |", s.detected[(size_t) p].percent());
                out (" %s |\n", c.gated ? verdict (s.detected[0].percent() >= gates::separabilityRecall
                                                    && s.vowelTriggered.percent() <= gates::separabilityVowelPercent)
                                         : "reported");
            }

            out ("\n### Decisions at T95 by kind\n\n| Corpus | Frames | Kinds |\n|---|---|---|\n");
            for (const auto& c : data)
            {
                if (! run.separable.contains (c.name))
                    continue;
                const auto s = summarise (run.separable.at (c.name), speech);
                if (s.vowelTriggered.hits == 0)
                    continue;
                out ("| %s | %ld |", c.name.c_str(), s.vowelTriggered.hits);
                for (const auto& [kind, n] : s.triggers)
                    out (" %s %.0f %%;", kind.c_str(), 100.0 * (double) n / (double) s.vowelTriggered.hits);
                out (" |\n");
            }
        }

        out ("\n## Inside the sibilant labels\n\nA token is detected when an event overlaps it; these columns show how much of it is "
             "decided and reduced, and how late the first decision comes after the label's start (aligner boundaries differ by about "
             "10 ms, so a few ms say little). Speech only, at the threshold set");
        out (run.separable.empty() ? ".\n\n" : " and at each corpus's T95.\n\n");
        const auto set = juce::String::formatted ("%+.2f dB", run.detector.settings.thresholdDb).toStdString();
        coverage (out, data, run.results, [&set] (const std::string&) { return set; });
        if (! run.separable.empty())
            coverage (out, data, run.separable, [&run] (const std::string& name) {
                return juce::String::formatted ("T95 %+.2f dB", run.separabilityDb.at (name)).toStdString();
            });

        out ("\n## Vowel frames at the threshold set (over 30 ms from any sibilant)\n\n"
             "Reduced: more than 0.5 dB of Ess reduction, decisions (own S) and release tails (reduced, not by their own S) together. "
             "Over 1 dB: the release-tail gate (Settled 47), decisions and tails together, at most %.0f %% at the defaults.\n\n"
             "| Corpus | Frames | Reduced | Decisions | Release tails | Over 1 dB | After an ess | Vowels after an ess, any frame | After a pause | Gate |\n"
             "|---|---|---|---|---|---|---|---|---|---|\n", gates::carriedVowelPercent);
        for (const auto& c : data)
        {
            const auto s = summarise (run.results.at (c.name), speech);
            if (s.vowel.total == 0)
                continue;
            out ("| %s | %ld | %.2f %% | %.2f %% | %.2f %% | %.2f %% | %.2f %% (%ld) | %.1f %% (%ld) | %.2f %% (%ld) | %s |\n", c.name.c_str(), s.vowel.total,
                 s.vowel.percent(), s.vowelTriggered.percent(), s.vowelTail.percent(), s.vowelCarried.percent(), s.vowelAfterEss.percent(),
                 s.vowelAfterEss.total, s.afterEssVowels.percent(), s.afterEssVowels.total, s.vowelAfterSilence.percent(), s.vowelAfterSilence.total,
                 c.gated ? verdict (s.vowelCarried.percent() <= gates::carriedVowelPercent) : "reported");
        }

        out ("\n## Events\n\n| Corpus | Events | On a sibilant | Elsewhere, by the phone they overlap most |\n|---|---|---|---|\n");
        for (const auto& c : data)
        {
            const auto s = summarise (run.results.at (c.name), [] (const gates::RecordingResult& r) { return ! r.sung; });
            if (s.events == 0)
                continue;
            std::vector<std::pair<int, std::string>> elsewhere;
            for (const auto& [phone, n] : s.eventsElsewhere)
                elsewhere.emplace_back (n, phone);
            std::sort (elsewhere.rbegin(), elsewhere.rend());
            out ("| %s | %d | %d |", c.name.c_str(), s.events, s.eventsOnSibilants);
            for (size_t i = 0; i < std::min<size_t> (elsewhere.size(), 12); ++i)
                out (" %s %d", elsewhere[i].second.c_str(), elsewhere[i].first);
            out (" |\n");
        }

        out ("\n## Bright isn't sibilant (vowel frames of bright material, per set)\n\n"
             "At the threshold set: frames with more than 0.5 dB of reduction (the gate: at most 0.5 dB on every frame). At T95 (the gated "
             "corpus's): frames with a decision and the largest S - T95. The flat synthetic spectrum is reported apart (Open 2).\n\n"
             "| Set | Recordings | Frames | Reduced frames | Recordings reduced | Largest reduction | Gate | Decisions at T95 | Largest S - T95 |\n"
             "|---|---|---|---|---|---|---|---|---|\n");
        for (const auto& c : data)
        {
            const auto& results = run.results.at (c.name);
            if (summarise (results).brightRecordings == 0)
                continue;
            std::vector<std::pair<std::string, std::function<bool (const gates::RecordingResult&)>>> sets;
            if (c.name == "synthetic")
                for (const auto& [tilt, unused] : byPerson (results))
                    sets.emplace_back (c.name + ", " + tilt, [t = tilt] (const gates::RecordingResult& r) { return r.speaker == t; });
            else
                sets.emplace_back (c.name, [] (const gates::RecordingResult& r) { return r.sung; });

            for (const auto& [name, keep] : sets)
            {
                const auto s = summarise (results, keep);
                if (s.brightRecordings == 0)
                    continue;
                out ("| %s | %d | %ld | %.3f %% | %d | %.2f dB | %s |", name.c_str(), s.brightRecordings, s.bright.total, s.bright.percent(),
                     s.brightRecordingsReduced, s.brightMaxDb,
                     name.find ("tilt +0 dB/oct") != std::string::npos ? "reported (Open 2)" : verdict (s.brightMaxDb <= gates::reducedDb));
                if (run.separable.contains (c.name))
                {
                    const auto t = summarise (run.separable.at (c.name), keep);
                    out (" %.3f %% | %+.1f dB |\n", t.vowelTriggered.percent(), t.brightLargestSibilanceDb - run.separabilityDb.at (c.name));
                }
                else
                {
                    out (" | |\n");
                }
            }
        }

        if (run.detector.settings.harshOn)
        {
            out ("\n## Harsh (reduction during sibilants: no stacking)\n\n"
                 "Every reading (Open 2): the pooled mean over every sibilant hop; *held* is the part on hops where Harsh holds its target "
                 "from before (during an ess, on noise in its band, the warm-up), *own* the part where it follows the sibilant itself; "
                 "*added* is the reduction over what the hop before each token carried. Per token: tokens whose mean, peak or added "
                 "reduction is over 0.5 dB. Per speaker: the worst, and how many speakers average over 0.5 dB. The gate column reads the "
                 "pooled mean, the plan's wording.\n\n"
                 "| Corpus | Sibilant hops | Pooled mean (held, own) | Added | Tokens over 0.5 dB: mean, peak, added | Worst speaker: mean, tokens over 0.5 dB "
                 "| Speakers over 0.5 dB | Mean in vowels | Vowel frames reduced | Gate |\n|---|---|---|---|---|---|---|---|---|---|\n");
            for (const auto& c : data)
            {
                const auto& results = run.results.at (c.name);
                const auto s = summarise (results, speech);
                if (s.harshSibilantHops == 0)
                    continue;
                std::string worst;
                auto worstMean = -1.0;
                auto over = 0, people = 0;
                gates::Summary worstSummary;
                for (const auto& [p, ps] : byPerson (results))
                {
                    if (ps.harshSibilantHops == 0)
                        continue;
                    ++people;
                    over += ps.harshSibilantMeanDb() > gates::reducedDb ? 1 : 0;
                    if (ps.harshSibilantMeanDb() > worstMean)
                    {
                        worstMean = ps.harshSibilantMeanDb();
                        worst = p;
                        worstSummary = ps;
                    }
                }
                out ("| %s | %ld | %.3f dB (%.3f, %.3f) | %.3f dB | %.1f %%, %.1f %%, %.1f %% (%ld) | %s %.3f dB, %.1f %% | %d of %d | %.3f dB | %.2f %% | %s |\n",
                     c.name.c_str(), s.harshSibilantHops, s.harshSibilantMeanDb(), s.harshSibilantHeldMeanDb(),
                     s.harshSibilantMeanDb() - s.harshSibilantHeldMeanDb(), s.harshSibilantAddedMeanDb(), s.harshTokens.percent(),
                     s.harshTokensPeak.percent(), s.harshTokensAdded.percent(), s.harshTokens.total, worst.c_str(), worstMean,
                     worstSummary.harshTokens.percent(), over, people, s.harshVowelMeanDb(), s.harshVowel.percent(),
                     c.gated ? verdict (s.harshSibilantMeanDb() <= 0.5) : "reported");
            }

            out ("\n### Harsh on sustained vowels (pumping, reported)\n\nVowel labels of at least %.0f ms in which Harsh reduces by more than "
                 "0.5 dB: how far the reduction moves in each (p95 - p5, median and p90 over the vowels) and how often it dips by more than "
                 "%.0f dB and comes back (per second of those vowels).\n\n| Corpus | Vowels | Seconds | Depth: median, p90 | Dips per second |\n"
                 "|---|---|---|---|---|\n", 1000.0 * gates::pumpVowelSeconds, gates::pumpDipDb);
            for (const auto& c : data)
            {
                auto s = summarise (run.results.at (c.name));
                if (s.harshDepthDb.empty())
                    continue;
                out ("| %s | %zu | %.0f | %.2f, %.2f dB | %.2f |\n", c.name.c_str(), s.harshDepthDb.size(), s.harshActiveSeconds,
                     gates::percentile (s.harshDepthDb, 50.0), gates::percentile (s.harshDepthDb, 90.0), (double) s.harshDips / s.harshActiveSeconds);
            }
        }

        if (! run.level.empty())
        {
            juce::StringArray gains;
            for (const auto g : levelGains)
                gains.add (juce::String (g, 1));
            out ("\n## Level independence (gains %s dB, each against the first)\n\nThe working range (Settled 34): events whose V stays at least "
                 "%.0f dB above the body floor in the quietest render, from 300 ms before them; the share of events it covers is reported.\n\n",
                 gains.joinIntoString (", ").toRawUTF8(), gates::levelMarginDb);
            if (std::isfinite (levelBodyDb))
                out ("Each recording offset first so its median V over vowel frames is %.1f dBFS at 0 dB.\n\n", levelBodyDb);
            out ("| Corpus | Events in range (share) | Pairs compared | List changes | Largest change | Outside the range | All events: list changes, largest change | Gate |\n"
                 "|---|---|---|---|---|---|---|---|\n");
            for (const auto& c : data)
            {
                if (! run.level.contains (c.name))
                    continue;
                const auto& l = run.level.at (c.name);
                const auto where = l.worstDifferenceDb > 0.0f ? juce::String::formatted (" (%s %.2f s, %+.0f dB)", l.worstId.c_str(), l.worstTime, l.worstGainDb)
                                                              : juce::String();
                const auto gate = l.events == 0 ? "not measured (no event in range)"
                                                : (c.gated ? verdict (l.listChanges == 0 && l.worstDifferenceDb <= 0.5f) : "reported");
                out ("| %s | %d (%.1f %%) | %d | %d | %.3f dB%s | %d | %d of %d, %.2f dB | %s |\n", c.name.c_str(), l.events,
                     100.0 * l.events / std::max (1, l.events + l.excluded), l.compared, l.listChanges, l.worstDifferenceDb, where.toRawUTF8(), l.excluded, l.allListChanges, l.allCompared + l.allListChanges, l.allWorstDifferenceDb,
                     gate);
            }

            out ("\nPer gain, each render against the first in its own working range (V at least %.0f dB above the floor in that render):\n\n"
                 "| Corpus | Gain | Events in range (share) | Pairs compared | List changes | Largest change | Reading |\n|---|---|---|---|---|---|---|\n",
                 gates::levelMarginDb);
            for (const auto& c : data)
                if (run.level.contains (c.name))
                    for (const auto& g : run.level.at (c.name).perGain)
                        out ("| %s | %+.0f dB | %d (%.1f %%) | %d | %d | %.3f dB | %s |\n", c.name.c_str(), g.gainDb, g.inRange,
                             100.0 * g.inRange / std::max (1, g.events), g.compared, g.listChanges, g.worstDifferenceDb,
                             g.inRange == 0 ? "not measured" : verdict (g.listChanges == 0 && g.worstDifferenceDb <= 0.5f));
        }

        if (histograms)
        {
            out ("\n## S of sibilants (largest S inside each token, dB)\n\n| Corpus | Phoneme | p1 | p5 | p10 | p25 | p50 | p75 | p90 | p99 |\n"
                 "|---|---|---|---|---|---|---|---|---|---|\n");
            for (const auto& c : data)
                for (int p = 0; p <= corpus::numSibilants; ++p)
                {
                    // The last row is /s/ and /z/ together, the gated pair.
                    std::vector<float> v;
                    for (const auto& r : run.results.at (c.name))
                        for (const auto& t : r.tokens)
                            if (t.sibilant == p || (p == corpus::numSibilants && t.sibilant < 2))
                                v.push_back (t.peakSibilanceDb);
                    if (v.empty())
                        continue;
                    out ("| %s | %s |", c.name.c_str(), p < corpus::numSibilants ? corpus::sibilantName (p) : "S + Z");
                    for (const auto q : { 1.0, 5.0, 10.0, 25.0, 50.0, 75.0, 90.0, 99.0 })
                        out (" %.1f |", gates::percentile (v, q));
                    out ("\n");
                }

            out ("\n## S of vowel frames and H - body of vowel frames (dB)\n\n| Corpus | Value | p1 | p10 | p50 | p90 | p98 | p99 | p99.5 |\n"
                 "|---|---|---|---|---|---|---|---|---|\n");
            for (const auto& c : data)
            {
                std::vector<float> s, h;
                for (const auto& r : run.results.at (c.name))
                {
                    s.insert (s.end(), r.vowelSibilanceDb.begin(), r.vowelSibilanceDb.end());
                    h.insert (h.end(), r.harshRelativeDb.begin(), r.harshRelativeDb.end());
                }
                for (auto* v : { &s, &h })
                {
                    if (v->empty())
                        continue;
                    out ("| %s | %s |", c.name.c_str(), v == &s ? "S" : "H - body");
                    for (const auto q : { 1.0, 10.0, 50.0, 90.0, 98.0, 99.0, 99.5 })
                        out (" %.1f |", gates::percentile (*v, q));
                    out ("\n");
                }
            }
        }

        if (speakers)
        {
            out ("\n## Per speaker\n\n| Corpus | Speaker | Sex | /s/ | /z/ | /ʃ ʒ tʃ dʒ/ | Vowel frames reduced | Decisions at T95, /s/ there "
                 "| Harsh in sibilants, tokens over 0.5 dB (mean, peak) |\n|---|---|---|---|---|---|---|---|---|\n");
            for (const auto& c : data)
            {
                std::set<std::pair<std::string, std::string>> people;
                for (const auto& r : run.results.at (c.name))
                    if (! r.sung)
                        people.insert ({ r.speaker, r.sex });
                for (const auto& [speaker, sex] : people)
                {
                    const auto s = summarise (run.results.at (c.name), [&] (const gates::RecordingResult& r) { return r.speaker == speaker; });
                    gates::Counts sh;
                    for (int p = 2; p < corpus::numSibilants; ++p)
                        sh.add (s.detected[(size_t) p]);
                    out ("| %s | %s | %s | %.1f %% (%ld) | %.1f %% (%ld) | %.1f %% (%ld) | %.2f %% |", c.name.c_str(), speaker.c_str(), sex.c_str(),
                         s.detected[0].percent(), s.detected[0].total, s.detected[1].percent(), s.detected[1].total, sh.percent(), sh.total,
                         s.vowel.percent());
                    if (run.separable.contains (c.name))
                    {
                        const auto t = summarise (run.separable.at (c.name), [&] (const gates::RecordingResult& r) { return r.speaker == speaker; });
                        out (" %.2f %%, %.1f %% |", t.vowelTriggered.percent(), t.detected[0].percent());
                    }
                    else
                    {
                        out (" |");
                    }
                    if (run.detector.settings.harshOn)
                        out (" %.3f dB, %.1f %%, %.1f %% |\n", s.harshSibilantMeanDb(), s.harshTokens.percent(), s.harshTokensPeak.percent());
                    else
                        out (" |\n");
                }
            }
        }

        if (failures > 0)
        {
            out ("\n## Failures\n");
            for (const auto& c : data)
            {
                std::vector<std::tuple<std::string, double, std::string, float>> missed;
                std::vector<std::pair<long, const gates::RecordingResult*>> worst;
                const auto& results = run.results.at (c.name);
                for (size_t i = 0; i < results.size(); ++i)
                {
                    const auto& r = results[i];
                    for (const auto& t : r.tokens)
                        if (t.sibilant < 2 && ! t.detected)
                        {
                            const auto [id, at] = corpus::locate (c.recordings[i], t.start);
                            missed.emplace_back (id, at, corpus::sibilantName (t.sibilant), t.peakSibilanceDb);
                        }
                    if (! r.sung && r.vowel.hits > 0)
                        worst.emplace_back (r.vowel.hits, &r);
                }
                std::sort (worst.begin(), worst.end(), [] (auto& a, auto& b) { return a.first > b.first; });
                std::sort (missed.begin(), missed.end(), [] (auto& a, auto& b) { return std::get<3> (a) < std::get<3> (b); });
                std::vector<std::pair<float, std::pair<std::string, double>>> bright;
                for (size_t i = 0; i < results.size(); ++i)
                    if (results[i].sung && results[i].brightMaxDb > gates::reducedDb)
                        bright.push_back ({ results[i].brightMaxDb, corpus::locate (c.recordings[i], results[i].brightMaxTime) });
                std::sort (bright.rbegin(), bright.rend());
                if (! bright.empty())
                {
                    out ("\n**%s**: bright recordings reduced (largest reduction):", c.name.c_str());
                    for (size_t i = 0; i < std::min (bright.size(), (size_t) failures); ++i)
                        out ("%s %s %.1f dB at %.2f s", i == 0 ? "" : ";", bright[i].second.first.c_str(), bright[i].first, bright[i].second.second);
                    out ("\n");
                }
                if (missed.empty() && worst.empty())
                    continue;
                out ("\n**%s**: %zu missed /s/ /z/ (lowest S first)", c.name.c_str(), missed.size());
                for (size_t i = 0; i < std::min (missed.size(), (size_t) failures); ++i)
                    out ("%s %s %.3f s /%s/ S %.1f", i == 0 ? ":" : ";", std::get<0> (missed[i]).c_str(), std::get<1> (missed[i]),
                         std::get<2> (missed[i]).c_str(), std::get<3> (missed[i]));
                out ("\n\nMost reduced vowel frames:");
                for (size_t i = 0; i < std::min (worst.size(), (size_t) failures); ++i)
                {
                    const auto& r = *worst[i].second;
                    const auto& rec = c.recordings[(size_t) std::distance (results.data(), worst[i].second)];
                    out ("%s %s %ld of %ld", i == 0 ? "" : ";", r.id.c_str(), r.vowel.hits, r.vowel.total);
                    float peak = 0.0f;
                    double at = 0.0;
                    for (const auto& h : r.vowelHits)
                        if (h.reductionDb > peak)
                        {
                            peak = h.reductionDb;
                            at = h.time;
                        }
                    const auto [id, local] = corpus::locate (rec, at);
                    out (" (largest %.1f dB in %s at %.2f s)", peak, id.c_str(), local);
                }
                out ("\n");
            }
        }
    }

    void dump (const corpus::Recording& rec, const Detector& detector)
    {
        const auto r = gates::run (rec, detector.settings, detector.tuning, detector.gainDb);
        std::printf ("time\tphone\tB\tV\tB-V\tN\trho\tflat\tS\ttarget\tess\tH-body\tharsh\tflags\tevent\tpresence\n");
        size_t k = 0;
        for (size_t i = 0; i < r.hops.size(); ++i)
        {
            const auto t = ((double) i + 0.5) * r.hopSize / r.sampleRate;
            while (k + 1 < rec.labels.size() && rec.labels[k].end <= t)
                ++k;
            const auto& h = r.hops[i];
            const auto& p = h.paths[0];
            std::printf ("%.4f\t%s\t%.1f\t%.1f\t%.1f\t%.2f\t%.2f\t%.1f\t%.1f\t%.2f\t%.2f\t%.1f\t%.2f\t%s%s%s%s%s%s%s\t%d\t%.1f\n", t,
                         rec.labels[k].phone.c_str(), p.bandDb, p.bodyDb, p.relativeDb, p.noise, p.periodicity, p.flatnessDb,
                         p.sibilanceDb, h.decisions[0].essTargetDb, h.reductions[0].essDb, p.harshRelativeDb, h.reductions[0].harshDb,
                         p.candidate ? "c" : "-", p.bodyHeld ? "h" : "-", p.bodyRising ? "r" : "-", p.bodyJumped ? "j" : "-",
                         p.voiceOnset ? "o" : "-", p.jumpHeld ? "J" : "-", p.bodyFloored ? "f" : "-", h.eventActive ? 1 : 0, p.presenceDb);
        }
    }

    //==============================================================================
    std::string jsonCounts (const gates::Counts& c) { return juce::String::formatted ("[%ld,%ld]", c.hits, c.total).toStdString(); }

    std::string jsonCoverage (const gates::Summary& s)
    {
        std::string j = "{";
        for (int p = 0; p < corpus::numSibilants; ++p)
        {
            auto c = s.coverage[(size_t) p];
            j += (p ? ",\"" : "\"") + std::string (corpus::sibilantName (p)) + "\":{\"decided\":" + jsonCounts (c.decided)
                 + ",\"reduced\":" + jsonCounts (c.reduced) + ",\"withDecision\":" + jsonCounts (c.withDecision) + ",\"late\":"
                 + jsonCounts (c.late) + ",\"onsetHeld\":" + jsonCounts (c.onsetHeld)
                 + juce::String::formatted (",\"meanReductionDb\":%.4f,\"firstDecisionMedianMs\":%.2f}", c.meanReductionDb(),
                                            c.firstDecisionMs.empty() ? -1.0 : gates::percentile (c.firstDecisionMs, 50.0))
                       .toStdString();
        }
        return j + "}";
    }

    std::string json (const std::vector<Corpus>& data, const Run& run)
    {
        std::string j = "{\"config\":{";
        for (size_t i = 0; i < run.config.size(); ++i)
            j += (i ? "," : "") + juce::JSON::toString (juce::String (run.config[i].first)).toStdString() + ":"
                 + juce::JSON::toString (juce::String (run.config[i].second)).toStdString();
        j += "},\"corpora\":{";
        for (size_t c = 0; c < data.size(); ++c)
        {
            const auto& name = data[c].name;
            const auto s = summarise (run.results.at (name), [] (const gates::RecordingResult& r) { return ! r.sung; });
            const auto b = summarise (run.results.at (name));
            j += (c ? "," : "") + ("\"" + name + "\":{\"detected\":{");
            for (int p = 0; p < corpus::numSibilants; ++p)
                j += (p ? ",\"" : "\"") + std::string (corpus::sibilantName (p)) + "\":" + jsonCounts (s.detected[(size_t) p]);
            j += "},\"afterVowel\":{";
            for (int p = 0; p < corpus::numSibilants; ++p)
                j += (p ? ",\"" : "\"") + std::string (corpus::sibilantName (p)) + "\":" + jsonCounts (s.detectedAfterVowel[(size_t) p]);
            j += "},\"vowel\":" + jsonCounts (s.vowel) + ",\"vowelTriggered\":" + jsonCounts (s.vowelTriggered)
                 + juce::String::formatted (",\"events\":%d,\"eventsOnSibilants\":%d", s.events, s.eventsOnSibilants).toStdString()
                 + ",\"vowelAfterEss\":" + jsonCounts (s.vowelAfterEss)
                 + ",\"afterEssVowels\":" + jsonCounts (s.afterEssVowels) + ",\"vowelAfterSilence\":" + jsonCounts (s.vowelAfterSilence)
                 + ",\"bright\":" + jsonCounts (b.bright)
                 + juce::String::formatted (",\"brightMaxDb\":%.4f,\"brightRecordings\":%d,\"brightRecordingsReduced\":%d,"
                                            "\"harshSibilantMeanDb\":%.4f,\"harshSibilantHeldMeanDb\":%.4f,\"harshVowelMeanDb\":%.4f,",
                                            b.brightMaxDb, b.brightRecordings, b.brightRecordingsReduced, s.harshSibilantMeanDb(),
                                            s.harshSibilantHeldMeanDb(), s.harshVowelMeanDb())
                       .toStdString()
                 + "\"harshVowel\":" + jsonCounts (s.harshVowel) + ",\"harshTokens\":" + jsonCounts (s.harshTokens)
                 + ",\"harshTokensPeak\":" + jsonCounts (s.harshTokensPeak) + ",\"harshTokensAdded\":" + jsonCounts (s.harshTokensAdded)
                 + juce::String::formatted (",\"harshSibilantAddedMeanDb\":%.4f", s.harshSibilantAddedMeanDb()).toStdString()
                 + ",\"vowelTail\":" + jsonCounts (s.vowelTail) + ",\"vowelCarried\":" + jsonCounts (s.vowelCarried) + ",\"coverage\":" + jsonCoverage (s);

            if (run.separable.contains (name))
            {
                const auto& results = run.separable.at (name);
                const auto t = summarise (results, speech);
                const auto tb = summarise (results);
                const auto spread = decisionSpread (results);
                j += juce::String::formatted (",\"separability\":{\"thresholdDb\":%.2f,\"spread\":[%.3f,%.3f,%.3f],\"brightLargestOverDb\":%.2f,",
                                              run.separabilityDb.at (name), spread[0], spread[1], spread[2],
                                              tb.brightLargestSibilanceDb - run.separabilityDb.at (name))
                         .toStdString()
                     + "\"vowelTriggered\":" + jsonCounts (t.vowelTriggered) + ",\"vowelOverThreshold\":" + jsonCounts (t.vowelOverThreshold)
                     + ",\"bright\":" + jsonCounts (tb.vowelTriggered) + ",\"coverage\":" + jsonCoverage (t) + ",\"detected\":{";
                for (int p = 0; p < corpus::numSibilants; ++p)
                    j += (p ? ",\"" : "\"") + std::string (corpus::sibilantName (p)) + "\":" + jsonCounts (t.detected[(size_t) p]);
                j += "}}";
            }

            // Per speaker: /s/, /z/ and the vowel frames, for the spread.
            j += ",\"speakers\":{";
            std::set<std::string> people;
            for (const auto& r : run.results.at (name))
                people.insert (r.speaker);
            auto first = true;
            for (const auto& p : people)
            {
                const auto ps = summarise (run.results.at (name), [&] (const gates::RecordingResult& r) { return r.speaker == p; });
                j += (first ? "\"" : ",\"") + p + "\":{\"S\":" + jsonCounts (ps.detected[0]) + ",\"Z\":" + jsonCounts (ps.detected[1])
                     + ",\"vowel\":" + jsonCounts (ps.vowel) + ",\"vowelTriggered\":" + jsonCounts (ps.vowelTriggered)
                     + ",\"harshTokens\":" + jsonCounts (ps.harshTokens)
                     + juce::String::formatted (",\"harshSibilantMeanDb\":%.4f,\"harshSibilantAddedMeanDb\":%.4f", ps.harshSibilantMeanDb(),
                                                ps.harshSibilantAddedMeanDb())
                           .toStdString();
                if (run.separable.contains (name))
                {
                    const auto t = summarise (run.separable.at (name), [&] (const gates::RecordingResult& r) { return r.speaker == p; });
                    j += ",\"separability\":{\"S\":" + jsonCounts (t.detected[0]) + ",\"Z\":" + jsonCounts (t.detected[1])
                         + ",\"vowelTriggered\":" + jsonCounts (t.vowelTriggered) + "}";
                }
                j += "}";
                first = false;
            }
            j += "}";

            if (run.level.contains (name))
            {
                const auto& l = run.level.at (name);
                j += juce::String::formatted (",\"level\":{\"events\":%d,\"compared\":%d,\"listChanges\":%d,\"excluded\":%d,\"worstDb\":%.4f,"
                                              "\"allCompared\":%d,\"allListChanges\":%d,\"allWorstDb\":%.4f}",
                                              l.events, l.compared, l.listChanges, l.excluded, l.worstDifferenceDb, l.allCompared, l.allListChanges,
                                              l.allWorstDifferenceDb)
                         .toStdString();
                j.insert (j.size() - 1, ",\"perGain\":[");
                for (size_t g = 0; g < l.perGain.size(); ++g)
                {
                    const auto& p = l.perGain[g];
                    j.insert (j.size() - 1, juce::String::formatted ("%s{\"gainDb\":%.1f,\"events\":%d,\"inRange\":%d,\"listChanges\":%d,\"worstDb\":%.4f}",
                                                                     g ? "," : "", p.gainDb, p.events, p.inRange, p.listChanges, p.worstDifferenceDb)
                                                 .toStdString());
                }
                j.insert (j.size() - 1, "]");
            }
            j += "}";
        }
        return j + "}}";
    }

    /** Label frames decided and tokens decided late, per sibilant: "frames 64.0/... late 1.2/...". */
    std::string coverageLine (const gates::Summary& s)
    {
        std::string frames = "frames", reduced = " reduced", late = " late";
        for (int p = 0; p < corpus::numSibilants; ++p)
        {
            const auto& c = s.coverage[(size_t) p];
            frames += juce::String::formatted ("%s%.1f", p ? "/" : " ", c.decided.percent()).toStdString();
            reduced += juce::String::formatted ("%s%.1f", p ? "/" : " ", c.reduced.percent()).toStdString();
            late += juce::String::formatted ("%s%.1f", p ? "/" : " ", c.late.percent()).toStdString();
        }
        return frames + reduced + late;
    }

    void line (const std::vector<Corpus>& data, const Run& run)
    {
        for (const auto& [k, v] : run.config)
            std::printf ("%s=%s ", k.c_str(), v.c_str());
        for (const auto& c : data)
        {
            const auto s = summarise (run.results.at (c.name), [] (const gates::RecordingResult& r) { return ! r.sung; });
            const auto b = summarise (run.results.at (c.name));
            std::printf ("| %s", c.name.c_str());
            if (s.sz().total > 0)
            {
                gates::Counts sh, after;
                for (int p = 2; p < corpus::numSibilants; ++p)
                    sh.add (s.detected[(size_t) p]);
                after.add (s.detectedAfterVowel[0]);
                after.add (s.detectedAfterVowel[1]);
                std::printf (" S %.1f Z %.1f sh %.1f after %.1f | vowel %.2f own %.2f over1 %.2f afterEss %.2f | ev %d/%d | %s", s.detected[0].percent(),
                             s.detected[1].percent(), sh.percent(), after.percent(), s.vowel.percent(), s.vowelTriggered.percent(),
                             s.vowelCarried.percent(), s.vowelAfterEss.percent(), s.eventsOnSibilants, s.events, coverageLine (s).c_str());
                if (s.harshSibilantHops > 0 && run.detector.settings.harshOn)
                    std::printf (" harsh %.2f/%.2f tok %.1f%%", s.harshSibilantMeanDb(), s.harshVowelMeanDb(), s.harshTokens.percent());
            }
            if (b.brightRecordings > 0)
                std::printf (" bright %.3f%% max %.1f (%d)", b.bright.percent(), b.brightMaxDb, b.brightRecordingsReduced);
            if (run.detector.settings.harshOn && ! b.harshDepthDb.empty())
            {
                auto depths = b.harshDepthDb;
                std::printf (" pump depth %.2f p90 %.2f dips %.2f/s (%zu vowels, %.0f s)", gates::percentile (depths, 50.0), gates::percentile (depths, 90.0),
                             (double) b.harshDips / b.harshActiveSeconds, b.harshDepthDb.size(), b.harshActiveSeconds);
            }
            if (run.separable.contains (c.name))
            {
                const auto& results = run.separable.at (c.name);
                const auto t = summarise (results, speech);
                const auto tb = summarise (results);
                const auto threshold = run.separabilityDb.at (c.name);
                if (t.vowel.total > 0)
                {
                    const auto spread = decisionSpread (results);
                    std::printf (" | T95 %+.2f S %.1f dec %.2f (max %.2f) Z %.1f SH %.1f ZH %.1f CH %.1f JH %.1f | %s", threshold, t.detected[0].percent(),
                                 t.vowelTriggered.percent(), spread[2], t.detected[1].percent(), t.detected[2].percent(), t.detected[3].percent(),
                                 t.detected[4].percent(), t.detected[5].percent(), coverageLine (t).c_str());
                }
                if (tb.brightRecordings > 0)
                {
                    std::printf (" | @T95 bright %.3f%% over %+.1f", tb.vowelTriggered.percent(), tb.brightLargestSibilanceDb - threshold);
                    if (c.name == "synthetic")
                        for (const auto& [tilt, ps] : byPerson (results))
                        {
                            const auto over = std::count_if (results.begin(), results.end(), [&t = tilt] (const gates::RecordingResult& r) {
                                return r.speaker == t && r.vowelTriggered.hits > 0;
                            });
                            std::printf (" [%s %ld/%d %+.1f]", tilt.c_str(), (long) over, ps.brightRecordings, ps.brightLargestSibilanceDb - threshold);
                        }
                }
            }
            std::printf (" ");
        }
        std::printf ("\n");
        std::fflush (stdout);
    }
}

int main (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    std::vector<std::string> dataNames { "vctk", "vocalset", "synthetic" };
    std::string split = "tune", subset = "m1";
    auto cache = corpus::cacheDirectory();
    auto committed = juce::File (HALITE_TEST_DATA_DIR).getChildFile ("corpus");
    int limit = 0, jobs = (int) std::thread::hardware_concurrency(), failures = 0;
    bool level = false, speakers = false, histograms = false, stream = false, ripple = false, separability = true, reductionMode = false;
    std::vector<Shape> shapes { Shape::split, Shape::spectral, Shape::wide };
    int worst = 0;
    bool tuningSet = false;
    auto levelBodyDb = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> levelGains { 0.0f, -10.0f, -20.0f, -30.0f };
    juce::File jsonFile, outFile;
    std::string dumpId;
    Detector base;
    Config fixed;
    std::vector<std::pair<std::string, std::vector<std::string>>> vary;
    bool harshSet = false;
    bool learnMode = false;
    learning::Options learnOptions;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        const auto next = [&] { return i + 1 < args.size() ? args[++i] : juce::String(); };
        if (a == "--help" || a == "-h")
        {
            std::fputs (usage, stdout);
            return 0;
        }
        if (a == "--data")
        {
            dataNames.clear();
            for (const auto& d : juce::StringArray::fromTokens (next(), ",", ""))
                dataNames.push_back (d.toStdString());
        }
        else if (a == "--split") split = next().toStdString();
        else if (a == "--subset") subset = next().toStdString();
        else if (a == "--corpus") cache = juce::File (next());
        else if (a == "--committed") committed = juce::File (next());
        else if (a == "--limit") limit = next().getIntValue();
        else if (a == "--jobs") jobs = std::max (1, next().getIntValue());
        else if (a == "--level") level = true;
        else if (a == "--level-gains")
        {
            level = true;
            levelGains.clear();
            for (const auto& g : juce::StringArray::fromTokens (next(), ",", ""))
                levelGains.push_back (g.getFloatValue());
        }
        else if (a == "--level-body")
        {
            level = true;
            levelBodyDb = next().getFloatValue();
        }
        else if (a == "--stream") stream = true;
        else if (a == "--no-separability") separability = false;
        else if (a == "--ripple") ripple = true;
        else if (a == "--speakers") speakers = true;
        else if (a == "--histograms") histograms = true;
        else if (a == "--failures") failures = next().getIntValue();
        else if (a == "--json") jsonFile = juce::File (next());
        else if (a == "--out") outFile = juce::File (next());
        else if (a == "--dump") dumpId = next().toStdString();
        else if (a == "--reduction") reductionMode = true;
        else if (a == "--learn") learnMode = true;
        else if (a == "--step") learnOptions.stepSeconds = next().getDoubleValue();
        else if (a == "--thresholds-out") learnOptions.thresholdsFile = next().toStdString();
        else if (a == "--thresholds") speakerThresholds = readThresholds (juce::File (next()));
        else if (a == "--normalise") learnOptions.normaliseDb = next().getFloatValue();
        else if (a == "--worst") worst = next().getIntValue();
        else if (a == "--shapes")
        {
            shapes.clear();
            for (const auto& name : juce::StringArray::fromTokens (next(), ",", ""))
            {
                if (name != "split" && name != "spectral" && name != "wide")
                {
                    std::fprintf (stderr, "Unknown shape %s\n", name.toRawUTF8());
                    return 2;
                }
                shapes.push_back (name == "split" ? Shape::split : (name == "spectral" ? Shape::spectral : Shape::wide));
            }
        }
        else if (a == "--set" || a == "--vary")
        {
            const auto kv = next();
            const auto key = kv.upToFirstOccurrenceOf ("=", false, false).toStdString();
            const auto value = kv.fromFirstOccurrenceOf ("=", false, false);
            if (dsp::LearnTuning learnKey; learning::apply (learnKey, key, value.toStdString()))
            {
                if (a == "--set")
                    learning::apply (learnOptions.learn, key, value.toStdString());
                else
                {
                    std::vector<std::string> values;
                    for (const auto& v : juce::StringArray::fromTokens (value, ",", ""))
                        values.push_back (v.toStdString());
                    learnOptions.vary.emplace_back (key, values);
                }
            }
            else if (a == "--set")
            {
                if (! apply (base, key, value.toStdString()))
                {
                    std::fprintf (stderr, "Unknown setting %s\n", kv.toRawUTF8());
                    return 2;
                }
                harshSet = harshSet || key == "harshOn";
                tuningSet = tuningSet || ! settingKeys.contains (key);
                fixed.emplace_back (key, value.toStdString());
            }
            else
            {
                std::vector<std::string> values;
                for (const auto& v : juce::StringArray::fromTokens (value, ",", ""))
                    values.push_back (v.toStdString());
                Detector probe;
                if (values.empty() || ! apply (probe, key, values[0]))
                {
                    std::fprintf (stderr, "Unknown setting %s\n", kv.toRawUTF8());
                    return 2;
                }
                vary.emplace_back (key, values);
            }
        }
        else
        {
            std::fprintf (stderr, "Unknown option %s\n\n%s", a.toRawUTF8(), usage);
            return 2;
        }
    }

    if (reductionMode)
    {
        if (! vary.empty() || tuningSet)
        {
            std::fprintf (stderr, "--reduction runs the chain with the plugin's own detector constants: settings only, no --vary\n");
            return 2;
        }

        // Entries only: each job loads its own recordings and drops them when measured.
        std::vector<reduction::CorpusEntries> entries;
        for (const auto& name : dataNames)
        {
            reduction::CorpusEntries c { name, {} };
            if (name == "synthetic")
            {
                std::fprintf (stderr, "--reduction skips the synthetic vowels (no esses)\n");
                continue;
            }
            if (name == "committed")
                c.entries = corpus::committedTier (committed);
            else if (name == "vctk" || name == "vocalset" || name == "librispeech")
            {
                for (auto& e : corpus::readManifest (cache.getChildFile ("labels").getChildFile (subset).getChildFile (name + ".tsv"), cache, name))
                    if (split == "all" || e.split == split)
                        c.entries.push_back (std::move (e));
            }
            else
            {
                std::fprintf (stderr, "Unknown data %s\n", name.c_str());
                return 2;
            }
            if (c.entries.empty())
            {
                std::fprintf (stderr, "No %s recordings for split %s\n", name.c_str(), split.c_str());
                return 1;
            }
            if (limit > 0 && (int) c.entries.size() > limit)
                c.entries.resize ((size_t) limit);
            entries.push_back (std::move (c));
        }

        if (entries.empty())
        {
            std::fprintf (stderr, "--reduction needs labelled recordings: committed, vctk, vocalset or librispeech\n");
            return 2;
        }

        reduction::Options options;
        options.settings = base.settings;
        options.gainDb = base.gainDb;
        options.shapes = shapes;
        options.stream = stream;
        options.jobs = jobs;
        options.worst = worst;
        options.thresholds = speakerThresholds;
        auto* file = outFile != juce::File() ? std::fopen (outFile.getFullPathName().toRawUTF8(), "w") : stdout;
        if (file == nullptr)
            return 1;
        const auto ok = reduction::measure (entries, options, file);
        if (file != stdout)
            std::fclose (file);
        return ok ? 0 : 1;
    }

    if (vary.empty() && ! harshSet)
        base.settings.harshOn = true;

    if (ripple)
        dataNames.clear();

    // Load every recording asked for, on every thread.
    std::vector<Corpus> data;
    for (const auto& name : dataNames)
    {
        Corpus c;
        c.name = name;
        std::vector<corpus::Entry> entries;
        if (name == "committed")
        {
            entries = corpus::committedTier (committed);
            c.gated = true;
            c.note = "committed tier (CI)";
        }
        else if (name == "cymbals")
        {
            c.recordings = learning::syntheticCymbals (48000.0);
            c.note = "synthetic hats and ride, -18 dBFS RMS, unlabelled";
        }
        else if (name == "synthetic")
        {
            if (split != "holdout")
                c.recordings = gates::syntheticBrightVowels (48000.0, false);
            if (split != "tune")
                for (auto& r : gates::syntheticBrightVowels (48000.0, true))
                    c.recordings.push_back (std::move (r));
            c.note = "harmonics to 12 kHz at 48 kHz, " + std::string (split == "all" ? "both sets" : split + " set");
        }
        else if (name == "vctk" || name == "vocalset" || name == "librispeech")
        {
            const auto manifest = cache.getChildFile ("labels").getChildFile (subset).getChildFile (name + ".tsv");
            for (auto& e : corpus::readManifest (manifest, cache, name))
                if (split == "all" || e.split == split)
                    entries.push_back (std::move (e));
            if (entries.empty())
            {
                std::fprintf (stderr, "No %s recordings for split %s in %s\n", name.c_str(), split.c_str(), manifest.getFullPathName().toRawUTF8());
                return 1;
            }
            c.gated = name == "vctk";
            c.note = "subset " + subset + ", split " + split + (name == "librispeech" ? ", 16 kHz: reported, not gated" : "");
        }
        else
        {
            std::fprintf (stderr, "Unknown data %s\n", name.c_str());
            return 2;
        }

        if (limit > 0 && (int) entries.size() > limit)
            entries.resize ((size_t) limit);
        if (! entries.empty())
        {
            c.recordings.resize (entries.size());
            std::atomic<int> failed { 0 };
            parallel (entries.size(), jobs, [&] (size_t i) {
                juce::String error;
                if (! corpus::load (entries[i], c.recordings[i], error))
                {
                    std::fprintf (stderr, "%s\n", error.toRawUTF8());
                    ++failed;
                }
            });
            if (failed > 0)
                return 1;
            if (stream)
            {
                c.recordings = corpus::streams (std::move (c.recordings));
                c.note += ", streamed per speaker";
            }
        }
        data.push_back (std::move (c));
    }

    if (learnMode)
    {
        if (! vary.empty())
        {
            std::fprintf (stderr, "--learn varies Learn constants only; set the detector with --set\n");
            return 2;
        }
        learnOptions.settings = base.settings;
        learnOptions.tuning = base.tuning;
        learnOptions.gainDb = base.gainDb;
        learnOptions.jobs = jobs;
        learnOptions.speakers = speakers;
        std::vector<learning::CorpusData> corpora;
        for (const auto& c : data)
            corpora.push_back ({ c.name, &c.recordings });
        auto* file = outFile != juce::File() ? std::fopen (outFile.getFullPathName().toRawUTF8(), "w") : stdout;
        if (file == nullptr)
            return 1;
        const auto ok = learning::measure (corpora, learnOptions, file);
        if (file != stdout)
            std::fclose (file);
        return ok ? 0 : 1;
    }

    if (! dumpId.empty())
    {
        for (const auto& c : data)
            for (const auto& rec : c.recordings)
                if (rec.entry.id == dumpId)
                {
                    dump (rec, base);
                    return 0;
                }
        std::fprintf (stderr, "No recording %s in the data asked for\n", dumpId.c_str());
        return 1;
    }

    // Every combination of the --vary values, after the --set ones.
    std::vector<Config> configs { {} };
    for (const auto& [key, values] : vary)
    {
        std::vector<Config> expanded;
        for (const auto& c : configs)
            for (const auto& v : values)
            {
                auto e = c;
                e.emplace_back (key, v);
                expanded.push_back (std::move (e));
            }
        configs = std::move (expanded);
    }

    std::unique_ptr<juce::FileOutputStream> jsonStream;
    if (jsonFile != juce::File())
    {
        jsonFile.deleteFile();
        jsonStream = std::make_unique<juce::FileOutputStream> (jsonFile);
    }

    for (const auto& config : configs)
    {
        auto detector = base;
        for (const auto& [k, v] : config)
            apply (detector, k, v);
        auto full = fixed;
        full.insert (full.end(), config.begin(), config.end());

        if (ripple)
        {
            for (const auto& [k, v] : full)
                std::printf ("%s=%s ", k.c_str(), v.c_str());
            for (const auto rate : { 44100.0, 48000.0, 96000.0 })
            {
                const auto b = gates::bandLevel (rate, detector.tuning);
                std::printf ("| %.1f kHz: ripple %.3f dB, offset %+.2f dB, rise 3 dB %.2f ms, 1 dB %.2f ms ", rate / 1000.0, b.rippleDb,
                             b.offsetDb, b.riseTo3DbMs, b.riseTo1DbMs);
            }
            std::printf ("\n");
            continue;
        }

        auto run = measure (data, full, detector, level, levelBodyDb, levelGains, jobs);

        // Separability: each speech corpus at its own T95; the sung sets at the gated corpus's (VCTK first).
        if (separability && detector.settings.detection == Detection::voice)
        {
            const gates::ForEach forEach = [jobs] (size_t n, const std::function<void (size_t)>& f) { parallel (n, jobs, f); };
            std::string reference;
            for (const auto& c : data)
            {
                const auto spoken = std::any_of (c.recordings.begin(), c.recordings.end(), [] (const corpus::Recording& r) { return ! r.entry.sung; });
                if (! spoken)
                    continue;
                run.separabilityDb[c.name] = gates::thresholdForRecall (c.recordings, detector.settings, detector.tuning, 0,
                                                                         gates::separabilityRecall, forEach);
                if (reference.empty() || (c.name == "vctk") || (c.gated && reference != "vctk"))
                    reference = c.name;
            }
            if (! reference.empty())
            {
                for (const auto& c : data)
                    if (! run.separabilityDb.contains (c.name))
                        run.separabilityDb[c.name] = run.separabilityDb.at (reference);
                run.separable = measure (data, full, detector, false, levelBodyDb, levelGains, jobs, run.separabilityDb).results;
            }
        }

        if (jsonStream != nullptr)
        {
            jsonStream->writeText (json (data, run) + "\n", false, false, nullptr);
            jsonStream->flush();
        }

        if (! vary.empty())
        {
            line (data, run);
            continue;
        }

        Out out;
        if (outFile != juce::File())
            out.file = std::fopen (outFile.getFullPathName().toRawUTF8(), "w");
        if (out.file == nullptr)
            return 1;
        report (out, data, run, speakers, histograms, failures, levelBodyDb, levelGains);
        if (out.file != stdout)
            std::fclose (out.file);
    }
    return 0;
}
