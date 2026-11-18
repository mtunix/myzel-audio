// Rows, the model specifications, the primitives, and the mechanical gates: identity, stereo,
// clicks (PLAN.md, Measurement and quality gates; dynamics/NOTES.md, Gate table).

#include "support/GateKit.h"

#include "common/dsp/FloatCompare.h"
#include "dsp/chain/BasaltChain.h"
#include "dsp/chain/Engines.h"
#include "support/Corpus.h"
#include "support/Reference.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <thread>

namespace basalt::measure
{

//==============================================================================
namespace kit
{
    std::vector<Row> runJobs (const std::vector<Job>& jobs, int threads)
    {
        std::vector<std::vector<Row>> results (jobs.size());
        std::atomic<size_t> next { 0 };
        const auto work = [&]
        {
            for (auto i = next++; i < jobs.size(); i = next++)
            {
                try
                {
                    results[i] = jobs[i]();
                }
                catch (const std::exception& e)
                {
                    results[i] = { gated ("error", "error", e.what(), std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0, "") };
                }
            }
        };

        std::vector<std::thread> pool;
        for (int t = 1; t < std::min<int> (threads, (int) jobs.size()); ++t)
            pool.emplace_back (work);
        work();
        for (auto& t : pool)
            t.join();

        std::vector<Row> rows;
        for (auto& r : results)
            rows.insert (rows.end(), std::make_move_iterator (r.begin()), std::make_move_iterator (r.end()));
        return rows;
    }

    std::string format (const char* text, ...)
    {
        char buffer[1024];
        va_list args;
        va_start (args, text);
        std::vsnprintf (buffer, sizeof (buffer), text, args);
        va_end (args);
        return buffer;
    }

    Row gated (const std::string& gate, const std::string& group, const std::string& item, double measured, double target,
               double low, double high, const std::string& unit, const std::string& note)
    {
        Row r { gate, group, item, measured, target, low, high, unit, Verdict::fail, note };
        if (std::isfinite (measured) && measured >= low && measured <= high)
            r.verdict = Verdict::pass;
        return r;
    }

    Row reported (const std::string& gate, const std::string& group, const std::string& item, double measured, double target,
                  const std::string& unit, const std::string& note)
    {
        Row r;
        r.gate = gate;
        r.group = group;
        r.item = item;
        r.measured = measured;
        r.target = target;
        r.unit = unit;
        r.note = note;
        r.verdict = Verdict::reported;
        return r;
    }

    Row unspecified (const std::string& gate, const std::string& group, const std::string& item, const std::string& note)
    {
        auto r = reported (gate, group, item, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), "", note);
        r.verdict = Verdict::notSpecified;
        return r;
    }

    bool distinctEngines (Model a, Model b)
    {
        return basalt::dsp::engineFor (a, VcaDetector::peak) != basalt::dsp::engineFor (b, VcaDetector::peak);
    }

    bool distinctDetectors()
    {
        return basalt::dsp::engineFor (Model::vca, VcaDetector::peak) != basalt::dsp::engineFor (Model::vca, VcaDetector::rms);
    }

    std::vector<Model> offeredModels()
    {
        std::vector<Model> m;
        for (int i = 0; i < numOfferedModels; ++i)
            m.push_back (static_cast<Model> (i));
        return m;
    }

    double mean (const std::vector<double>& x, int from, int to)
    {
        if (from < 0 || to > (int) x.size() || from >= to)
            return std::numeric_limits<double>::quiet_NaN();
        auto sum = 0.0;
        for (int i = from; i < to; ++i)
            sum += x[(size_t) i];
        return sum / (to - from);
    }

    double percentile (std::vector<double> v, double p)
    {
        if (v.empty())
            return std::numeric_limits<double>::quiet_NaN();
        std::sort (v.begin(), v.end());
        const auto at = std::clamp (p / 100.0, 0.0, 1.0) * (double) (v.size() - 1);
        const auto i = (size_t) at;
        return i + 1 < v.size() ? v[i] + (at - (double) i) * (v[i + 1] - v[i]) : v[i];
    }

    Settings measurementBase (Model m)
    {
        Settings s;
        s.model = m;
        s.autoRelease = false;
        s.releaseMs = 1000.0;
        s.kneeDb = 0.0;
        s.rangeDb = 40.0;
        s.lookaheadMs = 0.0;
        return s;
    }
}

using namespace kit;

const char* verdictName (Verdict v)
{
    switch (v)
    {
        case Verdict::pass: return "pass";
        case Verdict::fail: return "FAIL";
        case Verdict::reported: return "reported";
        case Verdict::notSpecified: return "not specified";
    }
    return "";
}

bool passed (const std::vector<Row>& rows)
{
    return std::none_of (rows.begin(), rows.end(), [] (const Row& r) { return r.verdict == Verdict::fail; });
}

std::string boundsText (const Row& r)
{
    const auto lowSet = std::isfinite (r.low), highSet = std::isfinite (r.high);
    if (r.verdict == Verdict::reported || r.verdict == Verdict::notSpecified || (! lowSet && ! highSet))
        return "";
    if (lowSet && highSet)
        return myzel::dsp::isExactly (r.low, r.high) ? format ("= %.6g", r.low) : format ("[%.6g, %.6g]", r.low, r.high);
    return lowSet ? format (">= %.6g", r.low) : format ("<= %.6g", r.high);
}

std::string describe (const Row& r)
{
    auto text = format ("%s | %s | %s: %.6g %s", r.gate.c_str(), r.group.c_str(), r.item.c_str(), r.measured, r.unit.c_str());
    if (std::isfinite (r.target))
        text += format (" (target %.6g)", r.target);
    if (const auto b = boundsText (r); ! b.empty())
        text += " " + b;
    text += std::string (" ") + verdictName (r.verdict);
    if (! r.note.empty())
        text += "; " + r.note;
    return text;
}

//==============================================================================
double crossingSeconds (const std::vector<double>& r, int n0, double target, bool rising, double rate)
{
    if (n0 < 1 || n0 > (int) r.size())
        return std::numeric_limits<double>::quiet_NaN();
    auto previousT = 0.0, previousR = r[(size_t) n0 - 1];
    if (! std::isfinite (previousR))
        return std::numeric_limits<double>::quiet_NaN();
    for (int j = 0; n0 + j < (int) r.size(); ++j)
    {
        const auto t = (j + 1) / rate, v = r[(size_t) (n0 + j)];
        if (! std::isfinite (v))
            return std::numeric_limits<double>::quiet_NaN();
        if (rising ? v >= target : v <= target)
            return myzel::dsp::isExactly (v, previousR) ? t : previousT + (target - previousR) / (v - previousR) * (t - previousT);
        previousT = t;
        previousR = v;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double clickCrestDb (const Samples& x, int from, int to)
{
    to = std::min (to, (int) x.size());
    auto peak = 0.0, sum = 0.0;
    for (int i = std::max (from, 3); i < to; ++i)
    {
        const auto d = (double) x[(size_t) i] - 3.0 * x[(size_t) i - 1] + 3.0 * x[(size_t) i - 2] - (double) x[(size_t) i - 3];
        peak = std::max (peak, std::abs (d));
    }
    for (int i = from; i < to; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];
    const auto rms = std::sqrt (sum / std::max (1, to - from));
    return 20.0 * std::log10 (std::max (peak, 1.0e-30) / std::max (rms, 1.0e-30));
}

long delayedMismatches (const Samples& out, const Samples& in, int delay, long* first)
{
    long count = 0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto expected = (long) i >= delay && i - (size_t) delay < in.size() ? in[i - (size_t) delay] : 0.0f;
        if (std::memcmp (&expected, &out[i], sizeof (float)) != 0)
        {
            if (count == 0 && first != nullptr)
                *first = (long) i;
            ++count;
        }
    }
    return count;
}

std::string dumpPath (const Options& o, const std::string& gate, const std::string& group, const std::string& item)
{
    if (o.dumpDir.empty())
        return {};
    auto name = gate + "_" + group + "_" + item;
    for (auto& c : name)
        if (! std::isalnum ((unsigned char) c) && c != '.' && c != '-')
            c = '_';
    return o.dumpDir + "/" + name;
}

double gainDifferenceDb (float a, float b)
{
    if (! std::isfinite (a) || ! std::isfinite (b) || a <= 0.0f || b <= 0.0f)
        return std::numeric_limits<double>::infinity();
    int32_t ia, ib;
    std::memcpy (&ia, &a, sizeof (a));
    std::memcpy (&ib, &b, sizeof (b));
    if (std::abs ((long long) ia - ib) <= 1)
        return 0.0;
    return std::abs (20.0 * std::log10 ((double) a / (double) b));
}

//==============================================================================
// Identity (PLAN.md; dynamics/NOTES.md, Gate table: every stereo mode, Look-ahead 0, 5, 10 ms,
// Normal and Live, block sizes 1 … 4096 and variable, key Internal and External).
namespace
{
    /**
        A setting that leaves the gain exactly 1, the noise that plays through it (main, and the
        key when External) and from when the output is compared: Upward over its knee is compared
        once its RMS has read the level (upward/NOTES.md, Gates: Onsets from silence).
    */
    struct IdentityCondition
    {
        std::string name;
        std::function<void (Settings&)> set;
        double quiet = 0.0;         // the detector's signal: 0 plays the loud noise
        bool gaps = true;           // digital silence at the start and in the middle
        double seconds = 0.3, compareFromSeconds = 0.0;
    };

    constexpr double loudNoise = 0.9;

    std::vector<IdentityCondition> identityConditions (Model model)
    {
        const auto& spec = specOf (model);
        std::vector<IdentityCondition> c {
            { "Ratio 1", [] (Settings& s) { s.ratio = 1.0; s.thresholdDb = -60.0; s.kneeDb = 0.0; } },
            { "Range 0", [] (Settings& s) { s.rangeDb = 0.0; s.thresholdDb = -60.0; s.ratio = infiniteRatio; s.kneeDb = 0.0; } },
        };
        // The noise sits under the model's onset with Threshold 0, Knee 24, ∞:1: T − W/2 (VCA's
        // T_eff − W/2, FET All's T − W/2, Vari-mu's T − 13), the Opto's T − 6w.
        if (spec.identityUnderOnset)
            c.push_back ({ "under the onset (T 0, Knee 24, inf:1)", [] (Settings& s) { s.thresholdDb = 0.0; s.kneeDb = 24.0; s.ratio = infiniteRatio; },
                           spec.identityQuiet });
        if (spec.lifts)
        {
            // −80 dBFS reads −81.8 dB on the RMS: under the default Floor, −70.
            c.push_back ({ "under Floor (T -18, Floor -70)", [] (Settings&) {}, 1.0e-4 });
            c.push_back ({ "over Threshold + Knee/2 (T -30), from 300 ms", [] (Settings& s) { s.thresholdDb = -30.0; }, 0.0, false, 0.6, 0.3 });
        }
        return c;
    }

    /** Noise, with digital silence at the start and, with gaps, in the middle. */
    Samples identityNoise (int n, double amplitude, unsigned seed, bool gaps)
    {
        auto x = whiteNoise (n, amplitude, seed);
        std::fill (x.begin(), x.begin() + n / 10, 0.0f);
        if (gaps)
            std::fill (x.begin() + (long) (0.45 * n), x.begin() + (long) (0.55 * n), 0.0f);
        return x;
    }

    Row identityCase (const Options& o, Model model, LatencyMode latency, const IdentityCondition& c, StereoMode stereo, double lookaheadMs,
                      KeySource key, const std::vector<int>& blocks, const std::string& blocksName, int mainChannels, unsigned seed)
    {
        Settings s;
        s.model = model;
        s.latency = latency;
        s.stereo = stereo;
        s.lookaheadMs = lookaheadMs;
        s.key = key;
        c.set (s);

        const auto n = samplesFor (c.seconds, o.rate);
        const auto external = key == KeySource::external;
        const auto detected = c.quiet > 0.0 ? c.quiet : loudNoise;
        Input in;
        for (int ch = 0; ch < mainChannels; ++ch)
            in.main.push_back (identityNoise (n, external ? loudNoise : detected, seed + (unsigned) ch, c.gaps));
        if (external)
            for (int ch = 0; ch < 2; ++ch)
                in.key.push_back (identityNoise (n, detected, seed + 10u + (unsigned) ch, c.gaps));

        auto device = makeDevice (o.device);
        const auto out = render (*device, s, in, { o.rate, blocks, {}, {} });
        const auto from = samplesFor (c.compareFromSeconds, o.rate) + out.latency;
        long mismatches = 0, first = -1;
        for (int ch = 0; ch < mainChannels; ++ch)
        {
            auto compared = out.main[(size_t) ch];
            auto expected = in.main[(size_t) ch];
            if (from > out.latency)
            {
                // Compared from `from` on: the samples before are made equal, so they count as none.
                for (int i = 0; i < from && i < (int) compared.size(); ++i)
                    compared[(size_t) i] = i >= out.latency ? expected[(size_t) (i - out.latency)] : 0.0f;
            }
            long f = -1;
            mismatches += delayedMismatches (compared, expected, out.latency, &f);
            if (first < 0)
                first = f;
        }

        const auto item = format ("%s, %s, look-ahead %g ms, key %s, %s, blocks %s", c.name.c_str(), mainChannels == 1 ? "mono" : stereoName (stereo),
                                  lookaheadMs, external ? "External" : "Internal", latencyName (latency), blocksName.c_str());
        const auto group = format ("%s, %s", modelKey (model), latencyName (latency));
        const auto note = mismatches == 0 ? format ("latency %d", out.latency) : format ("latency %d, first at output sample %ld", out.latency, first);

        // A slot whose engine this build does not have runs Clean's: its row repeats Clean's.
        if (model != Model::clean && ! distinctEngines (model, Model::clean))
            return reported ("identity", group, item, (double) mismatches, 0.0, "samples",
                             "not exercised: runs Clean's engine in this build (" + std::string (isOffered (model) ? "M2" : "1.1") + " builds its own); " + note);
        return gated ("identity", group, item, (double) mismatches, 0.0, 0.0, 0.0, "samples", note);
    }
}

std::vector<Row> identity (const Options& o)
{
    std::vector<Model> models;
    if (o.model)
        models = { *o.model };
    else
        for (int i = 0; i < numModels; ++i)
            models.push_back (static_cast<Model> (i));

    const std::vector<int> sizes = o.quick ? std::vector<int> { 1, 3, 64, 441, 4096 }
                                           : std::vector<int> { 1, 2, 3, 7, 16, 61, 64, 128, 256, 441, 480, 512, 1000, 1024, 2048, 4096 };
    const std::vector<double> lookaheads = o.quick ? std::vector<double> { 0.0, 10.0 } : std::vector<double> { 0.0, 5.0, 10.0 };

    std::vector<Job> jobs;
    for (const auto model : models)
        for (const auto latency : o.latencies)
            for (const auto detector : model == Model::vca ? o.detectors : std::vector<VcaDetector> { VcaDetector::peak })
            {
                const auto conditions = identityConditions (model);
                const auto withDetector = [detector] (IdentityCondition c)
                {
                    const auto set = c.set;
                    c.set = [set, detector] (Settings& s) { set (s); s.vcaDetector = detector; };
                    if (detector == VcaDetector::rms)
                        c.name += " (RMS)";
                    return c;
                };

                jobs.push_back ([=, &o]
                {
                    std::vector<Row> rows;
                    unsigned seed = 1;
                    for (const auto size : sizes)
                        rows.push_back (identityCase (o, model, latency, withDetector (conditions[0]), StereoMode::linked, 5.0, KeySource::internal, { size },
                                                      std::to_string (size), 2, seed++));
                    rows.push_back (identityCase (o, model, latency, withDetector (conditions[0]), StereoMode::linked, 5.0, KeySource::internal,
                                                  variableBlocks (seed), "variable", 2, seed));
                    return rows;
                });

                for (const auto& condition : conditions)
                    jobs.push_back ([=, &o]
                    {
                        const auto c = withDetector (condition);
                        std::vector<Row> rows;
                        unsigned seed = 100;
                        for (int st = 0; st < numStereoModes; ++st)
                            for (const auto la : lookaheads)
                                for (const auto key : { KeySource::internal, KeySource::external })
                                {
                                    ++seed;
                                    rows.push_back (identityCase (o, model, latency, c, static_cast<StereoMode> (st), la, key, variableBlocks (seed), "variable",
                                                                  2, seed));
                                }
                        for (const auto key : { KeySource::internal, KeySource::external })
                        {
                            ++seed;
                            rows.push_back (identityCase (o, model, latency, c, StereoMode::linked, 5.0, key, variableBlocks (seed), "variable", 1, seed));
                        }
                        return rows;
                    });
            }
    return runJobs (jobs, o.jobs);
}

//==============================================================================
// Stereo (dynamics/NOTES.md, Stereo and Gate table).
namespace
{
    constexpr float loudSide = 0.5f;            // −6.02 dBFS
    constexpr float quietSide = 0.0009765625f;  // 2⁻¹⁰, −60.2 dBFS: under every Threshold the stereo rows set

    struct StereoRender
    {
        Rendered out;
        Channels in;
    };

    StereoRender renderMain (const Options& o, Settings s, Channels main, double rate = 0.0)
    {
        auto device = makeDevice (o.device);
        Input in { main, {} };
        StereoRender r;
        r.out = render (*device, s, in, { rate > 0.0 ? rate : o.rate, { 512 }, {}, {} });
        r.in = std::move (main);
        return r;
    }

    struct Material
    {
        Channels audio;
        double rate;
        std::string name;
    };

    /** A stereo key: the committed overheads excerpt looped (at its own rate), or two decorrelated pink noises without the corpus. */
    Material stereoMaterial (const Options& o, double seconds)
    {
        const auto dir = o.corpusDir.empty() ? defaultCorpusDir() : o.corpusDir;
        if (const auto e = loadExcerpt (dir, "albumdb-extraordinary-overheads"); e && e->audio.size() == 2)
        {
            const auto n = samplesFor (seconds, e->rate);
            Channels c (2);
            for (int ch = 0; ch < 2; ++ch)
                while ((int) c[(size_t) ch].size() < n)
                    append (c[(size_t) ch], e->audio[(size_t) ch]);
            for (auto& ch : c)
                ch.resize ((size_t) n);
            return { c, e->rate, e->name + " looped" };
        }
        const auto n = samplesFor (seconds, o.rate);
        auto l = pink (n, o.rate, 21u, -18.0), r = pink (n, o.rate, 22u, -21.0);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t) i] *= (float) (1.0 + 0.9 * std::sin (2.0 * std::numbers::pi * 2.0 * i / o.rate));
            r[(size_t) i] *= (float) (1.0 + 0.9 * std::sin (2.0 * std::numbers::pi * 3.1 * i / o.rate + 1.0));
        }
        return { { l, r }, o.rate, "pink noise, two seeds, 2 and 3.1 Hz swells" };
    }

    long gainMismatches (const Trace& a, const Trace& b, int from = 0)
    {
        long count = 0;
        for (size_t i = (size_t) from; i < std::min (a.gain.size(), b.gain.size()); ++i)
            if (std::memcmp (&a.gain[i], &b.gain[i], sizeof (float)) != 0)
                ++count;
        return count;
    }

    /** M and S of a stereo render, out[n + latency] against in[n], in double. */
    struct MidSide
    {
        std::vector<double> midGainDb, sideGainDb;  // reduction of each, dB (NaN where the input is 0)
        double midDeviationDb = -300.0, sideDeviationDb = -300.0;   // the largest |out − in| against the input's peak, dB
    };

    MidSide midSideOf (const StereoRender& r)
    {
        MidSide ms;
        const auto latency = r.out.latency;
        const auto n = (int) r.in[0].size() - latency;
        auto midPeak = 0.0, sidePeak = 0.0, midDev = 0.0, sideDev = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto mi = 0.5 * ((double) r.in[0][(size_t) i] + r.in[1][(size_t) i]), si = 0.5 * ((double) r.in[0][(size_t) i] - r.in[1][(size_t) i]);
            const auto lo = (double) r.out.main[0][(size_t) (i + latency)], ro = (double) r.out.main[1][(size_t) (i + latency)];
            const auto mo = 0.5 * (lo + ro), so = 0.5 * (lo - ro);
            ms.midGainDb.push_back (! myzel::dsp::isExactly (mi, 0.0) ? -20.0 * std::log10 (mo / mi) : std::numeric_limits<double>::quiet_NaN());
            ms.sideGainDb.push_back (! myzel::dsp::isExactly (si, 0.0) ? -20.0 * std::log10 (so / si) : std::numeric_limits<double>::quiet_NaN());
            midPeak = std::max (midPeak, std::abs (mi));
            sidePeak = std::max (sidePeak, std::abs (si));
            midDev = std::max (midDev, std::abs (mo - mi));
            sideDev = std::max (sideDev, std::abs (so - si));
        }
        ms.midDeviationDb = 20.0 * std::log10 (std::max (midDev, 1.0e-15) / std::max (midPeak, 1.0e-15));
        ms.sideDeviationDb = 20.0 * std::log10 (std::max (sideDev, 1.0e-15) / std::max (sidePeak, 1.0e-15));
        return ms;
    }
}

std::vector<Row> stereo (const Options& o)
{
    const auto model = o.modelOrClean();
    const auto& spec = specOf (model);
    std::vector<Job> jobs;
    for (const auto detector : detectorsFor (o, model))
    {
        const auto group = modelLabel (model, detector);
        const auto n = samplesFor (1.0, o.rate);
        const auto tail = samplesFor (0.25, o.rate);    // the steady part read at the end
        const auto powerSum = spec.detectorSwitch && detector == VcaDetector::rms;

        Settings base;
        base.model = model;
        base.vcaDetector = detector;
        base.thresholdDb = -30.0;
        base.floorDb = -90.0;

        // Upward lifts what sits under Threshold: its Mid and Side rows play 36 dB lower, in the lift region.
        const auto scale = spec.lifts ? 1.0 / 64.0 : 1.0;

        /** The specified steady reduction of a carrier of this amplitude. */
        const auto expectedOf = [spec] (const Settings& s, double amplitude) { return spec.steadyReduction (s, Signal::carrier, dbOf (amplitude)); };

        /**
            What Link takes from a pair: the louder channel's reduction (eq. 4 is monotonic), the
            louder channel's lift for Upward (dynamics/NOTES.md, Stereo; upward/NOTES.md), the
            power sum's reduction for VCA RMS (vca/NOTES.md, 1. Detector).
        */
        const auto linkedOf = [spec, powerSum] (const Settings& s, double loud, double quiet)
        {
            if (powerSum)
                return spec.steadyReduction (s, Signal::carrier, dbOf (std::sqrt (loud * loud + quiet * quiet)) - carrierRmsOffsetDb);
            return std::max (spec.steadyReduction (s, Signal::carrier, dbOf (loud)), spec.steadyReduction (s, Signal::carrier, dbOf (quiet)));
        };

        jobs.push_back ([=, &o]
        {
            std::vector<Row> rows;
            for (const auto mode : { StereoMode::linked, StereoMode::unlinked })
            {
                auto s = base;
                s.stereo = mode;
                const auto r = renderMain (o, s, { carrier (n, loudSide), carrier (n, quietSide) });
                const auto left = traceOf (r.in[0], r.out.main[0], r.out.latency), right = traceOf (r.in[1], r.out.main[1], r.out.latency);
                const auto end = (int) left.reductionDb.size();
                if (mode == StereoMode::linked)
                {
                    rows.push_back (gated ("stereo", group, "Linked, Link 100 %: a one-sided signal gives both channels the same gain, bit for bit",
                                           (double) gainMismatches (left, right), 0.0, 0.0, 0.0, "samples"));
                    const auto expected = linkedOf (r.out.applied, loudSide, quietSide);
                    rows.push_back (gated ("stereo", group, "Linked, Link 100 %: the quiet channel takes the linked steady reduction",
                                           mean (right.reductionDb, end - tail, end), expected, expected - 0.25, expected + 0.25, "dB",
                                           powerSum ? "the power sum's" : (spec.lifts ? "the louder channel's lift" : "the louder channel's")));
                    continue;
                }

                // Unlinked: the channel with nothing to do (the quiet one, or for Upward the loud one over Threshold) is left exactly as it was.
                const auto loudExpected = expectedOf (r.out.applied, loudSide), quietExpected = expectedOf (r.out.applied, quietSide);
                // Under 10⁻⁶ dB is the core's snap to exactly 0 (Vari-mu's rise term is that small under T − 13 dB).
                const auto idleIsQuiet = std::abs (quietExpected) < 1.0e-6;
                const auto& idle = idleIsQuiet ? right : left;
                const auto& busy = idleIsQuiet ? left : right;
                const auto busyExpected = idleIsQuiet ? loudExpected : quietExpected;

                // Upward's RMS rises through the lift region at the onset (upward/NOTES.md, Onsets from silence): compared from 0.3 s.
                long touched = 0;
                for (size_t i = spec.lifts ? (size_t) samplesFor (0.3, o.rate) : 0; i < idle.gain.size(); ++i)
                    touched += myzel::dsp::isExactly (idle.gain[i], 1.0) ? 0 : 1;
                rows.push_back (gated ("stereo", group, std::string ("Unlinked: the ") + (idleIsQuiet ? "quiet" : "loud") + " channel, with nothing to do, is left exactly as it was"
                                           + (spec.lifts ? " (from 0.3 s)" : ""),
                                       (double) touched, 0.0, 0.0, 0.0, "samples"));
                rows.push_back (gated ("stereo", group, std::string ("Unlinked: the ") + (idleIsQuiet ? "loud" : "quiet") + " channel's own steady reduction",
                                       mean (busy.reductionDb, end - tail, end), busyExpected, busyExpected - 0.25, busyExpected + 0.25, "dB"));
            }

            auto s = base;
            s.linkPercent = 50.0;
            const auto r = renderMain (o, s, { carrier (n, loudSide), carrier (n, quietSide) });
            const auto left = traceOf (r.in[0], r.out.main[0], r.out.latency), right = traceOf (r.in[1], r.out.main[1], r.out.latency);
            const auto end = (int) left.reductionDb.size();
            // The Opto links the levels (opto/NOTES.md, 1. Detector): the quiet channel's cell reads the mean of the two in dB.
            const auto linked = linkedOf (r.out.applied, loudSide, quietSide);
            const auto quietExpected = spec.linksLevels ? expectedOf (r.out.applied, std::sqrt (loudSide * quietSide))
                                                        : 0.5 * expectedOf (r.out.applied, quietSide) + 0.5 * linked;
            const auto loudExpected = spec.linksLevels ? expectedOf (r.out.applied, loudSide) : 0.5 * expectedOf (r.out.applied, loudSide) + 0.5 * linked;
            const auto what = spec.linksLevels ? std::string ("the cell at the mean of the two levels") : std::string ("half its own and half the linked reduction");
            rows.push_back (gated ("stereo", group, "Linked, Link 50 %: the quiet channel, " + what, mean (right.reductionDb, end - tail, end), quietExpected,
                                   quietExpected - 0.05, quietExpected + 0.05, "dB"));
            rows.push_back (gated ("stereo", group, std::string ("Linked, Link 50 %: the loud channel, ") + (spec.linksLevels ? "its own level" : "half its own and half the linked reduction"),
                                   mean (left.reductionDb, end - tail, end), loudExpected, loudExpected - 0.25, loudExpected + 0.25, "dB"));
            return rows;
        });

        jobs.push_back ([=, &o]
        {
            std::vector<Row> rows;
            const auto material = stereoMaterial (o, 10.0);
            const auto key = renderKey (o.device, base, material.audio, Probe::external, { material.rate, { 512 }, {}, {} });
            const auto mismatches = gainMismatches (key.traces[0], key.traces[1]);
            rows.push_back (gated ("stereo", group, "Linked, Link 100 %: g_L and g_R bit-identical over 10 s of a stereo key from reset",
                                   (double) mismatches, 0.0, 0.0, 0.0, "samples",
                                   format ("%s; mean reduction %.2f dB", material.name.c_str(),
                                           mean (key.traces[0].reductionDb, 0, (int) key.traces[0].reductionDb.size()))));

            const auto five = samplesFor (5.0, material.rate);
            Channels excerpt { Samples (material.audio[0].begin(), material.audio[0].begin() + five),
                               Samples (material.audio[1].begin(), material.audio[1].begin() + five) };
            // dynamics/NOTES.md, Implementation (M1): a linked pair steers its ballistics from one
            // detector only while Link > 0, so Link 0 % is Unlinked with Auto release on too.
            for (const auto autoRelease : { false, true })
            {
                auto linked = base, unlinked = base;
                linked.linkPercent = 0.0;
                unlinked.stereo = StereoMode::unlinked;
                linked.autoRelease = unlinked.autoRelease = autoRelease;
                const auto a = renderMain (o, linked, excerpt, material.rate), b = renderMain (o, unlinked, excerpt, material.rate);
                long differ = 0;
                for (int ch = 0; ch < 2; ++ch)
                    differ += delayedMismatches (a.out.main[(size_t) ch], b.out.main[(size_t) ch], 0);
                rows.push_back (gated ("stereo", group, format ("Link 0 %% equals Unlinked, output bit for bit (5 s of the stereo key), Auto release %s", autoRelease ? "on" : "off"),
                                       (double) differ, 0.0, 0.0, 0.0, "samples"));
            }
            return rows;
        });

        jobs.push_back ([=, &o]
        {
            std::vector<Row> rows;

            // Mid: M = 0.375 over Threshold, S = 0.125 untouched.
            {
                auto s = base;
                s.stereo = StereoMode::mid;
                const auto r = renderMain (o, s, { carrier (n, 0.5 * scale), carrier (n, 0.25 * scale) });
                const auto ms = midSideOf (r);
                const auto end = (int) ms.midGainDb.size();
                const auto expected = expectedOf (r.out.applied, 0.375 * scale);
                rows.push_back (gated ("stereo", group, "Mid: the mid's steady reduction, detected on the mid", mean (ms.midGainDb, end - tail, end), expected,
                                       expected - 0.25, expected + 0.25, "dB"));
                rows.push_back (gated ("stereo", group, "Mid: the side passes untouched (largest deviation against its peak)", ms.sideDeviationDb, std::numeric_limits<double>::quiet_NaN(),
                                       -std::numeric_limits<double>::infinity(), -120.0, "dB"));
            }
            // Side: S = 0.375 over Threshold, M = 0.125 untouched.
            {
                auto s = base;
                s.stereo = StereoMode::side;
                const auto r = renderMain (o, s, { carrier (n, 0.5 * scale), carrier (n, -0.25 * scale) });
                const auto ms = midSideOf (r);
                const auto end = (int) ms.sideGainDb.size();
                const auto expected = expectedOf (r.out.applied, 0.375 * scale);
                rows.push_back (gated ("stereo", group, "Side: the side's steady reduction, detected on the side", mean (ms.sideGainDb, end - tail, end), expected,
                                       expected - 0.25, expected + 0.25, "dB"));
                rows.push_back (gated ("stereo", group, "Side: the mid passes untouched (largest deviation against its peak)", ms.midDeviationDb, std::numeric_limits<double>::quiet_NaN(),
                                       -std::numeric_limits<double>::infinity(), -120.0, "dB"));
            }
            // Mid/Side: both over a Threshold of −20, each with its own reduction.
            {
                auto s = base;
                s.stereo = StereoMode::midSide;
                s.thresholdDb = -20.0;
                const auto r = renderMain (o, s, { carrier (n, 0.5 * scale), carrier (n, -0.25 * scale) });
                const auto ms = midSideOf (r);
                const auto end = (int) ms.midGainDb.size();
                const auto mid = expectedOf (r.out.applied, 0.125 * scale), side = expectedOf (r.out.applied, 0.375 * scale);
                rows.push_back (gated ("stereo", group, "Mid/Side: the mid's own steady reduction", mean (ms.midGainDb, end - tail, end), mid, mid - 0.25, mid + 0.25, "dB"));
                rows.push_back (gated ("stereo", group, "Mid/Side: the side's own steady reduction", mean (ms.sideGainDb, end - tail, end), side, side - 0.25,
                                       side + 0.25, "dB"));
            }
            // A loud signal on the undetected half alone: nothing to do, so the output is the input.
            // Upward's Floor sits over the detected half's −72 dBFS, so it has nothing to lift either.
            const std::pair<StereoMode, Channels> idle[] {
                { StereoMode::mid, { carrier (n, 0.25 + 1.0 / 4096.0), carrier (n, -0.25 + 1.0 / 4096.0) } },
                { StereoMode::side, { carrier (n, 0.25 + 1.0 / 4096.0), carrier (n, 0.25 - 1.0 / 4096.0) } },
            };
            for (const auto& [mode, signal] : idle)
            {
                auto s = base;
                s.stereo = mode;
                s.floorDb = -60.0;
                const auto r = renderMain (o, s, signal);
                long differ = 0;
                for (int ch = 0; ch < 2; ++ch)
                    differ += delayedMismatches (r.out.main[(size_t) ch], r.in[(size_t) ch], r.out.latency);
                rows.push_back (gated ("stereo", group, format ("%s: a loud %s alone is not compressed; output bit-identical", stereoName (mode),
                                                                mode == StereoMode::mid ? "side" : "mid"),
                                       (double) differ, 0.0, 0.0, 0.0, "samples"));
            }
            // The M/S round trip at idle (Ratio 1, loud uncorrelated noise).
            for (const auto mode : { StereoMode::mid, StereoMode::side, StereoMode::midSide })
            {
                auto s = base;
                s.stereo = mode;
                s.ratio = 1.0;
                const auto r = renderMain (o, s, { whiteNoise (n, 0.9, 31u), whiteNoise (n, 0.7, 32u) });
                long differ = 0;
                for (int ch = 0; ch < 2; ++ch)
                    differ += delayedMismatches (r.out.main[(size_t) ch], r.in[(size_t) ch], r.out.latency);
                rows.push_back (gated ("stereo", group, format ("%s: the M/S round trip is exact at idle (Ratio 1)", stereoName (mode)), (double) differ,
                                       0.0, 0.0, 0.0, "samples"));
            }
            return rows;
        });
    }
    return runJobs (jobs, o.jobs);
}

//==============================================================================
// Clicks (PLAN.md: every knob min ↔ max, model and VCA detector switches, Character, latency,
// bypass; Halite's method: the 3rd-difference crest of low sines at or under −60 dB). The main
// input is a pair of 330 Hz sines; the key, on the side chain, is chosen per knob so the knob can
// reach the gain: two channels at different levels (Link, the stereo modes), a level inside the
// knee, tones inside the key filters' and the bell's bands, and a gated key whose releases the
// time knobs move in. Every change shows in the sines and nothing else does.
namespace
{
    constexpr double clickSeconds = 3.0;
    constexpr double clickSettle = 0.3;
    constexpr double clickLimitDb = -60.0;
    constexpr double clickKeyDb = -4.0;         // about 10.5 dB of reduction at the defaults
    constexpr double clickQuietKeyDb = -30.0;   // the other channel: under every default knee

    /**
        The keys' levels for a model: a compressor's loud channel reduced about 10 dB and its quiet
        one under the knee; Upward's in its lift region instead, about 10 dB of lift on the louder
        (a reading of −31.3 dB at the defaults) and the Range-capped lift 26 dB under it, and its
        gated key moves between over Threshold and that lift, so the time knobs move while the lift
        comes in.
    */
    struct ClickLevels
    {
        double loud = clickKeyDb, quiet = clickQuietKeyDb;
        double gatedOff = -300.0;   // dBFS of the gated key's off phase; under −150: digital silence
    };

    ClickLevels clickLevels (Model model)
    {
        if (! specOf (model).lifts)
            return {};
        constexpr double lifted = -31.3 - carrierRmsOffsetDb;
        return { lifted, lifted - 26.0, lifted };
    }
    constexpr double gatedPeriod = 0.3, gatedOn = 0.15;     // s: the gated key's cycle and how long it sounds
    constexpr double gatedToggleAfter = 0.01;   // the knob moves this long after each release starts
    constexpr double gatedBefore = 0.005, gatedAfter = 0.04;    // the window measured around each move

    enum class ClickKey
    {
        split,      // a carrier at −4 dBFS left and −30 dBFS right
        knee,       // a carrier at Threshold left (inside a 24 dB knee, outside a hard one), −30 dBFS right
        tone,       // sines at toneHz, −4 dBFS peak left and −30 dBFS right: inside the key EQ's bands
        gated,      // the split carrier, on for 150 ms in every 300 ms
        sines       // the 330 Hz test sines themselves (an internal key)
    };

    struct Scenario
    {
        std::string name;
        std::function<void (int toggle, Settings&)> set;    // toggle counts the changes so far
        double period = 0.31;
        ClickKey key = ClickKey::split;
        double toneHz = 1000.0;
        bool keyInput = false;      // moves only the detector's input: at Attack 0.02 ms the gain follows its key by design, so reported
        bool longHold = false;      // a sine reaches the detector: held with Release 5 s, Auto off (clickCase)
        bool referenceModels = false;
        bool gate = true;
        std::string notExercised = {};  // why this build cannot exercise it: reported with the reason
    };

    std::vector<Scenario> scenarios (Model model, VcaDetector detector)
    {
        std::vector<Scenario> list;
        const auto add = [&list] (std::string name, std::function<void (int, Settings&)> set, double period = 0.31, ClickKey key = ClickKey::split)
        {
            list.push_back ({ std::move (name), std::move (set), period, key });
            return &list.back();
        };
        const auto tone = [&add] (std::string name, std::function<void (int, Settings&)> set, double period, double hz)
        {
            auto* s = add (std::move (name), std::move (set), period, ClickKey::tone);
            s->toneHz = hz;
            s->keyInput = true;
            return s;
        };

        add ("Threshold -60 <-> 0 dB", [] (int k, Settings& s) { s.thresholdDb = k % 2 == 0 ? -60.0 : 0.0; })->referenceModels = true;
        add ("Ratio 1 <-> inf", [] (int k, Settings& s) { s.ratio = k % 2 == 0 ? 1.0 : infiniteRatio; }, 0.37)->referenceModels = true;
        add ("Knee 0 <-> 24 dB (key at Threshold)", [] (int k, Settings& s) { s.kneeDb = k % 2 == 0 ? 0.0 : 24.0; }, 0.29, ClickKey::knee)->referenceModels = true;
        add ("Attack 0.02 <-> 300 ms", [] (int k, Settings& s) { s.attackMs = k % 2 == 0 ? 0.02 : 300.0; }, 0.41)->referenceModels = true;
        add ("Release 5 <-> 5000 ms (in the releases)", [] (int k, Settings& s) { s.releaseMs = k % 2 == 0 ? 5.0 : 5000.0; }, 0.0, ClickKey::gated)
            ->referenceModels = true;
        add ("Auto release on <-> off (in the releases)", [] (int k, Settings& s) { s.autoRelease = k % 2 == 0; }, 0.0, ClickKey::gated)
            ->referenceModels = true;
        add ("Range 0 <-> 40 dB", [] (int k, Settings& s) { s.rangeDb = k % 2 == 0 ? 0.0 : 40.0; }, 0.35)->referenceModels = true;
        auto* floor = add ("Floor -90 <-> -30 dB", [] (int k, Settings& s) { s.floorDb = k % 2 == 0 ? -90.0 : -30.0; });
        if (runningModel (model) != Model::upward)
            floor->notExercised = "Floor acts on Upward alone (dimmed in this model)";
        else if (! distinctEngines (Model::upward, Model::clean))
            floor->notExercised = "Floor acts on Upward alone, which runs Clean's engine in this build";
        if (runningModel (model) == Model::fet)
            add ("Ratio 20 <-> inf (All: threshold, lag)", [] (int k, Settings& s) { s.ratio = k % 2 == 0 ? 20.0 : infiniteRatio; }, 0.37);
        add ("Gain -24 <-> +24 dB", [] (int k, Settings& s) { s.gainDb = k % 2 == 0 ? -24.0 : 24.0; }, 0.37);
        add ("Auto Gain off <-> on", [] (int k, Settings& s) { s.autoGain = k % 2 == 1; }, 0.29);
        add ("Mix 0 <-> 100 %", [] (int k, Settings& s) { s.mixPercent = k % 2 == 0 ? 0.0 : 100.0; }, 0.41);
        const auto characterFrom = list.size();
        add ("Character Off, Warm, Iron, Edge", [] (int k, Settings& s) { s.character = static_cast<CharacterMode> (k % numCharacterModes); }, 0.29);
        add ("Drive 0 <-> 24 dB (Warm)", [] (int k, Settings& s) { s.character = CharacterMode::warm; s.driveDb = k % 2 == 0 ? 0.0 : 24.0; }, 0.37);
        add ("Placement Pre <-> Post (Warm)", [] (int k, Settings& s) { s.character = CharacterMode::warm; s.charPost = k % 2 == 1; }, 0.33);
        add ("Drive Link off <-> on (Warm)", [] (int k, Settings& s) { s.character = CharacterMode::warm; s.driveLink = k % 2 == 1; }, 0.35);
        if (! characterBuilt)
            for (auto i = characterFrom; i < list.size(); ++i)
                list[i].notExercised = "Character is M3: this build runs every Character setting as Off";
        add ("Target 1 <-> 12 dB", [] (int k, Settings& s) { s.targetDb = k % 2 == 0 ? 1.0 : 12.0; })->notExercised =
            "Target only steers Learn (M3), never the audio";
        add ("Look-ahead 0 <-> 10 ms", [] (int k, Settings& s) { s.lookaheadMs = k % 2 == 0 ? 0.0 : 10.0; }, 0.37);
        add ("Stereo through all five", [] (int k, Settings& s) { s.stereo = static_cast<StereoMode> (k % numStereoModes); }, 0.29);
        add ("Link 0 <-> 100 % (channels 26 dB apart)", [] (int k, Settings& s) { s.linkPercent = k % 2 == 0 ? 0.0 : 100.0; }, 0.33);
        add ("Key Internal <-> External", [] (int k, Settings& s) { s.key = k % 2 == 0 ? KeySource::internal : KeySource::external; }, 0.41)->longHold = true;
        tone ("Key HPF Off <-> 2 kHz (1 kHz key)", [] (int k, Settings& s) { s.keyHpfHz = k % 2 == 0 ? 20.0 : 2000.0; }, 0.35, 1000.0);
        add ("Key LPF 2 kHz <-> Off", [] (int k, Settings& s) { s.keyLpfHz = k % 2 == 0 ? 2000.0 : 20000.0; }, 0.37)->keyInput = true;
        tone ("Key Bell Gain -18 <-> +18 dB (1 kHz key)", [] (int k, Settings& s) { s.keyBellDb = k % 2 == 0 ? -18.0 : 18.0; }, 0.29, 1000.0);
        tone ("Key Bell Freq 40 Hz <-> 16 kHz, +18 dB (16 kHz key)", [] (int k, Settings& s) { s.keyBellDb = 18.0; s.keyBellHz = k % 2 == 0 ? 40.0 : 16000.0; },
              0.31, 16000.0);
        tone ("Key Bell Q 0.3 <-> 6, +18 dB (2 kHz key)", [] (int k, Settings& s) { s.keyBellDb = 18.0; s.keyBellQ = k % 2 == 0 ? 0.3 : 6.0; }, 0.33, 2000.0);
        add ("Listen Output <-> Delta", [] (int k, Settings& s) { s.listen = k % 2 == 0 ? Listen::output : Listen::delta; }, 0.37);
        add ("Listen Output <-> Key (internal key)", [] (int k, Settings& s) { s.listen = k % 2 == 0 ? Listen::output : Listen::key; }, 0.41, ClickKey::sines)
            ->longHold = true;
        add ("Output -24 <-> +24 dB", [] (int k, Settings& s) { s.outputDb = k % 2 == 0 ? -24.0 : 24.0; }, 0.29);
        add ("Bypass off <-> on", [] (int k, Settings& s) { s.bypass = k % 2 == 1; }, 0.31);
        add ("Latency Normal <-> Live", [] (int k, Settings& s) { s.latency = k % 2 == 0 ? LatencyMode::normal : LatencyMode::live; }, 0.37);

        std::vector<Model> others;
        for (const auto m : offeredModels())
            if (m != runningModel (model))
                others.push_back (m);
        auto* switchModels = add ("Model: " + std::string (modelKey (model)) + " <-> each other model", [model, others] (int k, Settings& s)
                                  { s.model = k % 2 == 0 ? model : others[(size_t) (k / 2) % others.size()]; }, 0.33);
        if (std::none_of (others.begin(), others.end(), [model] (Model m) { return distinctEngines (model, m); }))
            switchModels->notExercised = "every model runs the same engine in this build: no switch happens";
        auto* switchDetector = add ("VCA detector Peak <-> RMS", [detector] (int k, Settings& s)
                                    { s.vcaDetector = k % 2 == 0 ? detector : (detector == VcaDetector::peak ? VcaDetector::rms : VcaDetector::peak); }, 0.35);
        if (runningModel (model) != Model::vca)
            switchDetector->notExercised = "the detector belongs to VCA (dimmed in this model)";
        else if (! distinctDetectors())
            switchDetector->notExercised = "VCA's two detectors run the same engine in this build";

        // What each key reads with nothing changing: the floor the rows above stand on.
        const std::pair<const char*, ClickKey> baselines[] {
            { "Nothing changes", ClickKey::split },
            { "Nothing changes (key at Threshold)", ClickKey::knee },
            { "Nothing changes (gated key)", ClickKey::gated },
            { "Nothing changes (internal key)", ClickKey::sines },
        };
        for (const auto& [name, key] : baselines)
        {
            auto* nothing = add (name, [] (int, Settings&) {}, 0.31, key);
            nothing->gate = false;
            nothing->referenceModels = key != ClickKey::sines;
            nothing->longHold = key == ClickKey::sines;
        }
        for (const auto hz : { 1000.0, 2000.0, 16000.0 })
        {
            auto* nothing = tone (format ("Nothing changes (%g kHz key)", hz / 1000.0), [] (int, Settings&) {}, 0.31, hz);
            nothing->gate = false;
        }
        return list;
    }

    bool isSwitch (const Scenario& s) { return s.name.rfind ("Model:", 0) == 0 || s.name.rfind ("VCA detector", 0) == 0 || s.name.rfind ("Nothing changes", 0) == 0; }

    /** The side chain for a scenario, two channels of n samples. */
    Channels clickKey (const Scenario& scenario, const Settings& base, int n, double rate)
    {
        const auto levels = clickLevels (base.model);
        const auto loud = gainOf (levels.loud), quiet = gainOf (levels.quiet);
        switch (scenario.key)
        {
            case ClickKey::knee:
                return { carrier (n, gainOf (base.thresholdDb)), carrier (n, quiet) };
            case ClickKey::tone:
                return { sine (scenario.toneHz, n, rate, loud), sine (scenario.toneHz, n, rate, quiet, 0.7) };
            case ClickKey::gated:
            {
                // On: the split carrier, or for Upward −4 dBFS, over Threshold. Off: silence, or the lift region.
                const auto on = levels.gatedOff > -150.0 ? gainOf (clickKeyDb) : loud;
                const auto off = levels.gatedOff > -150.0 ? gainOf (levels.gatedOff) : 0.0;
                Channels key { carrier (n, on), carrier (n, levels.gatedOff > -150.0 ? on : quiet) };
                for (auto& ch : key)
                    for (int i = 0; i < n; ++i)
                        if (std::fmod (i / rate, gatedPeriod) >= gatedOn)
                            ch[(size_t) i] = (float) (i % 2 == 0 ? off : -off);
                return key;
            }
            case ClickKey::split:
            case ClickKey::sines:
                break;
        }
        return { carrier (n, loud), carrier (n, quiet) };
    }

    /**
        Where a gated scenario's knob moves: at the first block start 10 ms into each release, as
        the output shows it (the key reaches the gain the latency later). The windows measured
        run from 5 ms before each move to 40 ms after it, inside the release, clear of the attacks.
    */
    std::vector<int> gatedToggles (int n, double rate, int latency, int block)
    {
        std::vector<int> at;
        for (auto t = gatedOn + gatedToggleAfter; t < n / rate; t += gatedPeriod)
        {
            const auto target = samplesFor (t, rate) + latency;
            at.push_back ((target + block - 1) / block * block);
        }
        return at;
    }

    Row clickCase (const Options& o, Model model, VcaDetector detector, LatencyMode latency, double attackMs, const Scenario& scenario)
    {
        const auto group = format ("%s, %s, Attack %g ms", modelLabel (model, detector).c_str(), latencyName (latency), attackMs);
        if (o.device != DeviceKind::processor && ! scenario.referenceModels)
            return unspecified ("clicks", group, scenario.name, "the reference does not model this control");

        Settings base;
        base.model = model;
        base.vcaDetector = detector;
        base.latency = latency;
        base.attackMs = attackMs;
        base.key = scenario.key == ClickKey::sines ? KeySource::internal : KeySource::external;

        // A sine key at a fast Attack is followed through its waveform, and the hold sags between
        // crests: at Release 150 ms that reads about −51 dB at Attack 0.02 ms with nothing
        // changing, the model's in-cycle sound. A 5 s hold keeps it under the gate, so the change
        // is measured.
        if (scenario.longHold)
        {
            base.releaseMs = 5000.0;
            base.autoRelease = false;
        }

        const auto n = samplesFor (clickSeconds, o.rate);
        constexpr int block = 256;
        Input in;
        in.main = { sine (330.0, n, o.rate, 0.25), sine (330.0, n, o.rate, 0.2, 1.1) };
        in.key = clickKey (scenario, base, n, o.rate);

        const auto inReleases = scenario.key == ClickKey::gated;
        const auto toggles = gatedToggles (n, o.rate, basalt::dsp::BasaltChain::latencySamples (latency, o.rate), block);
        const auto rate = o.rate;
        RenderOptions ro { o.rate, { block }, {}, {} };
        ro.dump = dumpPath (o, "clicks", group, scenario.name);
        ro.automate = [&scenario, base, rate, inReleases, toggles] (int pos, Settings& s)
        {
            s = base;
            const auto k = inReleases ? (int) std::count_if (toggles.begin(), toggles.end(), [pos] (int t) { return t <= pos; })
                                 : (int) (pos / (scenario.period * rate));
            scenario.set (k, s);
        };

        auto device = makeDevice (o.device);
        const auto out = render (*device, base, in, ro);
        const auto settled = samplesFor (clickSettle, o.rate) + out.latency;
        auto crest = -std::numeric_limits<double>::infinity();
        for (int ch = 0; ch < 2; ++ch)
        {
            if (! inReleases)
            {
                crest = std::max (crest, clickCrestDb (out.main[(size_t) ch], settled, n));
                continue;
            }
            for (const auto t : toggles)
                if (t - samplesFor (gatedBefore, o.rate) >= settled && t + samplesFor (gatedAfter, o.rate) <= n)
                    crest = std::max (crest, clickCrestDb (out.main[(size_t) ch], t - samplesFor (gatedBefore, o.rate), t + samplesFor (gatedAfter, o.rate)));
        }

        if (! scenario.notExercised.empty())
            return reported ("clicks", group, scenario.name, crest, clickLimitDb, "dB", "not exercised: " + scenario.notExercised);

        const auto reportOnly = ! scenario.gate || (scenario.keyInput && attackMs < 0.1);
        if (reportOnly)
            return reported ("clicks", group, scenario.name, crest, clickLimitDb, "dB",
                             scenario.gate ? "reported at Attack 0.02 ms: the knob moves the detector's input, and a 20 us attack follows its key" : "the baseline");
        return gated ("clicks", group, scenario.name, crest, clickLimitDb, -std::numeric_limits<double>::infinity(), clickLimitDb, "dB",
                      inReleases ? "the windows around each move in a release" : std::string());
    }

    /**
        A switch must not swell (dynamics/NOTES.md, Clicks: the incoming path starts at the applied
        reduction): over the 3 s after it the reduction stays within 0.5 dB of the band between the
        two settings' settled reductions. Read through the probe, so the gain is exact at every sample.
    */
    Row swellRow (const Options& o, const Settings& before, const Settings& after, const Samples& key, const std::string& group, const std::string& item,
                  const std::string& notExercised)
    {
        const auto n = (int) key.size(), switchAt = samplesFor (3.0, o.rate);
        const auto settledAfter = renderKey (o.device, after, { key }, o.probe, { o.rate, { 512 }, {}, {} }).traces[0].reductionDb;
        RenderOptions ro { o.rate, { 512 }, {}, {} };
        ro.automate = [before, after, switchAt] (int pos, Settings& s) { s = pos >= switchAt ? after : before; };
        const auto r = renderKey (o.device, before, { key }, o.probe, ro).traces[0].reductionDb;
        const auto a = r[(size_t) switchAt - 1], b = settledAfter[(size_t) n - 1];
        const auto lo = std::min (a, b) - 0.5, hi = std::max (a, b) + 0.5;
        auto excursion = 0.0;
        for (int i = switchAt; i < n; ++i)
            excursion = std::max ({ excursion, lo - r[(size_t) i], r[(size_t) i] - hi, std::isfinite (r[(size_t) i]) ? 0.0 : 1.0e9 });
        const auto note = format ("settled %.2f -> %.2f dB; dB outside their band +-0.5", a, b);
        if (! notExercised.empty())
            return reported ("clicks", group, item, excursion, 0.0, "dB", "not exercised: " + notExercised + "; " + note);
        return gated ("clicks", group, item, excursion, 0.0, 0.0, 0.0, "dB", note);
    }

    std::vector<Row> swellRows (const Options& o, Model model, VcaDetector detector, LatencyMode latency)
    {
        std::vector<Row> rows;
        const auto n = samplesFor (6.0, o.rate);

        Settings base;
        base.model = model;
        base.vcaDetector = detector;
        base.latency = latency;
        base.attackMs = 300.0;
        const auto group = format ("%s, %s, Attack 300 ms", modelLabel (model, detector).c_str(), latencyName (latency));
        const auto key = carrier (n, gainOf (clickKeyDb));
        for (const auto other : offeredModels())
        {
            if (other == runningModel (model))
                continue;
            auto alone = base;
            alone.model = other;
            alone.vcaDetector = VcaDetector::peak;
            rows.push_back (swellRow (o, base, alone, key, group, format ("No swell: %s -> %s", modelLabel (model, detector).c_str(), modelKey (other)),
                                      distinctEngines (model, other) ? std::string() : std::string ("both run the same engine in this build")));
        }

        // VCA's detectors (vca/NOTES.md, Gates, Clicks): about 6 dB of reduction on a 1 kHz sine,
        // which both detectors read alike (the carrier reads 3.01 dB apart on them), at Attack 10 and 300 ms.
        if (runningModel (model) == Model::vca)
            for (const auto attack : { 10.0, 300.0 })
            {
                auto before = base, after = base;
                before.attackMs = after.attackMs = attack;
                after.vcaDetector = detector == VcaDetector::peak ? VcaDetector::rms : VcaDetector::peak;
                const auto sineKey = sine (1000.0, n, o.rate, gainOf (levelFor (specOf (model), before, Signal::sine, 6.0)));
                rows.push_back (swellRow (o, before, after, sineKey, format ("%s, %s, Attack %g ms", modelLabel (model, detector).c_str(), latencyName (latency), attack),
                                          format ("No swell: detector %s -> %s, 1 kHz sine at 6 dB", detector == VcaDetector::peak ? "Peak" : "RMS",
                                                  detector == VcaDetector::peak ? "RMS" : "Peak"),
                                          distinctDetectors() ? std::string() : std::string ("both detectors run the same engine in this build")));
            }
        return rows;
    }
}

std::vector<Row> clicks (const Options& o)
{
    const auto model = o.modelOrClean();

    std::vector<Job> jobs;
    for (const auto detector : detectorsFor (o, model))
    {
        const auto list = std::make_shared<std::vector<Scenario>> (scenarios (model, detector));
        for (const auto latency : o.latencies)
            for (const auto attack : { 0.02, 10.0, 300.0 })
                for (size_t i = 0; i < list->size(); ++i)
                {
                    if (attack > 100.0 && ! isSwitch ((*list)[i]))
                        continue;
                    jobs.push_back ([&o, list, i, model, detector, latency, attack]
                                    { return std::vector<Row> { clickCase (o, model, detector, latency, attack, (*list)[i]) }; });
                }
        if (o.device == DeviceKind::processor)
            for (const auto latency : o.latencies)
                jobs.push_back ([&o, model, detector, latency] { return swellRows (o, model, detector, latency); });
    }
    if (o.device == DeviceKind::processor && specOf (model).lifts)
        for (const auto latency : o.latencies)
            jobs.push_back ([&o, latency] { return floorSweepRows (o, latency); });
    return runJobs (jobs, o.jobs);
}

} // namespace basalt::measure
