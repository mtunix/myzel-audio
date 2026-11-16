#include "LearnMeasure.h"

#include "support/Gates.h"

#include "dsp/detector/Reduction.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <random>
#include <set>
#include <thread>

namespace halite::learning
{

namespace
{
    using corpus::PhoneClass;
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();

    /** The bounds the parameters clamp a learned threshold to. */
    float clampThreshold (float db, bool voice, bool harsh)
    {
        const auto high = voice ? (harsh ? 12.0f : 24.0f) : 0.0f;
        const auto low = voice ? -36.0f : -60.0f;
        return std::clamp (db, low, high);
    }

    float percentileOf (std::vector<float> v, double p) { return v.empty() ? nan : (float) gates::percentile (v, p); }

    /** The plan's rule on a list of ess levels (the labels' tokens). */
    float ruleOf (const std::vector<float>& levels, const dsp::LearnTuning& t)
    {
        if (levels.empty())
            return nan;
        return std::max (percentileOf (levels, 50.0) - t.medianReductionDb, percentileOf (levels, 100.0 * t.quietShare));
    }

    struct Window
    {
        double start = 0.0;
        int esses = 0;                  // candidates in the capture
        float essDb = nan;              // the rule with any number of candidates (the minimum is applied later)
        float harshDb = nan;            // with enough voiced frames
        double voicedSeconds = 0.0;
    };

    struct Candidate
    {
        double start = 0.0, end = 0.0;
        float peakDb = 0.0f;
        std::string kind;               // sibilant, or the class it overlaps most
    };

    /** One recording (or stream) under one configuration. */
    struct Unit
    {
        std::string corpus, speaker;
        bool sung = false;
        double seconds = 0.0;
        std::vector<float> tokenDb;     // the largest S of each /s/ /z/ token
        float labelDb = nan;            // the rule on the tokens
        float labelHarshDb = nan;       // the level of the labelled vowel frames' loudest harshShare
        std::vector<float> vowelHarshDb;    // those frames' levels, to pool over a corpus
        float allDb = nan, allHarshDb = nan;   // Learn over the whole recording
        int allEsses = 0;
        dsp::LevelHistogram::Counts essCounts {}, voicedCounts {};     // its histograms, to pool over a corpus
        std::uint32_t numEsses = 0, numVoiced = 0;
        double hopSeconds = 0.0;
        std::vector<Window> windows;
        std::vector<Candidate> candidates;
        long sz = 0, szCovered = 0;     // /s/ /z/ tokens, and those a candidate overlaps
    };

    /** The reduction a token's peak gets at a threshold (the detector's target), and whether the quietest quarter is touched. */
    struct Outcome
    {
        float medianDb = nan;           // the median token's
        float quietReducedPercent = nan;// tokens of the quietest quarter reduced by more than 0.5 dB
        float reducedPercent = nan;     // every token reduced by more than 0.5 dB
    };

    Outcome outcomeOf (const std::vector<float>& tokens, float thresholdDb, float rangeDb, double quietShare)
    {
        Outcome o;
        if (tokens.empty() || ! std::isfinite (thresholdDb))
            return o;
        std::vector<float> r;
        for (const auto s : tokens)
            r.push_back (dsp::reductionDb (s - thresholdDb, rangeDb));
        o.medianDb = percentileOf (r, 50.0);
        const auto quiet = percentileOf (tokens, 100.0 * quietShare);
        long q = 0, qr = 0, all = 0;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            all += r[i] > gates::reducedDb ? 1 : 0;
            if (tokens[i] <= quiet)
            {
                ++q;
                qr += r[i] > gates::reducedDb ? 1 : 0;
            }
        }
        o.quietReducedPercent = q > 0 ? 100.0f * (float) qr / (float) q : nan;
        o.reducedPercent = 100.0f * (float) all / (float) tokens.size();
        return o;
    }

    std::string kindOf (const corpus::Recording& rec, double from, double to)
    {
        std::map<PhoneClass, double> overlap;
        for (const auto& seg : rec.labels)
        {
            const auto o = std::min (to, seg.end) - std::max (from, seg.start);
            if (o > 0.0)
                overlap[corpus::classOf (seg.phone)] += o;
        }
        if (overlap.contains (PhoneClass::sibilant))
            return "sibilant";
        auto best = PhoneClass::silence;
        auto most = -1.0;
        for (const auto& [c, o] : overlap)
            if (o > most)
            {
                most = o;
                best = c;
            }
        return best == PhoneClass::vowel ? "vowel" : (best == PhoneClass::consonant ? "consonant" : "pause");
    }

    Unit measureUnit (const corpus::Recording& rec, const dsp::probe::Result& probe, const Options& o, const dsp::LearnTuning& t)
    {
        Unit u;
        u.corpus = rec.entry.corpus;
        u.speaker = rec.entry.speaker;
        u.sung = rec.entry.sung;
        u.seconds = (double) rec.audio.size() / rec.sampleRate;
        const auto voice = o.settings.detection == Detection::voice;
        const auto hopSeconds = probe.hopSize / probe.sampleRate;
        const auto numHops = (long) probe.hops.size();

        // The labels: each /s/ /z/ token's largest S, and the Harsh level of the vowel frames.
        std::vector<float> vowelHarsh;
        std::vector<std::pair<double, double>> sz;
        for (const auto& seg : rec.labels)
        {
            const auto cls = corpus::classOf (seg.phone);
            const auto first = std::clamp ((long) std::floor (seg.start / hopSeconds), 0L, numHops);
            const auto last = std::clamp ((long) std::ceil (seg.end / hopSeconds), first, numHops);
            if (cls == PhoneClass::sibilant && corpus::sibilantIndex (seg.phone) < 2 && last > first)
            {
                auto peak = dsp::silentDb;
                for (auto i = first; i < last; ++i)
                    peak = std::max (peak, dsp::Learn::essLevelDb (probe.hops[(size_t) i]));
                u.tokenDb.push_back (peak);
                sz.emplace_back (seg.start, seg.end);
            }
            else if (cls == PhoneClass::vowel && o.settings.harshOn)
            {
                for (auto i = first; i < last; ++i)
                {
                    const auto& p = probe.hops[(size_t) i].paths[0];
                    if (p.harshDb > dsp::silentDb)
                        vowelHarsh.push_back (voice ? p.harshRelativeDb : p.harshDb);
                }
            }
        }
        u.labelDb = ruleOf (u.tokenDb, t);
        u.labelHarshDb = percentileOf (vowelHarsh, 100.0 * (1.0 - t.harshShare));
        u.vowelHarshDb = std::move (vowelHarsh);

        // Learn over the whole recording: every candidate and every voiced frame.
        dsp::EssCandidates cands;
        cands.prepare (hopSeconds, t);
        dsp::LevelHistogram::Counts allEsses {}, allVoiced {};
        std::uint32_t numEsses = 0, numVoiced = 0;
        const auto floorDb = voice ? t.floorDb : t.floorAbsDb;
        for (long i = 0; i < numHops; ++i)
        {
            const auto& f = probe.hops[(size_t) i];
            if (auto peak = 0.0f; cands.push (dsp::Learn::essLevelDb (f), floorDb, voice ? 0.0f : t.splitAbsDb, peak))
            {
                const auto hangover = std::max (1, (int) std::lround (t.hangoverMs * 0.001 / hopSeconds));
                const auto lastHop = i - hangover - 1;
                const auto firstHop = lastHop - cands.lengthHops() + 1;
                Candidate c { (double) firstHop * hopSeconds, (double) (lastHop + 1) * hopSeconds, peak, {} };
                c.kind = kindOf (rec, c.start, c.end);
                u.candidates.push_back (c);
                ++allEsses[(size_t) dsp::LevelHistogram::binOf (peak)];
                ++numEsses;
            }
            if (const auto h = dsp::Learn::voicedHarshDb (f, o.settings, t); std::isfinite (h))
            {
                ++allVoiced[(size_t) dsp::LevelHistogram::binOf (h)];
                ++numVoiced;
            }
        }
        auto anyCount = t;
        anyCount.minEsses = 1;
        const auto all = dsp::learnThresholds (allEsses, numEsses, allVoiced, numVoiced, o.settings.harshOn, hopSeconds, anyCount);
        u.allEsses = all.esses;
        u.essCounts = allEsses;
        u.voicedCounts = allVoiced;
        u.numEsses = numEsses;
        u.numVoiced = numVoiced;
        u.hopSeconds = hopSeconds;
        u.allDb = all.essSet ? clampThreshold (all.essDb, voice, false) : nan;
        u.allHarshDb = all.harshSet ? clampThreshold (all.harshDb, voice, true) : nan;

        for (const auto& [from, to] : sz)
        {
            ++u.sz;
            u.szCovered += std::any_of (u.candidates.begin(), u.candidates.end(),
                                        [from, to] (const Candidate& c) { return c.start < to && c.end > from; }) ? 1 : 0;
        }

        // Captures as the plugin makes them: the plugin's Learn, warm for half a second, then a request.
        const auto warmHops = (long) std::lround (0.5 / hopSeconds);
        const auto stepHops = std::max (1L, (long) std::lround (o.stepSeconds / hopSeconds));
        const auto captureHops = (long) std::lround (t.captureSeconds / hopSeconds);
        for (long start = 0; start + captureHops <= numHops; start += stepHops)
        {
            // A fresh Learn per capture: nothing of the previous capture or its live histogram carries over.
            const auto learner = std::make_unique<dsp::Learn>();
            auto& learn = *learner;
            learn.prepare (probe.sampleRate, t);
            for (auto i = std::max (0L, start - warmHops); i < start; ++i)
                learn.hop (probe.hops[(size_t) i], o.settings);
            learn.request();
            for (auto i = start; i < numHops && learn.phase() != dsp::Learn::Phase::captured; ++i)
                learn.hop (probe.hops[(size_t) i], o.settings);

            dsp::LevelHistogram::Counts esses {}, voiced {};
            learn.capturedEsses().read (esses);
            learn.capturedVoiced().read (voiced);
            const auto r = dsp::learnThresholds (esses, learn.capturedEsses().total(), voiced, learn.capturedVoiced().total(),
                                                 learn.capturedHarshOn(), learn.capturedHopSeconds(), anyCount);
            Window w;
            w.start = (double) start * hopSeconds;
            w.esses = r.esses;
            w.essDb = r.essSet ? clampThreshold (r.essDb, voice, false) : nan;
            w.harshDb = r.harshSet ? clampThreshold (r.harshDb, voice, true) : nan;
            w.voicedSeconds = r.voicedSeconds;
            u.windows.push_back (w);
        }
        return u;
    }

    std::vector<dsp::LearnTuning> configsOf (const Options& o, std::vector<std::string>& names)
    {
        std::vector<std::pair<dsp::LearnTuning, std::string>> configs { { o.learn, "" } };
        for (const auto& [key, values] : o.vary)
        {
            std::vector<std::pair<dsp::LearnTuning, std::string>> expanded;
            for (const auto& [c, name] : configs)
                for (const auto& v : values)
                {
                    auto e = c;
                    apply (e, key, v);
                    expanded.emplace_back (e, name + (name.empty() ? "" : " ") + key + "=" + v);
                }
            configs = std::move (expanded);
        }
        std::vector<dsp::LearnTuning> out;
        for (auto& [c, name] : configs)
        {
            out.push_back (c);
            names.push_back (name.empty() ? "defaults" : name);
        }
        return out;
    }

    std::string cell (float v, const char* format = "%+.1f") { return std::isfinite (v) ? juce::String::formatted (format, v).toStdString() : "–"; }

    void reportConfig (std::FILE* out, const std::string& name, const std::vector<Unit>& units, const dsp::LearnTuning& t, const Options& o,
                       const std::string& corpusName)
    {
        std::vector<float> dAll, dLabel, adAll, counts, med, quiet, reduced, allVsLabel, harshVsLabel, harshVsAll, harshAllVsLabel;
        long windows = 0, set = 0, harshSet = 0;
        std::map<int, std::pair<long, std::vector<float>>> byMinimum;     // minimum count: windows set, |T - whole|
        for (const auto& u : units)
        {
            if (std::isfinite (u.allDb) && std::isfinite (u.labelDb))
                allVsLabel.push_back (u.allDb - u.labelDb);
            if (std::isfinite (u.allHarshDb) && std::isfinite (u.labelHarshDb))
                harshAllVsLabel.push_back (u.allHarshDb - u.labelHarshDb);
            for (const auto& w : u.windows)
            {
                ++windows;
                counts.push_back ((float) w.esses);
                for (const auto minimum : { 1, 3, 4, 5, 6, 8, 10 })
                    if (w.esses >= minimum && std::isfinite (w.essDb))
                    {
                        auto& m = byMinimum[minimum];
                        ++m.first;
                        if (std::isfinite (u.allDb))
                            m.second.push_back (std::abs (w.essDb - u.allDb));
                    }
                if (std::isfinite (w.harshDb))
                {
                    ++harshSet;
                    if (std::isfinite (u.labelHarshDb))
                        harshVsLabel.push_back (w.harshDb - u.labelHarshDb);
                    if (std::isfinite (u.allHarshDb))
                        harshVsAll.push_back (std::abs (w.harshDb - u.allHarshDb));
                }
                if (w.esses < t.minEsses || ! std::isfinite (w.essDb))
                    continue;
                ++set;
                if (std::isfinite (u.allDb))
                {
                    dAll.push_back (w.essDb - u.allDb);
                    adAll.push_back (std::abs (w.essDb - u.allDb));
                }
                if (std::isfinite (u.labelDb))
                    dLabel.push_back (w.essDb - u.labelDb);
                const auto oc = outcomeOf (u.tokenDb, w.essDb, o.settings.rangeDb, t.quietShare);
                if (std::isfinite (oc.medianDb))
                {
                    med.push_back (oc.medianDb);
                    quiet.push_back (oc.quietReducedPercent);
                    reduced.push_back (oc.reducedPercent);
                }
            }
        }

        std::vector<float> labelMed, labelQuiet, wholeMed, wholeQuiet;
        for (const auto& u : units)
        {
            if (const auto oc = outcomeOf (u.tokenDb, u.labelDb, o.settings.rangeDb, t.quietShare); std::isfinite (oc.medianDb))
            {
                labelMed.push_back (oc.medianDb);
                labelQuiet.push_back (oc.quietReducedPercent);
            }
            if (const auto oc = outcomeOf (u.tokenDb, u.allDb, o.settings.rangeDb, t.quietShare); std::isfinite (oc.medianDb))
            {
                wholeMed.push_back (oc.medianDb);
                wholeQuiet.push_back (oc.quietReducedPercent);
            }
        }

        std::map<std::string, long> kinds;
        long cands = 0, sz = 0, covered = 0;
        for (const auto& u : units)
        {
            for (const auto& c : u.candidates)
            {
                ++kinds[c.kind];
                ++cands;
            }
            sz += u.sz;
            covered += u.szCovered;
        }

        std::fprintf (out, "| %s | %s | %ld | %.1f %% | %s / %s / %s | %s [%s, %s] | %s | %s [%s, %s] | %s / %s | %s %% | %s %% | %s [%s, %s] |",
                      corpusName.c_str(), name.c_str(), windows, windows > 0 ? 100.0 * (double) set / (double) windows : 0.0,
                      cell (percentileOf (counts, 10.0), "%.0f").c_str(), cell (percentileOf (counts, 50.0), "%.0f").c_str(),
                      cell (percentileOf (counts, 90.0), "%.0f").c_str(), cell (percentileOf (dAll, 50.0), "%+.2f").c_str(),
                      cell (percentileOf (dAll, 10.0)).c_str(), cell (percentileOf (dAll, 90.0)).c_str(), cell (percentileOf (adAll, 90.0), "%.1f").c_str(),
                      cell (percentileOf (dLabel, 50.0), "%+.2f").c_str(), cell (percentileOf (dLabel, 10.0)).c_str(), cell (percentileOf (dLabel, 90.0)).c_str(),
                      cell (percentileOf (med, 50.0), "%.1f").c_str(), cell (percentileOf (med, 90.0), "%.1f").c_str(),
                      cell (percentileOf (quiet, 50.0), "%.0f").c_str(), cell (percentileOf (reduced, 50.0), "%.0f").c_str(),
                      cell (percentileOf (allVsLabel, 50.0), "%+.2f").c_str(), cell (percentileOf (allVsLabel, 0.0)).c_str(),
                      cell (percentileOf (allVsLabel, 100.0)).c_str());
        std::fprintf (out, " %s | %.1f %% (%ld) |", cands > 0 ? juce::String::formatted ("%.1f %%", 100.0 * (double) kinds["sibilant"] / (double) cands).toRawUTF8() : "–",
                      sz > 0 ? 100.0 * (double) covered / (double) sz : 0.0, sz);
        if (o.settings.harshOn)
            std::fprintf (out, " %.1f %% | %s [%s, %s] | %s | %s |", windows > 0 ? 100.0 * (double) harshSet / (double) windows : 0.0,
                          cell (percentileOf (harshVsLabel, 50.0), "%+.2f").c_str(), cell (percentileOf (harshVsLabel, 10.0)).c_str(),
                          cell (percentileOf (harshVsLabel, 90.0)).c_str(), cell (percentileOf (harshVsAll, 90.0), "%.1f").c_str(),
                          cell (percentileOf (harshAllVsLabel, 50.0), "%+.2f").c_str());
        std::fprintf (out, "\n");

        // How many candidates a capture needs: windows set and the spread against the whole recording, per minimum.
        std::fprintf (out, "|   | minimum count: ");
        for (auto& [minimum, m] : byMinimum)
            std::fprintf (out, "%d: %.0f %% set, p50/p90 |T - whole| %s/%s dB; ", minimum, windows > 0 ? 100.0 * (double) m.first / (double) windows : 0.0,
                          cell (percentileOf (m.second, 50.0), "%.1f").c_str(), cell (percentileOf (m.second, 90.0), "%.1f").c_str());
        std::fprintf (out, "at the labels' rule: median token cut %s dB, quietest quarter %s %%; whole recording: %s dB, %s %% (medians over recordings); ",
                      cell (percentileOf (labelMed, 50.0), "%.1f").c_str(), cell (percentileOf (labelQuiet, 50.0), "%.0f").c_str(),
                      cell (percentileOf (wholeMed, 50.0), "%.1f").c_str(), cell (percentileOf (wholeQuiet, 50.0), "%.0f").c_str());
        std::fprintf (out, "candidates: ");
        for (const auto& [kind, n] : kinds)
            std::fprintf (out, "%s %.1f %%; ", kind.c_str(), cands > 0 ? 100.0 * (double) n / (double) cands : 0.0);
        std::fprintf (out, "|\n");
    }

    void parallel (size_t n, int jobs, const std::function<void (size_t)>& f)
    {
        std::atomic<size_t> next { 0 };
        std::vector<std::thread> threads;
        for (int j = 0; j < std::max (1, jobs); ++j)
            threads.emplace_back ([&] {
                for (auto i = next++; i < n; i = next++)
                    f (i);
            });
        for (auto& th : threads)
            th.join();
    }
} // namespace

std::vector<corpus::Recording> syntheticCymbals (double rate)
{
    // A biquad high- or low-pass (RBJ), Q 0.707.
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        Biquad (double hz, double fs, bool high)
        {
            const auto w = 2.0 * std::numbers::pi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071), a0 = 1.0 + alpha;
            b0 = (high ? (1.0 + c) : (1.0 - c)) / 2.0 / a0;
            b1 = (high ? -(1.0 + c) : (1.0 - c)) / a0;
            b2 = b0;
            a1 = -2.0 * c / a0;
            a2 = (1.0 - alpha) / a0;
        }
        double process (double x)
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    const auto n = (size_t) (32.0 * rate);
    const auto eighth = 0.25;
    std::vector<corpus::Recording> out;
    for (const auto* name : { "hats", "ride" })
    {
        const auto ride = std::string (name) == "ride";
        std::mt19937 rng (ride ? 31u : 29u);
        std::normal_distribution<double> white (0.0, 1.0);
        std::uniform_real_distribution<double> jitterDb (-1.5, 1.5);
        Biquad high (ride ? 3000.0 : 7000.0, rate, true), low (12000.0, rate, false);

        corpus::Recording rec;
        rec.sampleRate = rate;
        rec.entry.id = rec.entry.speaker = name;
        rec.entry.corpus = "cymbals";
        rec.entry.split = "synthetic";
        rec.audio.assign (n, 0.0f);

        auto envelope = 0.0, decay = 0.0, bell = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) i / rate;
            const auto step = (long) std::floor (t / eighth);
            if (t - (double) step * eighth < 1.0 / rate)
            {
                const auto accent = step % 2 == 0 ? 1.0 : (ride ? 0.6 : 0.7);
                envelope = accent * std::pow (10.0, jitterDb (rng) / 20.0);
                const auto open = ! ride && step % 8 == 7;
                decay = std::exp (-1.0 / ((ride ? 0.4 : (open ? 0.25 : 0.03)) * rate));
                bell = ride ? envelope : 0.0;
            }
            auto noise = high.process (white (rng));
            if (ride)
                noise = low.process (noise);
            auto x = envelope * noise;
            if (ride)
                for (const auto hz : { 2900.0, 4400.0, 6300.0 })
                    x += 0.3 * bell * std::sin (2.0 * std::numbers::pi * hz * t);
            rec.audio[i] = (float) x;
            envelope *= decay;
            bell *= std::exp (-1.0 / (0.6 * rate));
        }

        auto power = 0.0;
        for (const auto v : rec.audio)
            power += (double) v * v;
        const auto gain = std::pow (10.0, -18.0 / 20.0) / std::sqrt (power / (double) n);
        for (auto& v : rec.audio)
            v = (float) (v * gain);
        out.push_back (std::move (rec));
    }
    return out;
}

bool apply (dsp::LearnTuning& t, const std::string& key, const std::string& value)
{
    const auto v = juce::String (value).getDoubleValue();
    const std::map<std::string, std::function<void()>> keys {
        { "captureSeconds", [&] { t.captureSeconds = v; } },
        { "floorDb", [&] { t.floorDb = (float) v; } },
        { "floorAbsDb", [&] { t.floorAbsDb = (float) v; } },
        { "splitAbsDb", [&] { t.splitAbsDb = (float) v; } },
        { "hangoverMs", [&] { t.hangoverMs = v; } },
        { "minLengthMs", [&] { t.minLengthMs = v; } },
        { "medianReductionDb", [&] { t.medianReductionDb = (float) v; } },
        { "quietShare", [&] { t.quietShare = v; } },
        { "minEsses", [&] { t.minEsses = (int) v; } },
        { "harshShare", [&] { t.harshShare = v; } },
        { "minVoicedSeconds", [&] { t.minVoicedSeconds = v; } },
        { "voicedNoise", [&] { t.voicedNoise = (float) v; } },
    };
    if (const auto it = keys.find (key); it != keys.end())
    {
        it->second();
        return true;
    }
    return false;
}

bool measure (const std::vector<CorpusData>& data, const Options& o, std::FILE* out)
{
    std::vector<std::string> names;
    const auto configs = configsOf (o, names);
    const auto voice = o.settings.detection == Detection::voice;

    std::fprintf (out, "# Halite Learn (the plugin's Learn over its detector)\n\n%s, threshold scale %s, Range %.1f dB, Focus %.0f %%, Harsh %s; "
                       "captures of %.1f s every %.1f s (each warm for 0.5 s), and Learn over the whole recording or stream.\n\n",
                  voice ? "Voice" : "Allround", voice ? "dB over the voice" : "dBFS", o.settings.rangeDb, 100.0f * o.settings.focus,
                  o.settings.harshOn ? "on" : "off", o.learn.captureSeconds, o.stepSeconds);
    std::fprintf (out, "Columns: captures; share that set Threshold (enough candidates); candidates per capture p10/p50/p90; the capture's threshold "
                       "against Learn over the whole recording (median [p10, p90], p90 of the distance) and against the plan's rule on the labelled "
                       "/s/ /z/ tokens of that recording; at each capture's threshold, the reduction of the recording's median token (median / p90 "
                       "over captures), the share of its quietest quarter reduced over 0.5 dB, and of every token; whole recording against the "
                       "labels (median [min, max] over recordings); candidates on a sibilant label; /s/ /z/ tokens a candidate overlaps. "
                       "Harsh: captures that set it, against the labelled vowel frames' loudest share (the same harshShare), p90 distance to the whole recording, whole "
                       "against labels.\n\n");
    std::fprintf (out, "| Corpus | Configuration | Captures | Set | Candidates | − whole | p90 dist | − labels | Median token cut | Quietest quarter cut | Tokens cut "
                       "| Whole − labels | Candidates on sibilants | /s/ /z/ covered |%s\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|%s\n",
                  o.settings.harshOn ? " Harsh set | − labels | p90 dist | Whole − labels |" : "", o.settings.harshOn ? "---|---|---|---|" : "");

    std::vector<std::vector<std::vector<Unit>>> results (data.size(), std::vector<std::vector<Unit>> (configs.size()));
    for (size_t c = 0; c < data.size(); ++c)
    {
        const auto& recs = *data[c].recordings;
        for (auto& r : results[c])
            r.resize (recs.size());
        std::mutex mutex;
        parallel (recs.size(), o.jobs, [&] (size_t i) {
            auto gainDb = o.gainDb;
            if (std::isfinite (o.normaliseDb))
            {
                // The RMS over the labelled speech (everything but pauses), or over all of it.
                auto power = 0.0;
                long count = 0;
                const auto& rec = recs[i];
                const auto counts = [&rec] (size_t k) {
                    const auto t = (double) k / rec.sampleRate;
                    if (rec.labels.empty())
                        return true;
                    const auto seg = std::lower_bound (rec.labels.begin(), rec.labels.end(), t,
                                                       [] (const corpus::Segment& s, double x) { return s.end < x; });
                    return seg != rec.labels.end() && seg->start <= t && corpus::classOf (seg->phone) != PhoneClass::silence;
                };
                for (size_t k = 0; k < rec.audio.size(); ++k)
                    if (counts (k))
                    {
                        power += (double) rec.audio[k] * rec.audio[k];
                        ++count;
                    }
                if (count > 0 && power > 0.0)
                    gainDb = o.normaliseDb - (float) (10.0 * std::log10 (power / (double) count));
            }
            const auto probe = gates::run (recs[i], o.settings, o.tuning, gainDb);
            for (size_t k = 0; k < configs.size(); ++k)
            {
                auto u = measureUnit (recs[i], probe, o, configs[k]);
                const std::scoped_lock lock (mutex);
                results[c][k][i] = std::move (u);
            }
        });
        for (size_t k = 0; k < configs.size(); ++k)
            reportConfig (out, names[k], results[c][k], configs[k], o, data[c].name);
    }

    if (o.speakers)
    {
        std::fprintf (out, "\n## Per recording or speaker (first configuration)\n\n| Corpus | Speaker | Seconds | /s/ /z/ | Labels | Whole | Captures set | Capture: "
                           "p10 / median / p90 | Harsh labels | Harsh whole | Harsh captures: p10 / median / p90 |\n|---|---|---|---|---|---|---|---|---|---|---|\n");
        for (size_t c = 0; c < data.size(); ++c)
            for (const auto& u : results[c][0])
            {
                std::vector<float> t, h;
                for (const auto& w : u.windows)
                {
                    if (w.esses >= configs[0].minEsses && std::isfinite (w.essDb))
                        t.push_back (w.essDb);
                    if (std::isfinite (w.harshDb))
                        h.push_back (w.harshDb);
                }
                std::fprintf (out, "| %s | %s | %.0f | %zu | %s | %s | %zu of %zu | %s / %s / %s | %s | %s | %s / %s / %s |\n", data[c].name.c_str(),
                              u.speaker.c_str(), u.seconds, u.tokenDb.size(), cell (u.labelDb).c_str(), cell (u.allDb).c_str(), t.size(),
                              u.windows.size(), cell (percentileOf (t, 10.0)).c_str(), cell (percentileOf (t, 50.0)).c_str(),
                              cell (percentileOf (t, 90.0)).c_str(), cell (u.labelHarshDb).c_str(), cell (u.allHarshDb).c_str(),
                              cell (percentileOf (h, 10.0)).c_str(), cell (percentileOf (h, 50.0)).c_str(), cell (percentileOf (h, 90.0)).c_str());
            }
    }

    // The thresholds Learn sets per speaker over the whole recording, and the corpus's median of them: the default's reading.
    std::fprintf (out, "\n## Learn's thresholds over the corpus (first configuration)\n\n*Pooled*: Learn over every recording of the corpus at once; in brackets the "
                       "labels' pooled reading (the rule on every /s/ /z/ token; Harsh, the labelled vowel frames' loudest harshShare).\n\n| Corpus | Recordings | Threshold: p25 / median / p75 over recordings "
                       "| Labels: median | Pooled (labels) | Harsh: p25 / median / p75 | Harsh labels: median | Harsh pooled (labels) |\n|---|---|---|---|---|---|---|---|\n");
    juce::String tsv ("corpus\tspeaker\tthreshold\tharshThreshold\tlabelThreshold\tlabelHarshThreshold\n");
    for (size_t c = 0; c < data.size(); ++c)
    {
        std::vector<float> t, l, h, hl;
        for (const auto& u : results[c][0])
        {
            if (std::isfinite (u.allDb) && u.allEsses >= configs[0].minEsses)
                t.push_back (u.allDb);
            if (std::isfinite (u.labelDb))
                l.push_back (u.labelDb);
            if (std::isfinite (u.allHarshDb))
                h.push_back (u.allHarshDb);
            if (std::isfinite (u.labelHarshDb))
                hl.push_back (u.labelHarshDb);
            tsv << data[c].name << "\t" << u.speaker << "\t" << juce::String (u.allEsses >= configs[0].minEsses ? u.allDb : nan, 2) << "\t"
                << juce::String (u.allHarshDb, 2) << "\t" << juce::String (u.labelDb, 2) << "\t" << juce::String (u.labelHarshDb, 2) << "\n";
        }
        dsp::LevelHistogram::Counts pe {}, pv {};
        std::uint32_t ne = 0, nv = 0;
        std::vector<float> tokens, vowels;
        for (const auto& u : results[c][0])
        {
            vowels.insert (vowels.end(), u.vowelHarshDb.begin(), u.vowelHarshDb.end());
            for (size_t b = 0; b < pe.size(); ++b)
            {
                pe[b] += u.essCounts[b];
                pv[b] += u.voicedCounts[b];
            }
            ne += u.numEsses;
            nv += u.numVoiced;
            tokens.insert (tokens.end(), u.tokenDb.begin(), u.tokenDb.end());
        }
        const auto hop = results[c][0].empty() ? 0.0 : results[c][0].front().hopSeconds;
        const auto pooled = dsp::learnThresholds (pe, ne, pv, nv, o.settings.harshOn, hop, configs[0]);
        std::fprintf (out, "| %s | %zu | %s / %s / %s | %s | %s (%s) | %s / %s / %s | %s | %s (%s) |\n", data[c].name.c_str(), results[c][0].size(),
                      cell (percentileOf (t, 25.0), "%+.2f").c_str(), cell (percentileOf (t, 50.0), "%+.2f").c_str(), cell (percentileOf (t, 75.0), "%+.2f").c_str(),
                      cell (percentileOf (l, 50.0), "%+.2f").c_str(), cell (pooled.essSet ? pooled.essDb : nan, "%+.2f").c_str(),
                      cell (ruleOf (tokens, configs[0]), "%+.2f").c_str(), cell (percentileOf (h, 25.0), "%+.2f").c_str(),
                      cell (percentileOf (h, 50.0), "%+.2f").c_str(), cell (percentileOf (h, 75.0), "%+.2f").c_str(), cell (percentileOf (hl, 50.0), "%+.2f").c_str(),
                      cell (pooled.harshSet ? pooled.harshDb : nan, "%+.2f").c_str(),
                      cell (percentileOf (vowels, 100.0 * (1.0 - configs[0].harshShare)), "%+.2f").c_str());
    }
    if (! o.thresholdsFile.empty())
        juce::File (o.thresholdsFile).replaceWithText (tsv);
    return true;
}

} // namespace halite::learning
